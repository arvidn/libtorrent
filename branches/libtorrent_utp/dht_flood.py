#! /usr/bin/env python

import socket
import sys
from types import StringType, IntType, LongType, DictType, ListType, TupleType
import random

port = int(sys.argv[1])

# from BitTorrent 4.3.0
def encode_bencached(x,r):
    r.append(x.bencoded)

def encode_int(x, r):
    r.extend(('i', str(x), 'e'))

def encode_string(x, r):
    r.extend((str(len(x)), ':', x))

def encode_list(x, r):
    r.append('l')
    for i in x:
        encode_func[type(i)](i, r)
    r.append('e')

def encode_dict(x,r):
    r.append('d')
    ilist = x.items()
    ilist.sort()
    for k, v in ilist:
        r.extend((str(len(k)), ':', k))
        encode_func[type(v)](v, r)
    r.append('e')

encode_func = {}
encode_func[IntType] = encode_int
encode_func[LongType] = encode_int
encode_func[StringType] = encode_string
encode_func[ListType] = encode_list
encode_func[TupleType] = encode_list
encode_func[DictType] = encode_dict

def bencode(x):
    r = []
    encode_func[type(x)](x, r)
    return ''.join(r)

def send_dht_message(msg):
	s.sendto(bencode(msg), 0, ('127.0.0.1', port))

def random_key():
	ret = ''
	for i in range(0, 20):
		ret += chr(random.randint(0, 255))
	return ret

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
node_id = '1' * 20;
query = 'get_peers'

print 'test random info-hashes'
for i in xrange(1, 30000):
	send_dht_message({'a': {'id': node_id, 'info_hash': random_key()}, 'q': query, 'y': 'q', 't': '%d' % i})

print 'test random peer-ids'
for i in xrange(1, 30000):
	send_dht_message({'a': {'id': random_key(), 'info_hash': random_key()}, 'q': query, 'y': 'q', 't': '%d' % i})

