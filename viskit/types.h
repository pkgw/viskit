#ifndef _VISKIT_TYPES_H
#define _VISKIT_TYPES_H

#include <glib.h>

typedef enum _DSType {
    DST_UNK  = 0, /* not an actual type used in the file format */
    DST_I8   = 1,
    DST_I32  = 2,
    DST_I16  = 3,
    DST_F32  = 4,
    DST_F64  = 5,
    DST_TEXT = 6, /* written as DST_I8 in the file format */
    DST_C64  = 7,
    DST_I64  = 8
} DSType;

#define DST_VALID(v) ((v) > 0 && (v) < 9)

extern const char ds_type_codes[];
extern const guint8 ds_type_sizes[];
extern const guint8 ds_type_aligns[];

#endif
