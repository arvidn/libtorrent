dnl @synopsis AX_BOOST_THREAD
dnl
dnl This macro checks to see if the Boost.Thread library is installed.
dnl It also attempts to guess the currect library name using several
dnl attempts. It tries to build the library name using a user supplied
dnl name or suffix and then just the raw library.
dnl
dnl If the library is found, HAVE_BOOST_THREAD is defined and
dnl BOOST_THREAD_LIB is set to the name of the library.
dnl
dnl This macro calls AC_SUBST(BOOST_THREAD_LIB).
dnl
dnl @category InstalledPackages
dnl @author Michael Tindal <mtindal@paradoxpoint.com>
dnl @version 2004-09-20
dnl @license GPLWithACException

AC_DEFUN([AX_BOOST_THREAD],
[AC_REQUIRE([AC_CXX_NAMESPACES])dnl
AC_CACHE_CHECK(whether the Boost::Thread library is available,
ax_cv_boost_thread,
[AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 CXXFLAGS_SAVE=$CXXFLAGS
dnl FIXME: need to include a generic way to check for the flag
dnl to turn on threading support.
 CXXFLAGS="-pthread $CXXFLAGS"
 AC_COMPILE_IFELSE(AC_LANG_PROGRAM([[#include <boost/thread/thread.hpp>]],
			           [[boost::thread_group thrds; return 0;]]),
  	           ax_cv_boost_thread=yes, ax_cv_boost_thread=no)
 CXXFLAGS=$CXXFLAGS_SAVE
 AC_LANG_RESTORE
])
if test "$ax_cv_boost_thread" = yes; then
  AC_DEFINE(HAVE_BOOST_THREAD,,[define if the Boost::Thread library is available])
  dnl Now determine the appropriate file names
  AC_ARG_WITH([boost-thread],AS_HELP_STRING([--with-boost-thread],
  [specify the boost thread library or suffix to use]),
  [if test "x$with_boost_thread" != "xno"; then
    ax_thread_lib=$with_boost_thread
    ax_boost_thread_lib=boost_thread-$with_boost_thread
  fi])
  for ax_lib in $ax_thread_lib $ax_boost_thread_lib boost_thread; do
    AC_CHECK_LIB($ax_lib, main, [BOOST_THREAD_LIB=$ax_lib
break])
  done
  AC_SUBST(BOOST_THREAD_LIB)
fi
])dnl
