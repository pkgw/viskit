#include <iostream.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <string.h> /*memcpy*/


#define DEFAULT_BUFSZ 16384

static gssize _io_fd_read (int fd, gpointer buf, gsize nbytes, GError **err);
static gboolean _io_fd_write (int fd, gconstpointer buf, gsize nbytes,
			      GError **err);
static gboolean _io_read (IOStream *io, GError **err);
static gboolean _io_write (IOStream *io, GError **err);



/* Dealing with endianness conversion: MIRIAD datasets are
 * standardized to big-endian */

typedef union _IOBufptr {
    gpointer any;
    gint8 *i8;
    gint16 *i16;
    gint32 *i32;
    gint64 *i64;
    gfloat *f32;
    gdouble *f64;
    gchar *text;
} IOBufptr;

typedef union _IOConstBufptr {
    gconstpointer any;
    const gint8 *i8;
    const gint16 *i16;
    const gint32 *i32;
    const gint64 *i64;
    const gfloat *f32;
    const gdouble *f64;
    const gchar *text;
} IOConstBufptr;

void
io_recode_data_copy (const gchar *src, gchar *dest, DSType type, gsize nvals)
{
    gsize i;
    IOConstBufptr bsrc;
    IOBufptr bdest;

    bsrc.any = src;
    bdest.any = dest;

    switch (type) {
    case DST_BIN:
    case DST_I8:
    case DST_TEXT:
	memcpy (dest, src, nvals);
	break;
    case DST_I16:
	for (i = 0; i < nvals; i++) {
	    *(bdest.i16) = GINT16_FROM_BE(*(bsrc.i16));
	    bdest.i16++;
	    bsrc.i16++;
	}
	break;
    case DST_C64:
	nvals *= 2;
	/* fall through */
    case DST_I32:
    case DST_F32:
	for (i = 0; i < nvals; i++) {
	    *(bdest.i32) = GINT32_FROM_BE(*(bsrc.i32));
	    bdest.i32++;
	    bsrc.i32++;
	}
	break;
    case DST_I64:
    case DST_F64:
	for (i = 0; i < nvals; i++) {
	    *(bdest.i64) = GINT64_FROM_BE(*(bsrc.i64));
	    bdest.i64++;
	    bsrc.i64++;
	}
	break;
    default:
	g_error ("Unhandled data typecode %d!", type);
	break;
    }
}

void
io_recode_data_inplace (gchar *data, DSType type, gsize nvals)
{
    gsize i;
    IOBufptr bdata;

    bdata.any = data;

    switch (type) {
    case DST_BIN:
    case DST_I8:
    case DST_TEXT:
	break;
    case DST_I16:
	for (i = 0; i < nvals; i++) {
	    *(bdata.i16) = GINT16_FROM_BE(*(bdata.i16));
	    bdata.i16++;
	}
	break;
    case DST_C64:
	nvals *= 2;
	/* fall through */
    case DST_I32:
    case DST_F32:
	for (i = 0; i < nvals; i++) {
	    *(bdata.i32) = GINT32_FROM_BE(*(bdata.i32));
	    bdata.i32++;
	}
	break;
    case DST_I64:
    case DST_F64:
	for (i = 0; i < nvals; i++) {
	    *(bdata.i64) = GINT64_FROM_BE(*(bdata.i64));
	    bdata.i64++;
	}
	break;
    default:
	g_error ("Unhandled data typecode %d!", type);
	break;
    }
}


/* Actual I/O operations. */

struct _IOStream {
    IOMode mode;
    int fd;
    gsize bufsz;

    union {
	struct {
	    gchar *buf;
	    gchar *scratch; /* for block-crossing read requests. */
	    gsize curpos; /* position of read cursor within buffer. */
	    gsize endpos; /* location of EOF within the buffer */
	    gboolean eof; /* have we read to EOF? */
	} read;
	struct {
	    gchar *buf;
	    gsize curpos; /* position of read cursor within buffer. */
	} write;
    } s; /* short for "state" */
};


