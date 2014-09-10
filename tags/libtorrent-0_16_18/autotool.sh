#!/bin/sh

# Using this script should be identical to the resulto of "autoreconf -fi".
# Some code taken from the gnome macros/autogen.sh scripts.

# $Id$


###############################################################################
# utility functions
###############################################################################

# Not all echo versions allow -n, so we check what is possible. This test is
# based on the one in autoconf.
ECHO_C=
ECHO_N=
case `echo -n x` in
-n*)
  case `echo 'x\c'` in
  *c*) ;;
  *)   ECHO_C='\c';;
  esac;;
*)
  ECHO_N='-n';;
esac

# some terminal codes ...
boldface="`tput bold 2>/dev/null`"
normal="`tput sgr0 2>/dev/null`"

printbold() {
  echo $ECHO_N "$boldface" $ECHO_C
  echo "$@"
  echo $ECHO_N "$normal" $ECHO_C
}

printerr() {
  echo "$@" >&2
}

# Usage:
#     compare_versions MIN_VERSION ACTUAL_VERSION
# returns true if ACTUAL_VERSION >= MIN_VERSION
compare_versions() {
  ch_min_version=$1
  ch_actual_version=$2
  ch_status=0
  IFS="${IFS=         }"; ch_save_IFS="$IFS"; IFS="."
  set $ch_actual_version
  for ch_min in $ch_min_version; do
    ch_cur=`echo $1 | sed 's/[^0-9].*$//'`; shift # remove letter suffixes
    if [ -z "$ch_min" ]; then break; fi
    if [ -z "$ch_cur" ]; then ch_status=1; break; fi
    if [ $ch_cur -gt $ch_min ]; then break; fi
    if [ $ch_cur -lt $ch_min ]; then ch_status=1; break; fi
  done
  IFS="$ch_save_IFS"
  return $ch_status
}

# Usage:
#     version_check PACKAGE VARIABLE CHECKPROGS MIN_VERSION
# checks to see if the package is available
version_check() {
  vc_package=$1
  vc_variable=$2
  vc_checkprogs=$3
  vc_min_version=$4
  vc_status=1

  vc_checkprog=`eval echo "\\$$vc_variable"`
  if [ -n "$vc_checkprog" ]; then
	  printbold "Using $vc_checkprog for $vc_package"
  	return 0
  fi

  vc_comparator=">="

  printbold "Checking for $vc_package $vc_comparator $vc_min_version..."

  for vc_checkprog in $vc_checkprogs; do
	  echo $ECHO_N "  testing $vc_checkprog... " $ECHO_C
    	if $vc_checkprog --version < /dev/null > /dev/null 2>&1; then
	      vc_actual_version=`$vc_checkprog --version | head -n 1 | \
                            sed 's/^.*[ 	]\([0-9.]*[a-z]*\).*$/\1/'`
	      if compare_versions $vc_min_version $vc_actual_version; then
		      echo "found $vc_actual_version"
		      # set variables
      		eval "$vc_variable=$vc_checkprog; \
			          ${vc_variable}_VERSION=$vc_actual_version"
       		vc_status=0
      		break
	      else
    	  	echo "too old (found version $vc_actual_version)"
	      fi
    	else
	      echo "not found."
     	fi
  done

  if [ "$vc_status" != 0 ]; then
	  printerr "***Error***: $vc_package $vc_comparator $vc_min_version not found."
  fi
 
  return $vc_status
}

###############################################################################
# main section
###############################################################################

configure_ac="configure.ac"

(test -f $configure_ac && test -f src/torrent.cpp) || {
  printerr "***Error***: Run this script from the top-level source directory."
  exit 1
}

echo
printbold "Bootstrapping autotools for libtorrent-rasterbar"
echo

REQUIRED_AUTOCONF_VERSION=`cat $configure_ac | grep '^AC_PREREQ' |
sed -n -e 's/AC_PREREQ(\([^()]*\))/\1/p' | sed -e 's/^\[\(.*\)\]$/\1/' | sed -e 1q`

REQUIRED_AUTOMAKE_VERSION=`cat configure.ac | grep '^AM_INIT_AUTOMAKE' |
sed -n -e 's/AM_INIT_AUTOMAKE(\([^()]*\))/\1/p' | sed -e 's/^\[\(.*\)\]$/\1/' | sed -e 's/\(.*\) .*/\1/' | sed -e 1q`

REQUIRED_LIBTOOL_VERSION=`cat $configure_ac | grep '^LT_PREREQ' |
sed -n -e 's/LT_PREREQ(\([^()]*\))/\1/p' | sed -e 's/^\[\(.*\)\]$/\1/' | sed -e 1q`

printbold "Checking autotools requirements:"
echo

version_check autoconf AUTOCONF 'autoconf autoconf2.59 autoconf-2.53 autoconf2.50' $REQUIRED_AUTOCONF_VERSION || exit 1
AUTOHEADER=`echo $AUTOCONF | sed s/autoconf/autoheader/`

version_check automake AUTOMAKE "automake automake-1.11 automake-1.10" $REQUIRED_AUTOMAKE_VERSION || exit 1
ACLOCAL=`echo $AUTOMAKE | sed s/automake/aclocal/`

version_check libtool LIBTOOLIZE "libtoolize glibtoolize" $REQUIRED_LIBTOOL_VERSION || exit 1

##########################################
# Copy config.rpath to build dir
##########################################
build_dir=`cat $configure_ac | grep '^AC_CONFIG_AUX_DIR' |
sed -n -e 's/AC_CONFIG_AUX_DIR(\([^()]*\))/\1/p' | sed -e 's/^\[\(.*\)\]$/\1/' | sed -e 1q`

if [ -n "$build_dir" ]; then
  mkdir $build_dir
fi
config_rpath=m4/config.rpath
echo "Copying $config_rpath to $build_dir"
cp $config_rpath "$build_dir/"

##########################################

echo
printbold "Processing $configure_ac"
echo

if grep "^A[CM]_PROG_LIBTOOL" $configure_ac >/dev/null ||
    grep "^LT_INIT" $configure_ac >/dev/null; then
  printbold "Running $LIBTOOLIZE..."
  $LIBTOOLIZE --force --copy || exit 1
fi

m4dir=`cat $configure_ac | grep '^AC_CONFIG_MACRO_DIR' |
sed -n -e 's/AC_CONFIG_MACRO_DIR(\([^()]*\))/\1/p' | sed -e 's/^\[\(.*\)\]$/\1/' | sed -e 1q`
if [ -n "$m4dir" ]; then
  m4dir="-I $m4dir"
fi
printbold "Running $ACLOCAL..."
$ACLOCAL $m4dir || exit 1

printbold "Running $AUTOCONF..."
$AUTOCONF || exit 1
if grep "^A[CM]_CONFIG_HEADER" $configure_ac >/dev/null; then
  printbold "Running $AUTOHEADER..."
	$AUTOHEADER || exit 1
  # this prevents automake from thinking config.h.in is out of
  # date, since autoheader doesn't touch the file if it doesn't
  # change.
  test -f config.h.in && touch config.h.in
fi

printbold "Running $AUTOMAKE..."
$AUTOMAKE --gnu --add-missing --force --copy || exit 1

echo
printbold "Bootstrap complete, now run \`configure'."
