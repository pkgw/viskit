#ifndef _UVREADER_H
#define _UVREADER_H

#include <viskit/dataset.h>

typedef struct _UVReader UVReader;

extern UVReader *uvr_open (Dataset *ds, GError **err);
extern void uvr_free (UVReader *uvr);

#endif
