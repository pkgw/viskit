AC_INIT([viskit],[0.1],[peter@newton.cx])
AC_CONFIG_SRCDIR([doc/dataset.txt])

AM_INIT_AUTOMAKE([dist-bzip2])

AM_MAINTAINER_MODE
AC_PROG_LIBTOOL

AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O dnl For per-target CFLAGS

if test x"$ac_cv_prog_cc_c99" = xno ; then
  AC_MSG_ERROR([support for the C99 C language standard is required but not detected])
fi

GLOBALCFLAGS=-Wall
AC_SUBST([GLOBALCFLAGS])

PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.16)
AC_SUBST([GLIB_CFLAGS])
AC_SUBST([GLIB_LIBS])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
Makefile
demo/Makefile
viskit/Makefile
])

AC_OUTPUT
