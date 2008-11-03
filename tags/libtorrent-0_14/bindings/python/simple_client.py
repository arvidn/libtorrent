#!/bin/python
# Copyright Arvid Norberg 2008. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)


import libtorrent as lt
import time

ses = lt.session()
ses.listen_on(6881, 6891)

e = lt.bdecode(open("test.torrent", 'rb').read())
info = lt.torrent_info(e)

atp = {}
atp["ti"] = info
atp["save_path"] = "./"
atp["storage_mode"] = lt.storage_mode_t(1)
atp["paused"] = False
atp["auto_managed"] = True
atp["duplicate_is_error"] = True

h = ses.add_torrent(atp)

while (not h.is_seed()):
	s = h.status()

	state_str = ['queued', 'checking', 'downloading metadata', \
		'downloading', 'finished', 'seeding', 'allocating']
	print '\r%.2f%% complete (down: %.1f kb/s up: %.1f kB/s peers: %d) %s' % \
		(s.progress * 100, s.download_rate / 1000, s.upload_rate / 1000, \
		s.num_peers, state_str[s.state]),

	time.sleep(1)

