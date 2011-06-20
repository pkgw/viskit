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
#include <ctype.h> /*isprint etc*/
#include <stdio.h> /*rename*/

/* Note that these limits are set by the file format and MUST NOT be
 * changed by the user. Doing so will break compatibility with the
 * file format. */

#define DS_ITEMNAME_MAXLEN 8 /* bytes */
#define DS_HEADER_RECSIZE 16 /* bytes */
#define DS_HEADER_MAXDSIZE 64 /* bytes */

struct _Dataset {
    gsize  namelen;
    gchar *namebuf;
    IOMode mode;
    DSOpenFlags oflags;
    GHashTable *small_items;
    gboolean header_dirty;
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

#define DSI_DATA(small) ((gpointer) small->vals.i8)

static IOStream *_ds_open_large_item_full (Dataset *ds, const gchar *name,
					   IOMode mode, DSOpenFlags flags,
					   gboolean trunc_ok, gboolean check_name,
					   int *errno_dest, GError **err);
static gboolean _ds_item_name_ok (const gchar *name, GError **err);
static gboolean _ds_rename_large_item_full (Dataset *ds, const gchar *oldname,
					    const gchar *newname, gboolean check_new_name,
					    GError **err);

GQuark
ds_error_quark (void)
{
    return g_quark_from_static_string ("ds-error-quark");
}


const gchar *
ds_error_describe (DSError error)
{
    switch (error) {
    case DS_ERROR_NO_ERROR:
	return "no error";
    case DS_ERROR_FORMAT:
	return "invalid data format";
    case DS_ERROR_INTERNAL_PERMS:
	return "operation not permitted in this mode";
    case DS_ERROR_ITEM_NAME:
	return "illegal item name";
    case DS_ERROR_NONEXISTANT:
	return "does not exist";
    default:
	return "unknown error";
    }
}


void
_ds_set_name_dir (Dataset *ds)
{
    ds->namebuf[ds->namelen] = '\0';
}

void
_ds_set_name_item (Dataset *ds, const char *item)
{
    /* It's assumed here that we've sanitychecked
     * that @item is less than 8 characters long. */

    ds->namebuf[ds->namelen] = '/';
    strcpy (ds->namebuf + ds->namelen + 1, item);
}


static gboolean
_ds_truncate (Dataset *ds, GError **err)
{
    gboolean retval = TRUE;
    GDir *dir;
    const gchar *diritem;

    _ds_set_name_dir (ds);

    if ((dir = g_dir_open (ds->namebuf, 0, err)) == NULL)
	return TRUE;

    while ((diritem = g_dir_read_name (dir)) != NULL) {
	gchar *full = g_strdup_printf ("%s/%s", ds->namebuf, diritem);

	if (unlink (full)) {
	    IO_ERRNO_ERRV (err, errno, "Failed to unlink item file \"%s\"",
			   full);
	    g_free (full);
	    goto bail;
	}

	g_free (full);
    }

    retval = FALSE;
bail:
    g_dir_close (dir);
    return retval;
}


static gboolean
_ds_read_header (Dataset *ds, GError **err)
{
    gboolean retval = TRUE;
    IOStream *hio;
    GError *suberr;
    gsize nread;
    DSHeaderItem *hitem;

    if ((hio = ds_open_large_item (ds, "header", IO_MODE_READ, 0, err)) == NULL)
	goto bail;

    while ((nread = io_read_into_temp_buf (hio, DS_HEADER_RECSIZE,
					   (gpointer *) &hitem, &suberr)) > 0) {
	gchar *data;
	gsize ndata;
	DSSmallItem *si;

	if (nread != DS_HEADER_RECSIZE) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT, 
			 "Invalid dataset header: incomplete record");
	    goto bail;
	}

