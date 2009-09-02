AM_CFLAGS = $(GLOBALCFLAGS)

lib_LTLIBRARIES = libviskit.la

libviskit_la_CFLAGS = -I$(top_srcdir) $(GLOBALCFLAGS) $(GLIB_CFLAGS)
libviskit_la_LIBADD = $(GLIB_LIBS)

libviskit_la_SOURCES = \
 dataset.c \
 dataset.h \
 iostream.c \
 iostream.h \
 maskitem.c \
 maskitem.h \
 types.c \
 types.h \
 uvreader.c \
 uvreader.h
