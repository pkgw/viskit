#ifndef _VISKIT_IOSTREAM_H
#define _VISKIT_IOSTREAM_H

#include <glib.h>
#include <viskit/types.h>

typedef struct _InputStream InputStream;


/* Dealing with endianness conversions */

#define IO_RECODE_I16(buf) GINT16_FROM_BE (*((guint16 *)buf))
#define IO_RECODE_I32(buf) GINT32_FROM_BE (*((guint32 *)buf))
#define IO_RECODE_I64(buf) GINT64_FROM_BE (*((guint64 *)buf))

extern void io_recode_data_copy (gchar *src, gchar *dest, 
				 DSType type, gsize nvals);
extern void io_recode_data_inplace (gchar *data, DSType type, gsize nvals);


/* The actual I/O routines */

struct _InputStream {
    int fd;
    gsize bufsz;
    gchar *rbuf;
    gsize rpos; /* position of read cursor within buffer. */
    gboolean reof; /* have we read to EOF? */
    gsize rend; /* location of EOF within the buffer */
    gchar *scratch; /* for block-crossing read requests. */
};

#define IO_ERRNO_ERR(err, errno, msg)					\
    g_set_error (err, G_FILE_ERROR, g_file_error_from_errno(errno),	\
		 msg ": %s", g_strerror (errno))
#define IO_ERRNO_ERRV(err, errno, msg, rest...)				\
    g_set_error (err, G_FILE_ERROR, g_file_error_from_errno(errno),	\
		 msg ": %s", rest, g_strerror (errno))

extern void io_input_init (InputStream *io, gsize bufsz);
extern void io_input_uninit (InputStream *io);
extern InputStream *io_input_alloc (gsize bufsz);
extern void io_input_free (InputStream *io);

extern gssize io_fetch_temp (InputStream *io, gsize nbytes, gchar **dest,
			     GError **err);
extern gssize io_fetch_temp_typed (InputStream *io, DSType type, gsize nvals,
				   gpointer *dest, GError **err);
extern gssize io_fetch_prealloc (InputStream *io, DSType type, gsize nvals,
				 gpointer buf, GError **err);
extern gboolean io_nudge_align (InputStream *io, gsize align_size, 
				GError **err);

#endif
