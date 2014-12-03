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

# TODO: different parsers could be run on output from different actions
# if we would use the xml output instead of stdout/stderr
def style_output(logfile, outfile):
	subtle = False
	for l in logfile.split('\n'):
		l = l.encode('utf-8')
		l = l.replace('<', '&lt;')
		l = l.replace('>', '&gt;')
		if 'TEST_CHECK' in l or \
			'TEST_EQUAL_ERROR' in l or \
			'"ERROR: "' in l or \
			l.startswith('EXIT STATUS: ') or \
			' second time limit exceeded' in l or l.startswith('signal: SIG') or \
			'jump or move depends on uninitialised value(s)' in l or \
			'Invalid read of size' in l or \
			'Invalid write of size' in l or \
			'Use of uninitialised value of size' in l or \
			'Uninitialised byte(s) found during' in l or \
			'points to uninitialised byte(s)' in l:
			print >>outfile, '<span class="test-error">%s</span>' % l
		elif '**passed**' in l:
			print >>outfile, '<span class="test-pass">%s</span>' % l
		elif ': error: ' in l or \
			';1;31merror: ' in l or \
			': fatal error: ' in l or \
			' : fatal error ' in l or \
			'failed to write output file' in l or \
			') : error C' in l or \
			' : error LNK' in l or \
			': undefined reference to ' in l:
			print >>outfile, '<span class="compile-error">%s</span>' % l
		elif ': warning: ' in l or \
			') : warning C' in l or \
			'0;1;35mwarning: ' in l or \
			'Uninitialised value was created by a' in l or \
			'bytes after a block of size' in l or \
			'bytes inside a block of size' in l:
			print >>outfile, '<span class="compile-warning">%s</span>' % l.strip()
		elif l == '====== END OUTPUT ======' and not subtle:
			print >>outfile, '<span class="subtle">%s' % l
			subtle = True
		else:
			print >>outfile, '%s' % l
	if subtle: print >>outfile, '</span>'

def modification_time(file):
	mtime = 0
	try:
		mtime = os.stat(file).st_mtime
	except: pass
	return mtime

def save_log_file(log_name, project_name, branch_name, test_name, timestamp, data):

	if not os.path.exists(os.path.split(log_name)[0]):
		os.mkdir(os.path.split(log_name)[0])

	try:
		# if the log file already exists, and it's newer than
		# the source, no need to re-parse it
		mtime = os.stat(log_name).st_mtime
		if mtime >= timestamp: return
	except: pass

	html = open(log_name, 'w+')
	print >>html, '''<html><head><title>%s - %s</title><style type="text/css">
	.compile-error { color: #f13; font-weight: bold; }
	.compile-warning { font-weight: bold; color: black; }
	.test-error { color: #f13; font-weight: bold; }
	.test-pass { color: #1c2; font-weight: bold; }
	.subtle { color: #ddd; }
	pre { color: #999; white-space: pre-wrap; word-wrap: break-word; }
	</style>
	</head><body><h1>%s - %s</h1>''' % (project_name, branch_name, project_name, branch_name)
	print >>html, '<h3>%s</h3><pre>' % test_name.encode('utf-8')
	style_output(data, html)

	print >>html, '</pre></body></html>'
	html.close()
	sys.stdout.write('.')
	sys.stdout.flush()

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
		try:
			j = json.loads(open(f, 'rb').read())
			timestamp = os.stat(f).st_mtime
		except Exception, e:
			print '\nFAILED TO LOAD "%s": %s\n' % (f, e)
			continue
	
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
			platforms[platform][toolset][features][test_name]['timestamp'] = timestamp

	return (platforms, tests)


# TODO: remove this dependency by encoding it in the output files
# this script should work from outside of the repo, just having
# access to the shared folder
project_name = 'libtorrent'

# maps branch name to latest rev
revs = {}

input_dir = os.path.abspath('regression_tests')

for rev in os.listdir(input_dir):
	try:
		branch = rev.split('-')[0]
		if branch == 'logs': continue
		r = int(rev.split('-')[1])
		if not branch in revs:
			revs[branch] = r
		else:
			if r > revs[branch]:
				revs[branch] = r
	except:
		print 'ignoring %s' % rev

if revs == {}:
	print 'no test files found'
	sys.exit(1)

print 'latest versions'
for b in revs:
	print '%s\t%d' % (b, revs[b])

try: os.mkdir('regression_test_report')
except: pass

os.chdir('regression_test_report')

