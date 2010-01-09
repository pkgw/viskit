#ifndef _VISKIT_DATASET_H
#define _VISKIT_DATASET_H

#include <string.h> /* strlen */
#include <glib.h>
#include <viskit/iostream.h>

typedef struct _Dataset Dataset;

typedef enum _DSOpenFlags {
    /* For whole datasets, if opening for read only, flags are ignored.
     * Opening for write only is disallowed.
     * Otherwise,
     * - CREATE_OK indicates that if the named dataset doesn't exist,
     *   an empty dataset should be created.
     * - EXIST_BAD indicates that if the named dataset does exist,
     *   the open shall fail. Implies CREATE_OK.
     * - APPEND indicates that existing items in the dataset may
     *   not be rewritten; short items may be added, new long items
     *   may be created, and existing long items may be appended to.
     * - TRUNCATE indicates that if the named dataset exists, all
     *   of its contents will be deleted. TRUNCATE and APPEND are
     *   mutually exclusive.
     * The default behavior with write capability is thus:
     * - If no such dataset exists, fail.
     * - Existing items may be modified in any way.
     * - The dataset will not be modified upon open.
     *
     * For dataset items, if opening readonly, flags are ignored.
     * Otherwise,
     * - CREATE_OK indicates that if the named item doesn't exist,
     *   it should be created as an empty file.
     * - EXIST_BAD indicates that if the named item does exist,
     *   the open shall fail. Implies CREATE_OK.
     * - APPEND indicates that an existing item should be appended to.
     * - TRUNCATE indicates that if the named item exists, it will
     *   be truncated. TRUNCATE and APPEND are mutually exclusive and
     *   one of them must be specified.
     */

    DS_OFLAGS_NONE      = 0,
    DS_OFLAGS_CREATE_OK = 1 << 0,
    DS_OFLAGS_EXIST_BAD = 1 << 1,
    DS_OFLAGS_TRUNCATE  = 1 << 2,
    DS_OFLAGS_APPEND    = 1 << 3,
} DSOpenFlags;

/* Custom errors */

#define DS_ERROR ds_error_quark ()

extern GQuark ds_error_quark (void);

typedef enum _DSError {
    DS_ERROR_NO_ERROR = 0,
    DS_ERROR_FORMAT = 1,
    DS_ERROR_INTERNAL_PERMS = 2,
    DS_ERROR_ITEM_NAME = 3,
    DS_ERROR_NONEXISTANT = 4,
} DSError;

extern const gchar *ds_error_describe (DSError error);


/* Dataset access */

typedef struct _DSItemInfo {
    gchar *name;
    gboolean is_large;
    DSType type;
    gsize nvals;
    union {
	gint8 i8[64];
	gint16 i16[32];
	gint32 i32[16];
	gint64 i64[8];
	gfloat f32[16];
	gdouble f64[8];
	gchar text[64];
    } small;
} DSItemInfo;

extern Dataset *ds_open (const char *filename, IOMode mode, DSOpenFlags flags,
			 GError **err)
    G_GNUC_WARN_UNUSED_RESULT;
extern gboolean ds_close (Dataset *ds, GError **err);

extern gboolean ds_has_item (Dataset *ds, const gchar *name);
extern gboolean ds_list_items (Dataset *ds, GSList **items, GError **err);
extern IOStream *ds_open_large_item (Dataset *ds, const gchar *name, IOMode mode,
				     DSOpenFlags flags, GError **err);
extern gboolean ds_probe_item (Dataset *ds, const gchar *name, DSItemInfo **info,
			       GError **err);
extern void ds_item_info_free (DSItemInfo *dii);

extern IOStream *ds_open_large_item_for_replace (Dataset *ds, const gchar *name,
						 GError **err);
extern gboolean ds_finish_large_item_replace (Dataset *ds, const gchar *name,
					      GError **err);

extern gboolean ds_get_item_i64 (Dataset *ds, const gchar *name, gint64 *val);
extern gboolean ds_get_item_f64 (Dataset *ds, const gchar *name, gdouble *val);
extern gchar *ds_get_item_small_string (Dataset *ds, const gchar *name);

extern DSError ds_set_small_item (Dataset *ds, const gchar *name, DSType type,
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

extern gboolean ds_rename_large_item (Dataset *ds, const gchar *oldname,
				      const gchar *newname, GError **err);

extern gboolean ds_write_header (Dataset *ds, GError **err);

#endif
