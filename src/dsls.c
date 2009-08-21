#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include <glib.h>

/* Note that these limits are set by the file format and MUST NOT be
 * changed by the user. Doing so will break compatibility with the
 * file format. */

#define DS_ITEMNAME_MAXLEN 8 /* bytes */
#define DS_HEADER_RECSIZE 16 /* bytes */
#define DS_HEADER_MAXDSIZE 64 /* bytes */
#define ds_recsz_roundup(n) (((n) + 15) & ~0xF)

typedef enum _DSItemType {
    DSIT_UNK  = 0, /* not an actual type used in the file format */
    DSIT_I8   = 1,
    DSIT_I32  = 2,
    DSIT_I16  = 3,
    DSIT_F32  = 4,
    DSIT_F64  = 5,
    DSIT_TEXT = 6, /* written as DSIT_I8 in the file format */
    DSIT_C64  = 7,
    DSIT_I64  = 8
} DSItemType;

#define DS_NUM_TYPES 8

static const char ds_item_codes[] = "?bijrdacl";

static const gsize ds_item_sizes[] = {
    /* Item sizes in bytes. */
    0, 1, 4, 2, 4, 8, 1, 8, 8
};

static const gsize ds_item_aligns[] = {
    /* Alignment requirements for datatypes.  Identical to item sizes
     * except for the complex type, which can be aligned to the size
     * of its constituents. */
    0, 1, 4, 2, 4, 8, 1, 4, 8
};

typedef struct _c64 {
    gfloat real;
    gfloat imag;
} c64;

typedef union _DSPackF32 {
    gchar b[4];
    guint32 i;
    gfloat f;
} DSPackF32;

typedef union _DSPackF64 {
    gchar b[8];
    guint64 i;
    gdouble f;
} DSPackF64;

typedef union _DSBufptr {
    gpointer any;
    gint8 *i8;
    gint16 *i16;
    gint32 *i32;
    gint64 *i64;
    gfloat *f32;
    gdouble *f64;
    DSPackF32 *pf32;
    DSPackF64 *pf64;
    gchar *text;
} DSBufptr;

typedef union _DSHeaderItem {
    /* Helps handling loading items from the header file.
     * Take care that the values here (particularly 'type')
     * are not necessarily in the host's endianness. */
    
    struct {
	/* no padding to 64-bit alignment */
	gchar name[15];
	gint8 alen; /* aligned length */
	gint32 type;
	gchar data[1]; /* no padding */
    } np;

    struct {
	/* yes padding to 64-bit alignment */
	gchar name[15];
	gint8 alen; /* aligned length */
	gint32 type;
	gint32 _pad;
	gchar data[1]; /* no padding */
    } p;
} DSHeaderItem;

#define DS_HI_DATA(hi,type) ((ds_item_aligns[type] > 4) ? \
			     (hi)->p.data : (hi)->np.data)

#define DS_HI_DLEN(type,alen) ((ds_item_aligns[type] > 4) ? \
			       (alen) - 8 : (alen) - 4)

typedef struct _Dataset {
    gchar *oname;
    gchar *fnscratch;
    GDir *dir;
    GMappedFile *header;
    GHashTable *items;
} Dataset;

typedef struct _DSSmallItem {
    /* Shared entries between SmallItem and LargeItem */
    gboolean large;

    /* SmallItem-specific entries */

    DSItemType type;
    gsize nvals;

    union {
	gint8 i8[64];
	gint16 i16[32];
	gint32 i32[16];
	gint64 i64[8];
	gfloat f32[16];
	gdouble f64[8];
	gchar text[65]; /* byte for null terminator */
    } d;
} DSSmallItem;

typedef struct _DSLargeItem {
    /* Shared entries between SmallItem and LargeItem */
    gboolean large;
} DSLargeItem;

void ds_free (Dataset *ds);
Dataset *ds_open (const char *name);

/* **************************************** */

void 
_ds_mkitemfn (Dataset *ds, const gchar *name)
{
    strcpy (ds->fnscratch + strlen (ds->oname) + 1, name);
}

void
_ds_decode_data (gchar *src, gchar *dest, DSItemType type, 
		 gsize nvals)
{
    gsize i;
    DSBufptr bsrc, bdest;

    bsrc.any = src;
    bdest.any = dest;

    switch (type) {
    case DSIT_UNK:
	if (nvals != 0)
	    g_error ("Copying more than 0 unknown values");
	break;
    case DSIT_I8:
    case DSIT_TEXT:
	memcpy (dest, src, nvals);
	break;
    case DSIT_I16:
	for (i = 0; i < nvals; i++) {
	    *(bdest.i16) = GINT16_FROM_BE(*(bsrc.i16));
	    bdest.i16++;
	    bsrc.i16++;
	}
	break;
    case DSIT_C64:
	nvals *= 2;
	/* fall through */
    case DSIT_I32:
    case DSIT_F32:
	for (i = 0; i < nvals; i++) {
	    *(bdest.i32) = GINT32_FROM_BE(*(bsrc.i32));
	    bdest.i32++;
	    bsrc.i32++;
	}
	break;
    case DSIT_I64:
    case DSIT_F64:
	for (i = 0; i < nvals; i++) {
	    *(bdest.i64) = GINT64_FROM_BE(*(bsrc.i64));
	    bdest.i64++;
	    bsrc.i64++;
	}
	break;
    default:
	g_error ("Oh no unhandled data type.");
	break;
    }
}

