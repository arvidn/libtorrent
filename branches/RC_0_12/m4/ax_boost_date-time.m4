dnl @synopsis AX_BOOST_DATE_TIME
dnl
dnl This macro checks to see if the Boost.DateTime library is
dnl installed. It also attempts to guess the currect library name using
dnl several attempts. It tries to build the library name using a user
dnl supplied name or suffix and then just the raw library.
dnl
dnl If the library is found, HAVE_BOOST_DATE_TIME is defined and
dnl BOOST_DATE_TIME_LIB is set to the name of the library.
dnl
dnl This macro calls AC_SUBST(BOOST_DATE_TIME_LIB).
dnl
dnl @category InstalledPackages
dnl @author Michael Tindal <mtindal@paradoxpoint.com>
dnl @version 2004-09-20
dnl @license GPLWithACException

AC_DEFUN([AX_BOOST_DATE_TIME],
[AC_REQUIRE([AC_CXX_NAMESPACES])dnl
AC_CACHE_CHECK(whether the Boost::DateTime library is available,
ax_cv_boost_date_time,
[AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 AC_COMPILE_IFELSE(AC_LANG_PROGRAM([[#include <boost/date_time/gregorian/gregorian_types.hpp>]],
			           [[using namespace boost::gregorian; date d(2002,Jan,10); return 0;]]),
  	           ax_cv_boost_date_time=yes, ax_cv_boost_date_time=no)
 AC_LANG_RESTORE
])
if test "$ax_cv_boost_date_time" = yes; then
  AC_DEFINE(HAVE_BOOST_DATE_TIME,,[define if the Boost::DateTime library is available])
  dnl Now determine the appropriate file names
  AC_ARG_WITH([boost-date-time],AS_HELP_STRING([--with-boost-date-time],
  [specify the boost date-time library or suffix to use]),
  [if test "x$with_boost_date_time" != "xno"; then
    ax_date_time_lib=$with_boost_date_time
    ax_boost_date_time_lib=boost_date_time-$with_boost_date_time
  fi])
  for ax_lib in $ax_date_time_lib $ax_boost_date_time_lib boost_date_time; do
    AC_CHECK_LIB($ax_lib, main, [BOOST_DATE_TIME_LIB=$ax_lib
break])
  done
  AC_SUBST(BOOST_DATE_TIME_LIB)
fi
])dnl
