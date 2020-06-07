#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os
import time
import shutil
import subprocess
import sys
import multiprocessing

toolset = ''
if len(sys.argv) > 1:
    toolset = sys.argv[1]

ret = os.system('cd ../examples && b2 -j %d release %s stage_client_test stage_connection_tester'
                % (multiprocessing.cpu_count(), toolset))
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

if not os.path.exists('checking_benchmark.torrent'):
    ret = os.system('../examples/connection_tester gen-data -t checking_benchmark.torrent -p .')
    if ret != 0:
        print('ERROR: connection_tester failed: %d' % ret)
        sys.exit(1)

def run_test(name, test_cmd, client_arg):
    output_dir = 'logs_checking_%s' % name

    try:
        shutil.rmtree(output_dir)
    except Exception:
        pass
    try:
        os.mkdir(output_dir)
    except Exception:
        pass

    start = time.time()
    client_cmd = '../examples/client_test checking_benchmark.torrent ' \
                 '--enable_dht=0 --enable_lsd=0 --enable_upnp=0 --enable_natpmp=0 ' \
                 '-e 120 %s -f %s/events.log --alert_mask=8747' \
                 % (client_arg, output_dir)

    client_out = open('%s/client.out' % output_dir, 'w+')
    print(client_cmd)
    c = subprocess.Popen(client_cmd.split(' '), stdout=client_out, stderr=client_out, stdin=subprocess.PIPE)
