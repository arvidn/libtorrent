#!/usr/bin/env python3

import os

os.chdir('bindings/python')
exec(open('setup.py').read())
