#!/usr/bin/env python3
import os
import runpy

os.chdir('bindings/python')
runpy.run_path('setup.py')
