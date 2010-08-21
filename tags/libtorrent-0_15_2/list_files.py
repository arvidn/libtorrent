#! /usr/bin/env python
# Copyright Arvid Norberg 2008. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

import os
import sys

def list_directory(path):
	tree = os.walk(path)
	for i in tree:
		dirs = i[0].split('/')
		if 'CVS' in dirs: continue
		if '.svn' in dirs: continue

		for file in i[2]:
			if file.startswith('.#'): continue
			if file == '.DS_Store': continue
			print os.path.join(i[0], file) + ' \\'

list_directory(sys.argv[1])

