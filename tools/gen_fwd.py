#!/usr/bin/env python3
# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4

import os

file_header = '''/*

Copyright (c) 2017-2018, Steven Siloti
Copyright (c) 2017-2021, Arvid Norberg
Copyright (c) 2020, Alden Torres
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

#include "libtorrent/config.hpp"

namespace libtorrent {
'''

file_footer = '''

}

namespace lt = libtorrent;

#endif // TORRENT_FWD_HPP
'''

classes = os.popen(
    r'git grep "\(TORRENT_EXPORT\|TORRENT_DEPRECATED_EXPORT\|^TORRENT_[A-Z0-9]\+_NAMESPACE\)"').read().split('\n')


def print_classes(out, classes, keyword):
    current_file = ''

    # [(file, decl), ...]
    classes = [(x.split(':')[0].strip(), ':'.join(x.split(':')[1:]).strip()) for x in classes]

    # we only care about header files
    # ignore the forward header itself, that's the one we're generating
    # also ignore any header in the aux_ directory, those are private
    classes = [x for x in classes if x[0].endswith('.hpp') and not x[0].endswith('/fwd.hpp') and '/aux_/' not in x[0]]

    namespaces = ['TORRENT_VERSION_NAMESPACE_3',
                  'TORRENT_VERSION_NAMESPACE_3_END',
                  'TORRENT_VERSION_NAMESPACE_2',
                  'TORRENT_VERSION_NAMESPACE_2_END',
                  'TORRENT_CRYPTO_NAMESPACE',
                  'TORRENT_CRYPTO_NAMESPACE_END']

    # only include classes with the right kind of export
    classes = [
        x for x in classes if x[1] in namespaces or (
            x[1].split(' ')[0] in [
                'class',
                'struct'] and x[1].split(' ')[1] == keyword)]

    # collapse empty namespaces
    classes2 = []
    skip = 0
    for i in range(len(classes)):
        if skip > 0:
            skip -= 1
            continue
        if classes[i][1] in namespaces \
                and len(classes) > i + 1 \
                and classes[i + 1][1] == ('%s_END' % classes[i][1]):
            skip = 1
        else:
            classes2.append(classes[i])

    classes = classes2

    idx = -1
    for line in classes:
        idx += 1
        this_file = line[0]
        decl = line[1].split(' ')

        content = ''
        if this_file != current_file:
            out.write('\n// ' + this_file + '\n')
        current_file = this_file
        if len(decl) > 2 and decl[0] in ['struct', 'class']:
            decl = decl[0] + ' ' + decl[2]
            if not decl.endswith(';'):
                decl += ';'
            content = decl + '\n'
        else:
            content = line[1] + '\n'

        if 'kademlia' in this_file:
            out.write('namespace dht {\n')
            out.write(content)
            out.write('}\n')
        else:
            out.write(content)


os.remove('include/libtorrent/fwd.hpp')
with open('include/libtorrent/fwd.hpp', 'w+') as f:
    f.write(file_header)

    print_classes(f, classes, 'TORRENT_EXPORT')

    f.write('\n#if TORRENT_ABI_VERSION <= 2\n')

    print_classes(f, classes, 'TORRENT_DEPRECATED_EXPORT')

    f.write('\n#endif // TORRENT_ABI_VERSION')

    f.write(file_footer)
