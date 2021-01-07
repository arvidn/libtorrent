#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4
from __future__ import print_function

import sys
import os
import resource
import shutil
import shlex
import time
import subprocess
import random
import signal
import hashlib

# this is a disk I/O benchmark script. It runs benchmarks
# over different number of peers.

# to set up the test, build the example directory in release
# and stage client_test and connection_tester to the examples directory:
#
#   bjam link=static release debug-symbols=on stage
#
# make sure gnuplot is installed.

# the following lists define the space tests will be run in

peers = [50, 200, 500, 1000]
# builds = ['rtorrent', 'utorrent', 'libtorrent']
builds = ['libtorrent']

# the number of peers for the filesystem test. The
# idea is to stress test the filesystem by using a lot
# of peers, since each peer essentially is a separate
# read location on the platter
default_peers = peers[1]

# the amount of cache for the filesystem test
# 5.5 GiB of cache
default_cache = 400000

# the number of seconds to run each test. It's important that
# this is shorter than what it takes to finish downloading
# the test torrent, since then the average rate will not
# be representative of the peak anymore
# this has to be long enough to download a full copy
# of the test torrent. It's also important for the
# test to be long enough that the warming up of the
# disk cache is not a significant part of the test,
# since download rates will be extremely high while downloading
# into RAM
test_duration = 100

utorrent_version = 'utorrent-server-alpha-v3_3'

# make sure the environment is properly set up
try:
    if os.name == 'posix':
        resource.setrlimit(resource.RLIMIT_NOFILE, (4000, 5000))
except Exception:
    if resource.getrlimit(resource.RLIMIT_NOFILE)[0] < 4000:
        print('please set ulimit -n to at least 4000')
        sys.exit(1)


def build_stage_dirs():
    ret = []
    for i in builds[2:3]:
        ret.append('stage_%s' % i)
    return ret


# make sure we have all the binaries available
binaries = ['client_test', 'connection_tester']
for b in build_stage_dirs():
    for i in binaries:
        p = os.path.join(b, i)
        if not os.path.exists(p):
            print('make sure "%s" is available in ./%s' % (i, b))
            sys.exit(1)

# make sure we have a test torrent
if not os.path.exists('test.torrent'):
    print('generating test torrent')
    # generate a 100 GB torrent, to make sure it won't all fit in physical RAM
    os.system('./connection_tester gen-torrent -s 100000 -t test.torrent')

# use a new port for each test to make sure they keep working
# this port is incremented for each test run
port = 10000 + random.randint(0, 40000)

try:
    os.mkdir('benchmark-dir')
except Exception:
    pass


def clear_caches():
    if 'linux' in sys.platform:
        os.system('sync')
        try:
            open('/proc/sys/vm/drop_caches', 'w').write('3')
        except Exception:
            pass
    elif 'darwin' in sys.platform:
        os.system('purge')


def build_utorrent_commandline(config, port):
    num_peers = config['num-peers']
    torrent_path = config['torrent']
    target_folder = build_target_folder(config)

    try:
        os.mkdir('utorrent_session')
    except Exception:
        pass
    with open('utorrent_session/settings.dat', 'w+') as cfg:

        cfg.write('d')
        cfg.write('20:ul_slots_per_torrenti%de' % num_peers)
        cfg.write('17:conns_per_torrenti%de' % num_peers)
        cfg.write('14:conns_globallyi%de' % num_peers)
        cfg.write('9:bind_porti%de' % port)
        cfg.write('19:dir_active_download%d:%s' % (len(config['save-path']),
                                                   config['save-path']))
        cfg.write('19:diskio.sparse_filesi1e')
        cfg.write('14:cache.overridei1e')
        cfg.write('19:cache.override_sizei%de' % int(config['cache-size'] *
                                                     16 / 1024))
        cfg.write('17:dir_autoload_flagi1e')
        cfg.write('12:dir_autoload8:autoload')
        cfg.write('11:logger_maski4294967295e')
        cfg.write('1:vi0e')
        cfg.write('12:webui.enablei1e')
        cfg.write('19:webui.enable_listeni1e')
        cfg.write('14:webui.hashword20:' + hashlib.sha1(
            'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaadmin').digest())
        cfg.write('10:webui.porti8080e')
        cfg.write('10:webui.salt32:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa')
        cfg.write('14:webui.username5:admin')
        cfg.write('e')

    try:
        os.mkdir('utorrent_session/autoload')
    except Exception:
        pass
    try:
        shutil.copy(torrent_path, 'utorrent_session/autoload/')
    except Exception:
        pass
    return './%s/utserver -logfile %s/client.log -settingspath ' % \
        (utorrent_version, target_folder) + \
        'utorrent_session'


