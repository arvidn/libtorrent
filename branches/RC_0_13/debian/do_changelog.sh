#! /bin/sh

# $Id$

oldrev=$1
newrev=$2

[ -z "$newrev" -o -z "$oldrev" ] && { echo "USAGE: $0 <oldrev> <newrev>"; exit 1; }

set -e
set -x

CHLOG=changelog.new
echo "libtorrent-rasterbar-0.13 (${newrev}svn) unstable; urgency=low" >$CHLOG
echo >>$CHLOG
./svnlogxml.sh $oldrev $newrev | ./svnlogxml2changelog.pl >>$CHLOG
echo >>$CHLOG
echo -n " -- Roman Rybalko <libtorrent@romanr.info>  " >>$CHLOG
date -R >>$CHLOG
echo >>$CHLOG
cat changelog >>$CHLOG

mv -f $CHLOG changelog
