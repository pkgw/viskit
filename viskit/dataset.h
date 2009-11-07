#ifndef _VISKIT_DATASET_H
#define _VISKIT_DATASET_H

#include <glib.h>
#include <viskit/iostream.h>

typedef struct _Dataset Dataset;

typedef enum _DSMode {
    DSM_READ   = 0,
    DSM_RDWR   = 1,
    DSM_CREATE = 2,
    DSM_APPEND = 3, /* not valid for whole datasets */
} DSMode;


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

extern Dataset *ds_open (const char *filename, DSMode mode, GError **err)
    G_GNUC_WARN_UNUSED_RESULT;
extern void ds_close (Dataset *ds);

extern gboolean ds_has_item (Dataset *ds, gchar *name);
extern GSList *ds_list_items (Dataset *ds, GError **err)
    G_GNUC_WARN_UNUSED_RESULT;
extern gboolean ds_open_large (Dataset *ds, gchar *name,
			       DSMode mode, InputStream *io, GError **err);
extern DSItemInfo *ds_probe_item (Dataset *ds, gchar *name, GError **err);
extern void ds_item_info_free (DSItemInfo *dii);

extern gboolean ds_get_item_i64 (Dataset *ds, const gchar *name, gint64 *val);
extern gboolean ds_get_item_f64 (Dataset *ds, const gchar *name, gdouble *val);
extern gchar *ds_get_item_short_string (Dataset *ds, const gchar *name);

#endif