	if (hitem->alen < 5 && hitem->alen != 0) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid dataset header: bad record length");
	    goto bail;
	}

	if (hitem->alen > DS_HEADER_MAXDSIZE) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid dataset header: record len > MAXDSIZE");
	    goto bail;
	}

	/* DS_ITEMNAME_MAXLEN is > DS_HEADER_RECSIZE - 1, and the
	 * padding bytes are always NULs, so we should be able to
	 * safely use *data as a string giving the name of this
	 * item. Check this assumption though. */

	if (hitem->name[DS_ITEMNAME_MAXLEN] != '\0') {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid dataset header: non NUL-terminated item name");
	    goto bail;
	}

	si = g_new0 (DSSmallItem, 1);
	strcpy (si->name, hitem->name);

	if (hitem->alen == 0) {
	    /* No data for this item. */
	    si->type = DST_BIN;
	    si->nvals = 0;
	} else {
	    guint8 align;
	    gsize dlen;

	    ndata = io_read_into_temp_buf (hio, hitem->alen, (gpointer *) &data,
					   &suberr);

	    if (ndata < 0) {
		g_propagate_error (err, suberr);
		goto bail;
	    }

	    if (ndata != hitem->alen) {
		g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			     "Invalid dataset header: incomplete small item");
		goto bail;
	    }

	    si->type = IO_RECODE_I32 (data);

	    if (!DST_VALID (si->type)) {
		g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			     "Invalid dataset header: illegal type code");
		goto bail;
	    }

	    /* The header-writing code aligns based on the type sizes,
	     * not the type alignment values (which is relevant for
	     * complex-valued headers) */

	    align = MAX (4, ds_type_sizes[si->type]);
	    data += align;
	    dlen = hitem->alen - align;

	    if (dlen % ds_type_sizes[si->type] != 0) {
		g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			     "Invalid dataset header: nonintegral number of values");
		goto bail;
	    }

	    si->nvals = dlen / ds_type_sizes[si->type];
	    io_recode_data_copy (data, si->vals.text, si->type, si->nvals);
	    g_hash_table_insert (ds->small_items, si->name, si);
	}

	if (io_nudge_align (hio, DS_HEADER_RECSIZE, &suberr)) {
	    g_propagate_error (err, suberr);
	    goto bail;
	}
    }

    retval = FALSE;
bail:
    if (io_close_and_free (hio, err))
	retval = TRUE;
    return retval;
}

Dataset *
ds_open (const char *filename, IOMode mode, DSOpenFlags flags, GError **err)
{
    Dataset *ds;
    gboolean created = FALSE;

    g_return_val_if_fail (filename != NULL, NULL);
    g_return_val_if_fail (mode != IO_MODE_READ_WRITE, NULL);

    if ((mode & IO_MODE_WRITE) && (flags & DS_OFLAGS_EXIST_BAD)) {
	flags |= DS_OFLAGS_CREATE_OK;

	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
	    g_set_error (err, G_FILE_ERROR, G_FILE_ERROR_EXIST,
			 "Error creating dataset \"%s\": a "
			 "file of that name already exists.", filename);
	    return NULL;
	}
    }

    if ((mode & IO_MODE_WRITE) && (flags & DS_OFLAGS_CREATE_OK)) {
	/* Just go ahead and try to create the directory since it
	 * will need to exist. If we don't get EEXIST or success,
	 * there's gonna be a problem anyway. */
	if (mkdir (filename, 0755) == 0)
	    created = TRUE;
	else {
	    if (errno != EEXIST) {
		IO_ERRNO_ERR (err, errno, "Couldn't create dataset directory");
		return NULL;
	    }
	}
    } else {
	/* This isn't a fully comprehensive test, and as always it is
	 * subject to races. We'll have a better idea of whether this
	 * DS is ok when we try to read in the header. */

	if (!g_file_test (filename, G_FILE_TEST_IS_DIR)) {
	    g_set_error (err, G_FILE_ERROR, G_FILE_ERROR_NOTDIR,
			 "Cannot open the dataset \"%s\" since it "
			 "does not exist or is not a directory.", filename);
	    return NULL;
	}
    }

    ds = g_new0 (Dataset, 1);
    ds->namelen = strlen (filename);
    /* The + 8 is padding to allow for temporary illegal item names when,
     * e.g., rewriting certain items. */
    ds->namebuf = g_new (gchar, ds->namelen + DS_ITEMNAME_MAXLEN + 2 + 8);
    strcpy (ds->namebuf, filename);
    ds->mode = mode;
    ds->oflags = flags;
    ds->header_dirty = created; /* Write blank header if creating dset */
    ds->small_items = g_hash_table_new_full (g_str_hash,
					     g_str_equal,
					     NULL, g_free);

    if (!created) {
	if ((mode & IO_MODE_WRITE) && (flags & DS_OFLAGS_TRUNCATE)) {
	    if (_ds_truncate (ds, err))
		goto bail;
	} else {
	    if (_ds_read_header (ds, err))
		goto bail;
	}
    }

    return ds;

