#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <maskitem.h>

#include <unistd.h>

struct _MaskItem {
    InputStream stream;
    int bits_left_in_current;
    guint32 current_val;
};

static const guint32 bitmasks[31] = {
    0x00000001, 0x00000002, 0x00000004, 0x00000008,
    0x00000010, 0x00000020, 0x00000040, 0x00000080,
    0x00000100, 0x00000200, 0x00000400, 0x00000800,
    0x00001000, 0x00002000, 0x00004000, 0x00008000,
    0x00010000, 0x00020000, 0x00040000, 0x00080000,
    0x00100000, 0x00200000, 0x00400000, 0x00080000,
    0x01000000, 0x02000000, 0x04000000, 0x00080000,
    0x10000000, 0x20000000, 0x40000000
};

MaskItem *
mask_open (Dataset *ds, gchar *name, DSMode mode, GError **err)
{
    MaskItem *mask;

    mask = g_new0 (MaskItem, 1);
    io_input_init (&(mask->stream), 0);

    if (ds_open_large (ds, name, mode, &(mask->stream), err)) {
	mask_close (mask);
	return NULL;
    }

    mask->bits_left_in_current = 0;
    mask->current_val = 0xFFFFFFFF;
    return mask;
}

void
mask_close (MaskItem *mask)
{
    if (mask->stream.fd >= 0) {
	close (mask->stream.fd);
	mask->stream.fd = -1;
    }

    io_input_uninit (&(mask->stream));
    g_free (mask);
}

gboolean
mask_read_expand (MaskItem *mask, gchar *dest, gsize nbits, GError **err)
{
    int i;
    gsize toread;
    gssize nread;
    guint32 cur;
    gchar *bufptr;

    /* Read @nbits bits from the mask file, placing a 1 or 0 corresponding to each
     * bit in every entry in @dest; that is, @dest should allow at least @nbits
     * entries. The resulting setup is much less space-efficient than the original
     * binary packing, of course, but it's easier to deal with in many cases. */

    cur = mask->current_val;

    while (nbits > 0) {
	if (mask->bits_left_in_current > 0) {
	    /* We can make progress with the i32 that we've got buffered. */
	    toread = MIN (mask->bits_left_in_current, nbits);
	    nbits -= toread; /* do these here since we count with toread */
	    mask->bits_left_in_current -= toread;

	    i = 31 - mask->bits_left_in_current;
	    while (toread > 0) {
		*dest = (cur & bitmasks[i]) ? 1 : 0;
		dest++;
		i++;
		toread--;
	    }
	}

	if (nbits == 0)
	    /* Nice, all done. */
	    return FALSE;

	/* We need to read in another i32. */

	nread = io_fetch_temp (&(mask->stream), 4, &bufptr, err);

	if (nread < 0)
	    return TRUE;

	if (nread != 4) {
	    g_set_error (err, DS_ERROR, DS_ERROR_FORMAT,
			 "Invalid mask item: bad item length");
	    return TRUE;
	}

	mask->current_val = cur = IO_RECODE_I32 (bufptr);
	mask->bits_left_in_current = 31;
    }

    return FALSE;
}