def build_rtorrent_commandline(config, port):
    num_peers = config['num-peers']
    torrent_path = config['torrent']
    target_folder = build_target_folder(config)

    if os.path.exists(target_folder):
        add_command = ''
    else:
        try:
            os.mkdir(target_folder)
        except Exception:
            pass
        # it seems rtorrent may delete the original torrent when it's being added
        try:
            shutil.copy(torrent_path, target_folder)
        except Exception:
            pass
        add_command = '-O load_start_verbose=%s/%s ' % (target_folder, torrent_path)

    return ('rtorrent -d %s -n -p %d-%d -O max_peers=%d -O max_uploads=%d %s -s '
            '%s -O max_memory_usage=128000000000') % (
                config['save-path'], port, port, num_peers, num_peers, add_command, target_folder)


def build_libtorrent_commandline(config, port):
    num_peers = config['num-peers']
    torrent_path = config['torrent']
    target_folder = build_target_folder(config)

    return ('./client_test -k -O -F 500 --enable_upnp=0 --enable_natpmp=0 '
            '--enable_dht=0 --mixed_mode_algorithm=0 --peer_timeout=%d '
            '--listen_queue_size=%d --unchoke_slots_limit=%d -T %d '
            '--connections_limit=%d --cache_size=%d -s "%s" '
            '--listen_interfaces="0.0.0.0:%d" --aio_threads=%d '
            '-f %s/client.log %s') % (
                test_duration, num_peers, num_peers, num_peers, num_peers, config['cache-size'],
                config['save-path'], port, config['disk-threads'], target_folder, torrent_path)


def build_commandline(config, port):

    if config['build'] == 'utorrent':
        return build_utorrent_commandline(config, port)

    if config['build'] == 'rtorrent':
        return build_rtorrent_commandline(config, port)

    if config['build'] == 'libtorrent':
        return build_libtorrent_commandline(config, port)


def delete_files(files):
    for i in files:
        print('deleting %s' % i)
        try:
            os.remove(i)
        except Exception:
            try:
                shutil.rmtree(i)
            except Exception:
                try:
                    if os.path.exists(i):
                        print('failed to delete %s' % i)
                except Exception:
                    pass


def build_test_config(num_peers=default_peers, cache_size=default_cache,
                      test='download', build='libtorrent', profile='', disk_threads=16,
                      torrent='test.torrent', disable_disk=False):
    config = {'test': test, 'save-path': os.path.join('.', 'benchmark-dir'), 'num-peers': num_peers,
              'cache-size': cache_size, 'build': build, 'profile': profile,
              'disk-threads': disk_threads, 'torrent': torrent, 'disable-disk': disable_disk}
    return config


def build_target_folder(config):

    no_disk = ''
    if config['disable-disk']:
        no_disk = '_no-disk'

    return 'results_%s_%s_%d_%d_%d%s' % (config['build'],
                                         config['test'],
                                         config['num-peers'],
                                         config['cache-size'],
                                         config['disk-threads'],
                                         no_disk)


def find_library(name):
    paths = ['/usr/lib64/', '/usr/local/lib64/', '/usr/lib/', '/usr/local/lib/']

    for p in paths:
        try:
            if os.path.exists(p + name):
                return p + name
        except Exception:
            pass
    return name


def find_binary(names):
    paths = ['/usr/bin/', '/usr/local/bin/']
    for n in names:
        for p in paths:
            try:
                if os.path.exists(p + n):
                    return p + n
            except Exception:
                pass
    return names[0]


