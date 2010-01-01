#ifndef _VISKIT_UVIO_H
#define _VISKIT_UVIO_H

#include <viskit/dataset.h>

/* Reading interface */

typedef struct _UVIO UVIO;

typedef enum _UVEntryType {
    UVET_SIZE = 0,
    UVET_DATA = 1,
    UVET_EOR = 2,
    UVET_EOS = 3, /* not actually used in disk format */
    UVET_ERROR = -1 /* ditto */
} UVEntryType;

typedef struct _UVVariable {
    gchar name[9];
    guint8 ident;
    DSType type;
    gssize nvals;
    gchar *data;
} UVVariable;

extern UVIO *uvio_alloc (void);
extern void uvio_free (UVIO *uvio);

extern gboolean uvio_open (UVIO *uvio, Dataset *ds, IOMode mode, DSOpenFlags flags,
			   GError **err);
extern gboolean uvio_close (UVIO *uvio, GError **err);

extern GList *uvio_list_vars (UVIO *uvio);
extern UVVariable *uvio_query_var (UVIO *uvio, const gchar *name);

extern UVEntryType uvio_read_next (UVIO *uvio, gpointer *data, GError **err);

extern gboolean uvio_write_var (UVIO *uvio, const gchar *name,
				DSType type, guint32 nvals, const gpointer data,
				GError **err);
extern gboolean uvio_write_end_record (UVIO *uvio, GError **err);
extern gboolean uvio_update_vartable (UVIO *uvio, GError **err);


#endif
