#ifndef _VISKIT_UVREADER_H
#define _VISKIT_UVREADER_H

#include <viskit/dataset.h>

typedef struct _UVReader UVReader;

extern UVReader *uvr_open (Dataset *ds, GError **err);
extern void uvr_free (UVReader *uvr);

#endif
