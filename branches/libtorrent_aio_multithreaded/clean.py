import os

to_delete = [
	'session_stats',
	'libtorrent_logs*',
	'round_trip_ms.log',
	'dht.log',
	'upnp.log',
	'natpmp.log',
	'bin',
	'test_tmp_*'
]

directories = [
	'examples',
	'test',
	'.',
	'tools'
]

for d in directories:
	for f in to_delete:
		path = os.path.join(d, f)
		print path
		os.system('rm -rf %s' % path)

