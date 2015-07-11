#!/usr/bin/env python

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
		if 'Copyright (c) ' in l and 'Arvid Norberg' in l:
			year_idx = l.index('Copyright (c) ')
			first_year = int(l[year_idx + 14: year_idx + 18])
			if first_year != this_year:
				if l[year_idx + 18] == '-':
					l = l[:year_idx + 19] + str(this_year) + l[year_idx + 23:]
				else:
					l = l[:year_idx + 18] + '-' + str(this_year) + l[year_idx + 18:]

		subst += l

	f.close()
	open(name, 'w+').write(subst)

for i in glob.glob('src/*.cpp') + \
	glob.glob('include/libtorrent/*.hpp') + \
	glob.glob('include/libtorrent/extensions/*.hpp') + \
	glob.glob('include/libtorrent/kademlia/*.hpp') + \
	glob.glob('src/kademlia/*.cpp') + \
	['COPYING', 'LICENSE', 'AUTHORS']:
	update_file(i)

