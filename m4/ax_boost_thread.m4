# ===========================================================================
#         http://www.nongnu.org/autoconf-archive/ax_boost_thread.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_BOOST_THREAD
#
# DESCRIPTION
#
#   Test for Thread library from the Boost C++ libraries. The macro requires
#   a preceding call to AX_BOOST_BASE. Further documentation is available at
#   <http://randspringer.de/boost/index.html>.
#
#   This macro calls:
#
#     AC_SUBST(BOOST_THREAD_LIB)
#
#   And sets:
#
#     HAVE_BOOST_THREAD
#
# LICENSE
#
#   Copyright (c) 2009 Thomas Porschberg <thomas@randspringer.de>
#   Copyright (c) 2009 Michael Tindal
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.

AC_DEFUN([AX_BOOST_THREAD],
[
	AC_ARG_WITH([boost-thread],
	AS_HELP_STRING([--with-boost-thread@<:@=special-lib@:>@],
                   [use the Thread library from boost - it is possible to specify a certain library for the linker
                        e.g. --with-boost-thread=boost_thread-gcc-mt ]),
        [
        if test "x$withval" = "xno"; then
			want_boost="no"
        elif test "x$withval" = "xyes"; then
            want_boost="yes"
            ax_boost_user_thread_lib=""
        else
		    want_boost="yes"
        	ax_boost_user_thread_lib="$withval"
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

        AC_CACHE_CHECK(whether the Boost::Thread library is available,
					   ax_cv_boost_thread,
        [AC_LANG_PUSH([C++])
			 CXXFLAGS_SAVE=$CXXFLAGS

dnl			 if test "x$build_os" = "xsolaris" ; then
dnl  				 CXXFLAGS="-pthreads $CXXFLAGS"
dnl			 elif test "x$build_os" = "xming32" ; then
dnl				 CXXFLAGS="-mthreads $CXXFLAGS"
dnl			 else
dnl				CXXFLAGS="-pthread $CXXFLAGS"
dnl			 fi
			 AC_COMPILE_IFELSE(AC_LANG_PROGRAM([[@%:@include <boost/thread/thread.hpp>]],
                                   [[boost::thread_group thrds;
                                   return 0;]]),
                   ax_cv_boost_thread=yes, ax_cv_boost_thread=no)
			 CXXFLAGS=$CXXFLAGS_SAVE
             AC_LANG_POP([C++])
		])
		if test "x$ax_cv_boost_thread" = "xyes"; then
dnl           if test "x$build_os" = "xsolaris" ; then
dnl			  BOOST_CPPFLAGS="-pthreads $BOOST_CPPFLAGS"
dnl		   elif test "x$build_os" = "xming32" ; then
dnl			  BOOST_CPPFLAGS="-mthreads $BOOST_CPPFLAGS"
dnl		   else
dnl			  BOOST_CPPFLAGS="-pthread $BOOST_CPPFLAGS"
dnl		   fi

			AC_SUBST(BOOST_CPPFLAGS)

			AC_DEFINE(HAVE_BOOST_THREAD,[1],[define if the Boost::Thread library is available])
            BOOSTLIBDIR=`echo $BOOST_LDFLAGS | sed -e 's/@<:@^\/@:>@*//'`

			LDFLAGS_SAVE=$LDFLAGS
                        case "x$build_os" in
                          *bsd* )
                               LDFLAGS="-pthread $LDFLAGS"
                          break;
                          ;;
                        esac
            if test "x$ax_boost_user_thread_lib" = "x"; then
                for libextension in `ls $BOOSTLIBDIR/libboost_thread*.so* 2>/dev/null | sed 's,.*/,,' | sed -e 's;^lib\(boost_thread.*\)\.so.*$;\1;'` `ls $BOOSTLIBDIR/libboost_thread*.a* 2>/dev/null | sed 's,.*/,,' | sed -e 's;^lib\(boost_thread.*\)\.a*$;\1;'`; do
                     ax_lib=${libextension}
				    AC_CHECK_LIB($ax_lib, exit,
                                 [BOOST_THREAD_LIB="-l$ax_lib"; AC_SUBST(BOOST_THREAD_LIB) link_thread="yes"; break],
                                 [link_thread="no"])
  				done
                if test "x$link_thread" != "xyes"; then
                for libextension in `ls $BOOSTLIBDIR/boost_thread*.dll* 2>/dev/null | sed 's,.*/,,' | sed -e 's;^\(boost_thread.*\)\.dll.*$;\1;'` `ls $BOOSTLIBDIR/boost_thread*.a* 2>/dev/null | sed 's,.*/,,' | sed -e 's;^\(boost_thread.*\)\.a*$;\1;'` ; do
                     ax_lib=${libextension}
				    AC_CHECK_LIB($ax_lib, exit,
                                 [BOOST_THREAD_LIB="-l$ax_lib"; AC_SUBST(BOOST_THREAD_LIB) link_thread="yes"; break],
                                 [link_thread="no"])
  				done
                fi

            else
               for ax_lib in $ax_boost_user_thread_lib boost_thread-$ax_boost_user_thread_lib; do
				      AC_CHECK_LIB($ax_lib, exit,
                                   [BOOST_THREAD_LIB="-l$ax_lib"; AC_SUBST(BOOST_THREAD_LIB) link_thread="yes"; break],
                                   [link_thread="no"])
                  done

            fi
			if test "x$link_thread" = "xno"; then
				AC_MSG_ERROR(Could not link against $ax_lib !)
                        else
                           case "x$build_os" in
                              *bsd* )
			        BOOST_LDFLAGS="-pthread $BOOST_LDFLAGS"
                              break;
                              ;;
                           esac

			fi
		fi

		CPPFLAGS="$CPPFLAGS_SAVED"
    	LDFLAGS="$LDFLAGS_SAVED"
	fi
])