IOStream *
io_new_from_fd (IOMode mode, int fd, gsize bufsz, goffset align_hint)
{
    IOStream *io;

    if (bufsz == 0)
	bufsz = DEFAULT_BUFSZ;

    /* align_hint will be used to tell the IOStream of the alignment of
     * the FD handle within its stream. It's 0 if starting at the
     * beginning of a file, but if we're appending one, for instance,
     * it will be different. The align_hint should be reduced via modulus
     * to a small-ish number, specifically one smaller than bufsz.
     *
     * FIXME: currently ignored.
     */

    g_assert (align_hint == 0); /* !!!! temporary */

    /* bufsz must be a multiple of a large power of 2, say 256 ... */
    g_assert ((bufsz & 0xFF) == 0);
    g_assert (align_hint < bufsz);
    g_assert (fd >= 0);

    io = g_new0 (IOStream, 1);
    io->mode = mode;
    io->fd = fd;
    io->bufsz = bufsz;

    switch (mode) {
    case IO_MODE_READ:
	/* Allocate the two buffers as one block. */
	io->s.read.buf = g_new (gchar, bufsz * 2);
	io->s.read.scratch = io->s.read.buf + bufsz;
	io->s.read.eof = FALSE;
	io->s.read.curpos = bufsz; /* Forces a block to be read on first read */
	io->s.read.endpos = 0;
	break;
    case IO_MODE_WRITE:
	io->s.write.buf = g_new (gchar, bufsz);
	io->s.write.curpos = 0;
	break;
    default:
	/* Unsupported stream mode: we only do read or write but not both. */
	g_assert_not_reached ();
	break;
    }

    return io;
}


void
io_free (IOStream *io)
{
    if (io == NULL)
	return;

    switch (io->mode) {
    case IO_MODE_READ:
	g_free (io->s.read.buf);
	io->s.read.buf = NULL;
	io->s.read.scratch = NULL;
	break;
    case IO_MODE_WRITE:
	g_free (io->s.write.buf);
	io->s.write.buf = NULL;
	break;
    default:
	g_assert_not_reached ();
	break;
    }

    g_free (io);
}


gboolean
io_close_and_free (IOStream *io, GError **err)
{
    gboolean retval = FALSE;

    if (io == NULL)
	return FALSE;

    if (io->fd >= 0) {
	if (io->mode == IO_MODE_WRITE) {
	    /* Any pending writes to flush? */

	    if (io->s.write.curpos != 0) {
		if (_io_fd_write (io->fd, io->s.write.buf, io->s.write.curpos, err))
		    retval = TRUE;
	    }
	}

	if (close (io->fd)) {
	    IO_ERRNO_ERR (err, errno, "Failed to close stream");
	    retval = TRUE;
	}
    }

    io_free (io);
    return retval;
}


int
io_get_fd (IOStream *io)
{
    return io->fd;
}


static gssize
_io_fd_read (int fd, gpointer buf, gsize nbytes, GError **err)
{
    gsize nleft = nbytes;
    gssize nread;
    
    while (nleft > 0) {
	nread = read (fd, buf, nleft);

	if (nread < 0) {
	    if (errno == EINTR)
		continue;
	    IO_ERRNO_ERR (err, errno, "Failed to read stream");
	    return -1;
	}

	if (nread == 0) /* EOF */
	    break;

	buf += nread;
	nleft -= nread;
    }

    return nbytes - nleft;
}


static gboolean
_io_fd_write (int fd, gconstpointer buf, gsize nbytes, GError **err)
{
    gconstpointer bufiter = buf;
    gsize nleft = nbytes;
    gssize nwritten;

    while (nleft > 0) {
	nwritten = write (fd, bufiter, nleft);

	if (nwritten < 0) {
	    if (errno == EINTR)
		continue;
	    IO_ERRNO_ERR (err, errno, "Failed to read stream");
	    return TRUE;
	}

	bufiter += nwritten;
	nleft -= nwritten;
    }

    return FALSE;
}


