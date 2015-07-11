#!/usr/bin/env python
import sys
import os

gnuplot_scripts = []

def gen_stats_gnuplot(name, y, lines):

	global gnuplot_scripts

	stat = open(sys.argv[1])
	line = stat.readline()
	while not 'minute:' in line:
		line = stat.readline()

	names = line.strip().split(':')
	counter = 1
	for i in names:
		print '%d: %s' % (counter, i)
		counter += 1

	out = open('%s.gnuplot' % name, 'w+')
	out.write('''
set term png size 1200,700 small
set output "%s.png"
set title "%s"
set ylabel "%s"
set xlabel "time (minutes)"
plot ''' % (name, name.strip('_'), y))
	first = True
	for i in lines:
		if not first:
			out.write(', \\\n')
		first = False
		out.write('"%s" using 1:%d title "%s" with lines' % (sys.argv[1], names.index(i)+1, i))
	out.write('\n')

	out.write('''set terminal postscript
set output "%s.ps"
replot
''' % (name))
	out.close()
	gnuplot_scripts += [name]

gen_stats_gnuplot('dht_routing_table_size', 'nodes', ['active nodes','passive nodes', 'confirmed nodes'])
gen_stats_gnuplot('dht_tracker_table_size', '', ['num torrents', 'num peers'])
gen_stats_gnuplot('dht_announces', 'messages per minute', ['announces per min', 'failed announces per min'])
gen_stats_gnuplot('dht_clients', 'messages per minute', ['total msgs per min', 'az msgs per min', 'ut msgs per min', 'lt msgs per min', 'mp msgs per min', 'gr msgs per min'])
gen_stats_gnuplot('dht_rate', 'bytes per second',  ['bytes in per sec', 'bytes out per sec'])
gen_stats_gnuplot('dht_errors', 'messages per minute',  ['error replies sent', 'error queries recvd'])

for i in gnuplot_scripts:
	os.system('gnuplot %s.gnuplot' % i);
