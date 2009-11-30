#ifndef _VISKIT_DATASET_H
#define _VISKIT_DATASET_H

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
extern void ds_close (Dataset *ds);

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

#endif
