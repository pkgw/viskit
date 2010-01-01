#include <uvio.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h> /*sprintf*/

typedef struct _UVHeader {
    guint8 var;
    guint8 pad1;
    guint8 etype;
    guint8 pad2;
} UVHeader;

#define HSZ (sizeof (UVHeader))
#define VISDATA_ALIGN 8

/* This is fixed by the MIRIAD dataset format, so we
 * can't get past this limit while maintaining compatibility.
 * Lame! */

#define NUMVARS 256

struct _UVIO {
    IOMode mode;
    Dataset *ds; /* for writing vartable if needed */
    IOStream *vd; /* visdata */

    gint nvars;
    GHashTable *vars_by_name;
    UVVariable *vars[NUMVARS];

    gboolean vartable_dirty;
};


static void
_uvv_free (UVVariable *uvv)
{
    /* Recall that g_free of NULL is a noop */
    g_free (uvv->data);
    g_free (uvv);
}


UVIO *
uvio_alloc (void)
{
    UVIO *uvio;

    uvio = g_new0 (UVIO, 1);
    /* No init needed beyond everything being zeroed. */
    return uvio;
}


void
uvio_free (UVIO *uvio)
{
    if (uvio->vd != NULL)
	uvio_close (uvio, NULL);

    g_free (uvio);
}


static gboolean
_uvio_read_vartable (UVIO *uvio, GError **err)
{
    IOStream *vtab;
    gchar vtbuf[12]; /* char, space, 8 chars, newline, \0 */
    gchar *vtcur;
    int vtidx;
    gssize nread;
    UVVariable *var;
    gboolean retval = TRUE;

    if ((vtab = ds_open_large_item (uvio->ds, "vartable", IO_MODE_READ, 0, err)) == NULL)
	return TRUE;

    vtidx = 0;

    while ((nread = io_read_into_temp_buf (vtab, 1, (gpointer *) &vtcur, err)) == 1) {
	if (*vtcur != '\n') {
	    vtbuf[vtidx++] = *vtcur;

	    if (vtidx >= 12) {
		g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			     "Invalid UV vartable: too-long variable name");
		goto done;
	    }

	    continue;
	}

	if (vtidx < 3) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV vartable: no variable name");
	    goto done;
	}

	if (vtbuf[1] != ' ') {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV vartable: bad variable typename");
	    goto done;
	}

	var = g_new0 (UVVariable, 1);
	vtbuf[vtidx] = '\0';

	switch (vtbuf[0]) {
	case 'b': var->type = DST_I8; break;
	case 'j': var->type = DST_I16; break;
	case 'i': var->type = DST_I32; break;
	case 'l': var->type = DST_I64; break;
	case 'r': var->type = DST_F32; break;
	case 'd': var->type = DST_F64; break;
	case 'c': var->type = DST_C64; break;
	case 'a': var->type = DST_TEXT; break;
	default:
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV vartable: unknown variable typename");
	    goto done;
	}

	strcpy (var->name, vtbuf + 2);
	var->ident = uvio->nvars;
	var->nvals = -1;
	var->data = NULL;

	uvio->vars[uvio->nvars++] = var;
	g_hash_table_insert (uvio->vars_by_name, var->name, var);

	if (uvio->nvars >= NUMVARS) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV vartable: too many variables");
	    goto done;
	}

	vtidx = 0;
    }

    if (nread < 0)
	goto done;

    /* FIXME: check for vartable not ending in newline */

    retval = FALSE;

done:
    if (io_close_and_free (vtab, err))
	retval = TRUE;

    return retval;
}


gboolean
uvio_update_vartable (UVIO *uvio, GError **err)
{
    IOStream *vtab;
    int varnum;
    gboolean retval = TRUE;

    if (!(uvio->mode & IO_MODE_WRITE)) {
	g_set_error (err, DS_ERROR, DS_ERROR_INTERNAL_PERMS,
		     "Dataset not open in write mode");
	return TRUE;
    }

    if ((vtab = ds_open_large_item_for_replace (uvio->ds, "vartable", err)) == NULL)
	return TRUE;

    for (varnum = 0; varnum < uvio->nvars; varnum++) {
	UVVariable *var = uvio->vars[varnum];
	gchar vtbuf[12]; /* char, space, 8 chars, newline, \0 */

	sprintf (vtbuf, "%c %s\n", ds_type_codes[var->type], var->name);

	if (io_write_raw (vtab, strlen (vtbuf), vtbuf, err))
	    goto done;
    }

    if (io_close_and_free (vtab, err))
	goto done;

    vtab = NULL;

    if (ds_finish_large_item_replace (uvio->ds, "vartable", err))
	goto done;

    uvio->vartable_dirty = FALSE;
    retval = FALSE;

done:
    if (vtab != NULL && io_close_and_free (vtab, err))
	retval = TRUE;

    return retval;
}


