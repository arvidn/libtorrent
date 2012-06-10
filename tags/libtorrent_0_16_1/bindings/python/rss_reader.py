#!/usr/bin/python

import sys
import libtorrent as lt
import time

if len(sys.argv) != 2:
	print('usage: rss_reader.py rss-feed-url')
	sys.exit(1)

ses = lt.session()

h = ses.add_feed({'url': sys.argv[1], 'auto_download': False})
f = h.get_feed_status()
spinner = ['|', '/', '-', '\\']
i = 0
while f['updating']:
	time.sleep(0.1)
	i = (i + 1) % 4
	print('\b%s' % spinner[i]),
	sys.stdout.flush()
	f = h.get_feed_status()

print('\n\nFEED: %s' % f['url'])
if len(f['error']) > 0:
	print('ERROR: %s' % f['error'])

print('   %s\n   %s\n' % (f['title'], f['description']))
print('   ttl: %d minutes' % f['ttl'])

for item in f['items']:
	print('\n%s\n------------------------------------------------------' % item['title'])
	print('   url: %s\n   size: %d\n   uuid: %s\n   description: %s' % (item['url'], item['size'], item['uuid'], item['description']))
	print('   comment: %s\n   category: %s' % (item['comment'], item['category']))

