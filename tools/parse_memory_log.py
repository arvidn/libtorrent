#!/usr/bin/env python
import os
import sys

# usage: memory.log memory_index.log

lines = open(sys.argv[1], 'rb').readlines()
index = open(sys.argv[2], 'rb').readlines()

# logfile format:
# #<allocation-point> <time(ms)> <key ('A' | 'F')> <address> <size> <total-size> <total-space-time> <peak-total-size>
# example:
# #12 38 A 0xd902a0 16 16 0 16

allocation_points_to_print = 30


def print_allocation_point(ap):
    print('space_time: %d kBms' % (ap['spacetime'] / 1024))
    print('allocations: %d' % ap['allocations'])
    print('peak: %d kB' % (ap['peak'] / 1024))
    print('stack: ')
    counter = 0
    for e in ap['stack']:
        print('#%d %s' % (counter, e))
        counter += 1


allocation_points = []
for line in index:
    line = line.split('#')
    line.pop(0)
    ap = {'allocations': 0, 'peak': 0, 'spacetime': 0, 'allocation_point': len(allocation_points), 'stack': line}
    allocation_points.append(ap)

for line in lines:
    line = line.lstrip('#').rstrip('\n').split(' ')
    if len(line) != 8:
        print(line)
        continue
    try:
        ap = int(line[0])
        allocation_points[ap]['allocations'] += 1
        allocation_points[ap]['peak'] = int(line[7])
        allocation_points[ap]['spacetime'] = int(line[6])
    except Exception as e:
        print(type(e), e, line)

print('=== space time ===')

hot_ap = []
allocation_points.sort(key=lambda x: x['spacetime'], reverse=True)
counter = 0
for ap in allocation_points[0:allocation_points_to_print]:
    print('== %d ==' % counter)
    counter += 1
    print_allocation_point(ap)
    hot_ap.append(ap['allocation_point'])

print('=== allocations ===')

allocation_points.sort(key=lambda x: x['allocations'], reverse=True)
for ap in allocation_points[0:allocation_points_to_print]:
    print_allocation_point(ap)

print('=== peak ===')

allocation_points.sort(key=lambda x: x['peak'], reverse=True)
for ap in allocation_points[0:allocation_points_to_print]:
    print_allocation_point(ap)

# generate graph
lines = open(sys.argv[1], 'rb').readlines()

out = open('memory.dat', 'wb')
cur_line = [0] * allocation_points_to_print
prev_line = [0] * allocation_points_to_print
last_time = 0

for line in lines:
    line = line.lstrip('#').rstrip('\n').split(' ')
    if len(line) != 8:
        print(line)
        continue
    try:
        time = int(line[1])
        if time != last_time:
            print(last_time, '\t', end=' ', file=out)
            for i in range(allocation_points_to_print):
                if cur_line[i] == -1:
                    print(prev_line[i], '\t', end=' ', file=out)
                else:
                    print(cur_line[i], '\t', end=' ', file=out)
                    prev_line[i] = cur_line[i]
            print(file=out)
            cur_line = [-1] * allocation_points_to_print
            last_time = time

        size = int(line[5])
        ap = int(line[0])
        if ap in hot_ap:
            index = hot_ap.index(ap)
            cur_line[index] = max(cur_line[index], size)

    except Exception as e:
        print(type(e), e, line)

out.close()

out = open('memory.gnuplot', 'wb')
print("set term png size 1200,700", file=out)
print('set output "memory.png"', file=out)
print('set xrange [0:*]', file=out)
print('set xlabel "time (ms)"', file=out)
print('set ylabel "bytes (B)"', file=out)
print("set style data lines", file=out)
print("set key box", file=out)
print('plot', end=' ', file=out)
for k in range(allocation_points_to_print):
    print(' "memory.dat" using 1:(', end=' ', file=out)
    for i in range(k, allocation_points_to_print):
        if i == k:
            print('$%d' % (i + 2), end=' ', file=out)
        else:
            print('+$%d' % (i + 2), end=' ', file=out)
    print(') title "%d" with filledcurves x1, \\' % k, file=out)
print('x=0', file=out)
out.close()

os.system('gnuplot memory.gnuplot')
