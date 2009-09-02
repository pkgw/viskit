#ifndef _MASKITEM_H
#define _MASKITEM_H

#include <dataset.h>

typedef struct _MaskItem MaskItem;

extern MaskItem *mask_open (Dataset *ds, gchar *name, DSMode mode, GError **err);
extern void mask_close (MaskItem *mask);

extern gboolean mask_read_expand (MaskItem *mask, gchar *dest, gsize nbits, 
				  GError **err);

#endif
