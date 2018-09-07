#!/usr/bin/env python
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import glob
import datetime

this_year = datetime.date.today().year
print('current year: %d' % this_year)


def update_file(name):
    subst = ''
    f = open(name)
    for line in f:
        if 'Copyright (c) ' in line and 'Arvid Norberg' in line:
            year_idx = line.index('Copyright (c) ')
            first_year = int(line[year_idx + 14: year_idx + 18])
            if first_year != this_year:
                if line[year_idx + 18] == '-':
                    line = line[:year_idx + 19] + str(this_year) + line[year_idx + 23:]
                else:
                    line = line[:year_idx + 18] + '-' + str(this_year) + line[year_idx + 18:]

        subst += line

    f.close()
    open(name, 'w+').write(subst)


for i in glob.glob('src/*.cpp') + \
        glob.glob('include/libtorrent/*.hpp') + \
        glob.glob('include/libtorrent/extensions/*.hpp') + \
        glob.glob('include/libtorrent/kademlia/*.hpp') + \
        glob.glob('src/kademlia/*.cpp') + \
        ['COPYING', 'LICENSE', 'AUTHORS']:
    update_file(i)
