#!/bin/python

import sys
import os
import libtorrent

if len(sys.argv) < 3:
	print 'usage make_torrent.py file tracker-url'
	sys.exit(1)

input = os.path.abspath(sys.argv[1])

fs = libtorrent.file_storage()

#def predicate(f):
#	print f
#	return True
#libtorrent.add_files(fs, input, predicate)

parent_input = os.path.split(input)[0]

for root, dirs, files in os.walk(input):
	# skip directories starting with .
	if os.path.split(root)[1][0] == '.': continue

	for f in files:
		# skip files starting with .
		if f[0] == '.': continue

		# skip thumbs.db on windows
		if f == 'Thumbs.db': continue

		fname = os.path.join(root[len(parent_input)+1:], f)
		size = os.path.getsize(os.path.join(parent_input, fname))
		print '%10d kiB  %s' % (size / 1024, fname)
		fs.add_file(fname, size);

if fs.num_files() == 0:
	print 'no files added'
	sys.exit(1)

t = libtorrent.create_torrent(fs, 0, 4 * 1024 * 1024)

t.add_tracker(sys.argv[2])
t.set_creator('libtorrent %s' % libtorrent.version)

libtorrent.set_piece_hashes(t, parent_input, lambda x: sys.stderr.write('.'))
sys.stderr.write('\n')

f = open('out.torrent', 'wb+')
print >>f, libtorrent.bencode(t.generate())
f.close()

