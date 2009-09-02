#ifndef _VISKIT_UVREADER_H
#define _VISKIT_UVREADER_H

#include <viskit/dataset.h>

typedef struct _UVReader UVReader;

typedef enum _UVEntryType {
    UVET_SIZE = 0,
    UVET_DATA = 1,
    UVET_EOR = 2,
    UVET_EOS = 3, /* not actually used in disk format */
    UVET_ERROR = -1 /* ditto */
} UVEntryType;


extern UVReader *uvr_alloc (void);
extern void uvr_free (UVReader *uvr);

extern gboolean uvr_prep (UVReader *uvr, Dataset *ds, GError **err);
extern UVEntryType uvr_next (UVReader *uvr, gchar **data, GError **err);

#endif
