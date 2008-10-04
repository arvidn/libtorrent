#!/usr/bin/env python

from distutils.core import setup, Extension
import commands

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


ltmod = Extension(
            'libtorrent',
            sources = [ 'src/alert.cpp',
                        'src/big_number.cpp',   
                        'src/converters.cpp',   
                        'src/datetime.cpp',     
                        'src/docstrings.cpp',   
                        'src/entry.cpp',        
                        'src/extensions.cpp',   
                        'src/filesystem.cpp',   
                        'src/fingerprint.cpp',  
                        'src/ip_filter.cpp',       
                        'src/magnet_uri.cpp',       
                        'src/module.cpp',       
                        'src/peer_info.cpp',    
                        'src/peer_plugin.cpp',  
                        'src/session.cpp',      
                        'src/session_settings.cpp',
                        'src/torrent.cpp',      
                        'src/torrent_handle.cpp',
                        'src/torrent_info.cpp', 
                        'src/torrent_status.cpp',
                        'src/utility.cpp',      
                        'src/version.cpp' ],    
            **pkgconfig('libtorrent-rasterbar',
                libraries = [ 'boost_python-mt-1_35' ], 
            )           
        );      

setup(  name = 'py-libtorrent',
        version = '0.14',        
        description = 'Python bindings for libtorrent (rasterbar)',
        author = 'Arvid Norberg',
        ext_modules = [ltmod],
        include_dirs = ['/opt/local/include/boost-1_35'])