static gboolean
_io_read (IOStream *io, GError **err)
{
    gssize nread;

    g_assert (io->mode == IO_MODE_READ);

    nread = _io_fd_read (io->fd, io->s.read.buf, io->bufsz, err);

    if (nread < 0)
	return TRUE;

    if (nread != io->bufsz) {
	/* EOF, since we couldn't get as much data as we wanted */
	io->s.read.eof = TRUE;
	io->s.read.endpos = nread;
    }

    io->s.read.curpos = 0;
    return FALSE;
}


static gboolean
_io_write (IOStream *io, GError **err)
{
    g_assert (io->mode == IO_MODE_WRITE);

    if (_io_fd_write (io->fd, io->s.write.buf, io->bufsz, err) < 0)
	return TRUE;

    io->s.write.curpos = 0;
    return FALSE;
}


gssize
io_read_into_temp_buf (IOStream *io, gsize nbytes, gpointer *dest, GError **err)
{
    g_assert (io->mode == IO_MODE_READ);
    /* disallow this situation for now. */
    g_assert (nbytes <= io->bufsz);

    if (dest != NULL)
	*dest = NULL;

    if (io->s.read.eof) {
	/* The request is either entirely within the read
	 * buffer or truncated by EOF; either way, we don't
	 * need to use the scratch buf. */

	if (dest != NULL)
	    *dest = io->s.read.buf + io->s.read.curpos;

	if (io->s.read.curpos + nbytes <= io->s.read.endpos) {
	    /* Even though we're near EOF we can still
	     * fulfill this entire request. */
	    io->s.read.curpos += nbytes;
	    return nbytes;
	}

	/* We can only partially fulfill the request. */
	nbytes = io->s.read.endpos - io->s.read.curpos;
	io->s.read.curpos = io->s.read.endpos;
	return nbytes;
    }

    if (io->s.read.curpos == io->bufsz) {
	/* Last time we read exactly up to a block boundary.
	 * Read in a new block and try again. */

	if (_io_read (io, err))
	    return -1;

	return io_read_into_temp_buf (io, nbytes, dest, err);
    }

    if (io->s.read.curpos + nbytes <= io->bufsz) {
	/* Not at EOF, and the request is entirely buffered */
	if (dest != NULL)
	    *dest = io->s.read.buf + io->s.read.curpos;
	io->s.read.curpos += nbytes;
	return nbytes;
    }

    /* We handled EOF, and the request isn't entirely buffered,
     * and we handled the exact-buffer-boundary case. We must
     * be reading across the end of the current buffer. */

    {
	gsize nlow = io->bufsz - io->s.read.curpos;
	gpointer buf2;
	gsize nhi;
	GError *suberr = NULL;

	/* Copy in what we've already got in the current buffer. */

	memcpy (io->s.read.scratch, io->s.read.buf + io->s.read.curpos, nlow);

	/* Try to get the rest. May hit EOF. */

	if (_io_read (io, err))
	    return -1;

	nhi = io_read_into_temp_buf (io, nbytes - nlow, &buf2, &suberr);

	if (suberr != NULL) {
	    g_propagate_error (err, suberr);
	    return -1;
	}

	memcpy (io->s.read.scratch + nlow, buf2, nhi);
	*dest = io->s.read.scratch;
	return nlow + nhi;
    }
}

gssize
io_read_into_temp_buf_typed (IOStream *io, DSType type, gsize nvals, gpointer *dest,
			     GError **err)
{
    gssize retval;

    g_assert (io->mode == IO_MODE_READ);
    g_assert (dest != NULL);

    retval = io_read_into_temp_buf (io, nvals * ds_type_sizes[type], dest, err);

    if (retval < 0)
	return retval;

    io_recode_data_inplace (*dest, type, nvals);
    return retval / ds_type_sizes[type];
}

