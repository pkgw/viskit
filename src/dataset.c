#include <dataset.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h> /*printf*/

/* Types. */

const char ds_type_codes[] = "?bijrdacl";

const guint8 ds_type_sizes[] = {
    /* Item sizes in bytes. */
    0, 1, 4, 2, 4, 8, 1, 8, 8
};

const guint8 ds_type_aligns[] = {
    /* Alignment requirements for datatypes.  Identical to item sizes
     * except for the complex type, which can be aligned to the size
     * of its constituents. */
    0, 1, 4, 2, 4, 8, 1, 4, 8
};

/* endianness jazz */

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
_io_decode_data (gchar *src, gchar *dest, DSType type, gsize nvals)
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
	g_error ("Unhandled data type %d!", type);
	break;
    }
}

gint16
io_decode_i16 (gchar *buf)
{
    IOBufptr b;

    b.any = buf;
    return GINT16_FROM_BE (*(b.i16));
}

gint32
io_decode_i32 (gchar *buf)
{
    IOBufptr b;

    b.any = buf;
    return GINT32_FROM_BE (*(b.i32));
}

gint64
io_decode_i64 (gchar *buf)
{
    IOBufptr b;

    b.any = buf;
    return GINT64_FROM_BE (*(b.i64));
}

/* more direct io layer */

#define IO_ERRNO_ERR(err, errno, msg)					\
    g_set_error (err, G_FILE_ERROR, g_file_error_from_errno(errno),	\
		 msg ": %s", g_strerror (errno))
#define IO_ERRNO_ERRV(err, errno, msg, rest...)				\
    g_set_error (err, G_FILE_ERROR, g_file_error_from_errno(errno),	\
		 msg ": %s", rest, g_strerror (errno))

#define BLOCKSZ 4096

struct _IOStream {
    int fd;
    gsize bufsz;
    gchar *rbuf, *wbuf;
    gsize rpos, wpos; /* position of read and write cursors
		       * within buffers. */
    gboolean reof;
    gsize rend, wend; /* location of next needed r/w IO
		       * operations */
    gchar *scratch; /* for block-crossing read requests. */
};

IOStream *
_io_alloc (gsize bufsz)
{
    IOStream *io = g_new0 (IOStream, 1);

    io->bufsz = bufsz;
    io->rbuf = g_new (gchar, bufsz * 3);
    io->wbuf = io->rbuf + bufsz;
    io->scratch = io->wbuf + bufsz;
    io->rpos = bufsz; /* Forces a block to be read on first fetch */

    return io;
}

void
io_free (IOStream *io)
{
    g_free (io->rbuf);
    g_free (io);
}

