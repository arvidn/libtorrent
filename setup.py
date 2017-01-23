#!/usr/bin/env python

import os

os.chdir('bindings/python')
with open('setup.py') as filename:
    exec(filename.read())
