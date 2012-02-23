#!/bin/python

import sys
import os
import libtorrent

if len(sys.argv) < 3:
	print 'usage make_torrent.py file tracker-url'
	sys.exit(1)

input = os.path.abspath(sys.argv[1])

fs = libtorrent.file_storage()
libtorrent.add_files(fs, input)
if fs.num_files() == 0:
	print 'no files added'
	sys.exit(1)

t = libtorrent.create_torrent(fs, 0, 4 * 1024 * 1024)

t.add_tracker(sys.argv[2])
t.set_creator('libtorrent %s' % libtorrent.version)

libtorrent.set_piece_hashes(t, os.path.split(input)[0], lambda x: sys.stderr.write('.'))
sys.stderr.write('\n')

print libtorrent.bencode(t.generate())

