import os
import libtorrent

print("test_lib_path")
print(libtorrent.__version__)
print(os.path.abspath(libtorrent.__file__))
print(os.path.getctime(libtorrent.__file__))
print(os.path.getmtime(libtorrent.__file__))
