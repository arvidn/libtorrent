##### http://autoconf-archive.cryp.to/check_ssl.html
#
# SYNOPSIS
#
#   CHECK_SSL
#
# DESCRIPTION
#
#   This macro will check various standard spots for OpenSSL including
#   a user-supplied directory. The user uses '--with-ssl' or
#   '--with-ssl=/path/to/ssl' as arguments to configure.
#
#   If OpenSSL is found the include directory gets added to CFLAGS and
#   CXXFLAGS as well as '-DHAVE_SSL', '-lssl' & '-lcrypto' get added to
#   LIBS, and the libraries location gets added to LDFLAGS. Finally
#   'HAVE_SSL' gets set to 'yes' for use in your Makefile.in I use it
#   like so (valid for gmake):
#
#       HAVE_SSL = @HAVE_SSL@
#       ifeq ($(HAVE_SSL),yes)
#           SRCS+= @srcdir@/my_file_that_needs_ssl.c
#       endif
#
#   For bsd 'bmake' use:
#
#       .if ${HAVE_SSL} == "yes"
#           SRCS+= @srcdir@/my_file_that_needs_ssl.c
#       .endif
#
# LAST MODIFICATION
#
#   2003-01-28
#
# COPYLEFT
#
#   Copyright (c) 2003 Mark Ethan Trostler <trostler@juniper.net>
#
#   Copying and distribution of this file, with or without
#   modification, are permitted in any medium without royalty provided
#   the copyright notice and this notice are preserved.

AC_DEFUN([CHECK_SSL],
[
dnl AC_MSG_CHECKING(if ssl is wanted)
AC_ARG_WITH(ssl,
[  --with-ssl enable ssl [will check /usr/local/ssl
                            /usr/lib/ssl /usr/ssl /usr/pkg /usr/local /usr ]
])
dnl [   AC_MSG_RESULT(yes)
    for dir in $withval /usr/local/ssl /usr/lib/ssl /usr/ssl /usr/pkg /usr/local /usr; do
        ssldir="$dir"
        if test -f "$dir/include/openssl/ssl.h"; then
            found_ssl="yes";
            SSL_CFLAGS="-I$ssldir/include/openssl -DHAVE_SSL";
            SSL_CXXFLAGS="-I$ssldir/include/openssl -DHAVE_SSL";
            break;
        fi
        if test -f "$dir/include/ssl.h"; then
            found_ssl="yes";
            SSL_CFLAGS="-I$ssldir/include/ -DHAVE_SSL";
            SSL_CXXFLAGS="-I$ssldir/include/ -DHAVE_SSL";
            break
        fi
    done
    if test x_$found_ssl != x_yes; then
        AC_MSG_ERROR(Cannot find ssl libraries)
    else
        printf "OpenSSL found in $ssldir\n";
        SSL_LIBS="-lssl -lcrypto";
        SSL_LDFLAGS="-L$ssldir/lib";
        HAVE_SSL=yes
    fi
    AC_SUBST(SSL_CFLAGS)
    AC_SUBST(SSL_CXXFLAGS)
    AC_SUBST(SSL_LIBS)
    AC_SUBST(SSL_LDFLAGS)
    AC_SUBST(HAVE_SSL)
dnl ],
dnl [
dnl     AC_MSG_RESULT(no)
dnl ])
])dnl
