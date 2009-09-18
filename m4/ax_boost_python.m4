# ===========================================================================
#         http://www.nongnu.org/autoconf-archive/ax_boost_python.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_BOOST_PYTHON
#
# DESCRIPTION
#
#   This macro checks to see if the Boost.Python library is installed. It
#   also attempts to guess the currect library name using several attempts.
#   It tries to build the library name using a user supplied name or suffix
#   and then just the raw library.
#
#   If the library is found, HAVE_BOOST_PYTHON is defined and
#   BOOST_PYTHON_LIB is set to the name of the library.
#
#   This macro calls AC_SUBST(BOOST_PYTHON_LIB).
#
#   In order to ensure that the Python headers are specified on the include
#   path, this macro requires AX_PYTHON to be called.
#
#   EDIT:
#   2009-09-14 Cristian Greco <cristian.debian@gmail.com>
#    - Require AX_PYTHON_DEVEL to be called before this macro to properly
#      detect python include path, instead of AX_PYTHON.
#   2009-09-07 Cristian Greco <cristian.debian@gmail.com>
#    - Prefix BOOST_PYTHON_LIB with a `-l` for consistency with other
#      ax_boost_libname.m4 scripts.
#
# LICENSE
#
#   Copyright (c) 2008 Michael Tindal
#
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation; either version 2 of the License, or (at your
#   option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
#   Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program. If not, see <http://www.gnu.org/licenses/>.
#
#   As a special exception, the respective Autoconf Macro's copyright owner
#   gives unlimited permission to copy, distribute and modify the configure
#   scripts that are the output of Autoconf when processing the Macro. You
#   need not follow the terms of the GNU General Public License when using
#   or distributing such scripts, even though portions of the text of the
#   Macro appear in them. The GNU General Public License (GPL) does govern
#   all other use of the material that constitutes the Autoconf Macro.
#
#   This special exception to the GPL applies to versions of the Autoconf
#   Macro released by the Autoconf Archive. When you make and distribute a
#   modified version of the Autoconf Macro, you may extend this special
#   exception to the GPL to apply to your modified version as well.

AC_DEFUN([AX_BOOST_PYTHON],
[AC_REQUIRE([AX_PYTHON_DEVEL])dnl
AC_CACHE_CHECK(whether the Boost::Python library is available,
ac_cv_boost_python,
[AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 CPPFLAGS_SAVE=$CPPFLAGS
 if test "x$PYTHON_CPPFLAGS" != "x"; then
   CPPFLAGS="$PYTHON_CPPFLAGS $CPPFLAGS"
 fi
 AC_COMPILE_IFELSE(AC_LANG_PROGRAM([[
 #include <boost/python/module.hpp>
 using namespace boost::python;
 BOOST_PYTHON_MODULE(test) { throw "Boost::Python test."; }]],
 			   [[return 0;]]),
  			   ac_cv_boost_python=yes, ac_cv_boost_python=no)
 AC_LANG_RESTORE
 CPPFLAGS=$CPPFLAGS_SAVE
])
if test "x$ac_cv_boost_python" = "xyes"; then
  AC_DEFINE(HAVE_BOOST_PYTHON,[1],[define if the Boost::Python library is available])
  dnl
  LDFLAGS_SAVE=$LDFLAGS
  if test "x$PYTHON_LDFLAGS" != "x"; then
     LDFLAGS="$LDFLAGS $PYTHON_LDFLAGS"
  fi
  dnl
  ax_python_lib=boost_python
  AC_ARG_WITH([boost-python],AS_HELP_STRING([--with-boost-python],[specify the boost python library or suffix to use]),
  [if test "x$with_boost_python" != "xno"; then
     ax_python_lib="$with_boost_python"
     ax_boost_python_lib="boost_python-$with_boost_python"
   fi])
  for ax_lib in $ax_python_lib $ax_boost_python_lib boost_python; do
    AC_CHECK_LIB($ax_lib, main, [BOOST_PYTHON_LIB=-l$ax_lib break])
  done
  dnl
  LDFLAGS=$LDFLAGS_SAVE
  dnl
  AC_SUBST(BOOST_PYTHON_LIB)
fi
])dnl
