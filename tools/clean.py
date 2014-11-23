import os
import shutil
import glob

def clean():
	to_delete = [
		'session_stats',
		'libtorrent_logs*',
		'round_trip_ms.log',
		'dht.log',
		'upnp.log',
		'natpmp.log',
		'bin',
		'test_tmp_*',
		'bjam_build.*.xml'
		'*.exe',
		'*.pdb',
		'*.pyd',
		'dist',
		'build',
		'.libs'
	]
	
	directories = [
		'examples',
		'test',
		'.',
		'tools',
		'src',
		os.path.join('bindings', 'python')
	]
	
	for d in directories:
		for f in to_delete:
			path = os.path.join(d, f)
			entries = glob.glob(path)
			for p in entries:
				print p
				try:
					shutil.rmtree(p)
				except:
					pass
   	
if	__name__ == "__main__":
	clean()
   	
