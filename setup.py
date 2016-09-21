#!/usr/bin/env python

import os
import sys

if sys.version_info >= (3,0):
    def execfile(filename):
        exec(compile(open(filename, "rb").read(), filename, 'exec'), globals, locals)

os.chdir('bindings/python')
execfile('setup.py')