gssize
io_read_into_user_buf (IOStream *io, DSType type, gsize nvals, gpointer buf,
		       GError **err)
{
    gsize nbytes, ninbuf;

    g_assert (io->mode == IO_MODE_READ);
    g_assert (buf != NULL);

    nbytes = nvals * ds_type_sizes[type];

    /* At EOF? If we, all we need to do is decode the already-read
     * data into the caller's buffer. If there isn't as much data left
     * as the user requested, return a short read. */

    if (io->s.read.eof) {
	nbytes = MIN (io->s.read.endpos - io->s.read.curpos, nbytes);

	if (nbytes % ds_type_sizes[type] != 0)
	    /* Nonintegral number of items -- treat as truncated file
	     * which we consider to be an I/O error.*/
	    return -1;

	nvals = nbytes / ds_type_sizes[type];
	io_recode_data_copy (io->s.read.buf + io->s.read.curpos, buf, type, nvals);
	io->s.read.curpos += nbytes;
	return nvals;
    }

    /* Not at EOF. First, we copy over whatever data have already been
     * buffered. If there's no more to be done, we'll leave the buffer
     * in the correct state to be refilled for the next operation. */

    ninbuf = MIN (nbytes, io->bufsz - io->s.read.curpos);
    memcpy (buf, io->s.read.buf + io->s.read.curpos, ninbuf);
    io->s.read.curpos += ninbuf;

    /* If we need to read more, do so, reading directly into the
     * caller's buffer. Preserve alignment within our buffer at
     * the end of things, and read only in multiples of our block
     * size. */

    if (ninbuf < nbytes) {
	gsize ntoread = nbytes - ninbuf;
	gsize nblocks = ntoread / io->bufsz; /* truncating div. */

	if (nblocks > 0) {
	    /* Read all but the last block directly into the user's buffer. */
	    gssize nread;

	    ntoread = nblocks * io->bufsz;
	    nread = _io_fd_read (io->fd, buf + ninbuf, ntoread, err);

	    if (nread < 0)
		return -1;

	    if (nread < ntoread) {
		/* EOF, short read */
		io->s.read.curpos = 0;
		io->s.read.endpos = 0;
		io->s.read.eof = TRUE;
	    }

	    ntoread -= nread;
	    ninbuf += nread;
	}

	if (ntoread > 0 && !io->s.read.eof) {
	    /* Not EOF and we have a little bit more to read. (Specifically, we
	     * have less than bufsz.) This batch gets read into the IO stream
	     * buffer to preserve its alignment properties.*/
	    gsize navail;

	    if (_io_read (io, err))
		return -1;

	    if (io->s.read.eof)
		navail = MIN (ntoread, io->s.read.endpos);
	    else
		navail = ntoread;

	    memcpy (buf + ninbuf, io->s.read.buf, navail);
	    ninbuf += navail;
	    io->s.read.curpos = navail;
	}
    }

    /* We may have a short read -- integral number of items read? */

    if (ninbuf % ds_type_sizes[type] != 0)
	return -1;

    /* Yay, finished successfully. */

    nvals = ninbuf / ds_type_sizes[type];
    io_recode_data_inplace (buf, type, nvals);
    return nvals;
}


gboolean
io_nudge_align (IOStream *io, gsize align_size, GError **err)
{
    gsize n;

    if (io->mode == IO_MODE_READ) {
	if ((n = io->s.read.curpos % align_size) == 0)
	    return FALSE;

	n = align_size - n;

	if (io->s.read.eof) {
	    if (io->s.read.curpos + n <= io->s.read.endpos) {
		/* We're near EOF but this alignment keeps
		 * us within the buffer, so no sweat. */
		io->s.read.curpos += n;
		return FALSE;
	    }

	    /* Land us on EOF */
	    io->s.read.curpos = io->s.read.endpos;
	    return FALSE;
	}

	/* This "can't happen" because bufsz should be
	 * a multiple of any alignment size since it's
	 * a big power of 2. */
	g_assert (io->s.read.curpos != io->bufsz);

	/* This also "can't happen" since the exact end
	 * of the buffer should be a multiple of any
	 * alignment size, as above. */
	g_assert (io->s.read.curpos + n <= io->bufsz);

	/* Given those, we can do this alignment entirely within the
	 * buffer. */

	io->s.read.curpos += n;
    } else if (io->mode == IO_MODE_WRITE) {
	if ((n = io->s.write.curpos % align_size) == 0)
	    return FALSE;

	n = align_size - n;

	/* As above. */
	g_assert (io->s.write.curpos != io->bufsz);
	g_assert (io->s.write.curpos + n <= io->bufsz);

	memset (io->s.write.buf + io->s.write.curpos, 0, n);
	io->s.write.curpos += n;
    }

    return FALSE;
}


