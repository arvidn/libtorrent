#! /usr/bin/env python
# Copyright Arvid Norberg 2008. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

import os, sys, time

lines = open(sys.argv[1], 'rb').readlines()

#keys = ['send_buffer_utilization']
keys = ['send_buffer_size', 'used_send_buffer', 'protocol_buffer']
#keys = ['send_buffer_alloc', 'send_buffer', 'allocate_buffer_alloc', 'allocate_buffer', 'protocol_buffer']
#keys = ['send_buffer_alloc', 'send_buffer', 'allocate_buffer_alloc', 'allocate_buffer', 'protocol_buffer', 'append_send_buffer']

average = ['send_buffer_utilization', 'send_buffer_size', 'used_send_buffer']
average_interval = 120000
render = 'lines'

time_limit = -1
if len(sys.argv) > 2:
   time_limit = long(sys.argv[2])


# logfile format:
# <time(ms)> <key> <value>
# example:
# 16434 allocate_buffer: 17
for k in keys:

   last_sample = 0
   average_accumulator = 0
   average_samples = 0
   peak = 0

   out = open(k + '.dat', 'wb')
   eval_average = False
   if k in average:
      eval_average = True
      peak_out = open(k + '_peak.dat', 'wb')

   for l in lines:
      l = l.split(' ')
      if len(l) != 3:
         print l
         continue
      try:
         if l[1] == k + ':':
            if time_limit != -1 and long(l[0]) > time_limit: break
            time = l[0]
            value = l[2]
            if eval_average:
               while long(time) > last_sample + average_interval:
                  last_sample = last_sample + average_interval
                  if average_samples < 1: average_samples = 1
                  print >>out, '%d %f' % (last_sample, average_accumulator / average_samples)
                  print >>peak_out, '%d %f' % (last_sample, peak)
                  average_accumulator = 0
                  average_samples = 0
                  peak = 0
               average_accumulator = average_accumulator + float(value)
               average_samples = average_samples + 1
               if float(value) > peak: peak = float(value)
            else:
               print >>out, time + ' ' + value,
      except:
         print l

   out.close()
   peak_out.close()

out = open('send_buffer.gnuplot', 'wb')
print >>out, "set term png size 1200,700"
print >>out, 'set output "send_buffer.png"'
print >>out, 'set xrange [0:*]'
print >>out, 'set xlabel "time (ms)"'
print >>out, 'set ylabel "bytes (B)"'
print >>out, "set style data lines"
print >>out, "set key box"
print >>out, 'plot',
for k in keys:
   if k in average:
      print >>out, ' "%s.dat" using 1:2 title "%s %d seconds average" with %s,' % (k, k, average_interval / 1000., render),
      print >>out, ' "%s_peak.dat" using 1:2 title "%s %d seconds peak" with %s,' % (k, k, average_interval / 1000., render),
   else:
      print >>out, ' "%s.dat" using 1:2 title "%s" with %s,' % (k, k, render),
print >>out, 'x=0'
out.close()

os.system('gnuplot send_buffer.gnuplot')

