#ifndef _DATASET_H
#define _DATASET_H

#include <glib.h>

typedef struct _Dataset Dataset;

typedef enum _DSMode {
    DSM_READ   = 0,
    DSM_RDWR   = 1,
    DSM_CREATE = 2,
    DSM_APPEND = 3, /* not valid for whole datasets */
} DSMode;

typedef enum _DSType {
    DST_UNK  = 0, /* not an actual type used in the file format */
    DST_I8   = 1,
    DST_I32  = 2,
    DST_I16  = 3,
    DST_F32  = 4,
    DST_F64  = 5,
    DST_TEXT = 6, /* written as DST_I8 in the file format */
    DST_C64  = 7,
    DST_I64  = 8
} DSType;

#define DST_VALID(v) ((v) > 0 && (v) < 9)

extern const char ds_type_codes[];
extern const guint8 ds_type_sizes[];
extern const guint8 ds_type_aligns[];

/* IO on large dataset items */

typedef struct _IOStream IOStream;

extern void io_free (IOStream *io);
extern gssize io_fetch (IOStream *io, gsize nbytes, gchar **dest,
		       GError **err);
extern gboolean io_read_align (IOStream *io, gsize align_size, 
			       GError **err);

gint16 io_decode_i16 (gchar *buf);
gint32 io_decode_i32 (gchar *buf);
gint64 io_decode_i64 (gchar *buf);

/* Prototypes */

extern Dataset *ds_open (const char *filename, DSMode mode, GError **err);
extern void ds_close (Dataset *ds);
extern GSList *ds_list_items (Dataset *ds, GError **err)
    G_GNUC_WARN_UNUSED_RESULT;
extern IOStream *ds_open_large (Dataset *ds, gchar *name, 
				DSMode mode, GError **err);

#endif
