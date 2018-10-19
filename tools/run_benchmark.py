#!/usr/bin/env python
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
import time
import shutil
import subprocess
import sys

cache_size = 300  # in MiB

toolset = ''
if len(sys.argv) > 1:
    toolset = sys.argv[1]

ret = os.system('cd ../examples && bjam profile statistics=on %s stage_client_test' % toolset)
if ret != 0:
    print('ERROR: build failed: %d' % ret)
    sys.exit(1)

ret = os.system('cd ../examples && bjam release %s stage_connection_tester' % toolset)
if ret != 0:
    print('ERROR: build failed: %d' % ret)
    sys.exit(1)

try:
    os.remove('.ses_state')
except Exception as e:
    print(e)
try:
    shutil.rmtree('.resume')
except Exception as e:
    print(e)
try:
    shutil.rmtree('cpu_benchmark')
except Exception as e:
    print(e)

if not os.path.exists('cpu_benchmark.torrent'):
    ret = os.system('../examples/connection_tester gen-torrent -s 10000 -n 15 -t cpu_benchmark.torrent')
    if ret != 0:
        print('ERROR: connection_tester failed: %d' % ret)
        sys.exit(1)

try:
    shutil.rmtree('t')
except Exception:
    pass


def run_test(name, test_cmd, client_arg, num_peers):
    output_dir = 'logs_%s' % name

    try:
        shutil.rmtree(output_dir)
    except Exception:
        pass
    try:
        os.mkdir(output_dir)
    except Exception:
        pass

    port = (int(time.time()) % 50000) + 2000

    try:
        shutil.rmtree('session_stats')
    except Exception:
        pass
    try:
        shutil.rmtree('session_stats_report')
    except Exception:
        pass

    start = time.time()
    client_cmd = ('../examples/client_test -p %d cpu_benchmark.torrent -k -z -H -X -q 120 %s'
                  '-h -c %d -T %d -C %d -f %s/events.log').format(
                      port, client_arg, num_peers * 2, num_peers * 2, cache_size * 16, output_dir)
    test_cmd = '../examples/connection_tester %s -c %d -d 127.0.0.1 -p %d -t cpu_benchmark.torrent' % (
        test_cmd, num_peers, port)

    client_out = open('%s/client.out' % output_dir, 'w+')
    test_out = open('%s/test.out' % output_dir, 'w+')
    print(client_cmd)
    c = subprocess.Popen(client_cmd.split(' '), stdout=client_out, stderr=client_out, stdin=subprocess.PIPE)
    time.sleep(2)
    print(test_cmd)
    t = subprocess.Popen(test_cmd.split(' '), stdout=test_out, stderr=test_out)

    t.wait()

    end = time.time()

    try:
        c.communicate('q')
    except Exception:
        pass
    c.wait()

    client_out.close()
    test_out.close()

    print('runtime %d seconds' % (end - start))
    print('analyzing proile...')
    os.system('gprof ../examples/client_test >%s/gprof.out' % output_dir)
    print('generating profile graph...')
    os.system('python gprof2dot.py --strip <%s/gprof.out | dot -Tpng -o %s/cpu_profile.png' % (output_dir, output_dir))

    os.system('python parse_session_stats.py session_stats/*.log')
    try:
        shutil.move('session_stats_report', '%s/session_stats_report' % output_dir)
    except Exception:
        pass
    try:
        shutil.move('session_stats', '%s/session_stats' % output_dir)
    except Exception:
        pass


run_test('download', 'upload', '', 50)
run_test('upload', 'download', '-G', 20)
