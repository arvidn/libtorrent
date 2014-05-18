import urllib2
import sys

url = sys.argv[1]

r = urllib2.Request(url)
resp = urllib2.urlopen(r)
data = resp.read(8192)
f = open('single_request', 'wb+')
while len(data) > 0:
	f.write(data)
	data = resp.read(8192)
full_size = int(resp.info()['Content-Length'])
f.close()

print 'file size: %d' % full_size

f = open('multi_request', 'wb+')
rng = 1
start = 0
while full_size > 0:
	r = urllib2.Request(url)
	rng = min(rng, full_size)
	r.add_header('Range', 'bytes=%d-%d' % (start, start + rng - 1))
	print '%d-%d' % (start, start + rng - 1)
	full_size -= rng
	start += rng
	resp = urllib2.urlopen(r)
	data = resp.read(8192)

	request_size = 0
	while len(data) > 0:
		request_size += len(data)
		f.write(data)
		data = resp.read(8192)

	print 'received %d bytes' % request_size
	if request_size != rng:
		print 'received %d bytes, expected %d' % (request_size, rng)
		print 'http status: %d' % (resp.getcode())
		print resp.info()
		break

	rng *= 2

f.close()

