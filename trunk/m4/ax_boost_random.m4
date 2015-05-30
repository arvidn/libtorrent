# ===========================================================================
#       http://www.gnu.org/software/autoconf-archive/ax_boost_random.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_BOOST_RANDOM
#
# DESCRIPTION
#
#   Test for Random library from the Boost C++ libraries. The macro requires a
#   preceding call to AX_BOOST_BASE. Further documentation is available at
#   <http://randspringer.de/boost/index.html>.
#
#   This macro calls:
#
#     AC_SUBST(BOOST_RANDOM_LIB)
#
#   And sets:
#
#     HAVE_BOOST_RANDOM
#
# LICENSE
#
#   Copyright (c) 2008 Thomas Porschberg <thomas@randspringer.de>
#   Copyright (c) 2008 Michael Tindal
#   Copyright (c) 2013 Daniel Casimiro <dan.casimiro@gmail.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 1

AC_DEFUN([AX_BOOST_RANDOM],
[
	AC_ARG_WITH([boost-random],
		AS_HELP_STRING([--with-boost-random@<:@=special-lib@:>@],
		[use the Random library from boost - it is possible to specify a certain library for the linker
			e.g. --with-boost-random=boost_random-gcc-mt ]), [
		if test "$withval" = "no"; then
			want_boost="no"
		elif test "$withval" = "yes"; then
			want_boost="yes"
			ax_boost_user_random_lib=""
		else
			want_boost="yes"
			ax_boost_user_random_lib="$withval"
		fi
		], [want_boost="yes"]
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

		AC_CACHE_CHECK(whether the Boost::Random library is available,
			ax_cv_boost_random,
			[AC_LANG_PUSH([C++])
			CXXFLAGS_SAVE=$CXXFLAGS

			AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
				[[@%:@include <boost/random/random_device.hpp>]],
				[[boost::random::random_device()();]])],
				ax_cv_boost_random=yes, ax_cv_boost_random=no)
				CXXFLAGS=$CXXFLAGS_SAVE
			AC_LANG_POP([C++])
		])

		if test "x$ax_cv_boost_random" = "xyes"; then
			AC_SUBST(BOOST_CPPFLAGS)

			AC_DEFINE(HAVE_BOOST_RANDOM,,[define if the Boost::Random library is available])
			BOOSTLIBDIR=`echo $BOOST_LDFLAGS | sed -e 's/@<:@^\/@:>@*//'`

			if test "x$ax_boost_user_random_lib" = "x"; then
				for libextension in `ls $BOOSTLIBDIR/libboost_random*.so* $BOOSTLIBDIR/libboost_random*.dylib* $BOOSTLIBDIR/libboost_random*.a* 2>/dev/null | sed 's,.*/,,' | sed -e 's;^lib\(boost_random.*\)\.so.*$;\1;' -e 's;^lib\(boost_random.*\)\.dylib.*$;\1;' -e 's;^lib\(boost_random.*\)\.a.*$;\1;'` ; do
					ax_lib=${libextension}
					AC_CHECK_LIB($ax_lib, exit,
						[BOOST_RANDOM_LIB="-l$ax_lib"; AC_SUBST(BOOST_RANDOM_LIB) link_random="yes"; break],
					[link_random="no"])
				done

				if test "x$link_random" != "xyes"; then
					for libextension in `ls $BOOSTLIBDIR/boost_random*.dll* $BOOSTLIBDIR/boost_random*.a* 2>/dev/null | sed 's,.*/,,' | sed -e 's;^\(boost_random.*\)\.dll.*$;\1;' -e 's;^\(boost_random.*\)\.a.*$;\1;'` ; do
						ax_lib=${libextension}
						AC_CHECK_LIB($ax_lib, exit,
							[BOOST_RANDOM_LIB="-l$ax_lib"; AC_SUBST(BOOST_RANDOM_LIB) link_random="yes"; break],
							[link_random="no"])
					done
				fi

			else
				for ax_lib in $ax_boost_user_random_lib boost_random-$ax_boost_user_random_lib; do
					AC_CHECK_LIB($ax_lib, exit,
						[BOOST_RANDOM_LIB="-l$ax_lib"; AC_SUBST(BOOST_RANDOM_LIB) link_random="yes"; break],
						[link_random="no"])
				done
			fi

			if test "x$ax_lib" = "x"; then
				AC_MSG_ERROR(Could not find a version of the library!)
			fi

			if test "x$link_random" = "xno"; then
				AC_MSG_ERROR(Could not link against $ax_lib !)
			fi
		fi

		CPPFLAGS="$CPPFLAGS_SAVED"
		LDFLAGS="$LDFLAGS_SAVED"
	fi
])

