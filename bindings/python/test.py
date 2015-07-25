#!/usr/bin/env python

import libtorrent as lt

# test torrent_info


info = lt.torrent_info({ 'info': {'name': 'test_torrent', 'length': 1234,
	'piece length': 16 * 1024,
	'pieces': 'aaaaaaaaaaaaaaaaaaaa'}})



