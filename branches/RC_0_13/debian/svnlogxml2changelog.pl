#! /usr/bin/perl -w

# $Id$

use strict;

foreach(join('',<>) =~ m~<msg>(.+?)</msg>~sg)
{
	s/^\s*$//mg; # remove empty lines
	s/\n$//s; # remove the last LF
	s/\n/\n    /g; # handle multiline comments
	print "  * ";
	print;
	print "\n";
}