bail:
    ds_close (ds, NULL);
    return NULL;
}


static gboolean
_ds_rename_large_item_full (Dataset *ds, const gchar *oldname, const gchar *newname,
			    gboolean check_new_name, GError **err)
{
    gchar *oldpath, *newpath;
    gboolean retval = FALSE;

    g_assert (ds->mode & IO_MODE_WRITE);

    if (check_new_name && !_ds_item_name_ok (newname, err))
	return TRUE;

    _ds_set_name_dir (ds);
    oldpath = g_strdup_printf ("%s/%s", ds->namebuf, oldname);
    newpath = g_strdup_printf ("%s/%s", ds->namebuf, newname);
    if (rename (oldpath, newpath)) {
	IO_ERRNO_ERRV (err, errno, "Failed to rename \"%s\" -> \"%s\"",
		       oldpath, newpath);
	retval = TRUE;
    }
    g_free (oldpath);
    g_free (newpath);
    return retval;
}


gboolean
ds_rename_large_item (Dataset *ds, const gchar *oldname, const gchar *newname,
		      GError **err)
{
    return _ds_rename_large_item_full (ds, oldname, newname, TRUE, err);
}


gboolean
ds_write_header (Dataset *ds, GError **err)
{
    IOStream *hio;
    GHashTableIter hiter;
    DSSmallItem *small;

    g_assert (ds->mode & IO_MODE_WRITE);

    /* Write out new header alongside old one */

    if ((hio = ds_open_large_item_for_replace (ds, "header", err)) == NULL)
	return TRUE;

    g_hash_table_iter_init (&hiter, ds->small_items);

    while (g_hash_table_iter_next (&hiter, (gpointer *) &small, NULL)) {
	DSHeaderItem hitem;
	guint8 dsize;
	gint32 typecode;

	if (io_nudge_align (hio, DS_HEADER_RECSIZE, err))
	    goto bail;

	/* The header */

	memset (&hitem, 0, sizeof (hitem));
	strcpy (hitem.name, small->name);
	dsize = small->nvals * ds_type_sizes[small->type];
	if (dsize != 0) {
	    dsize += 4 % ds_type_aligns[small->type]; /* alignment */
	    dsize += 4; /* type code */
	}
	hitem.alen = dsize;

	if (io_write_raw (hio, sizeof (hitem), &hitem, err))
	    goto bail;

	if (dsize == 0)
	    continue;

	typecode = small->type;
	if (typecode == DST_TEXT)
	    /* Annoying dataset format quirk. */
	    typecode = DST_I8;
	typecode = GINT32_FROM_BE (typecode);
	if (io_write_raw (hio, 4, &typecode, err))
	    goto bail;

	if (io_nudge_align (hio, ds_type_aligns[small->type], err))
	    goto bail;

	if (io_write_typed (hio, small->type, small->nvals,
			    DSI_DATA (small), err))
	    goto bail;
    }

    if (io_close_and_free (hio, err))
	return TRUE;

    /* Move it into place. Avoid ds_finish_large_item_replace since it will
     * reject the destination name "header". */

    if (_ds_rename_large_item_full (ds, "header+new", "header", FALSE, err))
	return TRUE;

    ds->header_dirty = FALSE;
    return FALSE;

bail:
    io_close_and_free (hio, NULL);
    return TRUE;
}


