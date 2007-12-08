AC_DEFUN([AX_BOOST_IOSTREAMS],
[AC_REQUIRE([AC_CXX_NAMESPACES])dnl
AC_CACHE_CHECK(whether the Boost::IOStreams library is available,
ax_cv_boost_iostreams,
[AC_LANG_SAVE
 AC_LANG_CPLUSPLUS
 AC_COMPILE_IFELSE(AC_LANG_PROGRAM([[@%:@include <boost/iostreams/filtering_stream.hpp>
@%:@include <boost/range/iterator_range.hpp>]],
[[std::string  input = "Hello World!";namespace io = boost::iostreams;io::filtering_istream  in(boost::make_iterator_range(input));return 0;]]
),
  	           ax_cv_boost_iostreams=yes, ax_cv_boost_iostreams=no)
 AC_LANG_RESTORE
])
if test "$ax_cv_boost_iostreams" = yes; then
  AC_DEFINE(HAVE_BOOST_IOSTREAMS,,[define if the Boost::IOStreams library is available])
  dnl Now determine the appropriate file names
  AC_ARG_WITH([boost-iostreams],AS_HELP_STRING([--with-boost-iostreams],
  [specify the boost iostreams library or suffix to use]),
  [if test "x$with_boost_iostreams" != "xno"; then
    ax_iostreams_lib=$with_boost_iostreams
    ax_boost_iostreams_lib=boost_iostreams-$with_boost_iostreams
  fi])
  for ax_lib in $ax_iostreams_lib $ax_boost_iostreams_lib boost_iostreams; do
    AC_CHECK_LIB($ax_lib, main, [BOOST_IOSTREAMS_LIB=$ax_lib break])
  done
  AC_SUBST(BOOST_IOSTREAMS_LIB)
fi
])dnl
