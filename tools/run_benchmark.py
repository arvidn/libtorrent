#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import argparse
import os
import time
import shutil
import subprocess
import sys

from vmstat import capture_sample
from vmstat import plot_output
from vmstat import print_output_to_file

import platform

exe = ""

if platform.system() == "Windows":
    exe = ".exe"

def reset_download(save_path: str):
    rm_file_or_dir('.ses_state')
    rm_file_or_dir(save_path + '/.resume')
    rm_file_or_dir(save_path + '/cpu_benchmark')

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

    reset_download(args.save_path)

    if not os.path.exists('cpu_benchmark.torrent'):
        ret = subprocess.check_call([f'../examples/connection_tester{exe}', 'gen-torrent', '-s', '50000', '-n', '15', '-t', 'cpu_benchmark.torrent'])
        if ret != 0:
            print('ERROR: connection_tester failed: %d' % ret)
            sys.exit(1)

    rm_file_or_dir('t')

    for io_backend in ["mmap", "pread", "posix"]:
        run_test(f'download-write-through-{io_backend}', 'upload', ['-i', io_backend, '-1', '--disk_io_write_mode=write_through', '-s', args.save_path], args.download_peers)
        reset_download(args.save_path)
        run_test(f'download-full-cache-{io_backend}', 'upload', ['-i', io_backend, '-1', '--disk_io_write_mode=enable_os_cache', '-s', args.save_path], args.download_peers)
        run_test(f'upload-{io_backend}', 'download', ['-i', io_backend, '-G', '-e', '240', '-s', args.save_path], args.upload_peers)


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

    print("drop caches now. e.g. \"echo 1 | sudo tee /proc/sys/vm/drop_caches\"")
    input("Press Enter to continue...")

    start = time.monotonic()
    client_cmd = (f'../examples/client_test{exe} -k --listen_interfaces=127.0.0.1:{port} cpu_benchmark.torrent ' +
        f'--enable_dht=0 --enable_lsd=0 --enable_upnp=0 --enable_natpmp=0 ' +
        f' -O --allow_multiple_connections_per_ip=1 --connections_limit={num_peers*2} -T {num_peers*2} ' +
        f'-f {output_dir}/events.log --alert_mask=error,status,connect,performance_warning,storage,peer').split(' ') + client_arg

    test_cmd = f'../examples/connection_tester{exe} {test_cmd} -c {num_peers} -d 127.0.0.1 -p {port} -t cpu_benchmark.torrent'

    client_out = open('%s/client.out' % output_dir, 'w+')
    test_out = open('%s/test.out' % output_dir, 'w+')
    print(f"client_cmd: {' '.join(client_cmd)}")
    c = subprocess.Popen(client_cmd, stdout=client_out, stderr=client_out, stdin=subprocess.PIPE)
    time.sleep(2)
    print(f'test_cmd: "{test_cmd}"')
    t = subprocess.Popen(test_cmd.split(' '), stdout=test_out, stderr=test_out)

    out = {}
    while t.returncode is None:
        capture_sample(c.pid, start, out)
        time.sleep(0.1)
        t.poll()
    end = time.monotonic()

    stats_filename = f"{output_dir}/memory_stats.log"
    keys = print_output_to_file(out, stats_filename)
    plot_output(stats_filename, keys)

    t.wait()

    client_out.close()
    test_out.close()

    print(f'runtime {end-start:0.2f} seconds')
    print('analyzing profile...')
    try:
        os.system(f'gprof ../examples/client_test{exe} >%s/gprof.out' % output_dir)
        print('generating profile graph...')
        try:
            os.system('gprof2dot --strip <%s/gprof.out | dot -Tpng -o %s/cpu_profile.png' % (output_dir, output_dir))
        except Exception:
            print('please install gprof2dot and dot:\nsudo pip install gprof2dot\nsudo apt install graphviz')
    except Exception:
        print("gprof failed")

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
    p.add_argument('--download-peers', default=50, help="Number of peers to use for download test")
    p.add_argument('--upload-peers', default=20, help="Number of peers to use for upload test")
    p.add_argument('--save-path', default=".", help="The directory to download to or upload from")

    return p.parse_args()


if __name__ == '__main__':
    main()
