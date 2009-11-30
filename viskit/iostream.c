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

void
io_recode_data_copy (gchar *src, gchar *dest, DSType type, gsize nvals)
{
    gsize i;
    IOBufptr bsrc, bdest;

    bsrc.any = src;
    bdest.any = dest;

    switch (type) {
    case DST_UNK:
	g_assert (nvals == 0);
	break;
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
    case DST_UNK:
	g_assert (nvals == 0);
	break;
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
	/* handle write stream */
	g_assert (0);
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
    switch (io->mode) {
    case IO_MODE_READ:
	g_free (io->s.read.buf);
	io->s.read.buf = NULL;
	io->s.read.scratch = NULL;
	break;
    case IO_MODE_WRITE:
	/* handle write stream */
	g_assert (0);
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

    if (err != NULL)
	*err = NULL;

    if (io->fd >= 0) {
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
     * caller's buffer.
     *
     * FIXME: doing this causes us to lose our nice block-aligned
     * read behavior, and tests indicate that doing so can indeed
     * cause a massive performance hit. For now, we make the default
     * buffer size big enough for that not to be a problem for UV
     * variables, but if we have million-point spectra, the problem
     * will arise in the future. We could do block-aligned reads into
     * the user buffer, do a final read into the IO buffer again,
     * then copy the last bit of data into the caller's buffer. */

    if (ninbuf < nbytes) {
	gsize ntoread = nbytes - ninbuf;
	gssize nread = _io_fd_read (io->fd, buf + ninbuf, ntoread, err);
	nbytes = ninbuf + nread;

	if (nread < ntoread) {
	    /* EOF, short read */
	    io->s.read.curpos = 0;
	    io->s.read.endpos = 0;
	    io->s.read.eof = TRUE;
	} else {
	    /* Not EOF -- we need to partially refill the IO stream buffer
	     * to preserve its alignment properties.*/
	    io->s.read.curpos = ntoread % io->bufsz;
	    nread = _io_fd_read (io->fd, io->s.read.buf + io->s.read.curpos,
				 io->bufsz - io->s.read.curpos, err);

	    if (nread != (io->bufsz - io->s.read.curpos)) {
		/* OK, now we hit EOF. */
		io->s.read.endpos = io->s.read.curpos + nread;
		io->s.read.eof = TRUE;
	    }
	}
    }

    /* We may have a short read -- integral number of items read? */

    if (nbytes % ds_type_sizes[type] != 0)
	return -1;

    /* Yay, finished successfully. */

    nvals = nbytes / ds_type_sizes[type];
    io_recode_data_inplace (buf, type, nvals);
    return nvals;
}


gboolean
io_nudge_align (IOStream *io, gsize align_size, GError **err)
{
    gsize n = io->s.read.curpos % align_size;

    g_assert (io->mode == IO_MODE_READ);

    if (n == 0)
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
    return FALSE;
}
