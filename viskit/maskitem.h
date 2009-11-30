#ifndef _VISKIT_MASKITEM_H
#define _VISKIT_MASKITEM_H

#include <viskit/dataset.h>

typedef struct _MaskItem MaskItem;

extern MaskItem *mask_open (Dataset *ds, const gchar *name, IOMode mode,
			    IOOpenFlags flags, GError **err);
extern gboolean mask_close (MaskItem *mask, GError **err);

extern gboolean mask_read_expand (MaskItem *mask, guint8 *dest, gsize nbits,
				  GError **err);

#endif
