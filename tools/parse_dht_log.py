#! /usr/bin/env python
import sys
import os
import time
import calendar
import pprint

pp = pprint.PrettyPrinter(indent=4)

up_time_quanta = 500

f = open(sys.argv[1])

announce_histogram = {}

#TODO: make this histogram into a CDF

node_uptime_histogram = {}

counter = 0;

# maps search_id to a list of events. Each event is a dict containing:
#  t: timestamp
#  d: distance (from target)
#  o: outstanding searches
#  e: event (NEW, COMPLETED, ADD, INVOKE, TIMEOUT)
#  i: node-id
#  a: IP address and port
#  s: source node-id (only for ADD events)
outstanding_searches = {}

# list of completed searches
searches = []

def convert_timestamp(t):
	parts = t.split('.')
	hms = parts[0].split(':')
	return (int(hms[0]) * 3600 + int(hms[1]) * 60 + int(hms[2])) * 1000 + int(parts[1])

last_incoming = ''

our_node_id = ''

unique_ips = set()
client_version_histogram = {}
client_histogram = {}

for line in f:
	counter += 1
#	if counter % 1000 == 0:
#		print '\r%d' % counter,
	try:
		l = line.split(' ')
		if 'starting DHT tracker with node id:' in line:
			our_node_id = l[l.index('id:') + 1].strip()

		try:
			if len(l) > 4 and l[2] == '<==' and l[1] == '[dht_tracker]':
				ip = l[3].split(':')[0]
				if ip not in unique_ips:
					unique_ips.add(ip)
					json_blob = line.split(l[3])[1]
					version = json_blob.split("'v': '")[1].split("'")[0]
					if len(version) == 4:
						v = '%s-%d' % (version[0:2], (ord(version[2]) << 8) + ord(version[3]))
					elif len(version) == 8:
						v = '%c%c-%d' % (chr(int(version[0:2], 16)), chr(int(version[2:4], 16)), int(version[4:8], 16))
					else:
						v = 'unknown'

					if not v in client_version_histogram:
						client_version_histogram[v] = 1
					else:
						client_version_histogram[v] += 1

					if not v[0:2] in client_histogram:
						client_histogram[v[0:2]] = 1
					else:
						client_histogram[v[0:2]] += 1
		except: pass

		if 'announce-distance:' in line:
			idx = l.index('announce-distance:')

			d = int(l[idx+1].strip())
			if not d in announce_histogram: announce_histogram[d] = 0
			announce_histogram[d] += 1
		if 'NODE FAILED' in line:
			idx = l.index('fails:')
			if int(l[idx+1].strip()) != 1: continue;
			idx = l.index('up-time:')
			d = int(l[idx+1].strip())
			# quantize
			d = d - (d % up_time_quanta)
			if not d in node_uptime_histogram: node_uptime_histogram[d] = 0
			node_uptime_histogram[d] += 1

		search_id = l[2]
		ts = l[0]
		event = l[3]

		if event == 'RESPONSE':
			outstanding = int(l[l.index('invoke-count:')+1])
			nid = l[l.index('id:')+1]
			addr = l[l.index('addr:')+1]
			last_response = addr
			outstanding_searches[search_id].append({ 't': ts, 'd': distance,
				'o': outstanding + 1, 'a':addr, 'e': event,'i':nid, 's':source})
		elif event == 'NEW':
			nid = l[l.index('target:')+1]
			outstanding_searches[search_id] = [{ 't': ts, 'd': 0, 'o': 0, \
				'e': event, 'abstime': ts, 'i': nid}]
			last_response = ''
		elif event == 'INVOKE' or event == 'ADD' or event == '1ST_TIMEOUT' or \
			event == 'TIMEOUT' or event == 'PEERS':
			if not search_id in outstanding_searches:
				print 'orphaned event: %s' % line
			else:
				outstanding = int(l[l.index('invoke-count:')+1])
				distance = int(l[l.index('distance:')+1])
				nid = l[l.index('id:')+1]
				addr = l[l.index('addr:')+1]
				source = ''
				if event == 'ADD':
					if last_response == '': continue
					source = last_response

				outstanding_searches[search_id].append({ 't': ts, 'd': distance,
					'o': outstanding + 1, 'a':addr, 'e': event,'i':nid, 's':source})
		elif event == 'ABORTED':
				outstanding_searches[search_id].append({ 't': ts, 'e': event})
		elif event == 'COMPLETED':
				distance = int(l[l.index('distance:')+1])
				lookup_type = l[l.index('type:')+1].strip()
				outstanding_searches[search_id].append({ 't': ts, 'd': distance,
					'o': 0, 'e': event,'i':''})

				outstanding_searches[search_id][0]['type'] = lookup_type

				s = outstanding_searches[search_id]

				try:
					start_time = convert_timestamp(s[0]['t'])
					for i in range(len(s)):
						s[i]['t'] = convert_timestamp(s[i]['t']) - start_time
				except:
					pass
				searches.append(s)
				del outstanding_searches[search_id]

	except Exception, e:
		print e
		print line.split(' ')

lookup_times_min = []
lookup_times_max = []

# these are the timestamps for lookups crossing distance
# to target boundaries
lookup_distance = []
for i in range(0, 15):
	lookup_distance.append([])

