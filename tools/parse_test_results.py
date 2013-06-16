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

def style_output(o):
	ret = ''
	subtle = False
	for l in o.split('\n'):
		if 'TEST_CHECK' in l or 'TEST_EQUAL_ERROR' in l or l.startswith('EXIT STATUS: '):
			ret += '<span class="test-error">%s</span>\n' % l
		elif '**passed**' in l:
			ret += '<span class="test-pass">%s</span>\n' % l
		elif ': error: ' in l or ': fatal error: ' in l:
			ret += '<span class="compile-error">%s</span>\n' % l
		elif ': warning: ' in l:
			ret += '<span class="compile-warning">%s</span>\n' % l
		elif l == '====== END OUTPUT ======' and not subtle:
			ret += '<span class="subtle">%s\n' % l
			subtle = True
		else:
			ret += '%s\n' % l
	if subtle: ret += '</span>'
	return ret

project_name = ''

try:
	cfg = open('.regression.yml', 'r')
except:
	print '.regression.yml not found in current directory'
	sys.exit(1)
cfg = yaml.load(cfg.read())
if 'project' in cfg:
	project_name = cfg['project']

os.chdir('regression_tests')

def modification_time(file):
	mtime = 0
	try:
		mtime = os.stat(file).st_mtime
	except: pass
	return mtime

index_mtime = modification_time('index.html')

latest_rev = 0

for rev in os.listdir('.'):
	try:
		r = int(rev)
		if r > latest_rev: latest_rev = r
	except: pass

if latest_rev == 0:
	print 'no test files found'
	sys.exit(1)

rev_dir = '%d' % latest_rev

need_refresh = False

for f in glob.glob(os.path.join(rev_dir, '*.json')):
	mtime = modification_time(f)

	if mtime > index_mtime:
		need_refresh = True
		break

if not need_refresh:
	print 'all up to date'
	sys.exit(0)

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
#           warnings: 21
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
	

html = open('index.html', 'w')

print >>html, '''<html><head><title>regression tests, %s revision %d</title><style type="text/css">
	.passed { display: block; width: 5px; height: 1em; background-color: #6f8 }
	.failed { display: block; width: 5px; height: 1em; background-color: #f68 }
	table { border: 0; }
	td { border: 0; border-spacing: 0px; padding: 0px 0px 0px 0px; }
	th { white-space: nowrap; }
	.compile-error { color: #f13; font-weight: bold; }
	.compile-warning { color: #cb0; }
	.test-error { color: #f13; font-weight: bold; }
	.test-pass { color: #1c2; font-weight: bold; }
	.subtle { color: #ccc; }
	pre { color: #999; }
	</style><script type="text/javascript">
	var expanded = -1;
	function toggle(i) {
		if (expanded != -1) document.getElementById(expanded).style.display = 'none';
		expanded = i;
		document.getElementById(i).style.display = 'block';
	}
	</script></head><body>''' % (project_name, latest_rev)

print >>html, '<h1>%s revision %d</h1>' % (project_name, latest_rev)
print >>html, '<table border="1"><tr><th colspan="2" style="border:0;"></th>'

for f in tests:
	print >>html, '<th colspan="%d">%s</th>' % (len(tests[f]), f)
print >>html, '</tr>'

details_id = 0
details = []

for p in platforms:
	print >>html, '<tr><th rowspan="%d">%s</th>' % (len(platforms[p]), p)
	idx = 0
	for toolset in platforms[p]:
		if idx > 0: print >>html, '<tr>'
		print >>html, '<th>%s</th>' % toolset
		for f in platforms[p][toolset]:
			for t in platforms[p][toolset][f]:
				if platforms[p][toolset][f][t][u'status'] == 0: c = 'passed'
				else: c = 'failed'
				print >>html, '<td title="%s"><a class="%s" href="javascript:toggle(%d)"></a></td>' % ('%s %s' % (t, f), c, details_id)
				platforms[p][toolset][f][t]['name'] = t
				platforms[p][toolset][f][t]['features'] = f
				details.append(platforms[p][toolset][f][t])
				details_id += 1

		print >>html, '</tr>'
		idx += 1

print >>html, '</table>'

details_id = 0
for d in details:
	print >>html, '<div style="display: none" id="%d"><h3>%s - %s</h3><pre>%s</pre></div>' % \
		(details_id, d['name'].encode('utf8'), d['features'].encode('utf-8'), style_output(d['output']).encode('utf-8'))
	details_id += 1

print >>html, '</body></html>'

html.close()

