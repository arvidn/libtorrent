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

# This is meant to be run from the root directory of the repo. It will
# look for the .regression.yml file and expect a regression_tests directory
# with results from test runs previously produced by run_tests.py

import os
import sys
import glob
import json
import yaml

# TODO: different parsers could be run on output from different actions
# if we would use the xml output in stead of stdout/stderr
def style_output(o):
	ret = ''
	subtle = False
	for l in o.split('\n'):
		if 'TEST_CHECK' in l or 'TEST_EQUAL_ERROR' in l or l.startswith('EXIT STATUS: '):
			ret += '<span class="test-error">%s</span>\n' % l
		elif '**passed**' in l:
			ret += '<span class="test-pass">%s</span>\n' % l
		elif ': error: ' in l or ': fatal error: ' in l or ' : fatal error ' in l or \
			'failed to write output file' in l or ') : error C' in l or \
			') : error LNK' in l:
			ret += '<span class="compile-error">%s</span>\n' % l
		elif ': warning: ' in l or ') : warning C' in l:
			ret += '<span class="compile-warning">%s</span>\n' % l
		elif l == '====== END OUTPUT ======' and not subtle:
			ret += '<span class="subtle">%s\n' % l
			subtle = True
		else:
			ret += '%s\n' % l
	if subtle: ret += '</span>'
	return ret

def modification_time(file):
	mtime = 0
	try:
		mtime = os.stat(file).st_mtime
	except: pass
	return mtime

def parse_tests(rev_dir):
	
	# this contains mappings from platforms to
	# the next layer of dictionaries. The next
	# layer contains a mapping of toolsets to
	# dictionaries the next layer of dictionaries.
	# those dictionaries contain a mapping from
	# feature-sets to the next layer of dictionaries.
	# the next layer contains a mapping from
	# tests to information about those tests, such
	# as whether it passed and the output from the
	# command
	# example:
	
	# {
	#   darwin: {
	#     clang-4.2.1: {
	#       ipv6=off: {
	#         test_primitives: {
	#           output: ...
	#           status: 1
	#         }
	#       }
	#     }
	#   }
	# }

	platforms = {}

	tests = {}

	for f in glob.glob(os.path.join(rev_dir, '*.json')):
		platform_toolset = os.path.split(f)[1].split('.json')[0].split('#')
		j = json.loads(open(f, 'rb').read())
	
		platform = platform_toolset[0]
		toolset = platform_toolset[1]
	
		for cfg in j:
			test_name = cfg.split('|')[0]
			features = cfg.split('|')[1]
	
			if not features in tests:
				tests[features] = set()
	
			tests[features].add(test_name)
	
			if not platform in platforms:
				platforms[platform] = {}
	
			if not toolset in platforms[platform]:
				platforms[platform][toolset] = {}
	
			if not features in platforms[platform][toolset]:
				platforms[platform][toolset][features] = {}

			platforms[platform][toolset][features][test_name] = j[cfg]

	return (platforms, tests)


# TODO: remove this dependency by encoding it in the output files
project_name = ''

try:
	cfg = open('.regression.yml', 'r')
except:
	print '.regression.yml not found in current directory'
	sys.exit(1)
cfg = yaml.load(cfg.read())

if 'project' in cfg:
	project_name = cfg['project']

branch_name = 'trunk'
if 'branch' in cfg:
	branch_name = cfg['branch'].strip()

print 'branch: %s' % branch_name

os.chdir('regression_tests')

if len(sys.argv) > 1:
	latest_rev = int(sys.argv[1])
else:
	latest_rev = 0

	for rev in os.listdir('.'):
		try:
			if not rev.startswith('%s-' % branch_name): continue
			r = int(rev[len(branch_name)+1:])
			if r > latest_rev: latest_rev = r
		except: pass

	if latest_rev == 0:
		print 'no test files found'
		sys.exit(1)

print 'latest version: %d' % latest_rev

html_file = 'index.html'

'''
html_file = '%s.html' % rev_dir
index_mtime = modification_time(html_file)

need_refresh = False

for f in glob.glob(os.path.join(rev_dir, '*.json')):
	mtime = modification_time(f)

	if mtime > index_mtime:
		need_refresh = True
		break

if not need_refresh:
	print 'all up to date'
	sys.exit(0)
'''

html = open(html_file, 'w+')

print >>html, '''<html><head><title>regression tests, %s</title><style type="text/css">
	.passed { display: block; width: 8px; height: 1em; background-color: #6f8 }
	.failed { display: block; width: 8px; height: 1em; background-color: #f68 }
	table { border: 0; border-collapse: collapse; }
	h1 { font-size: 15pt; }
	th { font-size: 8pt; }
	td { border: 0; border-spacing: 0px; padding: 1px 1px 0px 0px; }
	.left-head { white-space: nowrap; }
	</style>
	</head><body><h1>%s - %s</h1>''' % (project_name, project_name, branch_name)

print >>html, '<table border="1">'

details_id = 0
details = []

for r in range(latest_rev, latest_rev - 20, -1):
	sys.stdout.write('.')
	sys.stdout.flush()

	print >>html, '<tr><th colspan="2" style="border:0;">revision %d</th>' % r

	rev_dir = '%s-%d' % (branch_name, r)
	(platforms, tests) = parse_tests(rev_dir)

	for f in tests:
		print >>html, '<th colspan="%d" style="width: %dpx;">%s</th>' % (len(tests[f]), len(tests[f])*9 - 3, f)
	print >>html, '</tr>'

	for p in platforms:
		print >>html, '<tr><th class="left-head" rowspan="%d">%s</th>' % (len(platforms[p]), p)
		idx = 0
		for toolset in platforms[p]:
			if idx > 0: print >>html, '<tr>'
			print >>html, '<th class="left-head">%s</th>' % toolset
			for f in platforms[p][toolset]:
				for t in platforms[p][toolset][f]:
					if platforms[p][toolset][f][t][u'status'] == 0: c = 'passed'
					else: c = 'failed'
					print >>html, '<td title="%s %s"><a class="%s" href="%s"></a></td>' % (t, f, c, os.path.join('logs', '%d.html' % details_id))
					platforms[p][toolset][f][t]['name'] = t
					platforms[p][toolset][f][t]['features'] = f
					details.append(platforms[p][toolset][f][t])
					details_id += 1

			print >>html, '</tr>'
			idx += 1

print >>html, '</table></body></html>'
html.close()

try: os.mkdir('logs')
except: pass

details_id = 0
for d in details:
	html = open(os.path.join('logs', '%d.html' % details_id), 'w+')
	print >>html, '''<html><head><title>%s - %s</title><style type="text/css">
	.compile-error { color: #f13; font-weight: bold; }
	.compile-warning { color: #cb0; }
	.test-error { color: #f13; font-weight: bold; }
	.test-pass { color: #1c2; font-weight: bold; }
	.subtle { color: #ccc; }
	pre { color: #999; }
	</style>
	</head><body><h1>%s - %s</h1>''' % (project_name, branch_name, project_name, branch_name)
	print >>html, '<h3>%s - %s</h3><pre>%s</pre>' % \
		(d['name'].encode('utf8'), d['features'].encode('utf-8'), style_output(d['output']).encode('utf-8'))
	details_id += 1

	print >>html, '</body></html>'
	html.close()
	sys.stdout.write('.')
	sys.stdout.flush()

print ''

