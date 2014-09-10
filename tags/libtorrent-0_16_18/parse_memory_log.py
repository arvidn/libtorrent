#! /usr/bin/env python
import os, sys, time

# usage: memory.log memory_index.log

lines = open(sys.argv[1], 'rb').readlines()
index = open(sys.argv[2], 'rb').readlines()

# logfile format:
# #<allocation-point> <time(ms)> <key ('A' | 'F')> <address> <size> <total-size> <total-space-time> <peak-total-size>
# example:
# #12 38 A 0xd902a0 16 16 0 16

allocation_points_to_print = 30

def print_allocation_point(ap):
   print 'space_time: %d kBms' % (ap['spacetime'] / 1024)
   print 'allocations: %d' % ap['allocations']
   print 'peak: %d kB' % (ap['peak'] / 1024)
   print 'stack: '
   counter = 0
   for e in ap['stack']:
      print '#%d %s' % (counter, e)
      counter += 1

allocation_points = []
for l in index:
   l = l.split('#')
   l.pop(0)
   ap = { 'allocations': 0, 'peak': 0, 'spacetime': 0, 'allocation_point': len(allocation_points), 'stack': l}
   allocation_points.append(ap);

for l in lines:
   l = l.lstrip('#').rstrip('\n').split(' ')
   if len(l) != 8:
      print l
      continue
   try:
      ap = int(l[0])
      allocation_points[ap]['allocations'] += 1
      allocation_points[ap]['peak'] = int(l[7])
      allocation_points[ap]['spacetime'] = int(l[6])
   except Exception, e:
      print type(e), e, l

print '=== space time ==='

hot_ap = []
allocation_points.sort(key = lambda x:x['spacetime'], reverse=True);
counter = 0
for ap in allocation_points[0:allocation_points_to_print]:
   print '== %d ==' % counter
   counter += 1
   print_allocation_point(ap)
   hot_ap.append(ap['allocation_point']);

print '=== allocations ==='

allocation_points.sort(key = lambda x:x['allocations'], reverse=True);
for ap in allocation_points[0:allocation_points_to_print]:
   print_allocation_point(ap)

print '=== peak ==='

allocation_points.sort(key = lambda x:x['peak'], reverse=True);
for ap in allocation_points[0:allocation_points_to_print]:
   print_allocation_point(ap)

# generate graph
lines = open(sys.argv[1], 'rb').readlines()

out = open('memory.dat', 'wb')
cur_line = [0] * allocation_points_to_print
prev_line = [0] * allocation_points_to_print
last_time = 0

for l in lines:
   l = l.lstrip('#').rstrip('\n').split(' ')
   if len(l) != 8:
      print l
      continue
   try:
      time = int(l[1])
      if time != last_time:
         print >>out, last_time, '\t',
         for i in range(allocation_points_to_print):
	    if cur_line[i] == -1:
	       print >>out, prev_line[i], '\t',
	    else:
	       print >>out, cur_line[i], '\t',
	       prev_line[i] = cur_line[i]
         print >>out
	 cur_line = [-1] * allocation_points_to_print
         last_time = time
 
      size = int(l[5])
      ap = int(l[0])
      if ap in hot_ap:
         index = hot_ap.index(ap)
         cur_line[index] = max(cur_line[index], size)

   except Exception, e:
      print type(e), e, l

out.close()

out = open('memory.gnuplot', 'wb')
print >>out, "set term png size 1200,700"
print >>out, 'set output "memory.png"'
print >>out, 'set xrange [0:*]'
print >>out, 'set xlabel "time (ms)"'
print >>out, 'set ylabel "bytes (B)"'
print >>out, "set style data lines"
print >>out, "set key box"
print >>out, 'plot',
for k in range(allocation_points_to_print):
   print >>out, ' "memory.dat" using 1:(',
   for i in range(k, allocation_points_to_print):
      if i == k: print >>out, '$%d' % (i + 2),
      else: print >>out, '+$%d' % (i + 2),
   print >>out, ') title "%d" with filledcurves x1, \\' % k
print >>out, 'x=0'
out.close()

os.system('gnuplot memory.gnuplot');

