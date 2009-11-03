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
    IOStream vd; /* visdata */
    IOStream sfd; /* spectral flag data */

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
    io_init (&(uvr->vd), 0);
    io_init (&(uvr->sfd), 0);
    return uvr;
}

void
uvr_free (UVReader *uvr)
{
    if (uvr->vd.fd >= 0) {
	close (uvr->vd.fd);
	uvr->vd.fd = -1;
    }

    if (uvr->sfd.fd >= 0) {
	close (uvr->sfd.fd);
	uvr->sfd.fd = -1;
    }

    io_uninit (&(uvr->vd));
    io_uninit (&(uvr->sfd));

    if (uvr->vars_by_name != NULL) {
	g_hash_table_destroy (uvr->vars_by_name);
	uvr->vars_by_name = NULL;
    }

    g_free (uvr);
}

gboolean
uvr_prep (UVReader *uvr, Dataset *ds, GError **err)
{
    IOStream vtab;
    gchar vtbuf[12]; /* char, space, 8 chars, newline, \0 */
    gchar *vtcur;
    int vtidx;
    gssize nread;
    UVVariable *var;

    uvr->vars_by_name = g_hash_table_new_full (g_str_hash, g_str_equal, 
					       NULL, 
					       (GDestroyNotify) _uvv_free);

    /* Read in variable table */

    io_init (&vtab, 0);
    if (ds_open_large (ds, "vartable", DSM_READ, &vtab, err))
	goto bail;

    vtidx = 0;
    while ((nread = io_fetch (&vtab, 1, &vtcur, err)) == 1) {
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
    io_uninit (&vtab);

    /* FIXME: check for vartable not ending in newline */

    /* FIXME: check for dataset items that override UV variables */

    /* Open visdata stream. FIXME: mode */

    if (ds_open_large (ds, "visdata", DSM_READ, &(uvr->vd), err))
	goto bail;
    if (ds_open_large (ds, "flags", DSM_READ, &(uvr->sfd), err))
	goto bail;

    return FALSE;

bail:
    if (vtab.fd >= 0)
	close (vtab.fd);
    if (uvr->vd.fd >= 0)
	close (uvr->vd.fd);
    if (uvr->sfd.fd >= 0)
	close (uvr->sfd.fd);
    return TRUE;
}

UVEntryType
uvr_next (UVReader *uvr, gchar **data, GError **err)
{
    UVHeader *header;
    gsize nread;
    gchar *buf;
    UVVariable *var;
    gint32 nbytes;

    *data = NULL;
    nread = io_fetch (&(uvr->vd), HSZ, (gpointer) &header, err);

    if (nread < 0)
	return UVET_ERROR;
    else if (nread == 0)
	return UVET_EOS;
    else if (nread != HSZ) {
	g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
		     "Invalid UV visdata: incomplete record");
	return UVET_ERROR;
    }

    switch (header->etype) {
    case UVET_SIZE:
	if (header->var >= NUMVARS) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV visdata: illegal variable number");
	    return UVET_ERROR;
	}

	var = uvr->vars[header->var];

	if ((nread = io_fetch (&(uvr->vd), 4, &buf, err)) < 0)
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
	if (header->var >= NUMVARS) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV visdata: illegal variable number");
	    return UVET_ERROR;
	}

	var = uvr->vars[header->var];

	if (io_nudge_align (&(uvr->vd), ds_type_aligns[var->type], err))
	    return UVET_ERROR;

	nbytes = var->nvals * ds_type_sizes[var->type];

	/* FIXME: cannot read in variables larger than the IO stream
	 * buffer size! */

	if ((nread = io_fetch (&(uvr->vd), nbytes, &buf, err)) < 0)
	    return UVET_ERROR;

	if (nread != nbytes) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid UV visdata: truncated variable data");
	    return UVET_ERROR;
	}

	io_recode_data_copy (buf, var->data, var->type, var->nvals);

	*data = (gchar *) var;
	break;
    case UVET_EOR:
	break;
    default:
	g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
		     "Invalid UV visdata: unknown record type %d",
		     header->etype);
	return UVET_ERROR;
    }

    if (io_nudge_align (&(uvr->vd), VISDATA_ALIGN, err))
	return UVET_ERROR;

    return header->etype;
}
