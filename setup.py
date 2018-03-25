#!/usr/bin/env python

import os
os.chdir('bindings/python')
exec(open('setup.py').read())