for s in searches:
	for i in s:
		if not 'last_dist' in i:
			i['last_dist'] = -1
		cur_dist = 160 - i['d']
		last_dist = i['last_dist']
		if cur_dist > last_dist:
			for j in range(last_dist + 1, cur_dist + 1):
				if j >= len(lookup_distance): break
				lookup_distance[j].append(i['t'])
			i['last_dist'] = cur_dist
		if i['e'] != 'PEERS': continue
		lookup_times_min.append(i['t'])
		break
	for i in reversed(s):
		if i['e'] != 'PEERS': continue
		lookup_times_max.append(i['t'])
		break


lookup_times_min.sort()
lookup_times_max.sort()
out = open('dht_lookup_times_cdf.txt', 'w+')
counter = 0
for i in range(len(lookup_times_min)):
	counter += 1
	print >>out, '%d\t%d\t%f' % (lookup_times_min[i], lookup_times_max[i], counter / float(len(lookup_times_min)))
out.close()

for i in lookup_distance:
	i.sort()

dist = 0
for i in lookup_distance:
	out = open('dht_lookup_distance_%d.txt' % dist, 'w+')
	dist += 1
	counter = 0
	for j in i:
		counter += 1
		print >>out, '%d\t%f' % (j, counter / float(len(i)))
	out.close()

out = open('dht_lookups.txt', 'w+')
for s in searches:
	for i in s:
		if i['e'] == 'INVOKE':
			print >>out, ' ->', i['t'], 160 - i['d'], i['i'], i['a']
		elif i['e'] == '1ST_TIMEOUT':
			print >>out, ' x ', i['t'], 160 - i['d'], i['i'], i['a']
		elif i['e'] == 'TIMEOUT':
			print >>out, ' X ', i['t'], 160 - i['d'], i['i'], i['a']
		elif i['e'] == 'ADD':
			print >>out, ' + ', i['t'], 160 - i['d'], i['i'], i['a'], i['s']
		elif i['e'] == 'RESPONSE':
			print >>out, ' <-', i['t'], 160 - i['d'], i['i'], i['a']
		elif i['e'] == 'PEERS':
			print >>out, ' <-', i['t'], 160 - i['d'], i['i'], i['a']
		elif i['e'] == 'ABORTED':
			print >>out, 'abort'
		elif i['e'] == 'COMPLETED':
			print >>out, '***', i['t'], 160 - i['d'], '\n'
		elif i['e'] == 'NEW':
			print >>out, '===', i['abstime'], i['type'], '==='
			print >>out, '<> ', 0, our_node_id, i['i']
out.close()

out = open('dht_announce_distribution.dat', 'w+')
print 'announce distribution items: %d' % len(announce_histogram)
for k,v in announce_histogram.items():
	print >>out, '%d %d' % (k, v)
	print '%d %d' % (k, v)
out.close()

out = open('dht_node_uptime_cdf.txt', 'w+')
s = 0

total_uptime_nodes = 0
for k,v in node_uptime_histogram.items():
	total_uptime_nodes += v

for k,v in sorted(node_uptime_histogram.items()):
	s += v
	print >>out, '%f %f' % (k / float(60), s / float(total_uptime_nodes))
	print '%f %f' % (k / float(60), s / float(total_uptime_nodes))
out.close()


print 'clients by version'
client_version_histogram = sorted(client_version_histogram.items(), key=lambda x: x[1], reverse=True)
pp.pprint(client_version_histogram)

print 'clients'
client_histogram = sorted(client_histogram.items(), key=lambda x: x[1], reverse=True)
pp.pprint(client_histogram)

out = open('dht.gnuplot', 'w+')
out.write('''
set term png size 1200,700 small
set output "dht_lookup_times_cdf.png"
set title "portion of lookups that have received at least one data response"
set ylabel "portion of lookups"
set xlabel "time from start of lookup (ms)"
set grid
plot "dht_lookup_times_cdf.txt" using 1:3 with lines title "time to first result", \
	"dht_lookup_times_cdf.txt" using 2:3 with lines title "time to last result"

set terminal postscript
set output "dht_lookup_times_cdf.ps"
replot

set term png size 1200,700 small
set xtics 100
set xrange [0:2000]
set output "dht_min_lookup_times_cdf.png"
plot "dht_lookup_times_cdf.txt" using 1:3 with lines title "time to first result"

set terminal postscript
set output "dht_min_lookup_times_cdf.ps"
replot

set term png size 1200,700 small
set output "dht_node_uptime_cdf.png"
set xrange [0:*]
set title "node up time"
set ylabel "portion of nodes being offline"
set xlabel "time from first seeing the node (minutes)"
set xtics 10
unset grid
plot  "dht_node_uptime_cdf.txt" using 1:2 title "nodes" with lines

set term png size 1200,700 small
set output "dht_announce_distribution.png"
set xrange [0:30]
set xtics 1
set title "bucket # announces are made against relative to target node-id"
set ylabel "# of announces"
set boxwidth 1
set xlabel "bit prefix of nodes in announces"
set style fill solid border -1 pattern 2
plot  "dht_announce_distribution.dat" using 1:2 title "announces" with boxes

set terminal postscript
set output "dht_announce_distribution.ps"
replot

set term png size 1200,700 small
set output "dht_lookup_distance_cdf.png"
set title "portion of lookups that have reached a certain distance in their lookups"
set ylabel "portion of lookups"
set xlabel "time from start of lookup (ms)"
set xrange [0:2000]
set xtics 100
set grid
plot ''')

dist = 0
for i in lookup_distance:
	if dist > 0: out.write(', ')
	out.write('"dht_lookup_distance_%d.txt" using 1:2 title "%d" with lines' % (dist, dist))
	dist += 1

out.close()

os.system('gnuplot dht.gnuplot');


