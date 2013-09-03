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

	p = subprocess.Popen(['svn', 'up'], stdout=subprocess.PIPE)

	revision = -1

	output = ''
	for l in p.stdout:
		if 'At revision ' in l:
			revision = int(l.split('At revision')[1].strip()[0:-1])
		if 'Updated to revision ' in l:
			revision = int(l.split('Updated to revision')[1].strip()[0:-1])
		output += l

	if revision == -1:
		print '\n\nsvn update failed\n\n%s' % indent(output)
		return []

	return range(last_rev + 1, revision + 1)

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
'''


def loop():

	if len(sys.argv) < 2:
		print_usage()
		sys.exit(1)

	rev_file = os.path.join(os.getcwd(), '.rev')
	print 'restoring last state from "%s"' % rev_file

	try:
		last_rev = int(open(rev_file, 'r').read())
	except:
		last_rev = run_tests.svn_info()[0] - 1
		open(rev_file, 'w+').write('%d' % last_rev)

	while True:
		revs = svn_fetch(last_rev)

		for r in revs:
			print '\n\nREVISION %d ===\n' % r
			svn_up(r)
	
			run_tests.main(sys.argv[1:])
			last_rev = r;

			open(rev_file, 'w+').write('%d' % last_rev)
	
		if revs == []: time.sleep(300)

if __name__ == "__main__":
	loop()
