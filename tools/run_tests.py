
#!/bin/python

# Copyright (c) 2013, Arvid Norberg
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the distribution.
#     * Neither the name of the author nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# this is meant to be run from the root of the repository
# the arguments are the boost-build toolsets to use.
# these will vary between testers and operating systems
# common ones are: clang, darwin, gcc, msvc, icc

import random
import os
import platform
import subprocess
import xml.etree.ElementTree as et
from datetime import datetime
import json
import sys
import yaml
from multiprocessing import Pool
import glob
import shutil
import traceback
import clean

# the .regression.yml configuration file format looks like this (it's yaml):

# test_dirs:
# - <path-to-test-folder>
# - ...
#
# features:
# - <list of boost-built features>
# - ...
#

def svn_info():
	# figure out which revision this is
	p = subprocess.Popen(['svn', 'info'], stdout=subprocess.PIPE)

	revision = -1
	author = ''

	for l in p.stdout:
		if 'Last Changed Rev' in l:
			revision = int(l.split(':')[1].strip())
		if 'Last Changed Author' in l:
			author = l.split(':')[1].strip()

	if revision == -1:
		print 'Failed to extract subversion revision'
		sys.exit(1)

	if author == '':
		print 'Failed to extract subversion author'
		sys.exit(1)

	return (revision, author)

def run_tests(toolset, tests, features, options, test_dir, time_limit):
	assert(type(features) == str)

	xml_file = 'bjam_build.%d.xml' % random.randint(0, 100000)
	try:

		results = {}
		toolset_found = False
   
		feature_list = features.split(' ')
		os.chdir(test_dir)
   
		for t in tests:
			options_copy = options[:]
			if t != '': options_copy.append(t)
			if t == '':
				t = os.path.split(os.getcwd())[1]
				# we can't pass in a launcher when just building, that only
				# works for actual unit tests
				if 'launcher=valgrind' in options_copy:
					options_copy.remove('launcher=valgrind')
			cmdline = ['bjam', '--out-xml=%s' % xml_file, '-l%d' % time_limit, '-q', '--abbreviate-paths', toolset] + options_copy + feature_list

#			print ' '.join(cmdline)

			p = subprocess.Popen(cmdline, stdout=subprocess.PIPE, cwd=test_dir)
			output = ''
			for l in p.stdout:
				output += l.decode('latin-1')
			p.wait()
   
			# parse out the toolset version from the xml file
			compiler = ''
			compiler_version = ''
			command = ''
   
			# make this parse the actual test to pick up the time
			# spent runnin the test
			try:
				dom = et.parse(xml_file)
   
				command = dom.find('./command').text
   
				prop = dom.findall('./action/properties/property')
				for a in prop:
					name = a.attrib['name']
					if name == 'toolset':
						compiler = a.text
						if compiler_version != '': break
					if name.startswith('toolset-') and name.endswith(':version'):
						compiler_version = a.text
						if compiler != '': break
   
				if compiler != '' and compiler_version != '':
					toolset = compiler + '-' + compiler_version
			except: pass
   
			r = { 'status': p.returncode, 'output': output, 'command': command }
			results[t + '|' + features] = r
   
			if p.returncode == 0: sys.stdout.write('.')
			else: sys.stdout.write('X')
			sys.stdout.flush()

	except Exception, e:
		# need this to make child processes exit
		print 'exiting test process: ', traceback.format_exc()
		sys.exit(1)
	finally:
		try: os.unlink(xml_file)
		except: pass

	return (toolset, results)

def print_usage():
		print '''usage: run_tests.py [options] bjam-toolset [bjam-toolset...]
options:
-j<n>     use n parallel processes
-h        prints this message and exits
-i        build incrementally (i.e. don't clean between checkouts)
-valgrind run tests with valgrind (requires valgrind to be installed)
'''

