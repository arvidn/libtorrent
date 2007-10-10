dnl @synopsis AX_BOOST_PROGRAM_OPTIONS
dnl
dnl This macro checks to see if the Boost.ProgramOptions library is installed.
dnl It also attempts to guess the currect library name using several
dnl attempts. It tries to build the library name using a user supplied
dnl name or suffix and then just the raw library.
dnl
dnl If the library is found, HAVE_BOOST_PROGRAM_OPTIONS is defined and
dnl BOOST_THREAD_LIB is set to the name of the library.
dnl
dnl This macro calls AC_SUBST(BOOST_PROGRAM_OPTIONS_LIB).
dnl



AC_DEFUN([AX_BOOST_PROGRAM_OPTIONS],
[AC_REQUIRE([AC_CXX_NAMESPACES])dnl
AC_CACHE_CHECK(whether the Boost::ProgramOptions library is available,
ax_cv_boost_program_options,
[AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 AC_COMPILE_IFELSE(AC_LANG_PROGRAM([[#include <boost/program_options.hpp>]],
			           [[boost::program_options::options_description desc("test"); return 0;]]),
  	           ax_cv_boost_program_options=yes, ax_cv_boost_program_options=no)
 AC_LANG_RESTORE
])
if test "$ax_cv_boost_program_options" = yes; then
  AC_DEFINE(HAVE_BOOST_PROGRAM_OPTIONS,,[define if the Boost::ProgramOptions library is available])
  dnl Now determine the appropriate file names
  AC_ARG_WITH([boost-program_options],AS_HELP_STRING([--with-boost-program_options],
  [specify the boost program_options library or suffix to use]),
  [if test "x$with_boost_thread" != "xno"; then
    ax_program_options_lib=$with_boost_program_options
    ax_boost_program_options_lib=boost_program_options-$with_boost_program_options
  fi])
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  for ax_lib in $ax_program_options_lib $ax_boost_program_options_lib boost_program_options-mt boost_program_options; do
    ax_save_LIBS="$LIBS"
    LIBS="$LIBS -l$ax_lib"
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <boost/program_options.hpp>]],
				    [[return 0;]])],
               [BOOST_PROGRAM_OPTIONS_LIB=$ax_lib
               break])
    LIBS="$ax_save_LIBS"
  done
  AC_LANG_RESTORE
  AC_SUBST(BOOST_PROGRAM_OPTIONS_LIB)
fi
])dnl