void 
_ds_dump_data (gchar *buf, gint32 type, gsize nvals)
{
    int i;
    DSBufptr ptr;

    ptr.text = buf;

    switch (type) {
    case DSIT_UNK:
	if (nvals != 0)
	    g_error ("Trying to dump more than 0 unknown-type values.");
	printf ("???");
	break;
    case DSIT_I8:
	for (i = 0; i < nvals; i++)
	    printf ("%d ", (int) ptr.i8[i]);
	break;
    case DSIT_I16:
	for (i = 0; i < nvals; i++)
	    printf ("%d ", (int) ptr.i16[i]);
	break;
    case DSIT_I32:
	for (i = 0; i < nvals; i++)
	    printf ("%d ", (int) ptr.i32[i]);
	break;
    case DSIT_I64:
	for (i = 0; i < nvals; i++)
	    printf ("%ld ", (long int) ptr.i64[i]);
	break;
    case DSIT_F32:
	for (i = 0; i < nvals; i++) {
	    printf ("%g ", ptr.f32[i]);
	}
	break;
    case DSIT_F64:
	for (i = 0; i < nvals; i++) {
	    printf ("%lg ", ptr.f64[i]);
	}
	break;
    case DSIT_TEXT:
	/* String is not NUL-terminated. */
	for (i = 0; i < nvals; i++)
	    printf ("%c", ptr.text[i]);
	break;
    default:
	g_error ("Oh no unhandled data type.");
    }
}

DSSmallItem *
_dssi_new_hitem (DSHeaderItem *hitem)
{
    DSSmallItem *si;

    /* We assume here that the headeritem has been
     * verified to be valid. */

    si = g_new (DSSmallItem, 1);
    si->large = FALSE;

    if (hitem->p.alen == 0) {
	si->type = DSIT_UNK;
	si->nvals = 0;
    } else {
	gsize dlen;
 
	si->type = GINT32_FROM_BE(hitem->p.type);
	dlen = DS_HI_DLEN(si->type,hitem->p.alen);
	si->nvals = dlen / ds_item_sizes[si->type];
    }

    _ds_decode_data (DS_HI_DATA(hitem,si->type), si->d.text,
		     si->type, si->nvals);
    return si;
}

void
_dssi_i8_to_text (DSSmallItem *si)
{
    g_assert (si->type == DSIT_I8);

    si->type = DSIT_TEXT;
    si->d.text[si->nvals] = '\0';
}

void
_dssi_dump (DSSmallItem *si)
{
    printf ("%c %d: ", ds_item_codes[si->type], si->nvals);
    _ds_dump_data (si->d.text, si->type, si->nvals);
}

DSLargeItem *
_dsli_new (void)
{
    DSLargeItem *li;

    li = g_new (DSLargeItem, 1);
    li->large = TRUE;

    return li;
}

void
_ds_dump_items (Dataset *ds)
{
    GHashTableIter iter;
    gchar *name;
    DSSmallItem *si;

    g_hash_table_iter_init (&iter, ds->items);

    while (g_hash_table_iter_next (&iter, (gpointer *) &name, 
				   (gpointer *) &si)) {
	printf ("%8s: ", name);

	if (si->large)
	    printf ("large item");
	else
	    _dssi_dump (si);

	printf ("\n");
    }
}

