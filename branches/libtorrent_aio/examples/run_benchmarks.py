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

# this is a disk I/O benchmark script. It runs menchmarks
# over different filesystems, different cache sizes and
# different number of peers (can be used to find a reasonable
# range for unchoke slots).

# it also measures performance improvements of re-ordering
# read requests based on physical location and OS hints
# like posix_fadvice(FADV_WILLNEED). It can also be used
# for the AIO branch to measure improvements over the
# classic thread based disk I/O

# to set up the test, build the example directory in release
# with statistics=on and copy fragmentation_test, client_test
# and connection_tester to a directory called 'stage_aio'
# and 'stage_syncio' (or make a symbolic link to the bjam
# output directory).
# make sure gnuplot is installed.

# the following lists define the space tests will be run in

# variables to test. All these are run on the first
# entry in the filesystem list.
cache_sizes = [0, 32768, 393216]
peers = [200, 1000, 2000]
builds = ['rtorrent', 'utorrent', 'aio', 'syncio']

# the drives are assumed to be mounted under ./<name>
# or have symbolic links to them.
filesystem = ['ext4', 'ext3', 'reiser', 'xfs']
default_fs = filesystem[0]

# the number of peers for the filesystem test. The
# idea is to stress test the filesystem by using a lot
# of peers, since each peer essentially is a separate
# read location on the platter
default_peers = peers[1]

# the amount of cache for the filesystem test
# 5.5 GiB of cache
default_cache = cache_sizes[-1]

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
test_duration = 200 #  700



# make sure the environment is properly set up
try:
	if os.name == 'posix':
		resource.setrlimit(resource.RLIMIT_NOFILE, (4000, 5000))
except:
	if resource.getrlimit(resource.RLIMIT_NOFILE)[0] < 4000:
		print 'please set ulimit -n to at least 4000'
		sys.exit(1)

def build_stage_dirs():
	ret = []
	for i in builds[2:3]:
		ret.append('stage_%s' % i)
	return ret

# make sure we have all the binaries available
binaries = ['client_test', 'connection_tester', 'fragmentation_test', 'parse_access_log']
for b in build_stage_dirs():
	for i in binaries:
		p = os.path.join(b, i)
		if not os.path.exists(p):
			print 'make sure "%s" is available in ./%s' % (i, b)
			sys.exit(1)

for i in filesystem:
	if not os.path.exists(i):
		print ('the path "%s" does not exist. This is directory/mountpoint is ' +
			'used as the download directory and is the filesystem that will be benchmarked ' +
			'and need to exist.') % i
		sys.exit(1)

# make sure we have a test torrent
if not os.path.exists('test.torrent'):
	print 'generating test torrent'
	# generate a 100 GB torrent, to make sure it won't all fit in physical RAM
	os.system('./stage_aio/connection_tester gen-torrent 40000 test.torrent')

# use a new port for each test to make sure they keep working
# this port is incremented for each test run
port = 10000 + random.randint(0, 5000)

def clear_caches():
	if 'linux' in sys.platform:
		os.system('sync')
		open('/proc/sys/vm/drop_caches', 'w').write('3')
	elif 'darwin' in sys.platform:
		os.system('purge')

