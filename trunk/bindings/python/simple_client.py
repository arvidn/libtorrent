#!/usr/bin/python

import libtorrent as lt
import time

ses = lt.session()
ses.listen_on(6881, 6891)

e = lt.bdecode(open("test.torrent", 'rb').read())
info = lt.torrent_info(e)

h = ses.add_torrent(info, "./", compact_mode = True)

while (not h.is_seed()):
	s = h.status()

	state_str = ['queued', 'checking', 'connecting', 'downloading metadata', \
		'downloading', 'finished', 'seeding', 'allocating']
	print '%.2f%% complete (down: %.1f kb/s up: %.1f kB/s peers: %d) %s' % \
		(s.progress * 100, s.download_rate / 1000, s.upload_rate / 1000, \
		s.num_peers, state_str[s.state])

	time.sleep(1)

