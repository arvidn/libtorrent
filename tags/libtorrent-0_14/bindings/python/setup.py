#!/usr/bin/env python

from distutils.core import setup, Extension
import commands
import os

def pkgconfig(*packages, **kw):
    flag_map = {'-I': 'include_dirs', '-L': 'library_dirs', '-l': 'libraries' }
    for token in commands.getoutput("pkg-config --libs --cflags %s" % ' '.join(packages)).split():
        if flag_map.has_key(token[:2]):
            kw.setdefault(flag_map.get(token[:2]), []).append(token[2:])
        else: # throw others to extra_link_args
            kw.setdefault('extra_link_args', []).append(token)
    for k, v in kw.iteritems(): # remove duplicated
        kw[k] = list(set(v))
    return kw

def build_extension():
    this_dir = os.path.dirname(__file__)
    source_list = os.listdir(os.path.join(this_dir, "src"))
    source_list = [os.path.join("src", s) for s in source_list if s.endswith(".cpp")]

    libtorrent_pkg_config = pkgconfig('libtorrent-rasterbar', libraries=['boost_python-mt'])

    return Extension(
                'libtorrent',
                sources = source_list,
                **libtorrent_pkg_config
            )

libtorrent_extension = build_extension()

setup( name = 'py-libtorrent',
       version = '0.14',        
       description = 'Python bindings for libtorrent (rasterbar)',
       author = 'Arvid Norberg',
       url = 'http://www.rasterbar.com/products/libtorrent/index.html',
       ext_modules = [libtorrent_extension]
)
