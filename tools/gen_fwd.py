#!/usr/bin/env python

import os
import sys

file_header ='''/*

Copyright (c) 2017, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_FWD_HPP
#define TORRENT_FWD_HPP

#include "libtorrent/export.hpp"

namespace libtorrent {
'''

file_footer = '''

}

#endif // TORRENT_FWD_HPP
'''

classes = os.popen('git grep TORRENT_EXPORT').read().split('\n')

deprecated_classes = os.popen('git grep TORRENT_DEPRECATED_EXPORT').read().split('\n')

def filter_classes(classes, keyword):
	current_file = ''
	ret = ''
	for c in classes:
		line = c.split(':', 1)
		if not line[0].endswith('.hpp'): continue
		# ignore the forward header itself, that's the one we're generating
		if line[0].endswith('/fwd.hpp'): continue
		# don't provide forward declarations of internal types
		if '/aux_/' in line[0]: continue
		this_file = line[0].strip()
		decl = ':'.join(line[1:]).strip().split(' ')

		if decl[0].strip() not in ['struct', 'class']: continue
		# TODO: support TORRENT_DEPRECATED_EXPORT
		if decl[1].strip() != keyword: continue

		if this_file != current_file:
			ret += '\n// ' + this_file + '\n'
		current_file = this_file;
		decl = decl[0] + ' ' + decl[2]
		if not decl.endswith(';'): decl += ';'
		ret += decl + '\n'
	return ret

os.remove('include/libtorrent/fwd.hpp')
with open('include/libtorrent/fwd.hpp', 'w+') as f:
	f.write(file_header)

	f.write(filter_classes(classes, 'TORRENT_EXPORT'));

	f.write('\n#ifndef TORRENT_NO_DEPRECATE\n')

	f.write(filter_classes(deprecated_classes, 'TORRENT_DEPRECATED_EXPORT'));

	f.write('\n#endif // TORRENT_NO_DEPRECATE')

	f.write(file_footer)