def build_commandline(config, port):

	num_peers = config['num-peers']

	if config['build'] == 'utorrent':
		try: os.mkdir('utorrent_session')
		except: pass
		cfg = open('utorrent_session/settings.dat', 'w+')

		cfg.write('d')
		cfg.write('20:ul_slots_per_torrenti%de' % num_peers)
		cfg.write('17:conns_per_torrenti%de' % num_peers)
		cfg.write('14:conns_globallyi%de' % num_peers)
		cfg.write('9:bind_porti%de' % port)
		cfg.write('19:dir_active_download%d:%s' % (len(config['save-path']), config['save-path']))
		cfg.write('19:diskio.sparse_filesi1e')
		cfg.write('14:cache.overridei1e')
		cfg.write('19:cache.override_sizei%de' % int(config['cache-size'] * 16 / 1024))
		cfg.write('17:dir_autoload_flagi1e')
		cfg.write('12:dir_autoload8:autoload')
		cfg.write('11:logger_maski4294967295e')
		cfg.write('1:vi0e')
		cfg.write('12:webui.enablei1e')
		cfg.write('19:webui.enable_listeni1e')
		cfg.write('14:webui.hashword20:' + hashlib.sha1('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaadmin').digest())
		cfg.write('10:webui.porti8080e')
		cfg.write('10:webui.salt32:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa')
		cfg.write('14:webui.username5:admin')
		cfg.write('e')
		cfg.close()
		try: os.mkdir('utorrent_session/autoload')
		except: pass
		try: shutil.copy('test.torrent', 'utorrent_session/autoload/')
		except: pass
		return './utorrent-server-v3_0/utserver -logfile session_stats/alerts_log.txt -settingspath utorrent_session'

	if config['build'] == 'rtorrent':
		if os.path.exists('rtorrent_session'):
			add_command = ''
		else:
			try: os.mkdir('rtorrent_session')
			except: pass
			# it seems rtorrent may delete the original torrent when it's being added
			try: shutil.copy('test.torrent', 'rtorrent_session/')
			except: pass
			add_command = '-O load_start_verbose=rtorrent_session/test.torrent '

		return 'rtorrent -d %s -n -p %d-%d -O max_peers=%d -O max_uploads=%d %s -s rtorrent_session -O max_memory_usage=128000000000' \
			% (config['save-path'], port, port, num_peers, num_peers, add_command)

	return './stage_%s/client_test -k -N -H -M -B %d -l %d -S %d -T %d -c %d -C %d -s "%s" -p %d -f session_stats/alerts_log.txt test.torrent' \
		% (config['build'], test_duration, num_peers, num_peers, num_peers, num_peers, config['cache-size'], config['save-path'], port)

def delete_files(files):
	for i in files:
		try: os.remove(i)
		except:
			try: shutil.rmtree(i)
			except:
				try:
					if os.path.exists(i): print 'failed to delete %s' % i
				except: pass

# typically the schedulers available are 'noop', 'deadline' and 'cfq'
def build_test_config(fs=default_fs, num_peers=default_peers, cache_size=default_cache, test='upload', build='aio', profile=False):
	config = {'test': test, 'save-path': os.path.join('./', fs), 'num-peers': num_peers, 'cache-size': cache_size, 'build': build, 'profile':profile }
	return config

def build_target_folder(config):
	test = 'seed'
	if config['test'] == 'upload': test = 'download'
	elif config['test'] == 'dual': test = 'dual'

	# todod, resolve 'sdh' by calling mount and look for the save directory instead
	io_scheduler = open('/sys/block/sdh/queue/scheduler').read().split('[')[1].split(']')[0]

	return 'results_%s_%s_%d_%d_%s_%s' % (config['build'], test, config['num-peers'], config['cache-size'], os.path.split(config['save-path'])[1], io_scheduler)

