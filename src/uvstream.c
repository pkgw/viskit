#include <uvstream.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h> /*printf*/
#include <string.h>

typedef enum _UVEntryType {
    UVET_SIZE = 0,
    UVET_DATA = 1,
    UVET_EOR = 2
} UVEntryType;

typedef struct _UVHeader {
    guint8 var;
    guint8 pad1;
    guint8 etype;
    guint8 pad2;
} UVHeader;

typedef struct _UVVariable {
    gchar name[9];
    guint8 ident;
    DSType type;
    gssize nvalues;
} UVVariable;

#define HSZ (sizeof (UVHeader))
#define VISDATA_ALIGN 8

/* This is fixed by the MIRIAD dataset format, so we
 * can't get past this limit while maintaining compatibility.
 * Lame! */

#define NUMVARS 256 

struct _UVReader {
    Dataset *ds;

    IOStream *vd; /* visdata */

    gint nvars;
    GHashTable *vars_by_name;
    UVVariable *vars[NUMVARS];
};

static void
_uvv_free (UVVariable *var)
{
    g_free (var);
}

UVReader *
uvr_open (Dataset *ds, GError **err)
{
    UVReader *uvr;
    IOStream *vtab;
    gchar vtbuf[12]; /* char, space, 8 chars, newline, \0 */
    gchar *vtcur;
    int vtidx;
    gsize nread;
    UVVariable *var;

    uvr = g_new0 (UVReader, 1);
    uvr->vars_by_name = g_hash_table_new_full (g_str_hash, g_str_equal, 
					       NULL, 
					       (GDestroyNotify) _uvv_free);

    /* Read in variable table */

    if ((vtab = ds_open_large (ds, "vartable", DSM_READ, err)) == NULL)
	goto bail;

    vtidx = 0;
    while ((nread = io_fetch (vtab, 1, &vtcur, err)) == 1) {
	if (*vtcur != '\n') {
	    vtbuf[vtidx++] = *vtcur;

	    if (vtidx >= 12)
		g_error ("Invalid vartable format!");

	    continue;
	}

	if (vtidx < 3)
	    g_error ("Invalid vartable format!");

	if (vtbuf[1] != ' ')
	    g_error ("Invalid vartable format!");

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
	    g_error ("Invalid vartable format!");
	}

	strcpy (var->name, vtbuf + 2);
	var->ident = uvr->nvars;
	var->nvalues = -1;

	uvr->vars[uvr->nvars++] = var;
	g_hash_table_insert (uvr->vars_by_name, var->name, var);

	if (uvr->nvars >= NUMVARS)
	    g_error ("Invalid vartable format!");

	vtidx = 0;
    }

    if (nread < 0)
	goto bail;

    io_free (vtab);

    /* FIXME: check for vartable not ending in newline */
   
    /* Open visdata stream */

    if ((uvr->vd = ds_open_large (ds, "visdata", DSM_READ, err)) == NULL)
	goto bail;

    return uvr;

bail:
    uvr_free (uvr);
    return NULL;
}

void
uvr_free (UVReader *uvr)
{
    if (uvr->vd != NULL) {
	io_free (uvr->vd);
	uvr->vd = NULL;
    }

    if (uvr->vars_by_name != NULL) {
	g_hash_table_destroy (uvr->vars_by_name);
	uvr->vars_by_name = NULL;
    }

    g_free (uvr);
}

gboolean
uv_demo (Dataset *ds, GError **err)
{
    UVReader *uvr;
    IOStream *visdata;
    UVHeader *header;
    gsize nread;
    gchar *buf;
    UVVariable *var;

    if ((uvr = uvr_open (ds, err)) == NULL)
	return TRUE;
    
    visdata = uvr->vd;

    while ((nread = io_fetch (visdata, HSZ, (gpointer) &header, err)) > 0) {
	if (nread != HSZ)
	    return TRUE;

	printf ("var %d, etype %d\n", header->var, header->etype);

	switch (header->etype) {
	case UVET_SIZE:
	    if (header->var >= NUMVARS)
		g_error ("fmt");

	    var = uvr->vars[header->var];

	    if (io_fetch (visdata, 4, &buf, err) != 4)
		return TRUE;

	    var->nvalues = io_decode_i32 (buf);

	    if (var->nvalues % ds_type_sizes[var->type] != 0)
		g_error ("uneven # of elements");

	    printf ("   %s: size %d, %d values\n", var->name, var->nvalues,
		    var->nvalues / ds_type_sizes[var->type]);
	    break;
	case UVET_DATA:
	    if (header->var >= NUMVARS)
		g_error ("fmt");

	    var = uvr->vars[header->var];

	    if (io_read_align (visdata, ds_type_aligns[var->type], err))
		return TRUE;
	    if (io_fetch (visdata, var->nvalues, &buf, err) != var->nvalues)
		return TRUE;
	    printf ("   %s: data\n", var->name);
	    break;
	case UVET_EOR:
	    printf ("   EOR\n");
	    break;
	default:
	    g_error ("invalid format!");
	}

	if (io_read_align (visdata, VISDATA_ALIGN, err))
	    return TRUE;
    }

    uvr_free (uvr);
    return FALSE;
}