def run_test(config):

    j = os.path.join

    target_folder = build_target_folder(config)
    if os.path.exists(target_folder):
        print('results already exists, skipping test (%s)' % target_folder)
        return

    print('\n\n*********************************')
    print('*          RUNNING TEST         *')
    print('*********************************\n\n')
    print('%s %s' % (config['build'], config['test']))

    # make sure any previous test file is removed
    # don't clean up unless we're running a download-test, so that we leave the test file
    # complete for a seed test.
    delete_files(['utorrent_session/settings.dat', 'utorrent_session/settings.dat.old', 'asserts.log'])
    if config['test'] == 'download' or config['test'] == 'dual':
        delete_files([j(config['save-path'], 'test'),
                      '.ses_state',
                      j(config['save-path'], '.resume'),
                      'utorrent_session',
                      '.dht_state',
                      'rtorrent_session'])

    try:
        os.mkdir(target_folder)
    except Exception:
        pass

    # save off the command line for reference
    global port
    cmdline = build_commandline(config, port)
    binary = cmdline.split(' ')[0]
    environment = None
    if config['profile'] == 'tcmalloc':
        environment = {'LD_PRELOAD': find_library('libprofiler.so.0'),
                       'CPUPROFILE': j(target_folder, 'cpu_profile.prof')}
    if config['profile'] == 'memory':
        environment = {'LD_PRELOAD': find_library('libprofiler.so.0'),
                       'HEAPPROFILE': j(target_folder, 'heap_profile.prof')}
    if config['profile'] == 'perf':
        cmdline = 'perf record -g --output=' + \
            j(target_folder, 'perf_profile.prof') + ' ' + cmdline
    with open(j(target_folder, 'cmdline.txt'), 'w+') as f:
        f.write(cmdline)

    with open(j(target_folder, 'config.txt'), 'w+') as f:
        print(config, file=f)

    print('clearing disk cache')
    clear_caches()
    print('OK')
    client_output = open(j(target_folder, 'client.output'), 'w+')
    client_error = open(j(target_folder, 'client.error'), 'w+')
    print('launching: %s' % cmdline)
    client = subprocess.Popen(
        shlex.split(cmdline),
        stdout=client_output,
        stdin=subprocess.PIPE,
        stderr=client_error,
        env=environment)
    print('OK')
    # enable disk stats printing
    if config['build'] == 'libtorrent':
        print('x', end=' ', file=client.stdin)
    time.sleep(4)
    test_dir = 'upload' if config['test'] == 'download' else 'download' if config['test'] == 'upload' else 'dual'
    cmdline = './connection_tester %s -c %d -d 127.0.0.1 -p %d -t %s' % (
        test_dir, config['num-peers'], port, config['torrent'])
    print('launching: %s' % cmdline)
    tester_output = open(j(target_folder, 'tester.output'), 'w+')
    tester = subprocess.Popen(shlex.split(cmdline), stdout=tester_output)
    print('OK')

    time.sleep(2)

    print('\n')
    i = 0
    while True:
        time.sleep(1)
        tester.poll()
        if tester.returncode is not None:
            print('tester terminated')
            break
        client.poll()
        if client.returncode is not None:
            print('client terminated')
            break
        print('\r%d / %d\x1b[K' % (i, test_duration), end=' ')
        sys.stdout.flush()
        i += 1
        # in download- and dual tests, connection_tester will exit once the
        # client is done downloading. In upload tests, we'll upload for
        # 'test_duration' number of seconds until we end the test
        if config['test'] != 'download' and config['test'] != 'dual' and i >= test_duration:
            break
    print('\n')

    if client.returncode is None:
        try:
            print('killing client')
            client.send_signal(signal.SIGINT)
        except Exception:
            pass

    time.sleep(10)
    client.wait()
    tester.wait()
    tester_output.close()
    client_output.close()
    terminate = False
    if tester.returncode != 0:
        print('tester returned %d' % tester.returncode)
        terminate = True
    if client.returncode != 0:
        print('client returned %d' % client.returncode)
        terminate = True

    try:
        shutil.copy('asserts.log', target_folder)
    except Exception:
        pass

    os.chdir(target_folder)

    if config['build'] == 'libtorrent':
        # parse session stats
        print('parsing session log')
        os.system('python ../../tools/parse_session_stats.py client.log')

    os.chdir('..')

    if config['profile'] == 'tcmalloc':
        print('analyzing CPU profile [%s]' % binary)
        os.system('%s --pdf %s %s/cpu_profile.prof >%s/cpu_profile.pdf' %
                  (find_binary(['google-pprof', 'pprof']), binary, target_folder, target_folder))
    if config['profile'] == 'memory':
        for i in range(1, 300):
            profile = j(target_folder, 'heap_profile.prof.%04d.heap' % i)
            try:
                os.stat(profile)
            except Exception:
                break
            print('analyzing heap profile [%s] %d' % (binary, i))
            os.system('%s --pdf %s %s >%s/heap_profile_%d.pdf' %
                      (find_binary(['google-pprof', 'pprof']), binary, profile, target_folder, i))
    if config['profile'] == 'perf':
        print('analyzing CPU profile [%s]' % binary)
        os.system(('perf report --input=%s/perf_profile.prof --threads --demangle --show-nr-samples '
                   '>%s/profile.txt' % (target_folder, target_folder)))

    port += 1

    if terminate:
        sys.exit(1)


for b in builds:
    for test in ['upload', 'download', 'dual']:
        config = build_test_config(build=b, test=test, profile='perf')
        run_test(config)

for p in peers:
    for test in ['upload', 'download', 'dual']:
        config = build_test_config(num_peers=p, test=test, profile='perf')
        run_test(config)