gboolean
ds_close (Dataset *ds, GError **err)
{
    gboolean retval = FALSE;

    if (ds == NULL)
	return FALSE;

    if (ds->header_dirty)
	if (ds_write_header (ds, err))
	    retval = TRUE;

    if (ds->namebuf != NULL) {
	g_free (ds->namebuf);
	ds->namebuf = NULL;
    }

    if (ds->small_items) {
	g_hash_table_destroy (ds->small_items);
	ds->small_items = NULL;
    }

    g_free (ds);
    return retval;
}


gboolean
ds_has_item (Dataset *ds, const gchar *name)
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
gboolean
ds_list_items (Dataset *ds, GSList **items, GError **err)
{
    GSList *itemwork = NULL;
    GDir *dir;
    const gchar *diritem;
    GHashTableIter hiter;

    *items = NULL;
    _ds_set_name_dir (ds);
    dir = g_dir_open (ds->namebuf, 0, err);

    if (dir == NULL)
	return TRUE;

    while ((diritem = g_dir_read_name (dir)) != NULL) {
	if (strlen (diritem) > DS_ITEMNAME_MAXLEN)
	    continue;

	if (strcmp (diritem, "header") == 0)
	    continue;

	if (g_hash_table_lookup (ds->small_items, diritem) != NULL) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid dataset: item %s has both file "
			 "and header entry.", diritem);
	    g_slist_foreach (itemwork, (GFunc) g_free, NULL);
	    g_slist_free (itemwork);
	    return TRUE;
	}

	itemwork = g_slist_prepend (itemwork, g_strdup (diritem));
    }

    g_dir_close (dir);
    g_hash_table_iter_init (&hiter, ds->small_items);

    while (g_hash_table_iter_next (&hiter, (gpointer *) &diritem, 
				   NULL))
	itemwork = g_slist_prepend (itemwork, g_strdup (diritem));

    *items = itemwork;
    return FALSE;
}

static IOStream *
_ds_open_large_item_full (Dataset *ds, const gchar *name, IOMode mode,
			  DSOpenFlags flags, gboolean trunc_ok,
			  gboolean check_name, int *errno_dest, GError **err)
{
    /* Note: this function is called in ds_open to read the header, so
     * keep in mind that ds may not be fully initialized. */

    int fd, oflags = 0;

    switch (mode) {
    case IO_MODE_READ:
	oflags = O_RDONLY;
	break;
    case IO_MODE_WRITE:
	oflags = O_WRONLY;

	if (flags & DS_OFLAGS_CREATE_OK)
	    oflags |= O_CREAT;

	if (flags & DS_OFLAGS_EXIST_BAD)
	    oflags |= O_CREAT | O_EXCL;

	if ((oflags & O_CREAT) && check_name && !_ds_item_name_ok (name, err))
	    return NULL;

	if (flags & DS_OFLAGS_TRUNCATE) {
	    if (ds->oflags & DS_OFLAGS_APPEND && !trunc_ok) {
		g_set_error (err, DS_ERROR, DS_ERROR_INTERNAL_PERMS, "Cannot "
			     "truncate item \"%s\" with dataset in append mode", name);
		return NULL;
	    }
	    oflags |= O_TRUNC;
	} else if (flags & DS_OFLAGS_APPEND)
	    oflags |= O_APPEND;
	else
	    g_assert_not_reached ();
	break;
    default:
	/* Simultaneous read-write not allowed. */
	g_assert_not_reached ();
    }

    _ds_set_name_item (ds, name);
    fd = open (ds->namebuf, oflags, 0644);

    if (fd < 0) {
	IO_ERRNO_ERRV (err, errno, "Failed to open item file \"%s\"",
		       ds->namebuf);
	if (errno_dest != NULL)
	    *errno_dest = errno;
	return NULL;
    }

    /* FIXME: if opening for write and not truncating, we should
     * figure out the alignment hint ... and prefill the write
     * buffer :-( */

    return io_new_from_fd (mode, fd, 0, 0);
}

