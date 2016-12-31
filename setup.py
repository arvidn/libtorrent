#!/usr/bin/env python

import os
import sys

os.chdir('bindings/python')
with open('setup.py') as filename:
    exec(filename.read())
