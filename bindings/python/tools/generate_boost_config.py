from sysconfig import get_paths
import platform
import shutil
import sys

paths = get_paths()

version = sys.version[:3]
executable = sys.executable.replace('\\', '/')
include = paths['include'].replace('\\', '/')

filename = sys.argv[1]
config = ' : '.join(['using python', version, executable, include]) + ' ;\n'

if shutil.which('ccache'):
    if platform.system() == 'Linux':
        config += 'using gcc : : ccache g++ ;\n'
    elif platform.system() == 'Darwin':
        config += 'using darwin : : ccache clang++ ;\n'

if shutil.which('sccache') and platform.system() == 'Windows':
    config += 'using msvc : : sccache cl ;\n'

with open(filename, 'w') as file:
    print(config)
    file.write(config)
