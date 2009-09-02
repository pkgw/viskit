#! /bin/sh
#
# Run this to create the configure script and Makefile.in
# files. Strongly based on the autogen.sh in x.org xf86-video-ati.
#
# Many packages have their autogen.sh also run the configure
# script; I don't like this behavior and avoid it here.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd $srcdir || exit $?

autoreconf -v --install || exit 1
cd $ORIGDIR || exit $?
