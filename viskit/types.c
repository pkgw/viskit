#include <types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

const gchar *ds_type_names[] = {
    "unknown", "int8", "int32", "int16", "float32",
    "float64", "text", "complex64", "int64"
};

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
	return g_strdup_printf ("\"%.*s\"", nvals, (gchar *) data);

    s = g_string_new ("");

    if (nvals > 1)
	g_string_append_c (s, '[');

    fmt = _ds_type_formats[type];

    for (i = 0; i < nvals; i++) {
	if (i > 0)
	    g_string_append (s, ", ");

	switch (type) {
	case DST_UNK:
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
