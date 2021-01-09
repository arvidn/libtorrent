#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
import time
import shutil
import subprocess
import sys

toolset = ''
if len(sys.argv) > 1:
    toolset = sys.argv[1]

ret = os.system('cd ../examples && b2 profile %s stage_client_test'
                % toolset)
if ret != 0:
    print('ERROR: build failed: %d' % ret)
    sys.exit(1)

ret = os.system('cd ../examples && b2 release %s stage_connection_tester'
                % toolset)
if ret != 0:
    print('ERROR: build failed: %d' % ret)
    sys.exit(1)

try:
    os.remove('.ses_state')
except Exception:
    pass
try:
    shutil.rmtree('.resume')
except Exception:
    pass
try:
    shutil.rmtree('cpu_benchmark')
except Exception:
    pass

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
    client_cmd = '../examples/client_test -k --listen_interfaces=127.0.0.1:%d cpu_benchmark.torrent ' \
        '--disable_hash_checks=1 --enable_dht=0 --enable_lsd=0 --enable_upnp=0 --enable_natpmp=0 ' \
        '-e 120 %s -O --allow_multiple_connections_per_ip=1 --connections_limit=%d -T %d ' \
        '-f %s/events.log --alert_mask=8747' \
        % (port, client_arg, num_peers * 2, num_peers * 2, output_dir)
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
    try:
        os.system('gprof2dot --strip <%s/gprof.out | dot -Tpng -o %s/cpu_profile.png' % (output_dir, output_dir))
    except Exception:
        print('please install gprof2dot and dot:\nsudo pip install gprof2dot\nsudo apt install graphviz')

    os.system('python3 parse_session_stats.py %s/events.log' % output_dir)

    try:
        shutil.move('session_stats_report', '%s/session_stats_report' % output_dir)
    except Exception:
        pass


run_test('download', 'upload', '', 50)
run_test('upload', 'download', '-G', 20)
