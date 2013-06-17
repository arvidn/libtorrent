#!/bin/python

import run_tests
import os
import time
import subprocess
import sys

def indent(s):
    s = string.split(s, '\n')
    s = [(3 * ' ') + string.lstrip(line) for line in s]
    s = string.join(s, '\n')
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
	os.system('svn up %d' % revision)

def print_usage():
	print '''usage: run_regression_tests.py [options] toolset [toolset...]

toolset are bjam toolsets. For instance clang, gcc, darwin, msvc etc.
The path "./regression_tests" is expected to be a shared folder
between all testsers.

options:

   -j<n>    use n parallel processes for running tests
'''


def loop():

	if len(sys.argv) < 3:
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
	
		time.sleep(120)

if __name__ == "__main__":
	loop()
