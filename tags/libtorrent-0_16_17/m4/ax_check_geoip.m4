# ===========================================================================
#
# SYNOPSIS
#
#   AX_CHECK_GEOIP([action-if-found], [action-if-not-found])
#
# DESCRIPTION
#
#   Tests for the GeoIP (libgeoip) library.
#
#   This macro calls:
#
#     AC_SUBST(GEOIP_CFLAGS) / AC_SUBST(GEOIP_LIBS)
#
# LAST MODIFICATION
#
#   2009-09-05
#
# LICENSE
#
#   Copyright (c) 2009 Cristian Greco <cristian.debian@gmail.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.

AC_DEFUN([AC_CHECK_GEOIP],
[AC_REQUIRE([PKG_PROG_PKG_CONFIG])dnl

  ac_geoip_found="no"

  AC_MSG_CHECKING([for libgeoip with pkg-config])
  PKG_CHECK_EXISTS([geoip], [
    AC_MSG_RESULT([yes])
    PKG_CHECK_MODULES([GEOIP], [geoip], [
      ac_geoip_found="yes"
     ], [
      AC_MSG_WARN([pkg-config: geoip module not found])
    ])
   ], [
    AC_MSG_RESULT([no])

    CPPFLAGS_SAVED="$CPPFLAGS"
    LDFLAGS_SAVED="$LDFLAGS"
    CFLAGS_SAVED="$CFLAGS"
    LIBS_SAVED="$LIBS"

    AC_CHECK_HEADER([GeoIP.h], [
      AC_CHECK_LIB([GeoIP], [GeoIP_new], [
        GEOIP_CFLAGS=""
        GEOIP_LIBS="-lGeoIP"
        ac_geoip_found="yes"
       ], [
        AC_MSG_WARN([libgeoip library not found])
      ])
     ], [
      for ac_geoip_path in /usr /usr/local /opt /opt/local; do
        AC_MSG_CHECKING([for GeoIP.h in $ac_geoip_path])
        if test -d "$ac_geoip_path/include/" -a -r "$ac_geoip_path/include/GeoIP.h"; then
          AC_MSG_RESULT([yes])
          GEOIP_CFLAGS="-I$ac_geoip_path/include"
          GEOIP_LIBS="-lGeoIP"
          break;
        else
          AC_MSG_RESULT([no])
        fi
      done

      CFLAGS="$GEOIP_CFLAGS $CFLAGS"
      export CFLAGS
      LIBS="$GEOIP_LIBS $LIBS"
      export LIBS

      AC_MSG_CHECKING([for GeoIP_new in -lGeoIP])
      AC_LINK_IFELSE([
        AC_LANG_PROGRAM([[ #include <GeoIP.h> ]], [[ GeoIP *g = GeoIP_new(GEOIP_STANDARD); ]])
       ], [
        AC_MSG_RESULT([yes])
        ac_geoip_found="yes"
       ], [
        AC_MSG_RESULT([no])
      ])
    ])

    CPPFLAGS="$CPPFLAGS_SAVED"
    LDFLAGS="$LDFLAGS_SAVED"
    CFLAGS="$CFLAGS_SAVED"
    LIBS="$LIBS_SAVED"
  ])

  AS_IF([ test "x$ac_geoip_found" != xno ], [$1], [$2])

AC_SUBST([GEOIP_CFLAGS])
AC_SUBST([GEOIP_LIBS])
])
