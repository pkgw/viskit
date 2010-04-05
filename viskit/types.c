#include <types.h>

#include <string.h> /*memcpy*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

const char ds_type_codes[] = "?bijrdacl";

const guint8 ds_type_sizes[] = {
    /* Item sizes in bytes. */
    1, 1, 4, 2, 4, 8, 1, 8, 8
};

const guint8 ds_type_aligns[] = {
    /* Alignment requirements for datatypes.  Identical to item sizes
     * except for the complex type, which can be aligned to the size
     * of its constituents. */
    1, 1, 4, 2, 4, 8, 1, 4, 8
};

const gchar *ds_type_names[] = {
    "binary", "int8", "int32", "int16", "float32",
    "float64", "text", "complex64", "int64"
};

/* Type upconversion */

typedef union _Dataptr {
    /* This union is identical to the endianness-recoding
     * one found in iostream.c */
    gpointer any;
    gint8 *i8;
    gint16 *i16;
    gint32 *i32;
    gint64 *i64;
    gfloat *f32;
    gdouble *f64;
    vkcomplex64 *c64;
    gchar *text;
} Dataptr;

#define UPCONV_BEGIN(dglib,dlowname) \
 static gboolean \
 _ds_type_upconvert_##dlowname (DSType srctype, gpointer srcdata, gpointer destdata, \
			     gsize nvals) \
 { \
    Dataptr s; \
    dglib *d = (dglib *) destdata; \
    s.any = srcdata; \
    switch (srctype) {
#define UPCONV_DO(dglib,scapname,slowname) \
    case DST_##scapname: while (nvals-- > 0) *d++ = (dglib) *s.slowname++; break;
#define UPCONV_END(dglib) \
    default: \
	return TRUE; \
    } \
    return FALSE; \
 }

UPCONV_BEGIN(gint16,i16)
UPCONV_DO(gint16,I8,i8)
UPCONV_END(gint16)

UPCONV_BEGIN(gint32,i32)
UPCONV_DO(gint32,I8,i8)
UPCONV_DO(gint32,I16,i16)
UPCONV_END(gint32)

UPCONV_BEGIN(gint64,i64)
UPCONV_DO(gint64,I8,i8)
UPCONV_DO(gint64,I16,i16)
UPCONV_DO(gint64,I32,i32)
UPCONV_END(gint64)

UPCONV_BEGIN(gfloat,f32)
UPCONV_DO(gfloat,I8,i8)
UPCONV_DO(gfloat,I16,i16)
UPCONV_DO(gfloat,I32,i32)
UPCONV_DO(gfloat,I64,i64)
UPCONV_END(gfloat)

UPCONV_BEGIN(gdouble,f64)
UPCONV_DO(gdouble,I8,i8)
UPCONV_DO(gdouble,I16,i16)
UPCONV_DO(gdouble,I32,i32)
UPCONV_DO(gdouble,I64,i64)
UPCONV_DO(gdouble,F32,f32)
UPCONV_END(gdouble)

UPCONV_BEGIN(vkcomplex64,c64)
UPCONV_DO(vkcomplex64,I8,i8)
UPCONV_DO(vkcomplex64,I16,i16)
UPCONV_DO(vkcomplex64,I32,i32)
UPCONV_DO(vkcomplex64,I64,i64)
UPCONV_DO(vkcomplex64,F32,f32)
UPCONV_END(vkcomplex64)

gboolean
ds_type_upconvert (DSType srctype, gpointer srcdata, DSType desttype,
		   gpointer destdata, gsize nvals)
{
    if (srctype == desttype) {
	memcpy (destdata, srcdata, nvals * ds_type_sizes[srctype]);
	return FALSE;
    }

    switch (desttype) {
    case DST_I16:
	return _ds_type_upconvert_i16 (srctype, srcdata, destdata, nvals);
    case DST_I32:
	return _ds_type_upconvert_i32 (srctype, srcdata, destdata, nvals);
    case DST_I64:
	return _ds_type_upconvert_i64 (srctype, srcdata, destdata, nvals);
    case DST_F32:
	return _ds_type_upconvert_f32 (srctype, srcdata, destdata, nvals);
    case DST_F64:
	return _ds_type_upconvert_f64 (srctype, srcdata, destdata, nvals);
    case DST_C64:
	return _ds_type_upconvert_c64 (srctype, srcdata, destdata, nvals);
    default:
	return TRUE;
    }
}

static const gchar *_ds_type_formats[] = {
    "?", "%hhd", "%" G_GINT32_FORMAT, "%" G_GINT16_FORMAT,
    "%g", "%g", NULL, "%g%+gi", "%" G_GINT64_FORMAT
};

gchar *
ds_type_format (gpointer data, DSType type, gssize nvals)
{
    GString *s;
    const gchar *fmt;
    gssize i;
    double d1, d2;

    g_assert (nvals >= 0);

    if (nvals == 0)
	return g_strdup ("<>");

    if (type == DST_TEXT)
	return g_strdup_printf ("\"%.*s\"", (int) nvals, (gchar *) data);

    s = g_string_new ("");

    if (nvals > 1)
	g_string_append_c (s, '[');

    fmt = _ds_type_formats[type];

    for (i = 0; i < nvals; i++) {
	if (i > 0)
	    g_string_append (s, ", ");

	switch (type) {
	case DST_BIN:
	    g_string_append (s, fmt);
	    break;
	case DST_I8:
	    g_string_append_printf (s, fmt, *(gint8 *) data);
	    break;
	case DST_I16:
	    g_string_append_printf (s, fmt, *(gint16 *) data);
	    break;
	case DST_I32:
	    g_string_append_printf (s, fmt, *(gint32 *) data);
	    break;
	case DST_I64:
	    g_string_append_printf (s, fmt, *(gint64 *) data);
	    break;
	case DST_F32:
	    g_string_append_printf (s, fmt, (double) (*(float *) data));
	    break;
	case DST_F64:
	    g_string_append_printf (s, fmt, *(double *) data);
	    break;
	case DST_C64:
	    d1 = (double) (*(float *) data);
	    d2 = (double) (*(float *) (data + 4));
	    g_string_append_printf (s, fmt, d1, d2);
	    break;
	default:
	    g_assert_not_reached ();
	}

	data += ds_type_sizes[type];
    }

    if (nvals > 1)
	g_string_append_c (s, ']');

    return g_string_free (s, FALSE);
}
