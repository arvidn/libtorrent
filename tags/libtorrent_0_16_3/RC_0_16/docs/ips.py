#/bin/python

import os
import sys

def num_ids(bits, total_bits):

	if total_bits == 32:
		bit_dec = 2
	else:
		bit_dec = 1

	num_used = 7;
	ret = 3

	while bits > 0:
		ret += min(num_used, bits)
		num_used -= bit_dec
		if num_used < 0: num_used = 0
		bits -= 8
	
	return 1 << ret

f = open('ip_id_v4.dat', 'w+')
for i in range(0, 33):
	print >>f, '%d\t%d\t%d' % (i, num_ids(i, 32), 1 << i)
f.close()

f = open('ip_id_v6.dat', 'w+')
for i in range(0, 65):
	print >>f, '%d\t%d\t%d' % (i, num_ids(i, 64), 1 << i)
f.close()

f = open('ip_id.gnuplot', 'w+')

f.write('''
set term png size 600,300
set output "ip_id_v4.png"
set logscale y
set title "Number of possible node IDs"
set ylabel "possible node IDs"
set xlabel "bits controlled in IPv4"
set xtics 4
set grid
plot "ip_id_v4.dat" using 1:2 title "octet-wise modulus" with lines, \
	"ip_id_v4.dat" using 1:3 title "hash of IP" with lines

set output "ip_id_v6.png"
set title "Number of possible node IDs"
set xlabel "bits controlled in IPv6"
plot "ip_id_v6.dat" using 1:2 title "octet-wise modulus" with lines, \
	"ip_id_v6.dat" using 1:3 title "hash of IP" with lines
''')
f.close()
os.system('gnuplot ip_id.gnuplot')