IOStream *
ds_open_large_item (Dataset *ds, const gchar *name, IOMode mode,
		    DSOpenFlags flags, GError **err)
{
    return _ds_open_large_item_full (ds, name, mode, flags, FALSE, TRUE, NULL, err);
}

IOStream *
ds_open_large_item_for_replace (Dataset *ds, const gchar *name, GError **err)
{
    gchar *repname;
    IOStream *retval;

    repname = g_strconcat (name, "+new", NULL);
    retval = _ds_open_large_item_full (ds, repname, IO_MODE_WRITE,
				       DS_OFLAGS_TRUNCATE | DS_OFLAGS_CREATE_OK,
				       TRUE, FALSE, NULL, err);
    g_free (repname);
    return retval;
}

gboolean
ds_finish_large_item_replace (Dataset *ds, const gchar *name, GError **err)
{
    gchar *repname;
    gboolean retval;

    repname = g_strconcat (name, "+new", NULL);
    retval = _ds_rename_large_item_full (ds, repname, name, TRUE, err);
    g_free (repname);
    return retval;
}


static gboolean
_ds_probe_large_item (Dataset *ds, const gchar *name, DSType *type,
		      gsize *nvals, int *openerr, GError **err)
{
    IOStream *io;
    gssize nread;
    gchar *data;
    guint32 v;
    gboolean retval;
    struct stat statbuf;
    int ofs;

    *type = DST_BIN;
    *nvals = 0;
    retval = TRUE;

    if ((io = _ds_open_large_item_full (ds, name, IO_MODE_READ, 0, FALSE,
					TRUE, openerr, err)) == NULL)
	return TRUE;

    if (fstat (io_get_fd (io), &statbuf)) {
	IO_ERRNO_ERRV (err, errno, "Unable to stat dataset item \"%s\"", name);
	goto done;
    }

    nread = io_read_into_temp_buf (io, 4, (gpointer *) &data, err);

    if (nread < 0)
	goto done;

    /* From here on out, we've done all of the IO that we need to,
     * so even if we can't identify the type we won't encounter
     * an error. */

    retval = FALSE;

    if (nread != 4)
	/* Unknown. */
	goto done;

    v = IO_RECODE_I32 (data);

    switch (v) {
    case DST_I8:
    case DST_I32:
    case DST_I16:
    case DST_F32:
    case DST_F64:
    case DST_C64:
    case DST_I64:
	*type = (DSType) v;

	ofs = MIN (4, ds_type_aligns[*type]);
	*nvals = (statbuf.st_size - ofs) / ds_type_sizes[*type];

	if (*nvals * ds_type_sizes[*type] + ofs != statbuf.st_size) {
	    *type = DST_BIN;
	    *nvals = 0;
	}

	goto done;
    case 0:
	/* Mixed binary type. Express it as DST_BIN but give it
	 * a size in bytes. */
	*nvals = statbuf.st_size - 4;
    default:
	break;
    }

    /* First four bytes didn't indicate type. Does this look like ASCII
     * text? */

    for (ofs = 0; ofs < 4; ofs++)
	if (!isprint ((int) data[ofs]))
	    break;

    if (ofs == 4) {
	*type = DST_TEXT;
	*nvals = statbuf.st_size;
    }

done:
    io_close_and_free (io, NULL);
    return retval;
}


gboolean
ds_probe_item (Dataset *ds, const gchar *name, DSItemInfo **info, GError **err)
{
    DSItemInfo *dii;
    DSSmallItem *small;
    gboolean is_large;
    DSType type;
    gsize nvals;
    int openerr;

    *info = NULL;
    small = (DSSmallItem *) g_hash_table_lookup (ds->small_items, name);
    is_large = (small == NULL);

    if (!is_large) {
	type = small->type;
	nvals = small->nvals;
    } else {
	if (_ds_probe_large_item (ds, name, &type, &nvals, &openerr, err))
	    return openerr != ENOENT;
    }

    *info = dii = g_new0 (DSItemInfo, 1);
    dii->name = g_strdup (name);
    dii->is_large = is_large;
    dii->type = type;
    dii->nvals = nvals;

    if (!is_large)
	memcpy (dii->small.i8, DSI_DATA (small), nvals * ds_type_sizes[type]);

    return FALSE;
}