gboolean
io_write_raw (IOStream *io, gsize nbytes, gconstpointer buf, GError **err)
{
    gconstpointer bufiter = buf;

    g_assert (io->mode == IO_MODE_WRITE);

    while (nbytes > 0) {
	gsize ntowrite;

	ntowrite = MIN (nbytes, io->bufsz - io->s.write.curpos);

	if (io->s.write.curpos == 0 && ntowrite == io->bufsz) {
	    /* We'd write an entire buffer of data. We can short-circuit
	     * the copying of the data to the write buffer. */
	    if (_io_fd_write (io->fd, bufiter, ntowrite, err))
		return TRUE;
	} else {
	    memcpy (io->s.write.buf + io->s.write.curpos, bufiter, ntowrite);
	    io->s.write.curpos += ntowrite;
	}

	if (io->s.write.curpos == io->bufsz) {
	    /* We've filled up the buffer. Write it out. */
	    if (_io_write (io, err))
		return TRUE;
	}

	bufiter += ntowrite;
	nbytes -= ntowrite;
    }

    return FALSE;
}


gboolean
io_write_typed (IOStream *io, DSType type, gsize nvals, gconstpointer buf,
		GError **err)
{
    gconstpointer bufiter = buf;
    guint8 tsize = ds_type_sizes[type];
    gsize nbytes = nvals * tsize;

    g_assert (io->mode == IO_MODE_WRITE);

    while (nbytes > 0) {
	gsize nbytestowrite, nvalstowrite;

	nbytestowrite = MIN (nbytes, io->bufsz - io->s.write.curpos);

	if (nbytestowrite % tsize != 0) {
	    g_assert (0);
	    /*g_set_error (err, foo, foo, "Alignment error in typed data write");*/
	    return TRUE;
	}

	nvalstowrite = nbytestowrite / tsize;

	/* Unlike the untyped write, we can't save a copy in the whole-
	 * buffer case since we need to byteswap the data anyway. */

	io_recode_data_copy (bufiter, io->s.write.buf + io->s.write.curpos,
			     type, nvalstowrite);
	io->s.write.curpos += nbytestowrite;

	if (io->s.write.curpos == io->bufsz) {
	    /* We've filled up the buffer. Write it out. */
	    if (_io_write (io, err))
		return TRUE;
	}

	bufiter += nbytestowrite;
	nbytes -= nbytestowrite;
    }

    return FALSE;
}


gboolean
io_pipe (IOStream *input, IOStream *output, GError **err)
{
    gsize neof;

    /* Invariants to make life easier. */
    g_return_val_if_fail (input->bufsz == output->bufsz, TRUE);
    g_assert (input->mode & IO_MODE_READ);
    g_assert (output->mode & IO_MODE_WRITE);

    if (input->s.read.curpos == input->bufsz) {
	if (_io_read (input, err))
	    return TRUE;
    }

    if (output->s.write.curpos == output->bufsz) {
	if (_io_write (output, err))
	    return TRUE;
    }

    g_return_val_if_fail (input->s.read.curpos == output->s.write.curpos, TRUE);

    while (!input->s.read.eof) {
	if (_io_fd_write (output->fd, input->s.read.buf + input->s.read.curpos,
			  input->bufsz - input->s.read.curpos, err))
	    return TRUE;
	if (_io_read (input, err))
	    return TRUE;
    }

    neof = input->s.read.endpos - input->s.read.curpos;

    if (neof > 0) {
	if (_io_fd_write (output->fd, input->s.read.buf + input->s.read.curpos,
			  neof, err))
	    return TRUE;
    }

    return FALSE;
}