gboolean
uvio_open (UVIO *uvio, Dataset *ds, IOMode mode, DSOpenFlags flags,
	   GError **err)
{
    gboolean read_vartable = FALSE;

    if (uvio->mode == IO_MODE_READ_WRITE) {
	g_set_error (err, DS_ERROR, DS_ERROR_INTERNAL_PERMS,
		     "Cannot open UV data in read/write mode");
	return TRUE;
    }

    uvio->mode = mode;
    uvio->ds = ds;

    if (mode == IO_MODE_READ)
	read_vartable = TRUE;
    if (flags & DS_OFLAGS_APPEND)
	read_vartable = ds_has_item (ds, "visdata");

    uvio->vars_by_name = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
						(GDestroyNotify) _uvv_free);


    if (!read_vartable)
	uvio->nvars = 0;
    else if (_uvio_read_vartable (uvio, err))
	goto bail;

    /* FIXME: if reading, check for dataset items that override UV variables */

    /* Open the visdata stream. */

    if ((uvio->vd = ds_open_large_item (ds, "visdata", mode, flags, err)) == NULL)
	goto bail;

    return FALSE;

bail:
    if (uvio->vd != NULL)
	io_close_and_free (uvio->vd, err);
    return TRUE;
}


gboolean
uvio_close (UVIO *uvio, GError **err)
{
    gboolean retval = FALSE;

    if (uvio->vartable_dirty) {
	retval |= uvio_update_vartable (uvio, err);
	uvio->vartable_dirty = FALSE;
    }

    if (uvio->vd != NULL) {
	retval |= io_close_and_free (uvio->vd, err);
	uvio->vd = NULL;
    }

    if (uvio->vars_by_name != NULL) {
	g_hash_table_destroy (uvio->vars_by_name);
	uvio->vars_by_name = NULL;
    }

    uvio->ds = NULL;
    uvio->nvars = 0;
    return retval;
}


GList *
uvio_list_vars (UVIO *uvio)
{
    /* Not valid to call before uvio_open has been run */
    g_assert (uvio->vars_by_name != NULL);

    /* Caller should not free list contents, only the
     * list nodes. */
    return g_hash_table_get_keys (uvio->vars_by_name);
}


UVVariable *
uvio_query_var (UVIO *uvio, const gchar *name)
{
    /* Not valid to call before uvio_open has been run */
    g_assert (uvio->vars_by_name != NULL);

    /* When 'nvals' is -1, the size of the variable is not
       yet known. When 'data' is NULL, the most recent value
       is unknown (when reading). 'data' is always NULL when
       writing. */
    return g_hash_table_lookup (uvio->vars_by_name, name);
}


