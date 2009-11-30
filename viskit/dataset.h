#ifndef _VISKIT_DATASET_H
#define _VISKIT_DATASET_H

#include <string.h> /* strlen */
#include <glib.h>
#include <viskit/iostream.h>

typedef struct _Dataset Dataset;

/* Custom errors */

#define DS_ERROR ds_error_quark ()

extern GQuark ds_error_quark (void);

typedef enum _DSError {
    DS_ERROR_FORMAT = 0
} DSError;


/* Dataset access */

typedef struct _DSItemInfo {
    gchar *name;
    gboolean is_large;
    DSType type;
    gsize nvals;
} DSItemInfo;

extern Dataset *ds_open (const char *filename, IOMode mode, IOOpenFlags flags,
			 GError **err)
    G_GNUC_WARN_UNUSED_RESULT;
extern gboolean ds_close (Dataset *ds, GError **err);

extern gboolean ds_has_item (Dataset *ds, const gchar *name);
extern GSList *ds_list_items (Dataset *ds, GError **err)
    G_GNUC_WARN_UNUSED_RESULT;
extern IOStream *ds_open_large_item (Dataset *ds, const gchar *name, IOMode mode,
				     IOOpenFlags flags, GError **err);
extern DSItemInfo *ds_probe_item (Dataset *ds, const gchar *name, GError **err);
extern void ds_item_info_free (DSItemInfo *dii);

extern gboolean ds_get_item_i64 (Dataset *ds, const gchar *name, gint64 *val);
extern gboolean ds_get_item_f64 (Dataset *ds, const gchar *name, gdouble *val);
extern gchar *ds_get_item_small_string (Dataset *ds, const gchar *name);

extern gboolean ds_set_small_item (Dataset *ds, const gchar *name, DSType type,
				   gsize nvals, gpointer data, gboolean create_ok);

#define ds_set_small_item_i16(ds, name, value, create_ok) \
    ds_set_small_item ((ds), (name), DST_I16, 1, &((gint16) (value)), (create_ok))
#define ds_set_small_item_i32(ds, name, value, create_ok) \
    ds_set_small_item ((ds), (name), DST_I32, 1, &((gint32) (value)), (create_ok))
#define ds_set_small_item_i64(ds, name, value, create_ok) \
    ds_set_small_item ((ds), (name), DST_I64, 1, &((gint64) (value)), (create_ok))
#define ds_set_small_item_f32(ds, name, value, create_ok) \
    ds_set_small_item ((ds), (name), DST_F32, 1, &((gfloat) (value)), (create_ok))
#define ds_set_small_item_f64(ds, name, value, create_ok) \
    ds_set_small_item ((ds), (name), DST_F64, 1, &((gdouble) (value)), (create_ok))
#define ds_set_small_item_string(ds, name, value, create_ok) \
    ds_set_small_item ((ds), (name), DST_I8, strlen (value), (value), (create_ok))

extern gboolean ds_write_header (Dataset *ds, GError **err);

#endif