void
_ds_parseheader (Dataset *ds, gchar *data, gsize len)
{
    gsize ofs = 0;
    
    while (ofs < len) {
	DSHeaderItem *hitem;
	DSSmallItem *sitem;
	gint8 alen, dlen;
	gint32 type;
	gint8 nvals;

	if ((ofs - len) < DS_HEADER_RECSIZE)
	    g_error ("Invalid header: expected >16 b left.");

	hitem = (DSHeaderItem *) (data + ofs);
	alen = hitem->p.alen;

	if (alen < 5 && alen != 0)
	    g_error ("Invalid header: len < 5 but not 0");
	if (alen > DS_HEADER_MAXDSIZE)
	    g_error ("Invalid header: len > MAXDSIZE");

	if ((ofs - len) < alen + DS_HEADER_RECSIZE)
	    g_error ("Invalid header: expected >%d b left.",
		     alen + DS_HEADER_RECSIZE);

	/* DS_ITEMNAME_MAXLEN is > DS_HEADER_RECSIZE - 1, and the
	 * padding bytes are always NULs, so we should be able to
	 * safely use *data as a string giving the name of this
	 * item. Check this assumption though. x*/

	if (hitem->p.name[DS_ITEMNAME_MAXLEN] != '\0')
	    g_error ("Invalid header: expected NUL-terminated item name");
	
	ofs += DS_HEADER_RECSIZE + ds_recsz_roundup(alen);

	if (alen == 0) {
	    /* No data if 0-length. */

	    type = DSIT_UNK;
	    dlen = nvals = 0;
	} else {
	    /* There are data. Get the item type. */

	    type = GINT_FROM_BE(hitem->p.type);

	    if (type < 1 || type > DS_NUM_TYPES) 
		g_error ("Unknown or invalid item type %d", type);

	    /* Now that we know the type, we can figure out how many
	     * values are in this item. Probably a better way to do
	     * this check, especially since item sizes are all powers
	     * of 2. */
	
	    dlen = DS_HI_DLEN(type,alen);
	    nvals = dlen / ds_item_sizes[type];

	    if (nvals * ds_item_sizes[type] != dlen)
		g_error ("Non-integral number of values in item %s: "
			 "%u bytes, itemsize %u", hitem->p.name, dlen,
			 ds_item_sizes[type]);
	}

	/* Decode the header item into a host-endian smallitem
	 * entry. */

	sitem = _dssi_new_hitem (hitem);
	g_hash_table_insert (ds->items, g_strdup (hitem->p.name),
			     sitem);

	if (sitem->type == DSIT_I8)
	    _dssi_i8_to_text (sitem);
    }
}

void
_ds_loaddir (Dataset *ds)
{
    const gchar *name;
    DSLargeItem *li;
    int i;
    size_t len;

    while ((name = g_dir_read_name (ds->dir)) != NULL) {
	/* Verify that the name is acceptable as an item name. */

	if (strcmp (name, "header") == 0)
	    continue;

	len = strlen (name);

	if (len > DS_ITEMNAME_MAXLEN)
	    continue;

	for (i = 0; i < len; i++) {
	    gchar c = name[i];

	    if (!((c >= 'a' && c <= 'z') ||
		  (c >= '0' && c <= '9') ||
		  (c == '-') || (c == '_')))
		break;
	}

	if (i != len)
	    /* Didn't get all the way through. */
	    continue;

	li = _dsli_new ();
	g_hash_table_insert (ds->items, g_strdup (name), li);
    }

    g_dir_rewind (ds->dir);
}

Dataset *
tmpds_open (const char *name)
{
    Dataset *ds;

    ds = g_new0(Dataset, 1);
    ds->oname = g_strdup (name);

    /* Get directory */

    ds->dir = g_dir_open (name, 0, NULL);

    if (ds->dir == NULL)
	goto bail;
  
    /* Create item table */

    ds->items = g_hash_table_new_full (g_str_hash, g_str_equal,
				       g_free, g_free);

    /* Header file */

    ds->fnscratch = g_new (gchar, strlen (ds->oname) + 2 + DS_ITEMNAME_MAXLEN);
    strcpy (ds->fnscratch, ds->oname);
    ds->fnscratch[strlen(ds->oname)] = G_DIR_SEPARATOR;
    _ds_mkitemfn (ds, "header");
    
    ds->header = g_mapped_file_new (ds->fnscratch, FALSE, /*=writeable!!!*/
				    NULL);

    if (ds->header == NULL)
	goto bail;

    _ds_parseheader (ds, g_mapped_file_get_contents (ds->header),
		     g_mapped_file_get_length (ds->header));

    _ds_loaddir (ds);

    return ds;

bail:
    ds_free (ds);
    return NULL;
}

void
ds_free (Dataset *ds)
{
    if (ds->oname != NULL) {
	g_free (ds->oname);
	ds->oname = NULL;
    }

    if (ds->dir != NULL) {
	g_dir_close (ds->dir);
	ds->dir = NULL;
    }

    if (ds->items != NULL) {
	/* This calls g_free on both keys and values. */
	g_hash_table_destroy (ds->items);
	ds->items = NULL;
    }

    if (ds->header != NULL) {
	g_mapped_file_free (ds->header);
	ds->header = NULL;
    }

    g_free (ds);
}
    

/* Main! */

#if 0
int
main (int argc, char **argv)
{
    Dataset *ds;

    if (argc != 2)
	g_error ("Usage: %s [dsname]", argv[0]);

    ds = tmpds_open (argv[1]);

    if (ds == NULL) {
	perror ("opendir");
	return 1;
    }

    _ds_dump_items (ds);
    ds_free (ds);
    return 0;
}
#endif
