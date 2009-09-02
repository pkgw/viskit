#include <types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

const char ds_type_codes[] = "?bijrdacl";

const guint8 ds_type_sizes[] = {
    /* Item sizes in bytes. */
    0, 1, 4, 2, 4, 8, 1, 8, 8
};

const guint8 ds_type_aligns[] = {
    /* Alignment requirements for datatypes.  Identical to item sizes
     * except for the complex type, which can be aligned to the size
     * of its constituents. */
    0, 1, 4, 2, 4, 8, 1, 4, 8
};
