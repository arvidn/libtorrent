#! /bin/sh

# $Id$

rev=$1
[ -z "$rev" ] && { echo "USAGE: $0 <from_revision>"; exit 1; }
newrev=$2
[ -z "$newrev" ] && newrev=head

exec svn log -r $newrev:$rev --xml ..
