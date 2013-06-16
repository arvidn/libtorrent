#!/bin/python

import run_tests
import os
import time
import subprocess
import sys

# returns a list of new revisions
def svn_fetch():

	current_version = run_tests.svn_info()[0]

	p = subprocess.Popen(['svn', 'up'], stdout=subprocess.PIPE)

	revision = -1

	output = ''
	for l in p.stdout:
		if 'At revision ' in l:
			revision = int(l.split('At revision')[1].strip()[0:-1])
		output += l

	if revision == -1:
		print '\n\nsvn update failed\n\n%s' % ouput
		sys.exit(1)

	return range(current_version + 1, revision + 1)

def svn_up(revision):
	os.system('svn up %d' % revision)

def print_usage():
	print '''usage: run_regression_tests.py remote-path [options] toolset [toolset...]

toolset are bjam toolsets. For instance clang, gcc, darwin, msvc etc.
remote-path is an scp path where the results are copied. This path has
the form: user@hostname:/path

options:

   -j<n>    use n parallel processes for running tests
'''


def loop():
	remote_path = sys.argv[1]
	root_path = os.path.join(os.getcwd(), 'regression_tests')

	if len(sys.argv) < 3:
		print_usage()
		sys.exit(1)

	while True:
		revs = svn_fetch()
		# reverse the list to always run the tests for the
		# latest version first, then fill in with the history
		revs.reverse()

		for r in revs:
			print '\n\nREVISION %d ===\n' % r
			svn_up(r)
	
			run_tests.main(sys.argv[2:])
	
			os.system('scp -r %s %s' % (os.path.join(root_path, '%d' % r), remote_path))

		time.sleep(120)

if __name__ == "__main__":
	loop()
