#!/usr/bin/env python3


import os
import sys

import libtorrent

if len(sys.argv) < 3:
    print("usage make_torrent.py file tracker-url")
    sys.exit(1)

input = os.path.abspath(sys.argv[1])

fs = []

parent_input = os.path.split(input)[0]

# if we have a single file, use it because os.walk does not work on a single files
if os.path.isfile(input):
    size = os.path.getsize(input)
    fs.append(libtorrent.create_file_entry(input, size))

for root, dirs, files in os.walk(input):
    # skip directories starting with .
    if os.path.split(root)[1][0] == ".":
        continue

    for f in files:
        # skip files starting with .
        if f[0] == ".":
            continue

        # skip thumbs.db on windows
        if f == "Thumbs.db":
            continue

        fname = os.path.join(root[len(parent_input) + 1 :], f)
        size = os.path.getsize(os.path.join(parent_input, fname))
        print("%10d kiB  %s" % (size / 1024, fname))
        fs.append(libtorrent.create_file_entry(fname, size))

if len(fs) == 0:
    print("no files added")
    sys.exit(1)

t = libtorrent.create_torrent(fs, 0, 4 * 1024 * 1024)

t.add_tracker(sys.argv[2])
t.set_creator("libtorrent %s" % libtorrent.__version__)

libtorrent.set_piece_hashes(t, parent_input, lambda x: sys.stdout.write("."))
sys.stdout.write("\n")

torrent_fp = open("out.torrent", "wb+")
torrent_fp.write(libtorrent.bencode(t.generate()))
torrent_fp.close()