UVEntryType
uvio_read_next (UVIO *uvio, gpointer *data, GError **err)
{
    UVHeader *header;
    UVEntryType etype;
    guint8 varnum;
    gsize nread;
    gchar *buf;
    UVVariable *var;
    gint32 nbytes;

    if (!(uvio->mode & IO_MODE_READ)) {
	g_set_error (err, DS_ERROR, DS_ERROR_INTERNAL_PERMS,
		     "Dataset not open in read mode");
	return UVET_ERROR;
    }

    *data = NULL;
    nread = io_read_into_temp_buf (uvio->vd, HSZ, (gpointer *) &header, err);

    if (nread < 0)
	return UVET_ERROR;
    else if (nread == 0)
	return UVET_EOS;
    else if (nread != HSZ) {
	g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
		     "Invalid UV visdata: incomplete record");
	return UVET_ERROR;
    }

    etype = header->etype;
    varnum = header->var;
    /* Don't use header after this point because it may be
     * destroyed when doing the I/O to read in the subsequent
     * pieces of data. */

    switch (etype) {
    case UVET_SIZE:
	if (varnum >= NUMVARS) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV visdata: illegal variable number");
	    return UVET_ERROR;
	}

	var = uvio->vars[varnum];

	if ((nread = io_read_into_temp_buf (uvio->vd, 4, (gpointer *) &buf, err)) < 0)
	    return UVET_ERROR;

	if (nread != 4) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV visdata: truncated variable data");
	    return UVET_ERROR;
	}

	nbytes = IO_RECODE_I32 (buf);

	if (nbytes % ds_type_sizes[var->type] != 0) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV visdata: illegal entry size");
	    return UVET_ERROR;
	}

	var->nvals = nbytes / ds_type_sizes[var->type];
	var->data = g_realloc (var->data, nbytes);

	*data = (gchar *) var;
	break;
    case UVET_DATA:
	if (varnum >= NUMVARS) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV visdata: illegal variable number");
	    return UVET_ERROR;
	}

	var = uvio->vars[varnum];

	if (io_nudge_align (uvio->vd, ds_type_aligns[var->type], err))
	    return UVET_ERROR;

	if ((nread = io_read_into_user_buf (uvio->vd, var->type, var->nvals,
					    var->data, err)) < 0)
	    return UVET_ERROR;

	if (nread != var->nvals) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV visdata: truncated variable data");
	    return UVET_ERROR;
	}

	*data = (gchar *) var;
	break;
    case UVET_EOR:
	break;
    default:
	g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
		     "Invalid UV visdata: unknown record type %d",
		     etype);
	return UVET_ERROR;
    }

    if (io_nudge_align (uvio->vd, VISDATA_ALIGN, err))
	return UVET_ERROR;

    return etype;
}


gboolean
uvio_write_var (UVIO *uvio, const gchar *name,
		DSType type, guint32 nvals, const gpointer data,
		GError **err)
{
    UVHeader header = { 0, 0, 0, 0 };
    UVVariable *var;

    if (!(uvio->mode & IO_MODE_WRITE)) {
	g_set_error (err, DS_ERROR, DS_ERROR_INTERNAL_PERMS,
		     "Dataset not open in write mode");
	return TRUE;
    }

    var = g_hash_table_lookup (uvio->vars_by_name, name);

    if (var == NULL) {
	/* Need to create a new variable entry */

	if (strlen (name) > 8) {
	    g_set_error (err, DS_ERROR, DS_ERROR_ITEM_NAME,
			 "Illegal UV variable name \"%s\"", name);
	    return TRUE;
	}

	/* FIXME: more checks on the variable name */

	if (uvio->nvars >= NUMVARS) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Trying to create too many UV variables");
	    return TRUE;
	}

	var = g_new0 (UVVariable, 1);
	var->ident = uvio->nvars;
	var->type = type;
	strcpy (var->name, name);
	var->nvals = -1; /* We'll set this in just a few lines */

	uvio->vars[uvio->nvars++] = var;
	g_hash_table_insert (uvio->vars_by_name, var->name, var);
	uvio->vartable_dirty = TRUE;
    }

    if (var->type != type) {
	g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
		     "Cannot change UV variable type");
	return TRUE;
    }

    if (io_nudge_align (uvio->vd, VISDATA_ALIGN, err))
	return TRUE;

    header.var = var->ident;

    if (var->nvals != nvals) {
	guint32 nbytes;

	header.etype = UVET_SIZE;

	if (io_write_raw (uvio->vd, HSZ, &header, err))
	    return TRUE;

	nbytes = nvals * ds_type_sizes[type];

	if (io_write_typed (uvio->vd, DST_I32, 1, &nbytes, err))
	    return TRUE;

	var->nvals = nvals;
    }

    if (io_nudge_align (uvio->vd, VISDATA_ALIGN, err))
	return TRUE;

    header.etype = UVET_DATA;

    if (io_write_raw (uvio->vd, HSZ, &header, err))
	return TRUE;

    if (io_nudge_align (uvio->vd, ds_type_aligns[type], err))
	return TRUE;

    return io_write_typed (uvio->vd, type, nvals, data, err);
}


static const UVHeader _uvio_eor_header = { 0, 0, UVET_EOR, 0 };


gboolean
uvio_write_end_record (UVIO *uvio, GError **err)
{
    if (!(uvio->mode & IO_MODE_WRITE)) {
	g_set_error (err, DS_ERROR, DS_ERROR_INTERNAL_PERMS,
		     "Dataset not open in write mode");
	return TRUE;
    }

    if (io_nudge_align (uvio->vd, VISDATA_ALIGN, err))
	return TRUE;
    return io_write_raw (uvio->vd, HSZ, &_uvio_eor_header, err);
}
