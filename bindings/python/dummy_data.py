import hashlib
import random

import libtorrent as lt

PIECE_LENGTH = 16384
NAME = b"test.txt"
LEN = PIECE_LENGTH * 9 + 1000
# Use 7-bit data so we can test piece data as either bytes or str
DATA = bytes(random.getrandbits(7) for _ in range(LEN))
PIECES = [DATA[i : i + PIECE_LENGTH] for i in range(0, LEN, PIECE_LENGTH)]

INFO_DICT = {
    b"name": NAME,
    b"piece length": PIECE_LENGTH,
    b"length": len(DATA),
    b"pieces": b"".join(hashlib.sha1(p).digest() for p in PIECES),
}

DICT = {
    b"info": INFO_DICT,
}


def get_infohash_bytes() -> bytes:
    return hashlib.sha1(lt.bencode(INFO_DICT)).digest()


def get_infohash() -> str:
    return get_infohash_bytes().hex()


def get_sha1_hash() -> lt.sha1_hash:
    return lt.sha1_hash(get_infohash_bytes())
