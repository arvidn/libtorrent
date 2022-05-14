#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import argparse
import os
import platform
import shutil
import subprocess
import sys
import time

from vmstat import capture_sample
from vmstat import plot_output
from vmstat import print_output_to_file


def main():
    args = parse_args()

    ret = os.system(f"cd ../examples && b2 release {args.toolset} stage_client_test stage_connection_tester")
    save_dir = args.directory
    print(f"save dir: {save_dir}")

    if ret != 0:
        print('ERROR: build failed: %d' % ret)
        sys.exit(1)

    rm_file_or_dir(".ses_state")
    rm_file_or_dir(".resume")

    if not os.path.exists('checking_benchmark.torrent'):
        ret = os.system('../examples/connection_tester gen-torrent -s 10000 -n 15 -t checking_benchmark.torrent')
        if ret != 0:
            print('ERROR: connection_tester failed: %d' % ret)
            sys.exit(1)

    if not os.path.exists(f"{save_dir}/checking_benchmark"):
        cmd_line = f'../examples/connection_tester gen-data -t checking_benchmark.torrent -P {save_dir}'
        print(cmd_line)
        ret = os.system(cmd_line)
        if ret != 0:
            print('ERROR: connection_tester failed: %d' % ret)
            sys.exit(1)

    for threads in [1, 2, 4, 8, 16, 32, 64]:
        print("drop caches now. e.g. \"echo 1 | sudo tee /proc/sys/vm/drop_caches\"")
        input("Press Enter to continue...")
        run_test(f"{threads}", f"--hashing_threads={threads}", save_dir)


def run_test(name, client_arg, save_dir: str):
    output_dir = 'logs_checking_%s' % name

    timing_path = os.path.join(output_dir, 'timing.txt')
    if os.path.exists(timing_path):
        print('file "{path}" exists, skipping test "{name}"'.format(path=timing_path, name=name))
        return

    rm_file_or_dir(output_dir)
    try:
        os.mkdir(output_dir)
    except Exception:
        pass

    rm_file_or_dir(f"{save_dir}/.resume")

    client_cmd = ('../examples/client_test checking_benchmark.torrent '
        '--enable_dht=0 --enable_lsd=0 --enable_upnp=0 --enable_natpmp=0 '
        f'-1 {client_arg} -s {save_dir} -f {output_dir}/events.log --alert_mask=all')

    client_out = open('%s/client.out' % output_dir, 'w+')
    print('client_cmd: "{cmd}"'.format(cmd=client_cmd))
    c = subprocess.Popen(client_cmd.split(' '), stdout=client_out, stderr=client_out, stdin=subprocess.PIPE)

    start = time.monotonic()
    if platform.system() == "Linux":
        out = {}
        while c.returncode is None:
            capture_sample(c.pid, start, out)
            time.sleep(0.1)
            c.poll()

        stats_filename = f"{output_dir}/memory_stats.log"
        keys = print_output_to_file(out, stats_filename)
        plot_output(stats_filename, keys)
    else:
        c.wait()

    client_out.close()

    start_time = 0
    end_time = 0
    for l in open('%s/events.log' % output_dir, 'r'):
        if 'checking_benchmark: start_checking, m_checking_piece: ' in l \
                and start_time == 0:
            start_time = int(l.split(' ')[0][1:-1])
        if 'state changed to: finished' in l \
                and start_time != 0:
            end_time = int(l.split(' ')[0][1:-1])

    print('%s: %d' % (name, end_time - start_time))
    with open('%s/timing.txt' % output_dir, 'w+') as f:
        f.write('%s: %d\n' % (name, end_time - start_time))


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
    p.add_argument('--directory', default=".")

    return p.parse_args()


if __name__ == '__main__':
    main()
