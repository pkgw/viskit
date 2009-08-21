#ifndef _UVSTREAM_H
#define _UVSTREAM_H

#include <dataset.h>

typedef struct _UVReader UVReader;

extern UVReader *uvr_open (Dataset *ds, GError **err);
extern void uvr_free (UVReader *uvr);

#endif
