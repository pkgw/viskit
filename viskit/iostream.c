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

void
io_init (IOStream *io, gsize bufsz)
{
    /* bufsz must be a multiple of a large power of 2, say 256 ... */
    g_assert ((bufsz & 0xFF) == 0);

    io->fd = -1;
    io->bufsz = bufsz;
    io->rbuf = g_new (gchar, bufsz * 3); /* Allocate the three buffers all as one block. */
    io->wbuf = io->rbuf + bufsz;
    io->scratch = io->wbuf + bufsz;
    io->rpos = bufsz; /* Forces a block to be read on first fetch */
}

void
io_uninit (IOStream *io)
{
    g_free (io->rbuf);
    io->rbuf = io->wbuf = io->scratch = NULL;
}

IOStream *
io_alloc (gsize bufsz)
{
    IOStream *io = g_new0 (IOStream, 1);
    io_init (io, bufsz);
    return io;
}

void
io_free (IOStream *io)
{
    io_uninit (io);
    g_free (io);
}

static gboolean
_io_read (IOStream *io, GError **err)
{
    gsize nleft = io->bufsz;
    gchar *buf = io->rbuf;
    gssize nread;
    
    while (nleft > 0) {
	nread = read (io->fd, buf, nleft);

	if (nread < 0 && errno != EINTR) {
	    IO_ERRNO_ERR (err, errno, "Failed to read stream");
	    return TRUE;
	}

	if (nread == 0) {
	    /* EOF */
	    io->rend = io->bufsz - nleft;
	    io->reof = TRUE;
	    return FALSE;
	}

	buf += nread;
	nleft -= nread;
    }

    return FALSE;
}

gssize
io_fetch (IOStream *io, gsize nbytes, gchar **dest, GError **err)
{
    /* disallow this situation for now. */
    g_assert (nbytes <= io->bufsz);

    if (dest != NULL)
	*dest = NULL;

    if (io->reof) {
	/* The request is either entirely within the read
	 * buffer or truncated by EOF; either way, we don't
	 * need to use the scratch buf. */

	if (dest != NULL)
	    *dest = io->rbuf + io->rpos;

	if (io->rpos + nbytes <= io->rend) {
	    /* Even though we're near EOF we can still
	     * fulfill this entire request. */
	    io->rpos += nbytes;
	    return nbytes;
	}

	/* We can only partially fulfill the request. */
	nbytes = io->rend - io->rpos;
	io->rpos = io->rend;
	return nbytes;
    }

    if (io->rpos == io->bufsz) {
	/* Last time we read exactly up to a block boundary.
	 * Read in a new block and try again. */

	if (_io_read (io, err))
	    return -1;

	io->rpos = 0;
	return io_fetch (io, nbytes, dest, err);
    }

    if (io->rpos + nbytes <= io->bufsz) {
	/* Not at EOF, and the request is entirely buffered */
	if (dest != NULL)
	    *dest = io->rbuf + io->rpos;
	io->rpos += nbytes;
	return nbytes;
    }

    /* We handled EOF, and the request isn't entirely buffered,
     * and we handled the exact-buffer-boundary case. We must
     * be reading across the end of the current buffer. */

    {
	gsize nlow = io->bufsz - io->rpos;
	gchar *buf2;
	gsize nhi;
	GError *suberr = NULL;

	/* Copy in what we've already got in the current buffer. */

	memcpy (io->scratch, io->rbuf + io->rpos, nlow);

	/* Try to get the rest. May hit EOF. */

	if (_io_read (io, err))
	    return -1;

	io->rpos = 0;
	nhi = io_fetch (io, nbytes - nlow, &buf2, &suberr);

	if (suberr != NULL) {
	    g_propagate_error (err, suberr);
	    return -1;
	}

	memcpy (io->scratch + nlow, buf2, nhi);
	*dest = io->scratch;
	return nlow + nhi;
    }
}

gssize
io_fetch_type (IOStream *io, DSType type, gsize nvals, gpointer *dest,
	       GError **err)
{
    gssize retval;

    retval = io_fetch (io, nvals * ds_type_sizes[type], 
		       (gchar **) dest, err);

    if (retval < 0)
	return retval;

    io_recode_data_inplace (*dest, type, nvals);
    return retval;
}

gboolean
io_nudge_align (IOStream *io, gsize align_size, GError **err)
{
    gsize n = io->rpos % align_size;

    if (n == 0)
	return FALSE;

    n = align_size - n;

    if (io->reof) {
	if (io->rpos + n <= io->rend) {
	    /* We're near EOF but this alignment keeps
	     * us within the buffer, so no sweat. */
	    io->rpos += n;
	    return FALSE;
	}

	/* Land us on EOF */
	io->rpos = io->rend;
	return FALSE;
    }

    /* This "can't happen" because bufsz should be 
     * a multiple of any alignment size since it's
     * a big power of 2. */
    g_assert (io->rpos != io->bufsz);

    /* This also "can't happen" since the exact end
     * of the buffer should be a multiple of any
     * alignment size, as above. */
    g_assert (io->rpos + n <= io->bufsz);

    /* Given those, we can do this alignment entirely within the
     * buffer. */

    io->rpos += n;
    return FALSE;
}