for branch_name in revs:

	latest_rev = revs[branch_name]

	html_file = '%s.html' % branch_name

	html = open(html_file, 'w+')

	print >>html, '''<html><head><title>regression tests, %s</title><style type="text/css">
		.passed { display: block; width: 6px; height: 1em; background-color: #6f8 }
		.failed { display: block; width: 6px; height: 1em; background-color: #f68 }
		.crash { display: block; width: 6px; height: 1em; background-color: #f08 }
		.compile-failed { display: block; width: 6px; height: 1em; background-color: #000 }
		.timeout { display: block; width: 6px; height: 1em; background-color: #86f }
		.valgrind-error { display: block; width: 6px; height: 1em; background-color: #f80 }
		table { border: 0; border-collapse: collapse; }
		h1 { font-size: 15pt; }
		th { font-size: 8pt; }
		td { border: 0; border-spacing: 0px; padding: 0px 0px 0px 0px; }
		.left-head { white-space: nowrap; }
		</style>
		</head><body><h1>%s - %s</h1>''' % (project_name, project_name, branch_name)

	print >>html, '<table border="1">'

	num_printed_revs = 0;
	for r in range(latest_rev, latest_rev - 40, -1):
		sys.stdout.write('.')
		sys.stdout.flush()

		rev_dir = os.path.join(input_dir, '%s-%d' % (branch_name, r))
		(platforms, tests) = parse_tests(rev_dir)

		if len(tests) + len(platforms) == 0: continue

		print >>html, '<tr><th colspan="2" style="border:0;">revision %d</th>' % r

		features = tests.keys()
		features = sorted(features, key=lambda x: len(tests[x]))

		for f in features:
			title = f
			if len(tests[f]) < 10: title = '#'
			print >>html, '<th colspan="%d" style="width: %dpx;">%s</th>' % (len(tests[f]), len(tests[f])*6 - 5, title)
		print >>html, '</tr>'

		for p in platforms:
			print >>html, '<tr><th class="left-head" rowspan="%d">%s</th>' % (len(platforms[p]), p)
			idx = 0
			for toolset in platforms[p]:
				if idx > 0: print >>html, '<tr>'
				log_dir = 'logs-%s-%d' % (branch_name, r)
				if not os.path.exists(log_dir):
					os.mkdir(log_dir)
				details_name = os.path.join(log_dir, '%s-%s.html' % (p, toolset))
				details_file = open(details_name, 'w+')

				print >>details_file, '''<html><head><title>%s %s [%s]</title><style type="text/css">
					.passed { background-color: #6f8 }
					.failed { background-color: #f68 }
					.missing { background-color: #fff }
					.crash { background-color: #f08 }
					.compile-failed { background-color: #000 }
					.timeout { background-color: #86f }
					.valgrind-error { background-color: #f80 }
					table { border: 0; border-collapse: collapse; display: inline-block; }
					th { font-size: 15pt; width: 18em; }
					td { border: 0; border-spacing: 0px; padding: 1px 0px 0px 1px; }
					</style>
					</head><body>''' % (p, toolset, branch_name)
				print >>html, '<th class="left-head"><a href="%s">%s</a></th>' % (details_name, toolset)

				deferred_end_table = False
				for f in features:
					title = f
					if len(tests[f]) < 10: title = '#'

					if title != '#':
						if deferred_end_table:
							print >>details_file, '</table><table>'
							print >>details_file, '<tr><th>%s</th></tr>' % title
							deferred_end_table = False
						else:
							print >>details_file, '<table>'
							print >>details_file, '<tr><th>%s</th></tr>' % title
					elif not deferred_end_table:
						print >>details_file, '<table>'
						print >>details_file, '<tr><th>%s</th></tr>' % title

					if not f in platforms[p][toolset]:
						for i in range(len(tests[f])):
							print >>html, '<td title="%s"><a class="missing"></a></td>' % (f)
						continue

					for t in platforms[p][toolset][f]:
						details = platforms[p][toolset][f][t]
						exitcode = details['status']

						if exitcode == 0:
							error_state = 'passed'
							c = 'passed'
						elif exitcode == 222:
							error_state = 'valgrind error'
							c = 'valgrind-error'
						elif exitcode == 139 or \
							exitcode == 138:
							error_state = 'crash'
							c = 'crash'
						elif exitcode == -1073740777:
							error_state = 'timeout'
							c = 'timeout'
						elif exitcode == 333 or \
							exitcode == 77:
							error_code = 'test-failed'
							c = 'failed'
						else:
							error_state = 'compile-failed (%d)' % exitcode
							c = 'compile-failed'

						log_name = os.path.join('logs-%s-%d' % (branch_name, r), p + '~' + toolset + '~' + t + '~' + f.replace(' ', '.') + '.html')
						print >>html, '<td title="%s %s"><a class="%s" href="%s"></a></td>' % (t, f, c, log_name)
						print >>details_file, '<tr><td class="%s"><a href="%s">%s [%s]</a></td></tr>' % (c, os.path.split(log_name)[1], t, error_state)
						save_log_file(log_name, project_name, branch_name, '%s - %s' % (t, f), int(details['timestamp']), details['output'])
					if title != '#':
						print >>details_file, '</table>'
						deferred_end_table = False
					else:
						deferred_end_table = True

				if deferred_end_table:
					print >>details_file, '</table>'

				print >>html, '</tr>'
				idx += 1
				print >>details_file, '</body></html>'
				details_file.close()
		num_printed_revs += 1
		if num_printed_revs >= 20: break

	print >>html, '</table></body></html>'
	html.close()

	print ''

