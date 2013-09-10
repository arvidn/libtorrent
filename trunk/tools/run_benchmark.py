import os
import time
import shutil
import subprocess
import sys

port = (int(time.time()) % 50000) + 2000

toolset = ''
if len(sys.argv) > 1:
	toolset = sys.argv[1]

ret = os.system('cd ../examples && bjam boost=source profile statistics=on -j3 %s stage_client_test' % toolset)
ret = os.system('cd ../examples && bjam boost=source release -j3 %s stage_connection_tester' % toolset)

if ret != 0:
	print 'ERROR: build failed: %d' % ret
	sys.exit(1)

if not os.path.exists('cpu_benchmark.torrent'):
	ret = os.system('../examples/connection_tester gen-torrent -s 10000 -n 15 -t cpu_benchmark.torrent')
	if ret != 0:
		print 'ERROR: connection_tester failed: %d' % ret
		sys.exit(1)

try: shutil.rmtree('torrent_storage')
except: pass

try: shutil.rmtree('session_stats')
except: pass

try: os.mkdir('logs')
except: pass

start = time.time();
client_cmd = '../examples/client_test -p %d cpu_benchmark.torrent -k -0 -z -H -X -q 120' % port
test_cmd = '../examples/connection_tester upload -c 50 -d 127.0.0.1 -p %d -t cpu_benchmark.torrent' % port

client_out = open('logs/client.out', 'w+')
test_out = open('logs/test.out', 'w+')
print client_cmd
c = subprocess.Popen(client_cmd.split(' '), stdout=client_out, stderr=client_out, stdin=subprocess.PIPE)
time.sleep(2)
print test_cmd
t = subprocess.Popen(test_cmd.split(' '), stdout=test_out, stderr=test_out)

t.wait()
c.communicate('q')
c.wait()

end = time.time();

print 'runtime %d seconds' % (end - start)
print 'analyzing proile...'
os.system('gprof ../examples/client_test >logs/gprof.out')
print 'generating profile graph...'
os.system('python gprof2dot.py <logs/gprof.out | dot -Tpng -o logs/cpu_profile.png')

os.system('python parse_session_stats.py session_stats/*.log')
try: os.rename('session_stats_report', 'logs/session_stats_report')
except: pass
try: os.rename('session_stats', 'logs/session_stats')
except: pass

