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

import run_tests
import os
import time
import subprocess
import sys

def indent(s):
    s = s.split('\n')
    s = [(3 * ' ') + line.lstrip() for line in s]
    s = '\n'.join(s)
    return s

# returns a list of new revisions
def svn_fetch(last_rev):

	if os.system('svn up') != 0:
		print 'svn up failed'
		return []

# log command and output
# $ svn log -l10 --incremental -q
# ------------------------------------------------------------------------
# r9073 | arvidn | 2013-10-04 21:49:00 -0700 (Fri, 04 Oct 2013)
# ------------------------------------------------------------------------
# r9072 | arvidn | 2013-10-04 21:18:24 -0700 (Fri, 04 Oct 2013)
# ------------------------------------------------------------------------
# r9068 | arvidn | 2013-10-04 08:51:32 -0700 (Fri, 04 Oct 2013)
# ------------------------------------------------------------------------
# r9067 | arvidn | 2013-10-04 08:45:47 -0700 (Fri, 04 Oct 2013)
# ------------------------------------------------------------------------

	p = subprocess.Popen(['svn', 'log', '-l10', '--incremental', '-q'], stdout=subprocess.PIPE)

	revision = -1

	output = ''
	ret = []
	for l in p.stdout:
		if not l.startswith('r'): continue
		rev = int(l.split(' ')[0][1:])
		if rev == last_rev: break
		ret.append(rev)

	print 'svn up: ',
	for r in ret: print '%d ' % r,
	print ''
	return ret

def svn_up(revision):
	os.system('svn up -r %d' % revision)

def print_usage():
	print '''usage: run_regression_tests.py [options] toolset [toolset...]

toolset are bjam toolsets. For instance clang, gcc, darwin, msvc etc.
The path "./regression_tests" is expected to be a shared folder
between all testsers.

options:

   -j<n>     use n parallel processes for running tests
   -i        build incrementally (i.e. don't clean between checkouts)
   -valgrind run tests with valgrind (requires valgrind to be installed)
   -s        skip. always run tests on the latest version
'''


def loop():

	if len(sys.argv) < 2:
		print_usage()
		sys.exit(1)

	skip = '-s' in sys.argv

	rev_file = os.path.join(os.getcwd(), '.rev')
	if skip:
		sys.argv.remove('-s')
	print 'restoring last state from "%s"' % rev_file

	try:
		last_rev = int(open(rev_file, 'r').read())
	except:
		last_rev = run_tests.svn_info()[0] - 1
		open(rev_file, 'w+').write('%d' % last_rev)

	revs = []

	while True:
		new_revs = svn_fetch(last_rev)

		if len(new_revs) > 0:
			revs = new_revs + revs

		# in skip mode, only ever run the latest version
		if skip and len(revs): revs = revs[:1]

		if revs == []:
			time.sleep(300)
			continue

		print 'revs: ',
		for r in revs: print '%d ' % r,
		print ''

		r = revs[0]
		print '\n\nREVISION %d ===\n' % r
		svn_up(r)
	
		try:
			run_tests.main(sys.argv[1:])
			last_rev = r;

			# pop the revision we just completed
			revs = revs[1:]

			open(rev_file, 'w+').write('%d' % last_rev)
		except Exception, e:
			print e

if __name__ == "__main__":
	loop()
