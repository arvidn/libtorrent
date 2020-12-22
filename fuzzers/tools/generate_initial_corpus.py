import os
import shutil
import hashlib
import struct
import random

corpus_dirs = [
    'torrent_info', 'upnp', 'gzip', 'base32decode', 'base32encode',
    'base64encode', 'bdecode_node', 'convert_from_native', 'convert_to_native',
    'dht_node', 'escape_path', 'escape_string', 'file_storage_add_file',
    'http_parser', 'lazy_bdecode', 'parse_int', 'parse_magnet_uri', 'resume_data',
    'sanitize_path', 'utf8_codepoint', 'utp',
    'verify_encoding', 'peer_conn', 'add_torrent', 'idna', 'parse_url', 'http_tracker']

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
    name = hashlib.sha1(x.encode('ascii')).hexdigest()
    with open(os.path.join('corpus', 'upnp', name), 'w+') as f:
        f.write(x)

gzip_dir = '../test'
for f in ['zeroes.gz', 'corrupt.gz', 'invalid1.gz']:
    shutil.copy(os.path.join(gzip_dir, f), os.path.join('corpus', 'gzip'))

idna = ['....', 'xn--foo-.bar', 'foo.xn--bar-.com', 'Xn--foobar-', 'XN--foobar-', '..xnxn--foobar-']

counter = 0
for i in idna:
    open(os.path.join('corpus', 'idna', '%d' % counter), 'w+').write(i)
    counter += 1

urls = ['https://user:password@example.com:8080/path?query']

counter = 0
for i in urls:
    open(os.path.join('corpus', 'parse_url', '%d' % counter), 'w+').write(i)
    counter += 1

counter = 0
tracker_fields = ['interval', 'min interval', 'tracker id', 'failure reason',
    'warning message', 'complete', 'incomplete', 'downloaded', 'downloaders', 'external ip']
tracker_values = ['i-1e', 'i0e', 'i1800e', '6:foobar', 'de', '0:', 'le']
peer_fields = ['peer id', 'ip', 'port']
peer_values = ['i-1e', 'i0e', 'i1800e', '6:foobar', 'de', '0:', 'le', '9:127.0.0.1']

for i in range(1000):
    tracker_msg = 'd'
    for f in tracker_fields:
        tracker_msg += '%d:' % len(f) + f
        tracker_msg += random.choice(tracker_values)

    tracker_msg += '5:filesd20:ababababababababababd'
    for f in tracker_fields:
        tracker_msg += '%d:' % len(f) + f
        tracker_msg += random.choice(tracker_values)
    tracker_msg += 'ee'

    tracker_msg += '5:peers'
    if random.getrandbits(1) == 0:
        tracker_msg += 'l'
        for k in range(10):
            tracker_msg += 'd'
            for f in peer_fields:
                tracker_msg += '%d:' % len(f) + f
                tracker_msg += random.choice(peer_values)
            tracker_msg += 'e'
        tracker_msg += 'e'
    else:
        tracker_msg += '60:'
        for k in range(6*10):
            tracker_msg += chr(random.getrandbits(8))

    tracker_msg += '6:peers6'
    tracker_msg += '180:'
    for k in range(18*10):
        tracker_msg += chr(random.getrandbits(8))

    tracker_msg += 'e'
    open(os.path.join('corpus', 'http_tracker', '%d' % counter), 'w+').write(tracker_msg)
    counter += 1

# generate peer protocol messages
messages = []


def add_length(msg):
    return struct.pack('>I', len(msg)) + msg


def add_reserved(msg):
    return b'\0\0\0\0\0\x18\0\x05' + msg


# extended handshake
def add_extended_handshake(msg):
    ext_handshake = b'd1:md11:ut_metadatai1e11:lt_donthavei2e12:ut_holepunch' + \
        b'i3e11:upload_onlyi4ee11:upload_onlyi1e10:share_modei1e4:reqqi1234e6:yourip4:0000e'
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
    messages.append(add_length(struct.pack('>Bii', 7, i, 0) + (b'a' * 0x4000)))

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
            for m in range(-1, 1):
                for n in range(-1, 1):
                    messages.append(add_length(struct.pack('>Biiiii', 21, i, j, k, m, n)))

# hash reject
for i in range(-10, 200, 20):
    for j in range(-1, 1):
        for k in range(-1, 1):
            for m in range(-1, 1):
                for n in range(-1, 1):
                    messages.append(add_length(struct.pack('>Biiiii', 23, i, j, k, m, n)))

# hash
for i in range(-10, 200, 20):
    for j in range(-1, 1):
        messages.append(add_length(struct.pack('>Biiiii', 22, i, j, 0, 2, 0) + (b'0' * 32 * 5)))

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
    messages.append(add_length(struct.pack('B', 5) + (struct.pack('B', i) * bitfield_len)))

mixes = []

for i in range(200):
    random.shuffle(messages)
    mixes.append(b''.join(messages[1:20]))

messages += mixes

for m in messages:
    f = open('corpus/peer_conn/%s' % hashlib.sha1(m).hexdigest(), 'wb+')
    f.write(add_reserved(m))
    f.close()