gboolean
_io_read (IOStream *io, GError **err)
{
    gsize nleft = io->bufsz;
    gchar *buf = io->rbuf;
    gssize nread;
    
    while (nleft > 0) {
	nread = read (io->fd, buf, nleft);

	if (nread < 0 && errno != EINTR) {
	    IO_ERRNO_ERR (err, errno, "Failed to read file");
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

gboolean
io_read_align (IOStream *io, gsize align_size, GError **err)
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

/* Note that these limits are set by the file format and MUST NOT be
 * changed by the user. Doing so will break compatibility with the
 * file format. */

#define DS_ITEMNAME_MAXLEN 8 /* bytes */
#define DS_HEADER_RECSIZE 16 /* bytes */
#define DS_HEADER_MAXDSIZE 64 /* bytes */

struct _Dataset {
    gsize  namelen;
    gchar *namebuf;
    DSMode mode;
    GHashTable *small_items;
};

typedef struct _DSHeaderItem {
    /* no padding to 64-bit alignment */
    gchar name[15];
    gint8 alen; /* aligned length */
} DSHeaderItem;

typedef struct _DSSmallItem {
    gchar name[9];
    DSType type;
    gsize nvals;
    union {
	gint8 i8[64];
	gint16 i16[32];
	gint32 i32[16];
	gint64 i64[8];
	gfloat f32[16];
	gdouble f64[8];
	gchar text[64];
    } vals;
} DSSmallItem;

void
_ds_set_name_dir (Dataset *ds)
{
    ds->namebuf[ds->namelen] = '\0';
}

void
_ds_set_name_item (Dataset *ds, char *item)
{
    /* It's assumed here that we've sanitychecked
     * that @item is less than 8 characters long. */

    ds->namebuf[ds->namelen] = '/';
    strcpy (ds->namebuf + ds->namelen + 1, item);
}

Dataset *
ds_open (const char *filename, DSMode mode, GError **err)
{
    Dataset *ds;
    IOStream *hio = NULL;
    GError *suberr;
    gsize nread;
    DSHeaderItem *hitem;

    g_return_val_if_fail (filename != NULL, NULL);
    g_return_val_if_fail (mode != DSM_APPEND, NULL);

    if (mode == DSM_CREATE) {
	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
	    g_set_error (err, G_FILE_ERROR, G_FILE_ERROR_EXIST,
			 "Cannot create the dataset \"%s\" since a "
			 "file of that name already exists.", filename);
	    return NULL;
	}

	if (mkdir (filename, 0755)) {
	    IO_ERRNO_ERRV (err, errno, "Failed to create dataset "
			  "directory \"%s\"", filename);
	    return NULL;
	}
    } else {
	/* This isn't a fully comprehensive test. We'll have a better
	 * idea of whether this DS is ok when we try to read in the 
	 * header. */

	if (!g_file_test (filename, G_FILE_TEST_IS_DIR)) {
	    g_set_error (err, G_FILE_ERROR, G_FILE_ERROR_NOTDIR,
			 "Cannot open the dataset \"%s\" since it "
			 "does not exist or is not a directory.", filename);
	    return NULL;
	}
    }

    ds = g_new0 (Dataset, 1);
    ds->namelen = strlen (filename);
    ds->namebuf = g_new (gchar, ds->namelen + DS_ITEMNAME_MAXLEN + 2);
    strcpy (ds->namebuf, filename);
    ds->mode = mode;
    ds->small_items = g_hash_table_new_full (g_str_hash,
					     g_str_equal,
					     NULL, g_free);

    /* Read in that thar header */

    hio = _io_alloc (BLOCKSZ);
    _ds_set_name_item (ds, "header");
    hio->fd = open (ds->namebuf, O_RDONLY);

    if (hio->fd < 0) {
	IO_ERRNO_ERRV (err, errno, "Failed to open header \"%s\"", 
		       ds->namebuf);
	goto bail;
    }

    while ((nread = io_fetch (hio, DS_HEADER_RECSIZE, 
			      (gchar **) &hitem, &suberr)) > 0) {
	gchar *data;
	gsize ndata;
	DSSmallItem *si;

	if (nread != DS_HEADER_RECSIZE) {
	    g_error ("Use a real error.");
	}

	if (hitem->alen < 5 && hitem->alen != 0)
	    g_error ("Invalid header: len < 5 but not 0");
	if (hitem->alen > DS_HEADER_MAXDSIZE)
	    g_error ("Invalid header: len > MAXDSIZE");

	/* DS_ITEMNAME_MAXLEN is > DS_HEADER_RECSIZE - 1, and the
	 * padding bytes are always NULs, so we should be able to
	 * safely use *data as a string giving the name of this
	 * item. Check this assumption though. x*/

	if (hitem->name[DS_ITEMNAME_MAXLEN] != '\0')
	    g_error ("Invalid header: expected NUL-terminated item name");

	si = g_new0 (DSSmallItem, 1);
	strcpy (si->name, hitem->name);

	if (hitem->alen == 0) {
	    /* No data for this item. */
	    si->type = DST_UNK;
	    si->nvals = 0;
	} else {
	    guint8 align;
	    gsize dlen;

	    ndata = io_fetch (hio, hitem->alen, &data, &suberr);

	    if (ndata < 0) {
		g_propagate_error (err, suberr);
		close (hio->fd);
		goto bail;
	    }

	    if (ndata != hitem->alen) {
		g_error ("read error!");
	    }

	    si->type = io_decode_i32 (data);

	    if (!DST_VALID (si->type)) {
		g_error ("Illegal type code!");
	    }

	    align = ds_type_aligns[si->type];
	    dlen = hitem->alen - 4;

	    if (4 % align > 0) {
		data += align - (4 % align);
		dlen -= align - (4 % align);
	    }

	    if (dlen % ds_type_sizes[si->type] != 0) {
		g_error ("Nonintegral number of elements?");
	    }

	    si->nvals = dlen / ds_type_sizes[si->type];

	    _io_decode_data (data, si->vals.text, si->type, si->nvals);

	    g_hash_table_insert (ds->small_items, si->name, si);

	    /*printf ("ITEM: %s %c %d\n", hitem->name, ds_type_codes[si->type],
	      dlen / ds_type_sizes[si->type]);*/
	}

	if (io_read_align (hio, DS_HEADER_RECSIZE, &suberr)) {
	    g_propagate_error (err, suberr);
	    close (hio->fd);
	    goto bail;
	}
    }
	    
    return ds;

bail:
    if (hio != NULL)
	io_free (hio);

    ds_close (ds);
    return NULL;
}

void
ds_close (Dataset *ds)
{
    if (ds->namebuf != NULL) {
	g_free (ds->namebuf);
	ds->namebuf = NULL;
    }

    if (ds->small_items) {
	g_free (ds->small_items);
	ds->small_items = NULL;
    }

    g_free (ds);
}

gboolean
ds_has_item (Dataset *ds, gchar *name)
{
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (strlen (name) <= DS_ITEMNAME_MAXLEN,
			  FALSE);

    if (g_hash_table_lookup (ds->small_items, name) != NULL)
	return TRUE;

    _ds_set_name_item (ds, name);
    return g_file_test (ds->namebuf, G_FILE_TEST_EXISTS);
}

/* Caller's reponsibility to free the list as well as its
 * contents */
GSList *
ds_list_items (Dataset *ds, GError **err)
{
    GSList *items = NULL;
    GDir *dir;
    const gchar *diritem;
    GHashTableIter hiter;

    _ds_set_name_dir (ds);
    dir = g_dir_open (ds->namebuf, 0, err);

    if (dir == NULL)
	return NULL;

    while ((diritem = g_dir_read_name (dir)) != NULL) {
	if (strlen (diritem) > DS_ITEMNAME_MAXLEN)
	    continue;

	if (g_hash_table_lookup (ds->small_items, diritem) != NULL) {
	    g_set_error (err, G_FILE_ERROR, G_FILE_ERROR_INVAL,
			 "Invalid dataset: item %s has both file "
			 "and header entry.", diritem);
	    g_slist_foreach (items, (GFunc) g_free, NULL);
	    g_slist_free (items);
	}

	items = g_slist_prepend (items, g_strdup (diritem));
    }

    g_hash_table_iter_init (&hiter, ds->small_items);

    while (g_hash_table_iter_next (&hiter, (gpointer *) &diritem, 
				   NULL))
	items = g_slist_prepend (items, g_strdup (diritem));

    return items;
}

IOStream *
ds_open_large (Dataset *ds, gchar *name, DSMode mode, GError **err)
{
    IOStream *io;

    g_assert (mode == DSM_READ); /* FIXME */

    io = _io_alloc (BLOCKSZ);
    _ds_set_name_item (ds, name);
    io->fd = open (ds->namebuf, O_RDONLY); /* FIXME */

    if (io->fd < 0) {
	IO_ERRNO_ERRV (err, errno, "Failed to open item file \"%s\"",
		       ds->namebuf);
	io_free (io);
	return NULL;
    }

    return io;
}

extern gboolean uv_demo (Dataset *ds, GError **err);

int
main (int argc, char **argv)
{
    Dataset *ds;
    GError *err = NULL;
    GSList *items, *items0;

    if (argc != 2)
	g_error ("Usage: %s [fname]", argv[0]);

    ds = ds_open (argv[1], DSM_READ, &err);

    if (err != NULL)
	g_error (err->message);

    /* List items */

    items = ds_list_items (ds, &err);
    items0 = items;

    if (items == NULL)
	g_error (err->message);

    while (items != NULL) {
	printf ("  %s\n", (gchar *) items->data);
	g_free (items->data);
	items = items->next;
    }

    g_slist_free (items0);

    /* UV stuff */

    if (uv_demo (ds, &err))
	g_error (err->message);

    /* All done */

    ds_close (ds);

    return 0;
}
