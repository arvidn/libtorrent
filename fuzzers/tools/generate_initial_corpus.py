import os
import shutil
import hashlib
from random import shuffle
import struct

corpus_dirs = [
    'torrent_info', 'upnp', 'gzip', 'base32decode', 'base32encode',
    'base64encode', 'bdecode_node', 'convert_from_native', 'convert_to_native',
    'dht_node', 'escape_path', 'escape_string', 'file_storage_add_file',
    'http_parser', 'lazy_bdecode', 'parse_int', 'parse_magnet_uri', 'resume_data',
    'sanitize_path', 'utf8_codepoint', 'utf8_wchar', 'utp',
    'verify_encoding', 'wchar_utf8', 'peer_conn']

for p in corpus_dirs:
    try:
        os.makedirs(os.path.join('corpus', p))
    except Exception as e:
        print(e)

torrent_dir = '../test/test_torrents'
for f in os.listdir(torrent_dir):
    shutil.copy(os.path.join(torrent_dir, f), os.path.join('corpus', 'torrent_info'))

xml_tests = [
    '<a blah="b"></a>', '<a b=c></a>', '<a b"c"></a>', '<a b="c></a>',
    '<![CDATA[<sender>John Smith</sender>]]>', '<![CDATA[<sender>John S',
    '<!-- comment -->', '<empty></empty>', '<tag',
    '''<?xml version="1.0" encoding="ISO-8859-1" ?>
<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema"></xs:schema>''',
    '<selfclosing />']

for x in xml_tests:
    name = hashlib.sha1(x).hexdigest()
    with open(os.path.join('corpus', 'upnp', name), 'w+') as f:
        f.write(x)

gzip_dir = '../test'
for f in ['zeroes.gz', 'corrupt.gz', 'invalid1.gz']:
    shutil.copy(os.path.join(gzip_dir, f), os.path.join('corpus', 'gzip'))

# generate peer protocol messages
messages = []


def add_length(msg):
    return struct.pack('>I', len(msg)) + msg


def add_reserved(msg):
    return '\0\0\0\0\0\x18\0\x05' + msg


# extended handshake
def add_extended_handshake(msg):
    ext_handshake = 'd1:md11:ut_metadatai1e11:lt_donthavei2e12:ut_holepunch' + \
        'i3e11:upload_onlyi4ee11:upload_onlyi1e10:share_modei1e4:reqqi1234e6:yourip4:0000e'
    return add_length(struct.pack('BB', 20, 0) + ext_handshake) + msg


# request
for i in range(101):
    for j in range(-1, 1):
        messages.append(add_length(struct.pack('>Biii', 6, i, j, 0x4000)))

# cancel
for i in range(101):
    for j in range(-1, 1):
        messages.append(add_length(struct.pack('>Biii', 8, i, j, 0x4000)))

# piece
for i in range(101):
    messages.append(add_length(struct.pack('>Bii', 7, i, 0) + ('a' * 0x4000)))

# single-byte
for i in range(256):
    messages.append(add_length(struct.pack('B', i)))

# reject
for i in range(101):
    messages.append(add_length(struct.pack('>Biii', 16, i, 0, 0x4000)))

# suggest
for i in range(101):
    messages.append(add_length(struct.pack('>Bi', 13, i)))

# allow-fast
for i in range(101):
    messages.append(add_length(struct.pack('>Bi', 17, i)))

# have
for i in range(101):
    messages.append(add_length(struct.pack('>Bi', 4, i)))

# DHT-port
for i in range(101):
    messages.append(add_length(struct.pack('>BH', 9, i * 10)))

# hash request
for i in range(-10, 200, 20):
    for j in range(-1, 1):
        for k in range(-1, 1):
            for l in range(-1, 1):
                for m in range(-1, 1):
                    messages.append(add_length(struct.pack('>Biiiii', 21, i, j, k, l, m)))

# hash reject
for i in range(-10, 200, 20):
    for j in range(-1, 1):
        for k in range(-1, 1):
            for l in range(-1, 1):
                for m in range(-1, 1):
                    messages.append(add_length(struct.pack('>Biiiii', 23, i, j, k, l, m)))

# hash
for i in range(-10, 200, 20):
    for j in range(-1, 1):
        messages.append(add_length(struct.pack('>Biiiii', 22, i, j, 0, 2, 0) + ('0' * 32 * 5)))

# lt_dont_have
messages.append(add_extended_handshake(add_length(struct.pack('>BBi', 20, 7, -1))))
messages.append(add_extended_handshake(add_length(struct.pack('>BBi', 20, 7, 0))))
messages.append(add_extended_handshake(add_length(struct.pack('>BBi', 20, 7, 0x7fffffff))))

# share mode
messages.append(add_extended_handshake(add_length(struct.pack('BBB', 20, 8, 255))))
messages.append(add_extended_handshake(add_length(struct.pack('BBB', 20, 8, 0))))
messages.append(add_extended_handshake(add_length(struct.pack('BBB', 20, 8, 1))))

# holepunch
for i in range(0, 2):
    for j in range(0, 1):
        messages.append(add_extended_handshake(add_length(struct.pack('>BBBBiH', 20, 4, i, j, 0, 0))))
        messages.append(add_extended_handshake(add_length(struct.pack('>BBBBiiH', 20, 4, i, j, 0, 0, 0))))

# upload only
for i in range(0, 1):
    messages.append(add_extended_handshake(add_length(struct.pack('BBB', 20, 3, i))))

# bitfields
bitfield_len = (100 + 7) // 8

for i in range(256):
    messages.append(add_length(struct.pack('B', 5) + (chr(i) * bitfield_len)))

mixes = []

for i in range(200):
    shuffle(messages)
    mixes.append(''.join(messages[1:20]))

messages += mixes

for m in messages:
    f = open('corpus/peer_conn/%s' % hashlib.sha1(m).hexdigest(), 'w+')
    f.write(add_reserved(m))
    f.close()
