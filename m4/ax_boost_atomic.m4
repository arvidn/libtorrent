# SYNOPSIS
#
#   AX_BOOST_ATOMIC
#
# DESCRIPTION
#
#   Test for Atomic library from the Boost C++ libraries. The macro requires
#   a preceding call to AX_BOOST_BASE. Further documentation is available at
#   <http://randspringer.de/boost/index.html>.
#
#   This macro calls:
#
#     AC_SUBST(BOOST_ATOMIC_LIB)
#
#   And sets:
#
#     HAVE_BOOST_ATOMIC

#
# LICENSE
#
#   Copyright (c) 2015 Sarah Hoffmann <lonvia@denofr.de>
#   Copyright (c) 2008 Thomas Porschberg <thomas@randspringer.de>
#   Copyright (c) 2008 Michael Tindal
#   Copyright (c) 2008 Daniel Casimiro <dan.casimiro@gmail.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 1

AC_DEFUN([AX_BOOST_ATOMIC],
[
    AC_ARG_WITH([boost-atomic],
    AS_HELP_STRING([--with-boost-atomic@<:@=special-lib@:>@],
                   [use the Atomic library from boost - it is possible to specify a certain library for the linker
                        e.g. --with-boost-atomic=boost_atomic-gcc-mt ]),
        [
        if test "$withval" = "no"; then
            want_boost="no"
        elif test "$withval" = "yes"; then
            want_boost="yes"
            ax_boost_user_atomic_lib=""
        else
            want_boost="yes"
        ax_boost_user_atomic_lib="$withval"
        fi
        ],
        [want_boost="yes"]
    )

    if test "x$want_boost" = "xyes"; then
        AC_REQUIRE([AC_PROG_CC])
        AC_REQUIRE([AC_CANONICAL_BUILD])
        CPPFLAGS_SAVED="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS $BOOST_CPPFLAGS"
        export CPPFLAGS

        LDFLAGS_SAVED="$LDFLAGS"
        LDFLAGS="$LDFLAGS $BOOST_LDFLAGS"
        export LDFLAGS

        AC_CACHE_CHECK(whether the Boost::Atomic library is available,
                       ax_cv_boost_atomic,
        [AC_LANG_PUSH([C++])
             CXXFLAGS_SAVE=$CXXFLAGS

             AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[@%:@include <boost/atomic.hpp>]],
                                   [[boost::atomic_size_t x;]])],
                   ax_cv_boost_atomic=yes, ax_cv_boost_atomic=no)
             CXXFLAGS=$CXXFLAGS_SAVE
             AC_LANG_POP([C++])
        ])
        if test "x$ax_cv_boost_atomic" = "xyes"; then
            AC_SUBST(BOOST_CPPFLAGS)

            AC_DEFINE(HAVE_BOOST_ATOMIC,,[define if the Boost::Atomic library is available])
            BOOSTLIBDIR=`echo $BOOST_LDFLAGS | sed -e 's/@<:@^\/@:>@*//'`

            LDFLAGS_SAVE=$LDFLAGS
            if test "x$ax_boost_user_atomic_lib" = "x"; then
                for libextension in `ls -r $BOOSTLIBDIR/libboost_atomic* 2>/dev/null | sed 's,.*/lib,,' | sed 's,\..*,,'` ; do
                     ax_lib=${libextension}
                    AC_CHECK_LIB($ax_lib, exit,
                                 [BOOST_ATOMIC_LIB="-l$ax_lib"; AC_SUBST(BOOST_ATOMIC_LIB) link_atomic="yes"; break],
                                 [link_atomic="no"])
                done
                if test "x$link_atomic" != "xyes"; then
                for libextension in `ls -r $BOOSTLIBDIR/boost_atomic* 2>/dev/null | sed 's,.*/,,' | sed -e 's,\..*,,'` ; do
                     ax_lib=${libextension}
                    AC_CHECK_LIB($ax_lib, exit,
                                 [BOOST_ATOMIC_LIB="-l$ax_lib"; AC_SUBST(BOOST_ATOMIC_LIB) link_atomic="yes"; break],
                                 [link_atomic="no"])
                done
                fi

            else
               for ax_lib in $ax_boost_user_atomic_lib boost_atomic-$ax_boost_user_atomic_lib; do
                      AC_CHECK_LIB($ax_lib, exit,
                                   [BOOST_ATOMIC_LIB="-l$ax_lib"; AC_SUBST(BOOST_ATOMIC_LIB) link_atomic="yes"; break],
                                   [link_atomic="no"])
                  done

            fi
            if test "x$ax_lib" = "x"; then
                AC_MSG_ERROR(Could not find a version of the library!)
            fi
            if test "x$link_atomic" = "xno"; then
                AC_MSG_ERROR(Could not link against $ax_lib !)
            fi
        fi

        CPPFLAGS="$CPPFLAGS_SAVED"
    LDFLAGS="$LDFLAGS_SAVED"
    fi
])

