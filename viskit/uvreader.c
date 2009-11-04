#include <uvreader.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h> /*close*/
#include <string.h>

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

struct _UVReader {
    InputStream vd; /* visdata */

    gint nvars;
    GHashTable *vars_by_name;
    UVVariable *vars[NUMVARS];
};


static void
_uvv_free (UVVariable *uvv)
{
    if (uvv->data != NULL)
	g_free (uvv->data);
    g_free (uvv);
}


UVReader *
uvr_alloc (void)
{
    UVReader *uvr;

    uvr = g_new0 (UVReader, 1);
    io_input_init (&(uvr->vd), 0);
    return uvr;
}

void
uvr_free (UVReader *uvr)
{
    if (uvr->vd.fd >= 0) {
	close (uvr->vd.fd);
	uvr->vd.fd = -1;
    }

    io_input_uninit (&(uvr->vd));

    if (uvr->vars_by_name != NULL) {
	g_hash_table_destroy (uvr->vars_by_name);
	uvr->vars_by_name = NULL;
    }

    g_free (uvr);
}

gboolean
uvr_prep (UVReader *uvr, Dataset *ds, GError **err)
{
    InputStream vtab;
    gchar vtbuf[12]; /* char, space, 8 chars, newline, \0 */
    gchar *vtcur;
    int vtidx;
    gssize nread;
    UVVariable *var;

    uvr->vars_by_name = g_hash_table_new_full (g_str_hash, g_str_equal, 
					       NULL, 
					       (GDestroyNotify) _uvv_free);

    /* Read in variable table */

    io_input_init (&vtab, 0);
    if (ds_open_large (ds, "vartable", DSM_READ, &vtab, err))
	goto bail;

    vtidx = 0;
    while ((nread = io_fetch_temp (&vtab, 1, &vtcur, err)) == 1) {
	if (*vtcur != '\n') {
	    vtbuf[vtidx++] = *vtcur;

	    if (vtidx >= 12) {
		g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			     "Invalid UV vartable: too-long variable name");
		goto bail;
	    }

	    continue;
	}

	if (vtidx < 3) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV vartable: no variable name");
	    goto bail;
	}

	if (vtbuf[1] != ' ') {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV vartable: bad variable typename");
	    goto bail;
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
	    goto bail;
	}

	strcpy (var->name, vtbuf + 2);
	var->ident = uvr->nvars;
	var->nvals = -1;
	var->data = NULL;

	uvr->vars[uvr->nvars++] = var;
	g_hash_table_insert (uvr->vars_by_name, var->name, var);

	if (uvr->nvars >= NUMVARS) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV vartable: too many variables");
	    goto bail;
	}

	vtidx = 0;
    }

    if (nread < 0)
	goto bail;

    close (vtab.fd);
    vtab.fd = -1;
    io_input_uninit (&vtab);

    /* FIXME: check for vartable not ending in newline */

    /* FIXME: check for dataset items that override UV variables */

    /* Open the visdata stream for reading. */

    if (ds_open_large (ds, "visdata", DSM_READ, &(uvr->vd), err))
	goto bail;

    return FALSE;

bail:
    if (vtab.fd >= 0)
	close (vtab.fd);
    if (uvr->vd.fd >= 0)
	close (uvr->vd.fd);
    return TRUE;
}


GList *
uvr_list_vars (UVReader *uvr)
{
    /* Not valid to call before uvr_prep has been run */
    g_assert (uvr->vars_by_name != NULL);

    /* Caller should not free list contents, only the
     * list nodes. */
    return g_hash_table_get_keys (uvr->vars_by_name);
}


UVVariable *
uvr_query_var (UVReader *uvr, const gchar *name)
{
    /* Not valid to call before uvr_prep has been run */
    g_assert (uvr->vars_by_name != NULL);

    /* Caller should not use the 'nvals' or 'data' fields
     * before reading any UV data. */
    return g_hash_table_lookup (uvr->vars_by_name, name);
}


UVEntryType
uvr_next (UVReader *uvr, gchar **data, GError **err)
{
    UVHeader *header;
    UVEntryType etype;
    guint8 varnum;
    gsize nread;
    gchar *buf;
    UVVariable *var;
    gint32 nbytes;

    *data = NULL;
    nread = io_fetch_temp (&(uvr->vd), HSZ, (gpointer) &header, err);

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

	var = uvr->vars[varnum];

	if ((nread = io_fetch_temp (&(uvr->vd), 4, &buf, err)) < 0)
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

	var = uvr->vars[varnum];

	if (io_nudge_align (&(uvr->vd), ds_type_aligns[var->type], err))
	    return UVET_ERROR;

	if ((nread = io_fetch_prealloc (&(uvr->vd), var->type, var->nvals,
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

    if (io_nudge_align (&(uvr->vd), VISDATA_ALIGN, err))
	return UVET_ERROR;

    return etype;
}