void
ds_item_info_free (DSItemInfo *dii)
{
    g_free (dii->name);
    g_free (dii);
}

gboolean
ds_get_item_i64 (Dataset *ds, const gchar *name, gint64 *val)
{
    DSSmallItem *small;

    small = (DSSmallItem *) g_hash_table_lookup (ds->small_items, name);

    if (small == NULL)
	return TRUE;

    if (small->nvals != 1)
	return TRUE;

    return ds_type_upconvert (small->type, DSI_DATA(small), DST_I64,
			      (gpointer) val, 1);
}

gboolean
ds_get_item_f64 (Dataset *ds, const gchar *name, gdouble *val)
{
    DSSmallItem *small;

    small = (DSSmallItem *) g_hash_table_lookup (ds->small_items, name);

    if (small == NULL)
	return TRUE;

    if (small->nvals != 1)
	return TRUE;

    return ds_type_upconvert (small->type, DSI_DATA (small), DST_F64,
			      (gpointer) val, 1);
}

gchar *
ds_get_item_small_string (Dataset *ds, const gchar *name)
{
    DSSmallItem *small;

    small = (DSSmallItem *) g_hash_table_lookup (ds->small_items, name);

    if (small == NULL)
	return NULL;

    if (small->type != DST_I8)
	/* Note: textual small items are stored with a type
	 * indicator of i8, not text. Unsure if there is a way to
	 * distinguish between the two -- probably nothing creates
	 * a small item with an actual type of i8 unless it's
	 * trying to break things intentionally. */
	return NULL;

    return g_strndup (small->vals.text, small->nvals);
}

static gboolean
_ds_item_name_ok (const gchar *name, GError **err)
{
    size_t len;
    int i;

    g_return_val_if_fail (name != NULL, FALSE);

    len = strlen (name);

    if (len < 1 || len > 8)
	goto invalid;

    if (len == 6 && strcmp (name, "header") == 0)
	goto invalid;

    if (!islower (name[0]))
	goto invalid;

    for (i = 1; i < len; i++) {
	int c = (int) name[i];

	if (!(islower (c) || isdigit (c) || c == '-' || c == '_'))
	    goto invalid;
    }

    return TRUE;

invalid:
    g_set_error (err, DS_ERROR, DS_ERROR_ITEM_NAME,
		 "Bad item name \"%s\"", name);
    return FALSE;
}

DSError
ds_set_small_item (Dataset *ds, const gchar *name, DSType type, gsize nvals,
		   gpointer data, gboolean create_ok)
{
    DSSmallItem *small;

    if (!(ds->mode & IO_MODE_WRITE))
	return DS_ERROR_INTERNAL_PERMS;

    if (nvals * ds_type_sizes[type] > 64)
	return DS_ERROR_FORMAT;

    small = (DSSmallItem *) g_hash_table_lookup (ds->small_items, name);

    if (small != NULL) {
	/* Can't modify existing items in append mode. */
	if (ds->oflags & DS_OFLAGS_APPEND)
	    return DS_ERROR_INTERNAL_PERMS;
    } else {
	if (!create_ok)
	    return DS_ERROR_NONEXISTANT;

	if (!_ds_item_name_ok (name, NULL))
	    return DS_ERROR_ITEM_NAME;

	small = g_new0 (DSSmallItem, 1);
	strcpy (small->name, name);
	g_hash_table_insert (ds->small_items, small->name, small);
    }

    small->type = type;
    small->nvals = nvals;
    memcpy (DSI_DATA (small), data, nvals * ds_type_sizes[type]);
    ds->header_dirty = TRUE;
    return DS_ERROR_NO_ERROR;
}
