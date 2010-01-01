#ifndef _VISKIT_IOSTREAM_H
#define _VISKIT_IOSTREAM_H

#include <glib.h>
#include <viskit/types.h>

typedef struct _IOStream IOStream;

typedef enum _IOMode {
    IO_MODE_READ       = 1 << 0,
    IO_MODE_WRITE      = 1 << 1,
    IO_MODE_READ_WRITE = IO_MODE_READ | IO_MODE_WRITE,
} IOMode;


/* Dealing with endianness conversions */

#define IO_RECODE_I16(buf) GINT16_FROM_BE (*((guint16 *)buf))
#define IO_RECODE_I32(buf) GINT32_FROM_BE (*((guint32 *)buf))
#define IO_RECODE_I64(buf) GINT64_FROM_BE (*((guint64 *)buf))

extern void io_recode_data_copy (const gchar *src, gchar *dest,
				 DSType type, gsize nvals);
extern void io_recode_data_inplace (gchar *data, DSType type, gsize nvals);


/* The actual I/O routines */

#define IO_ERRNO_ERR(err, errno, msg)					\
    g_set_error (err, G_FILE_ERROR, g_file_error_from_errno(errno),	\
		 msg ": %s", g_strerror (errno))
#define IO_ERRNO_ERRV(err, errno, msg, rest...)				\
    g_set_error (err, G_FILE_ERROR, g_file_error_from_errno(errno),	\
		 msg ": %s", rest, g_strerror (errno))

extern IOStream *io_new_from_fd (IOMode mode, int fd, gsize bufsz, goffset align_hint);
extern void io_free (IOStream *io);
extern gboolean io_close_and_free (IOStream *io, GError **err);

extern int io_get_fd (IOStream *io);

extern gssize io_read_into_temp_buf (IOStream *io, gsize nbytes, gpointer *dest,
				     GError **err);
extern gssize io_read_into_temp_buf_typed (IOStream *io, DSType type, gsize nvals,
					   gpointer *dest, GError **err);
extern gssize io_read_into_user_buf (IOStream *io, DSType type, gsize nvals,
				     gpointer buf, GError **err);

extern gboolean io_write_raw (IOStream *io, gsize nbytes, gconstpointer buf,
			      GError **err);
extern gboolean io_write_typed (IOStream *io, DSType type, gsize nvals,
				gconstpointer buf, GError **err);

extern gboolean io_nudge_align (IOStream *io, gsize align_size, GError **err);

#endif