def run_test(config):

	target_folder = build_target_folder(config)
	if os.path.exists(target_folder):
		print 'results already exists, skipping test (%s)' % target_folder
		return

	print '\n\n*********************************'
	print '*          RUNNING TEST         *'
	print '*********************************\n\n'
	print '%s %s' % (config['build'], config['test'])

	# make sure any previous test file is removed
	# don't clean up unless we're running a download-test, so that we leave the test file
	# complete for a seed test.
	delete_files(['utorrent_session/settings.dat', 'utorrent_session/settings.dat.old', 'asserts.log'])
	if config['test'] == 'upload' or config['test'] == 'dual':
		print 'deleting files'
		delete_files([os.path.join(config['save-path'], 'stress_test_file'), '.ses_state', os.path.join(config['save-path'], '.resume'), 'utorrent_session', '.dht_state', 'session_stats', 'rtorrent_session'])

	try: os.mkdir('session_stats')
	except: pass

	# save off the command line for reference
	global port
	cmdline = build_commandline(config, port)
	binary = cmdline.split(' ')[0]
	environment = None
	if config['profile']: environment = {'LD_PRELOAD':'/usr/lib/libprofiler.so.0', 'CPUPROFILE': 'session_stats/cpu_profile.prof'}
	f = open('session_stats/cmdline.txt', 'w+')
	f.write(cmdline)
	f.close()

	f = open('session_stats/config.txt', 'w+')
	print >>f, config
	f.close()

	print 'clearing disk cache'
	clear_caches()
	print 'OK'
	client_output = open('session_stats/client.output', 'w+')
	client_error = open('session_stats/client.error', 'w+')
	print 'launching: %s' % cmdline
	client = subprocess.Popen(shlex.split(cmdline), stdout=client_output, stdin=subprocess.PIPE, stderr=client_error, env=environment)
	print 'OK'
	# enable disk stats printing
	if config['build'] != 'rtorrent' and config['build'] != 'utorrent':
		print >>client.stdin, 'x',
	time.sleep(4)
	cmdline = './stage_aio/connection_tester %s %d 127.0.0.1 %d test.torrent' % (config['test'], config['num-peers'], port)
	print 'launching: %s' % cmdline
	tester_output = open('session_stats/tester.output', 'w+')
	tester = subprocess.Popen(shlex.split(cmdline), stdout=tester_output)
	print 'OK'
	
	time.sleep(2)

	print '\n'
	i = 0
	while True:
		time.sleep(1)
		tester.poll()
		if tester.returncode != None:
			print 'tester terminated'
			break
		client.poll()
		if client.returncode != None:
			print 'client terminated'
			break
		print '\r%d / %d' % (i, test_duration),
		sys.stdout.flush()
		i += 1
		if config['test'] != 'upload' and config['test'] != 'dual' and i >= test_duration: break
	print '\n'

	if client.returncode == None:
		try:
			print 'killing client'
			client.send_signal(signal.SIGINT)
		except:
			pass

	time.sleep(10)
	client.wait()
	tester.wait()
	tester_output.close()
	client_output.close()
	if tester.returncode != 0:
		print 'tester returned %d' % tester.returncode
		sys.exit(tester.returncode)
	if client.returncode != 0:
		print 'client returned %d' % client.returncode
		sys.exit(client.returncode)

	try: shutil.copy('asserts.log', 'session_stats/')
	except: pass


	# run fragmentation test
	print 'analyzing fragmentation'
	os.system('./stage_aio/fragmentation_test test.torrent %s' % (config['save-path']))
	shutil.copy('fragmentation.log', 'session_stats/')
	shutil.copy('fragmentation.png', 'session_stats/')
	shutil.copy('fragmentation.gnuplot', 'session_stats/')
	shutil.copy('file_access.log', 'session_stats/')

	os.system('filefrag %s >session_stats/filefrag.out' % config['save-path'])
	os.system('filefrag -v %s >session_stats/filefrag_verbose.out' % config['save-path'])

	os.chdir('session_stats')

	# parse session stats
	print 'parsing session log'
	os.system('python ../../parse_session_stats.py *.0000.log')
	os.system('../stage_aio/parse_access_log file_access.log %s' % (os.path.join('..', config['save-path'], 'stress_test_file')))

	os.chdir('..')

	if config['profile']:
		print 'analyzing CPU profile [%s]' % binary
		os.system('google-pprof --pdf %s session_stats/cpu_profile.prof >session_stats/cpu_profile.pdf' % binary)

	# move the results into its final place
	print 'saving results'
	os.rename('session_stats', build_target_folder(config))

	port += 1

for b in ['aio', 'syncio']:
	for test in ['dual', 'upload', 'download']:
		config = build_test_config(build=b, test=test)
		run_test(config)
sys.exit(0)

for b in builds:
	for test in ['upload', 'download']:
		config = build_test_config(build=b, test=test)
		run_test(config)

for p in peers:
	for test in ['upload', 'download']:
		config = build_test_config(num_peers=p, test=test)
		run_test(config)

for c in cache_sizes:
	for test in ['upload', 'download']:
		config = build_test_config(cache_size=c, test=test)
		run_test(config)

for fs in filesystem:
	for test in ['upload', 'download']:
		config = build_test_config(fs=fs, test=test)
		run_test(config)