def main(argv):

	toolsets = []

	num_processes = 4
	incremental = False

	test_dirs = []
	configs = []
	options = ['boost=source', 'preserve-test-targets=on']
	time_limit = 1200

	for arg in argv:
		if arg[0] == '-':
			if arg[1] == 'j':
				num_processes = int(arg[2:])
			elif arg[1] == 'h':
				print_usage()
				sys.exit(1)
			elif arg[1] == 'i':
				incremental = True
			elif arg[1:] == 'valgrind':
				options.append('launcher=valgrind')
			else:
				print 'unknown option: %s' % arg
				print_usage()
				sys.exit(1)
		else:
			toolsets.append(arg)

	if toolsets == []:
		print_usage()
		sys.exit(1)

	if not incremental:
		print 'cleaning repo'
		clean.clean()

	try:
		cfg = open('.regression.yml', 'r')
	except:
		print '.regression.yml not found in current directory'
		sys.exit(1)

	cfg = yaml.load(cfg.read())

	if 'test_dirs' in cfg:
		for d in cfg['test_dirs']:
			test_dirs.append(os.path.abspath(d))
	else:
		print 'no test directory specified by .regression.yml'
		sys.exit(1)

	configs = []
	if 'features' in cfg:
		for d in cfg['features']:
			configs.append(d)
	else:
		configs = ['']

	clean_files = []
	if 'clean' in cfg:
		clean_files = cfg['clean']

	branch_name = 'trunk'
	if 'branch' in cfg:
		branch_name = cfg['branch']

	if 'time_limit' in cfg:
		time_limit = int(cfg['time_limit'])

	# it takes a bit longer to run in valgrind
	if 'launcher=valgrind' in options:
		time_limit *= 6

	architecture = platform.machine()
	build_platform = platform.system() + '-' + platform.release()

	revision, author = svn_info()

	timestamp = datetime.now()

#	tester_pool = Pool(processes=num_processes)

	print '%s-%d - %s - %s' % (branch_name, revision, author, timestamp)

	print 'toolsets: %s' % ' '.join(toolsets)
#	print 'configs: %s' % '|'.join(configs)

	current_dir = os.getcwd()

	try:
		rev_dir = os.path.join(current_dir, 'regression_tests')
		try: os.mkdir(rev_dir)
		except: pass
		rev_dir = os.path.join(rev_dir, '%s-%d' % (branch_name, revision))
		try: os.mkdir(rev_dir)
		except: pass

		results = {}
		for test_dir in test_dirs:
			print 'running tests from "%s" in %s' % (test_dir, branch_name)
			os.chdir(test_dir)
			test_dir = os.getcwd()

			# figure out which tests are exported by this Jamfile
			p = subprocess.Popen(['bjam', '--dump-tests', 'non-existing-target'], stdout=subprocess.PIPE, cwd=test_dir)

			tests = []

			output = ''
			for l in p.stdout:
				output += l
				if not 'boost-test(RUN)' in l: continue
				test_name = os.path.split(l.split(' ')[1][1:-1])[1]
				tests.append(test_name)
			print 'found %d tests' % len(tests)
			if len(tests) == 0:
				tests = ['']

			for toolset in toolsets:
				if not toolset in results: results[toolset] = {}
				toolset_found = False

#				futures = []
#				for features in configs:
#					futures.append(tester_pool.apply_async(run_tests, [toolset, tests, features, options, test_dir, time_limit]))

#				for future in futures:
#					(compiler, r) = future.get()
#					results[toolset].update(r)

				for features in configs:
					(compiler, r) = run_tests(toolset, tests, features, options, test_dir, time_limit)
					results[toolset].update(r)

				print ''

				if len(clean_files) > 0:
					print 'deleting ',
					for filt in clean_files:
						for f in glob.glob(filt):
							# a precaution to make sure a malicious repo
							# won't clean things outside of the test directory
							if not os.path.abspath(f).startswith(test_dir): continue
							print '%s ' % f,
							try: shutil.rmtree(f)
							except: pass
					print ''

		for toolset in toolsets:
			# each file contains a full set of tests for one speific toolset and platform
			try:
				f = open(os.path.join(rev_dir, build_platform + '#' + toolset + '.json'), 'w+')
			except IOError:
				rev_dir = os.path.join(current_dir, 'regression_tests')
				try: os.mkdir(rev_dir)
				except: pass
				rev_dir = os.path.join(rev_dir, '%s-%d' % (branch_name, revision))
				try: os.mkdir(rev_dir)
				except: pass
				f = open(os.path.join(rev_dir, build_platform + '#' + toolset + '.json'), 'w+')

			print >>f, json.dumps(results[toolset])
			f.close()

			
	finally:
		# always restore current directory
		try:
			os.chdir(current_dir)
		except: pass

if __name__ == "__main__":
    main(sys.argv[1:])

