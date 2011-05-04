import sys
import os
import resource
import shutil
import shlex
import time
import subprocess
import random

# this is a disk I/O benchmark script. It runs menchmarks
# over different filesystems, different cache sizes and
# different number of peers (can be used to find a reasonable
# range for unchoke slots).

# it also measures performance improvements of re-ordering
# read requests based on physical location and OS hints
# like posix_fadvice(FADV_WILLNEED). It can also be used
# for the AIO branch to measure improvements over the
# classic thread based disk I/O

# to set up the test, build the example directoryin release
# with statistics=on and copy fragmentation_test, client_test
# and connection_tester to the current directory.
# make sure gnuplot is installed.

# the following lists define the space tests will be run in

# variables to test. All these are run on the first
# entry in the filesystem list.
cache_sizes = [0, 256, 512, 1024, 2048, 4096, 8192]
peers = [10, 100, 500, 1000, 2000]

# the drives are assumed to be mounted under ./<name>
# or have symbolic links to them.
filesystem = ['ext4', 'ext3', 'reiser', 'xfs']

# the number of peers for the filesystem test. The
# idea is to stress test the filesystem by using a lot
# of peers, since each peer essentially is a separate
# read location on the platter
filesystem_peers = 200

# the amount of cache for the filesystem test
filesystem_cache = 2096

# the number of seconds to run each test. It's important that
# this is shorter than what it takes to finish downloading
# the test torrent, since then the average rate will not
# be representative of the peak anymore
test_duration = 300



# make sure the environment is properly set up
if resource.getrlimit(resource.RLIMIT_NOFILE)[0] < 4000:
	print 'please set ulimit -n to at least 4000'
	sys.exit(1)

# make sure we have all the binaries available
binaries = ['client_test', 'connection_tester', 'fragmentation_test']
for i in binaries:
	if not os.path.exists(i):
		print 'make sure "%s" is available in current working directory' % i
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
	os.system('./connection_tester gen-torrent test.torrent')

# use a new port for each test to make sure they keep working
# this port is incremented for each test run
port = 10000 + random.randint(0, 5000)

def build_commandline(config, port):
	num_peers = config['num-peers']
	no_disk_reorder = '';
	if config['allow-disk-reorder'] == False:
		no_disk_reorder = '-O'
	no_read_ahead = ''
	if config['read-ahead'] == False:
		no_read_ahead = '-j'
		
	global test_duration

	return './client_test -k -z -N -h -H -M -B %d -l %d -S %d -T %d -c %d -C %d -s "%s" %s %s -q %d -p %d -f session_stats/alerts_log.txt test.torrent' \
		% (test_duration, num_peers, num_peers, num_peers, num_peers, config['cache-size'], config['save-path'] \
			, no_disk_reorder, no_read_ahead, test_duration, port)

def delete_files(files):
	for i in files:
		try: os.remove(i)
		except:
			try: shutil.rmtree(i)
			except: pass

def build_test_config(fs, num_peers, cache_size, readahead=True, reorder=True):
	config = {'test': 'dual', 'save-path': os.path.join('./', fs), 'num-peers': num_peers, 'allow-disk-reorder': reorder, 'cache-size': cache_size, 'read-ahead': readahead}
	return config

def build_target_folder(config):
	reorder = 'reorder'
	if config['allow-disk-reorder'] == False: reorder = 'no-reorder'
	readahead = 'readahead'
	if config['read-ahead'] == False: readahead = 'no-readahead'

	return 'results_%d_%d_%s_%s_%s_%s' % (config['num-peers'], config['cache-size'], os.path.split(config['save-path'])[1], config['test'], reorder, readahead)

def run_test(config):

	if os.path.exists(build_target_folder(config)):
		print 'results already exists, skipping test'
		return

	# make sure any previous test file is removed
	delete_files([os.path.join(config['save-path'], 'stress_test_file'), '.ses_state', os.path.join(config['save-path'], '.resume'), '.dht_state', 'session_stats'])

	try: os.mkdir('session_stats')
	except: pass

	# save off the command line for reference
	global port
	cmdline = build_commandline(config, port)
	f = open('session_stats/cmdline.txt', 'w+')
	f.write(cmdline)
	f.close()

	f = open('session_stats/config.txt', 'w+')
	print >>f, config
	f.close()

	f = open('session_stats/client.output', 'w+')
	print 'launching: %s' % cmdline
	client = subprocess.Popen(shlex.split(cmdline), stdout=f)
	time.sleep(1)
	print '\n\n*********************************'
	print '*          RUNNING TEST         *'
	print '*********************************\n\n'
	print 'launching connection tester'
	os.system('./connection_tester %s %d 127.0.0.1 %d test.torrent >session_stats/tester.output' % (config['test'], config['num-peers'], port))
	
	f.close()

	# run fragmentation test
#	print 'analyzing fragmentation'
#	os.system('./fragmentation_test test.torrent %s' % config['save-path'])
#	shutil.copy('fragmentation.log', 'session_stats/')
#	shutil.copy('fragmentation.png', 'session_stats/')
#	shutil.copy('fragmentation.gnuplot', 'session_stats/')

	os.chdir('session_stats')

	# parse session stats
	print 'parsing session log'
	os.system('python ../../parse_session_stats.py *.0000.log')

	os.chdir('..')

	# move the results into its final place
	print 'saving results'
	os.rename('session_stats', build_target_folder(config))

	# clean up
	print 'cleaning up'
	delete_files([os.path.join(config['save-path'], 'stress_test_file'), '.ses_state', os.path.join(config['save-path'], '.resume'), '.dht_state'])

	port += 1

for fs in filesystem:
	config = build_test_config(fs, filesystem_peers, filesystem_cache)
	run_test(config)

for c in cache_sizes:
	for p in peers:
		for rdahead in [True, False]:
			for reorder in [True, False]:
				config = build_test_config(filesystem[0], p, c, rdahead, reorder)
				run_test(config)

