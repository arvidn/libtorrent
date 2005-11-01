dnl @synopsis AX_BOOST_REGEX
dnl
dnl This macro checks to see if the Boost.Regex library is installed.
dnl It also attempts to guess the currect library name using several
dnl attempts.  It tries to build the library name using a user supplied
dnl name or suffix and then just the raw library.
dnl
dnl If the library is found, HAVE_BOOST_REGEX is defined and
dnl BOOST_REGEX_LIB is set to the name of the library.
dnl
dnl This macro calls AC_SUBST(BOOST_REGEX_LIB).
dnl
dnl @version $Id$
dnl @author Michael Tindal <mtindal@paradoxpoint.com>
dnl
AC_DEFUN([AX_BOOST_REGEX],
[AC_REQUIRE([AC_CXX_NAMESPACES])dnl
AC_CACHE_CHECK(whether the Boost::Regex library is available,
ax_cv_boost_regex,
[AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 AC_COMPILE_IFELSE(AC_LANG_PROGRAM([[#include <boost/regex.hpp>]],
			           [[boost::regex r(); return 0;]]),
  	           ax_cv_boost_regex=yes, ax_cv_boost_regex=no)
 AC_LANG_RESTORE
])
if test "$ax_cv_boost_regex" = yes; then
  AC_DEFINE(HAVE_BOOST_REGEX,,[define if the Boost::Regex library is available])
  dnl Now determine the appropriate file names
  AC_ARG_WITH([boost-regex],AS_HELP_STRING([--with-boost-regex],
  [specify the boost regex library or suffix to use]),
  [if test "x$with_boost_regex" != "xno"; then
    ax_regex_lib=$with_boost_regex
    ax_boost_regex_lib=boost_regex-$with_boost_regex
  fi])
  for ax_lib in $ax_regex_lib $ax_boost_regex_lib boost_regex; do
    AC_CHECK_LIB($ax_lib, main, [BOOST_REGEX_LIB=$ax_lib break])
  done
  AC_SUBST(BOOST_REGEX_LIB)
fi
])dnl
