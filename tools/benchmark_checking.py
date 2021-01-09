#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
import shutil
import subprocess
import sys

toolset = ''
if len(sys.argv) > 1:
    toolset = sys.argv[1]

ret = os.system('cd ../examples && b2 release %s stage_client_test stage_connection_tester'
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

if not os.path.exists('checking_benchmark.torrent'):
    ret = os.system('../examples/connection_tester gen-torrent -s 10000 -n 15 -t checking_benchmark.torrent')
    if ret != 0:
        print('ERROR: connection_tester failed: %d' % ret)
        sys.exit(1)

if not os.path.exists('checking_benchmark'):
    ret = os.system('../examples/connection_tester gen-data -t checking_benchmark.torrent -p .')
    if ret != 0:
        print('ERROR: connection_tester failed: %d' % ret)
        sys.exit(1)


def run_test(name, client_arg):
    output_dir = 'logs_checking_%s' % name

    if os.path.exists(output_dir) and os.path.exists(os.path.join(output_dir, 'timing.txt')):
        return

    try:
        shutil.rmtree(output_dir)
    except Exception:
        pass
    try:
        os.mkdir(output_dir)
    except Exception:
        pass

    try:
        shutil.rmtree(".resume")
    except Exception:
        pass

    client_cmd = '../examples/client_test checking_benchmark.torrent ' \
                 '--enable_dht=0 --enable_lsd=0 --enable_upnp=0 --enable_natpmp=0 ' \
                 '-1 %s -s . -f %s/events.log --alert_mask=all' \
                 % (client_arg, output_dir)

    client_out = open('%s/client.out' % output_dir, 'w+')
    print(client_cmd)
    c = subprocess.Popen(client_cmd.split(' '), stdout=client_out, stderr=client_out, stdin=subprocess.PIPE)
    c.wait()

    client_out.close()

    start_time = 0
    end_time = 0
    for l in open('%s/events.log' % output_dir, 'r'):
        if 'checking_benchmark: start_checking, m_checking_piece: ' in l \
                and start_time == 0:
            start_time = int(l.split(' ')[0][1:-1])
        if 'checking_benchmark: on_piece_hashed, completed' in l \
                and start_time != 0:
            end_time = int(l.split(' ')[0][1:-1])

    print('%s: %d' % (name, end_time - start_time))
    with open('%s/timing.txt' % output_dir, 'w+') as f:
        f.write('%s: %d\n' % (name, end_time - start_time))


for threads in [4, 8, 16, 32, 64]:
    run_test('%d' % threads, '--hashing_threads=%d ' % threads)
