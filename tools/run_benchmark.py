#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import argparse
import os
import time
import shutil
import subprocess
import sys


def main():
    args = parse_args()

    ret = os.system('cd ../examples && b2 profile %s stage_client_test'
                    % args.toolset)
    if ret != 0:
        print('ERROR: build failed: %d' % ret)
        sys.exit(1)

    ret = os.system('cd ../examples && b2 release %s stage_connection_tester'
                    % args.toolset)
    if ret != 0:
        print('ERROR: build failed: %d' % ret)
        sys.exit(1)

    rm_file_or_dir('.ses_state')
    rm_file_or_dir('.resume')
    rm_file_or_dir('cpu_benchmark')

    if not os.path.exists('cpu_benchmark.torrent'):
        ret = os.system('../examples/connection_tester gen-torrent -s 10000 -n 15 -t cpu_benchmark.torrent')
        if ret != 0:
            print('ERROR: connection_tester failed: %d' % ret)
            sys.exit(1)

    rm_file_or_dir('t')

    run_test('download', 'upload', '', args.download_peers)
    run_test('upload', 'download', '-G', args.download_peers)


def run_test(name, test_cmd, client_arg, num_peers):
    output_dir = 'logs_%s' % name

    rm_file_or_dir(output_dir)
    try:
        os.mkdir(output_dir)
    except Exception:
        pass

    port = (int(time.time()) % 50000) + 2000

    rm_file_or_dir('session_stats')
    rm_file_or_dir('session_stats_report')

    start = time.time()
    client_cmd = f'../examples/client_test -k --listen_interfaces=127.0.0.1:{port} cpu_benchmark.torrent ' + \
        f'--disable_hash_checks=1 --enable_dht=0 --enable_lsd=0 --enable_upnp=0 --enable_natpmp=0 ' + \
        f'-e 120 {client_arg} -O --allow_multiple_connections_per_ip=1 --connections_limit={num_peers*2} -T {num_peers*2} ' + \
        f'-f {output_dir}/events.log --alert_mask=8747'

    test_cmd = f'../examples/connection_tester {test_cmd} -c {num_peers} -d 127.0.0.1 -p {port} -t cpu_benchmark.torrent'

    client_out = open('%s/client.out' % output_dir, 'w+')
    test_out = open('%s/test.out' % output_dir, 'w+')
    print(f'client_cmd: "{client_cmd}"')
    c = subprocess.Popen(client_cmd.split(' '), stdout=client_out, stderr=client_out, stdin=subprocess.PIPE)
    time.sleep(2)
    print(f'test_cmd: "{test_cmd}"')
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
    print('analyzing profile...')
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


def rm_file_or_dir(path):
    """ Attempt to remove file or directory at path
    """
    try:
        shutil.rmtree(path)
    except Exception:
        pass

    try:
        os.remove(path)
    except Exception:
        pass


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--toolset', default="")
    p.add_argument('--download-peers', default=50, help="Number of peers to use for upload test")
    p.add_argument('--upload-peers', default=20, help="Number of peers to use for upload test")

    return p.parse_args()


if __name__ == '__main__':
    main()
