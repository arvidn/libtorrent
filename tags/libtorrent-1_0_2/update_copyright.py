#! /usr/bin/env python
import os
import sys
import glob
import datetime

this_year = datetime.date.today().year
print 'current year: %d' % this_year

def update_file(name):
	subst = ''
	f = open(name)
	for l in f:
		if l.startswith('Copyright (c) ') and 'Arvid Norberg' in l:
			first_year = int(l[14:18])
			if first_year != this_year:
				if l[18] == '-':
					l = l[:19] + str(this_year) + l[23:]
				else:
					l = l[:18] + '-' + str(this_year) + l[18:]

		subst += l

	f.close()
	open(name, 'w+').write(subst)

for i in glob.glob('src/*.cpp') + glob.glob('include/libtorrent/*.hpp') + \
	glob.glob('include/libtorrent/kademlia/*.hpp') + glob.glob('src/kademlia/*.cpp') + \
	['COPYING', 'LICENSE']:
	update_file(i)

