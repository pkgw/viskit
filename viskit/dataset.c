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

GQuark
ds_error_quark (void)
{
    return g_quark_from_static_string ("ds-error-quark");
}

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
    InputStream hio;
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
    ds->namebuf = g_new (gchar, ds->namelen + DS_ITEMNAME_MAXLEN + 2);
    strcpy (ds->namebuf, filename);
    ds->mode = mode;
    ds->small_items = g_hash_table_new_full (g_str_hash,
					     g_str_equal,
					     NULL, g_free);

    /* Read in that thar header */

    io_input_init (&hio, 0);
    if (ds_open_large (ds, "header", DSM_READ, &hio, err))
	goto bail;

    while ((nread = io_fetch_temp (&hio, DS_HEADER_RECSIZE,
				   (gchar **) &hitem, &suberr)) > 0) {
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
	    si->type = DST_UNK;
	    si->nvals = 0;
	} else {
	    guint8 align;
	    gsize dlen;

	    ndata = io_fetch_temp (&hio, hitem->alen, &data, &suberr);

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

	    /* We may have to jump around a bit to realign ourselves if
	     * this small item is an 8-byte type. */

	    align = ds_type_aligns[si->type];
	    dlen = hitem->alen - 4;

	    if (4 % align > 0) {
		data += align - (4 % align);
		dlen -= align - (4 % align);
	    }

	    if (dlen % ds_type_sizes[si->type] != 0) {
		g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			     "Invalid dataset header: nonintegral number of values");
		goto bail;
	    }

	    si->nvals = dlen / ds_type_sizes[si->type];
	    io_recode_data_copy (data, si->vals.text, si->type, si->nvals);
	    g_hash_table_insert (ds->small_items, si->name, si);
	}

	if (io_nudge_align (&hio, DS_HEADER_RECSIZE, &suberr)) {
	    g_propagate_error (err, suberr);
	    goto bail;
	}
    }
	    
    return ds;

bail:
    if (hio.fd >= 0)
	close (hio.fd);

    io_input_uninit (&hio);
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

gboolean
ds_open_large (Dataset *ds, gchar *name, DSMode mode, InputStream *io, GError **err)
{
    /* Note: this function is called in ds_open to read the header, so
     * keep in mind that ds may not be fully initialized. */

    g_return_val_if_fail (io != NULL, TRUE);
    g_assert (mode == DSM_READ); /* FIXME */

    _ds_set_name_item (ds, name);
    io->fd = open (ds->namebuf, O_RDONLY); /* FIXME */

    if (io->fd < 0) {
	IO_ERRNO_ERRV (err, errno, "Failed to open item file \"%s\"",
		       ds->namebuf);
	return TRUE;
    }

    return FALSE;
}
