#!/usr/bin/env python
# Copyright Arvid Norberg 2008. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
from __future__ import print_function

import libtorrent as lt
import time
import sys

ses = lt.session()
ses.listen_on(6881, 6891)

info = lt.torrent_info(sys.argv[1])
h = ses.add_torrent({'ti': info, 'save_path': './'})
print('starting', h.name())

while (not h.is_seed()):
	s = h.status()

	state_str = ['queued', 'checking', 'downloading metadata', \
		'downloading', 'finished', 'seeding', 'allocating', 'checking fastresume']
	print('\r%.2f%% complete (down: %.1f kb/s up: %.1f kB/s peers: %d) %s' % \
		(s.progress * 100, s.download_rate / 1000, s.upload_rate / 1000, \
		s.num_peers, state_str[s.state]), end=' ')
	sys.stdout.flush()

	time.sleep(1)

print(h.name(), 'complete')
