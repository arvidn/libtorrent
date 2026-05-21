#!/usr/bin/env python3
# Copyright (c) 2026, Arvid Norberg
# All rights reserved.
#
# You may use, distribute and modify this code under the terms of the BSD license,
# see LICENSE file.
#
# Generate seed corpus files for multiple fuzzers:
#   - fuzzers/src/peer_conn.cpp
#   - fuzzers/src/natpmp.cpp
#   - fuzzers/src/udp_tracker.cpp
#   - fuzzers/src/upnp.cpp
#   - fuzzers/src/ut_metadata.cpp
#   - fuzzers/src/ut_pex.cpp
#   - fuzzers/src/utp_stream.cpp
#
# Each file is laid out exactly as the fuzzer expects:
#
#   [8 bytes : extension flags (reserved field of BT handshake)]
#   [repeated : 1-byte msg_type | 2-byte big-endian payload_len | payload]
#
# The fuzzer always sends a fixed BEP 10 extended handshake before
# processing corpus messages, registering:
#   ut_pex       -> ext_id 1
#   ut_metadata  -> ext_id 2
#   upload_only  -> ext_id 3
#
# libtorrent's fixed ext_ids for incoming extended messages (from
# bt_peer_connection.hpp enum):
#   upload_only_msg  = 3
#   holepunch_msg    = 4   (requires ut_holepunch in our handshake)
#   dont_have_msg    = 7
#   share_mode_msg   = 8
#
# Holepunch messages need m_holepunch_id != 0, which is set when libtorrent
# reads our extended handshake and finds "ut_holepunch". Corpus files that
# exercise holepunch send an additional extended handshake registering it
# before the holepunch message.

import os
import struct
from typing import Callable
from typing import Optional
from typing import Sequence

# Extension flag constants (8-byte reserved field of the BT handshake)
# bit positions as offsets from the start of the reserved bytes:
#   byte 0, bit 0x01  protocol version: 0 = v2 (SHA-256, protocol_v2=true)
#                                        1 = v1 (SHA-1,   protocol_v2=false)
#   byte 5, bit 0x10  BEP 10 extended protocol
#   byte 7, bit 0x04  BEP 6 FAST extension
#   byte 7, bit 0x01  BEP 5 DHT


def _flags(*bits: tuple) -> bytes:
    f = bytearray(8)
    for byte_idx, bit in bits:
        f[byte_idx] |= bit
    return bytes(f)


FLAGS_NONE = _flags()
FLAGS_EXTENDED = _flags((5, 0x10))
FLAGS_FAST = _flags((7, 0x04))
FLAGS_DHT = _flags((7, 0x01))
FLAGS_EXT_FAST = _flags((5, 0x10), (7, 0x04))
FLAGS_ALL = bytes([0xFF] * 8)
FLAGS_V1 = _flags((0, 0x01))
FLAGS_EXT_FAST_V1 = _flags((0, 0x01), (5, 0x10), (7, 0x04))


# Wire-encoding helpers
def msg(msg_type: int, payload: bytes = b"") -> bytes:
    """Encode one message in fuzzer wire format: [type:1][len:2][payload]."""
    assert 0 <= msg_type <= 255
    assert len(payload) <= 0xFFFF, f"payload too large: {len(payload)}"
    return bytes([msg_type]) + struct.pack(">H", len(payload)) + payload


def ext_msg(ext_id: int, payload: bytes = b"") -> bytes:
    """Encode a BEP 10 extended message (msg_type 20)."""
    return msg(20, bytes([ext_id & 0xFF]) + payload)


def u32(n: int) -> bytes:
    return struct.pack(">I", n & 0xFFFFFFFF)


def u16(n: int) -> bytes:
    return struct.pack(">H", n & 0xFFFF)


# Minimal bencode encoder
def _bencode(obj: object) -> bytes:
    if isinstance(obj, bool):
        raise TypeError("use int, not bool")
    if isinstance(obj, int):
        return f"i{obj}e".encode()
    if isinstance(obj, (bytes, bytearray)):
        return f"{len(obj)}:".encode() + bytes(obj)
    if isinstance(obj, str):
        enc = obj.encode()
        return f"{len(enc)}:".encode() + enc
    if isinstance(obj, (list, tuple)):
        return b"l" + b"".join(_bencode(x) for x in obj) + b"e"
    if isinstance(obj, dict):
        pairs = sorted(
            ((_bencode(k), _bencode(v)) for k, v in obj.items()),
            key=lambda kv: kv[0],
        )
        return b"d" + b"".join(k + v for k, v in pairs) + b"e"
    raise TypeError(f"cannot bencode {type(obj).__name__}")


# Constants matching the torrent created in LLVMFuzzerInitialize
PIECE_SIZE = 1024 * 1024  # 1 MiB
NUM_PIECES = 100
BLOCK_SIZE = 16 * 1024  # 16 KiB
BLOCKS_PER_PIECE = PIECE_SIZE // BLOCK_SIZE  # 64
BITFIELD_BYTES = (NUM_PIECES + 7) // 8  # 13 bytes for 100 pieces

# ext_ids as announced in the fuzzer's fixed extended handshake
EXT_HANDSHAKE = 0
EXT_UT_PEX = 1
EXT_UT_METADATA = 2
EXT_UPLOAD_ONLY = 3  # must match upload_only_msg in bt_peer_connection.hpp

# libtorrent's fixed internal ext_ids for incoming messages
# (bt_peer_connection.hpp enum)
HOLEPUNCH_MSG = 4  # holepunch_msg
DONT_HAVE_MSG = 7  # dont_have_msg
SHARE_MODE_MSG = 8  # share_mode_msg

# Holepunch support requires a non-zero m_holepunch_id, set when libtorrent
# reads "ut_holepunch" from our extended handshake.
EXT_HP_ANNOUNCE = 4  # value we place in our "ut_holepunch" field

# Fixed extended handshake the fuzzer always sends (keep in sync with peer_conn.cpp)
FUZZER_EXT_HANDSHAKE = _bencode(
    {
        "m": {
            "ut_pex": EXT_UT_PEX,
            "ut_metadata": EXT_UT_METADATA,
            "upload_only": EXT_UPLOAD_ONLY,
        },
        "reqq": 500,
        "v": "fuzzer",
    }
)

# Extended handshake that also registers holepunch
EXT_HANDSHAKE_WITH_HOLEPUNCH = _bencode(
    {
        "m": {
            "ut_pex": EXT_UT_PEX,
            "ut_metadata": EXT_UT_METADATA,
            "upload_only": EXT_UPLOAD_ONLY,
            "ut_holepunch": EXT_HP_ANNOUNCE,
        },
        "reqq": 500,
        "v": "fuzzer",
    }
)


# Corpus writer
#
# Each corpus file is the concatenation of its parts, optionally with a
# per-part framing function applied first (e.g. a 2-byte length prefix for
# datagram-oriented fuzzers).
def _len_prefixed(part: bytes) -> bytes:
    """Frame one part with a 2-byte big-endian length prefix."""
    assert len(part) <= 0xFFFF, f"part too large: {len(part)}"
    return u16(len(part)) + part


def _identity(part: bytes) -> bytes:
    return part


class Corpus:
    def __init__(
        self, outdir: str, frame: Callable[[bytes], bytes] = _identity
    ) -> None:
        self._outdir = outdir
        self._frame = frame
        self._count = 0
        os.makedirs(outdir, exist_ok=True)

    def add(self, name: str, *parts: bytes) -> None:
        data = b"".join(self._frame(p) for p in parts)
        path = os.path.join(self._outdir, name)
        with open(path, "wb") as f:
            f.write(data)
        self._count += 1

    @property
    def count(self) -> int:
        return self._count


# Generation
def generate_peer_conn(outdir: str) -> None:
    c = Corpus(outdir)

    # Extension flag variations
    # The fuzzer ORs in BEP 10 and BEP 6 bits unconditionally, so these
    # exercise the remaining flag bits (DHT, encryption negotiation, etc.).
    c.add("flags_none", FLAGS_NONE)
    c.add("flags_extended", FLAGS_EXTENDED)
    c.add("flags_fast", FLAGS_FAST)
    c.add("flags_dht", FLAGS_DHT)
    c.add("flags_ext_fast", FLAGS_EXT_FAST)
    c.add("flags_all", FLAGS_ALL)

    # choke(0), unchoke(1), interested(2), not_interested(3)
    # No payload. Sending an unexpected payload exercises the length check.
    for type_id, name in [
        (0, "choke"),
        (1, "unchoke"),
        (2, "interested"),
        (3, "not_interested"),
    ]:
        c.add(name, FLAGS_EXT_FAST, msg(type_id))
        c.add(f"{name}_extra_payload", FLAGS_EXT_FAST, msg(type_id, b"\x00" * 4))

    # have(4): 4-byte big-endian piece index
    for idx, label in [
        (0, "first"),
        (NUM_PIECES - 1, "last_valid"),
        (NUM_PIECES, "oob"),
        (0x7FFFFFFF, "max_signed"),
        (0xFFFFFFFF, "max_uint"),
    ]:
        c.add(f"have_{label}", FLAGS_EXT_FAST, msg(4, u32(idx)))

    for n in range(4):  # 0..3 bytes: too short
        c.add(f"have_short_{n}", FLAGS_EXT_FAST, msg(4, b"\x00" * n))
    c.add("have_long", FLAGS_EXT_FAST, msg(4, u32(0) + b"\x00"))  # 5 bytes

    # bitfield(5): ceil(num_pieces/8) bytes, one bit per piece
    # 100 pieces -> 13 bytes; top 4 bits of last byte are spare and should be 0
    c.add("bitfield_none", FLAGS_EXT_FAST, msg(5, bytes(BITFIELD_BYTES)))
    c.add("bitfield_all", FLAGS_EXT_FAST, msg(5, b"\xff" * BITFIELD_BYTES))
    c.add(
        "bitfield_first_only",
        FLAGS_EXT_FAST,
        msg(5, b"\x80" + bytes(BITFIELD_BYTES - 1)),
    )
    c.add(
        "bitfield_spare_bits_set",  # last 4 spare bits set
        FLAGS_EXT_FAST,
        msg(5, b"\xff" * (BITFIELD_BYTES - 1) + b"\xff"),
    )
    c.add("bitfield_too_short", FLAGS_EXT_FAST, msg(5, bytes(BITFIELD_BYTES - 1)))
    c.add("bitfield_too_long", FLAGS_EXT_FAST, msg(5, bytes(BITFIELD_BYTES + 1)))
    c.add("bitfield_empty", FLAGS_EXT_FAST, msg(5))

    # request(6) and cancel(8): index(4), begin(4), length(4) = 12 bytes
    for type_id, name in [(6, "request"), (8, "cancel")]:
        c.add(
            f"{name}_first_block",
            FLAGS_EXT_FAST,
            msg(type_id, u32(0) + u32(0) + u32(BLOCK_SIZE)),
        )
        c.add(
            f"{name}_last_piece_first_block",
            FLAGS_EXT_FAST,
            msg(type_id, u32(NUM_PIECES - 1) + u32(0) + u32(BLOCK_SIZE)),
        )
        c.add(
            f"{name}_last_piece_last_block",
            FLAGS_EXT_FAST,
            msg(
                type_id,
                u32(NUM_PIECES - 1)
                + u32((BLOCKS_PER_PIECE - 1) * BLOCK_SIZE)
                + u32(BLOCK_SIZE),
            ),
        )
        c.add(
            f"{name}_oob_piece",
            FLAGS_EXT_FAST,
            msg(type_id, u32(NUM_PIECES) + u32(0) + u32(BLOCK_SIZE)),
        )
        c.add(
            f"{name}_large_block",  # length > 16 KiB
            FLAGS_EXT_FAST,
            msg(type_id, u32(0) + u32(0) + u32(BLOCK_SIZE * 4)),
        )
        c.add(
            f"{name}_zero_length",
            FLAGS_EXT_FAST,
            msg(type_id, u32(0) + u32(0) + u32(0)),
        )
        c.add(f"{name}_max_values", FLAGS_EXT_FAST, msg(type_id, b"\xff" * 12))
        for n in [0, 4, 8, 11]:  # various truncations
            c.add(f"{name}_short_{n}", FLAGS_EXT_FAST, msg(type_id, b"\x00" * n))

    # piece(7): index(4), begin(4), data(variable)
    c.add(
        "piece_valid_block", FLAGS_EXT_FAST, msg(7, u32(0) + u32(0) + bytes(BLOCK_SIZE))
    )
    c.add("piece_empty_data", FLAGS_EXT_FAST, msg(7, u32(0) + u32(0)))
    c.add(
        "piece_oob_index",
        FLAGS_EXT_FAST,
        msg(7, u32(NUM_PIECES) + u32(0) + bytes(BLOCK_SIZE)),
    )
    c.add(
        "piece_misaligned_begin",  # begin not on a block boundary
        FLAGS_EXT_FAST,
        msg(7, u32(0) + u32(1) + bytes(BLOCK_SIZE)),
    )
    c.add(
        "piece_begin_oob",  # begin past end of piece
        FLAGS_EXT_FAST,
        msg(7, u32(0) + u32(PIECE_SIZE) + bytes(BLOCK_SIZE)),
    )
    for n in [0, 4, 7]:
        c.add(f"piece_short_{n}", FLAGS_EXT_FAST, msg(7, b"\x00" * n))

    # dht_port(9): 2-byte port number
    c.add("dht_port_valid", FLAGS_DHT, msg(9, u16(6881)))
    c.add("dht_port_zero", FLAGS_DHT, msg(9, u16(0)))
    c.add("dht_port_max", FLAGS_DHT, msg(9, u16(65535)))
    c.add("dht_port_short", FLAGS_DHT, msg(9, b"\x00"))
    c.add("dht_port_long", FLAGS_DHT, msg(9, u16(6881) + b"\x00"))

    # BEP 6 FAST extension

    # suggest_piece(13), allowed_fast(17): 4-byte piece index
    for type_id, name in [(13, "suggest_piece"), (17, "allowed_fast")]:
        for idx, label in [
            (0, "first"),
            (NUM_PIECES - 1, "last_valid"),
            (NUM_PIECES, "oob"),
            (0xFFFFFFFF, "max"),
        ]:
            c.add(f"{name}_{label}", FLAGS_EXT_FAST, msg(type_id, u32(idx)))

    # have_all(14), have_none(15): no payload
    c.add("have_all", FLAGS_EXT_FAST, msg(14))
    c.add("have_none", FLAGS_EXT_FAST, msg(15))
    c.add("have_all_then_none", FLAGS_EXT_FAST, msg(14), msg(15))
    c.add("have_none_then_all", FLAGS_EXT_FAST, msg(15), msg(14))
    c.add("have_all_extra_payload", FLAGS_EXT_FAST, msg(14, b"\x00" * 4))
    c.add("have_none_extra_payload", FLAGS_EXT_FAST, msg(15, b"\x00" * 4))

    # reject_request(16): same 12-byte layout as request/cancel
    c.add(
        "reject_request_valid",
        FLAGS_EXT_FAST,
        msg(16, u32(0) + u32(0) + u32(BLOCK_SIZE)),
    )
    c.add(
        "reject_request_oob",
        FLAGS_EXT_FAST,
        msg(16, u32(NUM_PIECES) + u32(0) + u32(BLOCK_SIZE)),
    )
    c.add("reject_request_short", FLAGS_EXT_FAST, msg(16, b"\x00" * 4))
    c.add("reject_request_max_values", FLAGS_EXT_FAST, msg(16, b"\xff" * 12))

    # 10 allowed_fast in a row (exercises the per-peer set-size limit)
    c.add("allowed_fast_bulk", FLAGS_EXT_FAST, *[msg(17, u32(i)) for i in range(10)])

    # Unknown message types: should be rejected cleanly without crashing
    for type_id in [10, 11, 12, 18, 19, 24, 127, 254, 255]:
        c.add(f"unknown_msg_{type_id}", FLAGS_EXT_FAST, msg(type_id, b"\x00" * 4))

    # BEP 10 extended protocol: extended handshake (ext_id 0)
    #
    # The fuzzer sends a fixed handshake before corpus messages. A second
    # one exercises the re-negotiation path and lets us vary the fields.
    c.add(
        "ext_handshake_repeat",
        FLAGS_EXTENDED,
        ext_msg(EXT_HANDSHAKE, FUZZER_EXT_HANDSHAKE),
    )
    c.add("ext_handshake_empty_dict", FLAGS_EXTENDED, ext_msg(EXT_HANDSHAKE, b"de"))
    c.add(
        "ext_handshake_invalid_bencode",
        FLAGS_EXTENDED,
        ext_msg(EXT_HANDSHAKE, b"garbage"),
    )
    c.add("ext_handshake_truncated", FLAGS_EXTENDED, ext_msg(EXT_HANDSHAKE, b"d1:m"))
    c.add("ext_handshake_empty_payload", FLAGS_EXTENDED, ext_msg(EXT_HANDSHAKE))
    c.add(
        "ext_handshake_conflicting_ids",  # two extensions share the same ID
        FLAGS_EXTENDED,
        ext_msg(EXT_HANDSHAKE, _bencode({"m": {"ut_pex": 1, "ut_metadata": 1}})),
    )
    c.add(
        "ext_handshake_id_zero_for_all",  # ID 0 is reserved for the handshake
        FLAGS_EXTENDED,
        ext_msg(EXT_HANDSHAKE, _bencode({"m": {"ut_pex": 0, "ut_metadata": 0}})),
    )
    c.add(
        "ext_handshake_large_reqq",
        FLAGS_EXTENDED,
        ext_msg(EXT_HANDSHAKE, _bencode({"reqq": 2**31 - 1})),
    )
    c.add(
        "ext_handshake_negative_reqq",
        FLAGS_EXTENDED,
        ext_msg(EXT_HANDSHAKE, _bencode({"reqq": -1})),
    )
    c.add(
        "ext_handshake_yourip_v4",
        FLAGS_EXTENDED,
        ext_msg(EXT_HANDSHAKE, _bencode({"yourip": b"\x7f\x00\x00\x01", "reqq": 250})),
    )
    c.add(
        "ext_handshake_yourip_v6",
        FLAGS_EXTENDED,
        ext_msg(
            EXT_HANDSHAKE, _bencode({"yourip": b"\x00" * 15 + b"\x01", "reqq": 250})
        ),
    )
    c.add(
        "ext_handshake_yourip_wrong_length",  # neither 4 nor 16 bytes
        FLAGS_EXTENDED,
        ext_msg(EXT_HANDSHAKE, _bencode({"yourip": b"\x7f\x00\x00"})),
    )
    c.add(
        "ext_handshake_large_version_string",
        FLAGS_EXTENDED,
        ext_msg(EXT_HANDSHAKE, _bencode({"v": "A" * 200})),
    )
    c.add(
        "ext_handshake_listen_port",
        FLAGS_EXTENDED,
        ext_msg(EXT_HANDSHAKE, _bencode({"p": 6881})),
    )

    # ut_metadata (ext_id 2)
    # Incoming wire format: bencoded dict, optionally followed by raw bytes.
    # dict fields: msg_type (0=request, 1=data, 2=reject), piece, total_size
    for piece_idx, label in [
        (0, "0"),
        (1, "1"),
        (100, "100"),
        (0x7FFFFFFF, "max"),
    ]:
        c.add(
            f"ut_metadata_request_piece_{label}",
            FLAGS_EXTENDED,
            ext_msg(EXT_UT_METADATA, _bencode({"msg_type": 0, "piece": piece_idx})),
        )
        c.add(
            f"ut_metadata_reject_piece_{label}",
            FLAGS_EXTENDED,
            ext_msg(EXT_UT_METADATA, _bencode({"msg_type": 2, "piece": piece_idx})),
        )

    # data (msg_type 1): bencoded header immediately followed by raw metadata
    c.add(
        "ut_metadata_data_small",
        FLAGS_EXTENDED,
        ext_msg(
            EXT_UT_METADATA,
            _bencode({"msg_type": 1, "piece": 0, "total_size": 48}) + bytes(48),
        ),
    )
    c.add(
        "ut_metadata_data_no_trailing_bytes",
        FLAGS_EXTENDED,
        ext_msg(
            EXT_UT_METADATA, _bencode({"msg_type": 1, "piece": 0, "total_size": 100})
        ),
    )
    c.add(
        "ut_metadata_data_total_size_zero",
        FLAGS_EXTENDED,
        ext_msg(
            EXT_UT_METADATA, _bencode({"msg_type": 1, "piece": 0, "total_size": 0})
        ),
    )
    c.add(
        "ut_metadata_data_total_size_negative",
        FLAGS_EXTENDED,
        ext_msg(
            EXT_UT_METADATA, _bencode({"msg_type": 1, "piece": 0, "total_size": -1})
        ),
    )
    c.add(
        "ut_metadata_data_total_size_over_max",  # default max is 4 MiB
        FLAGS_EXTENDED,
        ext_msg(
            EXT_UT_METADATA,
            _bencode({"msg_type": 1, "piece": 0, "total_size": 4 * 1024 * 1024 + 1}),
        ),
    )
    c.add(
        "ut_metadata_unknown_msg_type",
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_METADATA, _bencode({"msg_type": 99, "piece": 0})),
    )
    c.add(
        "ut_metadata_missing_piece_key",
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_METADATA, _bencode({"msg_type": 0})),
    )
    c.add(
        "ut_metadata_missing_msg_type_key",
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_METADATA, _bencode({"piece": 0})),
    )
    c.add("ut_metadata_empty_dict", FLAGS_EXTENDED, ext_msg(EXT_UT_METADATA, b"de"))
    c.add(
        "ut_metadata_invalid_bencode",
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_METADATA, b"garbage"),
    )
    c.add("ut_metadata_empty_payload", FLAGS_EXTENDED, ext_msg(EXT_UT_METADATA))

    # Flood: many requests back-to-back (exercises the 1024-pending-request limit)
    c.add(
        "ut_metadata_request_flood",
        FLAGS_EXTENDED,
        *[
            ext_msg(EXT_UT_METADATA, _bencode({"msg_type": 0, "piece": i}))
            for i in range(30)
        ],
    )

    # ut_pex (ext_id 1)
    # Compact IPv4: 6 bytes/peer (4-byte IP + 2-byte port)
    # Compact IPv6: 18 bytes/peer (16-byte IP + 2-byte port)
    peer_v4 = b"\x7f\x00\x00\x01" + u16(6881)  # 127.0.0.1:6881
    peer_v6 = b"\x00" * 15 + b"\x01" + u16(6881)  # [::1]:6881

    c.add("ut_pex_empty", FLAGS_EXTENDED, ext_msg(EXT_UT_PEX, b"de"))
    c.add(
        "ut_pex_one_ipv4",
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_PEX, _bencode({"added": peer_v4, "added.f": b"\x00"})),
    )
    c.add(
        "ut_pex_one_ipv4_seeder_flag",  # 0x02 = seeder flag
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_PEX, _bencode({"added": peer_v4, "added.f": b"\x02"})),
    )
    c.add(
        "ut_pex_50_ipv4",
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_PEX, _bencode({"added": peer_v4 * 50, "added.f": bytes(50)})),
    )
    c.add(
        "ut_pex_200_ipv4_overflow",  # over the 100-peer limit
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_PEX, _bencode({"added": peer_v4 * 200})),
    )
    c.add(
        "ut_pex_flag_length_mismatch",  # added.f length != number of peers
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_PEX, _bencode({"added": peer_v4 * 3, "added.f": b"\x00"})),
    )
    c.add(
        "ut_pex_added_odd_length",  # not a multiple of 6
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_PEX, _bencode({"added": b"\x7f\x00\x00"})),
    )
    c.add(
        "ut_pex_one_ipv6",
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_PEX, _bencode({"added6": peer_v6, "added6.f": b"\x00"})),
    )
    c.add(
        "ut_pex_added6_odd_length",  # not a multiple of 18
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_PEX, _bencode({"added6": b"\x00" * 17})),
    )
    c.add(
        "ut_pex_dropped_ipv4",
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_PEX, _bencode({"dropped": peer_v4})),
    )
    c.add(
        "ut_pex_dropped_ipv6",
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_PEX, _bencode({"dropped6": peer_v6})),
    )
    c.add(
        "ut_pex_all_fields",
        FLAGS_EXTENDED,
        ext_msg(
            EXT_UT_PEX,
            _bencode(
                {
                    "added": peer_v4 * 2,
                    "added.f": bytes(2),
                    "added6": peer_v6,
                    "added6.f": b"\x00",
                    "dropped": peer_v4,
                    "dropped6": peer_v6,
                }
            ),
        ),
    )
    c.add("ut_pex_invalid_bencode", FLAGS_EXTENDED, ext_msg(EXT_UT_PEX, b"garbage"))
    c.add("ut_pex_empty_payload", FLAGS_EXTENDED, ext_msg(EXT_UT_PEX))

    # upload_only (ext_id 3 == upload_only_msg)
    # Payload: 1-byte boolean
    c.add("upload_only_true", FLAGS_EXTENDED, ext_msg(EXT_UPLOAD_ONLY, b"\x01"))
    c.add("upload_only_false", FLAGS_EXTENDED, ext_msg(EXT_UPLOAD_ONLY, b"\x00"))
    c.add("upload_only_empty", FLAGS_EXTENDED, ext_msg(EXT_UPLOAD_ONLY))
    c.add("upload_only_large_value", FLAGS_EXTENDED, ext_msg(EXT_UPLOAD_ONLY, b"\xff"))

    # dont_have (ext_id 7 == dont_have_msg): 4-byte piece index
    # Packet size must be exactly 6 (2 ext header + 4 index).
    c.add("dont_have_first", FLAGS_EXTENDED, ext_msg(DONT_HAVE_MSG, u32(0)))
    c.add("dont_have_last", FLAGS_EXTENDED, ext_msg(DONT_HAVE_MSG, u32(NUM_PIECES - 1)))
    c.add("dont_have_oob", FLAGS_EXTENDED, ext_msg(DONT_HAVE_MSG, u32(NUM_PIECES)))
    c.add("dont_have_max", FLAGS_EXTENDED, ext_msg(DONT_HAVE_MSG, u32(0xFFFFFFFF)))
    c.add("dont_have_short", FLAGS_EXTENDED, ext_msg(DONT_HAVE_MSG, b"\x00" * 2))
    c.add("dont_have_long", FLAGS_EXTENDED, ext_msg(DONT_HAVE_MSG, u32(0) + b"\x00"))

    # share_mode (ext_id 8 == share_mode_msg): 1-byte boolean
    c.add("share_mode_true", FLAGS_EXTENDED, ext_msg(SHARE_MODE_MSG, b"\x01"))
    c.add("share_mode_false", FLAGS_EXTENDED, ext_msg(SHARE_MODE_MSG, b"\x00"))
    c.add("share_mode_empty", FLAGS_EXTENDED, ext_msg(SHARE_MODE_MSG))

    # ut_holepunch (ext_id 4 == holepunch_msg)
    #
    # Requires m_holepunch_id != 0. We set this by sending an additional
    # extended handshake that includes "ut_holepunch" before each test case.
    #
    # Wire format:
    #   [msg_type:1] 0=rendezvous 1=connect 2=failed
    #   [addr_type:1] 0=IPv4 1=IPv6 other=unknown
    #   [ip: 4 or 16 bytes]
    #   [port: 2 bytes]
    #   [error: 4 bytes, only present when msg_type==failed]
    def holepunch_payload(
        hp_type: int, addr_type: int, ip: bytes, port: int, error: Optional[int] = None
    ) -> bytes:
        data = bytes([hp_type, addr_type]) + ip + u16(port)
        if error is not None:
            data += u32(error)
        return data

    enable_hp = ext_msg(EXT_HANDSHAKE, EXT_HANDSHAKE_WITH_HOLEPUNCH)
    ipv4 = b"\x7f\x00\x00\x01"
    ipv6 = b"\x00" * 15 + b"\x01"

    hp_cases = [
        # (name,                  hp_type, addr_type, ip,   port, error)
        ("rendezvous_ipv4", 0, 0, ipv4, 6881, None),
        ("connect_ipv4", 1, 0, ipv4, 6881, None),
        ("failed_ipv4_no_peer", 2, 0, ipv4, 6881, 1),  # hp_error::no_such_peer
        ("failed_ipv4_not_conn", 2, 0, ipv4, 6881, 2),  # hp_error::not_connected
        ("failed_ipv4_no_self", 2, 0, ipv4, 6881, 4),  # hp_error::no_self
        ("rendezvous_ipv6", 0, 1, ipv6, 6881, None),
        ("connect_ipv6", 1, 1, ipv6, 6881, None),
        ("failed_ipv6", 2, 1, ipv6, 6881, 1),
        ("unknown_msg_type", 3, 0, ipv4, 6881, None),  # msg_type > failed
        ("unknown_addr_type", 0, 2, ipv4, 6881, None),  # addr_type unknown
        ("short_payload", 0, 0, b"", 0, None),  # too short
        ("failed_missing_error", 2, 0, ipv4, 6881, None),  # failed without error code
    ]
    for name, hp_type, addr_type, ip, port, error in hp_cases:
        c.add(
            f"holepunch_{name}",
            FLAGS_EXTENDED,
            enable_hp,
            ext_msg(
                HOLEPUNCH_MSG, holepunch_payload(hp_type, addr_type, ip, port, error)
            ),
        )

    # Without registering holepunch first (m_holepunch_id stays 0, silently ignored)
    c.add(
        "holepunch_unregistered",
        FLAGS_EXTENDED,
        ext_msg(HOLEPUNCH_MSG, holepunch_payload(0, 0, ipv4, 6881)),
    )

    # Unknown extended message IDs: should disconnect with invalid_message
    for ext_id in [5, 6, 9, 10, 100, 127, 255]:
        c.add(f"unknown_ext_{ext_id}", FLAGS_EXTENDED, ext_msg(ext_id, b"\x00" * 4))

    # BEP 78 hash exchange (v2 torrents)
    # hash_request(21), hash_reject(23):
    #   [file_root:32][base:4][index:4][count:4][proof_layers:4] = 44 bytes
    # hashes(22): same header + (count + proof_hashes) * 32 bytes
    #
    # File is identified by its SHA-256 merkle root. A zero root is not a
    # valid root in the torrent, exercising the "file not found" path.
    # These messages are also rejected if the peer is not a v2 peer
    # (protocol_v2 flag unset after handshake).
    def hash_hdr(
        file_root: bytes, base: int, index: int, count: int, proof_layers: int
    ) -> bytes:
        assert len(file_root) == 32
        return file_root + u32(base) + u32(index) + u32(count) + u32(proof_layers)

    ZERO_ROOT = bytes(32)
    valid_hdr = hash_hdr(ZERO_ROOT, 0, 0, 1, 0)
    one_sha256 = bytes(32)

    # hash_request(21)
    c.add("hash_request_valid", FLAGS_EXT_FAST, msg(21, valid_hdr))
    c.add(
        "hash_request_oob_count",  # count > 8192
        FLAGS_EXT_FAST,
        msg(21, hash_hdr(ZERO_ROOT, 0, 0, 8193, 0)),
    )
    c.add(
        "hash_request_large_proof_layers",
        FLAGS_EXT_FAST,
        msg(21, hash_hdr(ZERO_ROOT, 0, 0, 1, 999)),
    )
    c.add(
        "hash_request_base_nonzero",
        FLAGS_EXT_FAST,
        msg(21, hash_hdr(ZERO_ROOT, 5, 0, 1, 0)),
    )
    c.add("hash_request_max_values", FLAGS_EXT_FAST, msg(21, b"\xff" * 44))
    for n in [0, 8, 32, 43]:
        c.add(f"hash_request_short_{n}", FLAGS_EXT_FAST, msg(21, b"\x00" * n))

    # hashes(22)
    c.add("hashes_one", FLAGS_EXT_FAST, msg(22, valid_hdr + one_sha256))
    c.add(
        "hashes_count_mismatch",  # header says 4 hashes, only 1 sent
        FLAGS_EXT_FAST,
        msg(22, hash_hdr(ZERO_ROOT, 0, 0, 4, 0) + one_sha256),
    )
    c.add("hashes_no_hash_data", FLAGS_EXT_FAST, msg(22, valid_hdr))
    c.add("hashes_empty", FLAGS_EXT_FAST, msg(22))
    for n in [0, 8, 32, 43]:
        c.add(f"hashes_short_{n}", FLAGS_EXT_FAST, msg(22, b"\x00" * n))

    # hash_reject(23)
    c.add("hash_reject_valid", FLAGS_EXT_FAST, msg(23, valid_hdr))
    c.add("hash_reject_max_values", FLAGS_EXT_FAST, msg(23, b"\xff" * 44))
    for n in [0, 8, 32, 43]:
        c.add(f"hash_reject_short_{n}", FLAGS_EXT_FAST, msg(23, b"\x00" * n))

    # Multi-message sequences: exercise protocol state transitions

    # interested -> request (before unchoke; "request while choked" path)
    c.add(
        "seq_interested_then_request",
        FLAGS_EXT_FAST,
        msg(2),
        msg(6, u32(0) + u32(0) + u32(BLOCK_SIZE)),
    )

    # bitfield(all) + interested (standard leecher greeting)
    c.add(
        "seq_bitfield_all_interested",
        FLAGS_EXT_FAST,
        msg(5, b"\xff" * BITFIELD_BYTES),
        msg(2),
    )

    # have_all (FAST) -> unchoke -> interested -> allowed_fast -> request
    c.add(
        "seq_fast_have_all_request",
        FLAGS_EXT_FAST,
        msg(14),
        msg(1),
        msg(2),
        msg(17, u32(0)),
        msg(6, u32(0) + u32(0) + u32(BLOCK_SIZE)),
    )

    # have_none -> allowed_fast -> request (FAST "free piece" download)
    c.add(
        "seq_fast_have_none_allowed_request",
        FLAGS_EXT_FAST,
        msg(15),
        msg(17, u32(0)),
        msg(6, u32(0) + u32(0) + u32(BLOCK_SIZE)),
    )

    # choke/unchoke cycling while interested
    c.add(
        "seq_choke_cycle",
        FLAGS_EXT_FAST,
        msg(2),
        msg(1),
        msg(0),
        msg(1),
        msg(0),
        msg(1),
    )

    # bitfield after have_all (invalid: bitfield must come first)
    c.add(
        "seq_bitfield_after_have_all",
        FLAGS_EXT_FAST,
        msg(14),
        msg(5, b"\xff" * BITFIELD_BYTES),
    )

    # request flood while claiming all pieces (tests queue-depth limits)
    c.add(
        "seq_request_flood",
        FLAGS_EXT_FAST,
        msg(5, b"\xff" * BITFIELD_BYTES),
        msg(1),
        *[msg(6, u32(i % NUM_PIECES) + u32(0) + u32(BLOCK_SIZE)) for i in range(30)],
    )

    # all 100 have messages in order (exercises piece_picker interactions)
    c.add("seq_all_haves", FLAGS_EXT_FAST, *[msg(4, u32(i)) for i in range(NUM_PIECES)])

    # upload_only -> then try to request (should be rejected)
    c.add(
        "seq_upload_only_then_request",
        FLAGS_EXTENDED,
        ext_msg(EXT_UPLOAD_ONLY, b"\x01"),
        msg(6, u32(0) + u32(0) + u32(BLOCK_SIZE)),
    )

    # ut_metadata request immediately after the fuzzer's fixed handshake
    c.add(
        "seq_ext_metadata_request",
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_METADATA, _bencode({"msg_type": 0, "piece": 0})),
    )

    # two consecutive pex updates
    c.add(
        "seq_ext_pex_two_updates",
        FLAGS_EXTENDED,
        ext_msg(EXT_UT_PEX, _bencode({"added": peer_v4, "added.f": b"\x00"})),
        ext_msg(EXT_UT_PEX, _bencode({"dropped": peer_v4})),
    )

    # hash_request followed immediately by unsolicited hashes reply
    c.add(
        "seq_hash_request_then_hashes",
        FLAGS_EXT_FAST,
        msg(21, valid_hdr),
        msg(22, valid_hdr + one_sha256),
    )

    # v1 peer seeds (byte 0 bit 0x01 set -> SHA-1 info-hash, protocol_v2=false)
    # Exercises the protocol paths that differ from v2: hash messages 21/22/23
    # must be rejected with invalid_message for v1 peers.
    c.add("v1_baseline", FLAGS_EXT_FAST_V1)
    c.add("v1_have_first", FLAGS_EXT_FAST_V1, msg(4, u32(0)))
    c.add("v1_bitfield_all", FLAGS_EXT_FAST_V1, msg(5, b"\xff" * BITFIELD_BYTES))
    c.add(
        "v1_seq_bitfield_interested",
        FLAGS_EXT_FAST_V1,
        msg(5, b"\xff" * BITFIELD_BYTES),
        msg(2),
    )
    # hash messages are rejected with invalid_message for v1 peers
    c.add("v1_hash_request_rejected", FLAGS_EXT_FAST_V1, msg(21, valid_hdr))
    c.add("v1_hashes_rejected", FLAGS_EXT_FAST_V1, msg(22, valid_hdr + one_sha256))
    c.add("v1_hash_reject_rejected", FLAGS_EXT_FAST_V1, msg(23, valid_hdr))

    print(f"Generated {c.count} peer_conn corpus files in {outdir}")


def generate_ut_metadata(outdir: str) -> None:
    """Seed corpus for fuzzers/src/ut_metadata.cpp.

    Each file is the raw body of a ut_metadata extended message (the bencoded
    dict, optionally followed by raw bytes). There is no flags prefix or
    ext_id byte -- the fuzzer wraps the input itself.
    """
    c = Corpus(outdir)
    add = c.add

    # request (msg_type 0)
    for piece_idx, label in [(0, "0"), (1, "1"), (100, "100"), (0x7FFFFFFF, "max")]:
        add(
            f"request_piece_{label}",
            _bencode({"msg_type": 0, "piece": piece_idx}),
        )

    # dont_have (msg_type 2)
    for piece_idx, label in [(0, "0"), (1, "1"), (100, "100"), (0x7FFFFFFF, "max")]:
        add(
            f"reject_piece_{label}",
            _bencode({"msg_type": 2, "piece": piece_idx}),
        )

    # data (msg_type 1): bencoded header immediately followed by raw metadata
    add(
        "data_small",
        _bencode({"msg_type": 1, "piece": 0, "total_size": 48}) + bytes(48),
    )
    add(
        "data_no_trailing_bytes",
        _bencode({"msg_type": 1, "piece": 0, "total_size": 100}),
    )
    add(
        "data_total_size_zero",
        _bencode({"msg_type": 1, "piece": 0, "total_size": 0}),
    )
    add(
        "data_total_size_negative",
        _bencode({"msg_type": 1, "piece": 0, "total_size": -1}),
    )
    add(
        "data_total_size_over_max",
        _bencode({"msg_type": 1, "piece": 0, "total_size": 4 * 1024 * 1024 + 1}),
    )

    # Edge cases
    add("unknown_msg_type", _bencode({"msg_type": 99, "piece": 0}))
    add("missing_piece_key", _bencode({"msg_type": 0}))
    add("missing_msg_type_key", _bencode({"piece": 0}))
    add("empty_dict", b"de")
    add("invalid_bencode", b"garbage")
    add("empty_payload", b"")

    # Flood: many pieces (exercises the 1024-pending-request limit in peer_conn,
    # and the request-tracking path in ut_metadata itself)
    for i in range(30):
        add(f"request_piece_flood_{i:02d}", _bencode({"msg_type": 0, "piece": i}))

    print(f"Generated {c.count} ut_metadata corpus files in {outdir}")


def generate_ut_pex(outdir: str) -> None:
    """Seed corpus for fuzzers/src/ut_pex.cpp.

    Each file is the raw bencoded body of a ut_pex extended message. There is
    no flags prefix or ext_id byte -- the fuzzer wraps the input itself.
    """
    c = Corpus(outdir)
    add = c.add

    peer_v4 = b"\x7f\x00\x00\x01" + u16(6881)  # 127.0.0.1:6881
    peer_v6 = b"\x00" * 15 + b"\x01" + u16(6881)  # [::1]:6881

    add("empty", b"de")
    add("one_ipv4", _bencode({"added": peer_v4, "added.f": b"\x00"}))
    add(
        "one_ipv4_seeder_flag",
        _bencode({"added": peer_v4, "added.f": b"\x02"}),
    )
    add(
        "50_ipv4",
        _bencode({"added": peer_v4 * 50, "added.f": bytes(50)}),
    )
    add(
        "200_ipv4_overflow",
        _bencode({"added": peer_v4 * 200}),
    )
    add(
        "flag_length_mismatch",
        _bencode({"added": peer_v4 * 3, "added.f": b"\x00"}),
    )
    add(
        "added_odd_length",
        _bencode({"added": b"\x7f\x00\x00"}),
    )
    add("one_ipv6", _bencode({"added6": peer_v6, "added6.f": b"\x00"}))
    add(
        "added6_odd_length",
        _bencode({"added6": b"\x00" * 17}),
    )
    add("dropped_ipv4", _bencode({"dropped": peer_v4}))
    add("dropped_ipv6", _bencode({"dropped6": peer_v6}))
    add(
        "all_fields",
        _bencode(
            {
                "added": peer_v4 * 2,
                "added.f": bytes(2),
                "added6": peer_v6,
                "added6.f": b"\x00",
                "dropped": peer_v4,
                "dropped6": peer_v6,
            }
        ),
    )
    add("invalid_bencode", b"garbage")
    add("empty_payload", b"")

    print(f"Generated {c.count} ut_pex corpus files in {outdir}")


def _natpmp_public_ip(
    result: int = 0, epoch: int = 1, ip: bytes = b"\x7f\x00\x00\x01"
) -> bytes:
    assert len(ip) == 4
    return bytes([0, 0x80]) + u16(result) + u32(epoch) + ip


def _natpmp_map_response(
    op: int,
    result: int = 0,
    epoch: int = 1,
    private_port: int = 6881,
    public_port: int = 16881,
    lifetime: int = 3600,
) -> bytes:
    return (
        bytes([0, op & 0xFF])
        + u16(result)
        + u32(epoch)
        + u16(private_port)
        + u16(public_port)
        + u32(lifetime)
    )


def _pcp_map_response(
    result: int = 0,
    lifetime: int = 3600,
    epoch: int = 1,
    nonce: bytes = bytes(12),
    protocol: int = 6,
    private_port: int = 6881,
    public_port: int = 16881,
    external_ip: bytes = (b"\x00" * 10 + b"\xff\xff" + b"\x7f\x00\x00\x01"),
) -> bytes:
    assert len(nonce) == 12
    assert len(external_ip) == 16
    hdr = (
        bytes([2, 0x81, 0, result & 0xFF]) + u32(lifetime) + u32(epoch) + (b"\x00" * 12)
    )
    body = (
        nonce
        + bytes([protocol & 0xFF])
        + (b"\x00" * 3)
        + u16(private_port)
        + u16(public_port)
        + external_ip
    )
    return hdr + body


def generate_natpmp(outdir: str) -> None:
    c = Corpus(outdir, frame=_len_prefixed)
    add = c.add

    add("natpmp_public_ip_ok", _natpmp_public_ip())
    add(
        "natpmp_map_tcp_ok",
        _natpmp_map_response(0x82, private_port=6881, public_port=16881),
    )
    add(
        "natpmp_map_udp_ok",
        _natpmp_map_response(0x81, private_port=6882, public_port=16882),
    )
    add(
        "natpmp_public_ip_then_map",
        _natpmp_public_ip(ip=b"\x0a\x00\x00\x01"),
        _natpmp_map_response(0x82, private_port=6881, public_port=40000, lifetime=1800),
    )
    add("natpmp_map_result_unsupp_opcode", _natpmp_map_response(0x82, result=4))
    add("natpmp_map_lifetime_zero", _natpmp_map_response(0x82, lifetime=0))
    add("natpmp_map_public_port_zero", _natpmp_map_response(0x82, public_port=0))
    add("natpmp_map_private_port_zero", _natpmp_map_response(0x82, private_port=0))
    add("natpmp_bad_opcode", bytes([0, 0x90]) + b"\x00" * 14)
    add("natpmp_too_short", b"\x00\x82\x00")
    add("natpmp_public_ip_short", _natpmp_public_ip()[:11])
    add("natpmp_map_short", _natpmp_map_response(0x82)[:15])
    add("natpmp_map_long", _natpmp_map_response(0x82) + b"\x00")
    add("natpmp_unknown_version", bytes([9, 0x82]) + b"\x00" * 14)
    add(
        "pcp_map_ok_tcp",
        _pcp_map_response(protocol=6, private_port=6881, public_port=50000),
    )
    add(
        "pcp_map_ok_udp",
        _pcp_map_response(protocol=17, private_port=6882, public_port=50001),
    )
    add("pcp_map_lifetime_zero", _pcp_map_response(lifetime=0))
    add("pcp_map_result_unsupp_version", _pcp_map_response(result=1))
    add("pcp_map_result_no_resources", _pcp_map_response(result=8))
    add("pcp_map_protocol_zero", _pcp_map_response(protocol=0))
    add(
        "pcp_map_ipv6_external",
        _pcp_map_response(external_ip=(b"\x20\x01\x0d\xb8" + b"\x00" * 11 + b"\x01")),
    )
    add("pcp_too_short", b"\x02\x81\x00\x00\x00")
    add("pcp_header_only_24", _pcp_map_response()[:24])
    add("pcp_map_short_59", _pcp_map_response()[:59])
    add("pcp_map_long_61", _pcp_map_response() + b"\x00")
    add("pcp_bad_opcode", bytes([2, 0x82, 0, 0]) + b"\x00" * 56)
    add("pcp_response_bit_not_set", bytes([2, 0x01, 0, 0]) + b"\x00" * 56)
    add(
        "seq_natpmp_then_pcp",
        _natpmp_public_ip(),
        _natpmp_map_response(0x82, private_port=6881),
        _pcp_map_response(protocol=6, private_port=6881),
    )
    add(
        "seq_noise_then_valid",
        b"\xff\xff\xff",
        _natpmp_map_response(0x82, private_port=6881, public_port=40000),
        _pcp_map_response(protocol=17, private_port=6882, public_port=40001),
    )
    add(
        "seq_many_small_packets",
        b"\x00",
        b"\x00\x80",
        b"\x02",
        _natpmp_public_ip()[:8],
        _pcp_map_response()[:20],
    )

    print(f"Generated {c.count} natpmp corpus files in {outdir}")


def _udp_frame(ctrl: int, payload: bytes = b"") -> bytes:
    assert 0 <= ctrl <= 0xFF
    assert len(payload) <= 0xFFFF
    return bytes([ctrl]) + struct.pack(">H", len(payload)) + payload


def generate_udp_tracker(outdir: str) -> None:
    c = Corpus(outdir)
    add = c.add

    action_connect = 0
    action_announce = 1
    action_scrape = 2
    action_error = 3
    ctrl_src_mismatch = 0x04
    ctrl_tid_from_payload = 0x08

    connect_ok = struct.pack(">Q", 0x41727101980)
    connect_alt = struct.pack(">Q", 0x1122334455667788)
    peer1 = b"\x7f\x00\x00\x01" + struct.pack(">H", 6881)
    peer2 = b"\x0a\x00\x00\x01" + struct.pack(">H", 51413)
    announce_base = (
        struct.pack(">I", 1800) + struct.pack(">I", 10) + struct.pack(">I", 50)
    )
    announce_1_peer = announce_base + peer1
    announce_2_peers = announce_base + peer1 + peer2
    announce_bad_stride = announce_base + b"\x01\x02\x03"
    err_msg = b"failure reason"

    add("connect_ok", _udp_frame(action_connect, connect_ok))
    add("connect_short", _udp_frame(action_connect, b"\x00" * 7))
    add("connect_empty", _udp_frame(action_connect, b""))
    add("announce_alone", _udp_frame(action_announce, announce_1_peer))
    add("scrape_alone", _udp_frame(action_scrape, b"\x00" * 12))
    add("error_alone", _udp_frame(action_error, err_msg))
    add(
        "connect_tid_mismatch",
        _udp_frame(
            action_connect | ctrl_tid_from_payload,
            struct.pack(">I", 0xDEADBEEF) + connect_ok,
        ),
    )
    add(
        "connect_src_mismatch",
        _udp_frame(action_connect | ctrl_src_mismatch, connect_ok),
    )
    add(
        "connect_tid_and_src_mismatch",
        _udp_frame(
            action_connect | ctrl_tid_from_payload | ctrl_src_mismatch,
            struct.pack(">I", 1) + connect_ok,
        ),
    )
    add(
        "seq_connect_then_announce_1_peer",
        _udp_frame(action_connect, connect_ok),
        _udp_frame(action_announce, announce_1_peer),
    )
    add(
        "seq_connect_then_announce_2_peers",
        _udp_frame(action_connect, connect_alt),
        _udp_frame(action_announce, announce_2_peers),
    )
    add(
        "seq_connect_then_announce_bad_stride",
        _udp_frame(action_connect, connect_ok),
        _udp_frame(action_announce, announce_bad_stride),
    )
    add(
        "seq_connect_then_error",
        _udp_frame(action_connect, connect_ok),
        _udp_frame(action_error, err_msg),
    )
    add(
        "seq_connect_then_announce_tid_override",
        _udp_frame(action_connect, connect_ok),
        _udp_frame(
            action_announce | ctrl_tid_from_payload,
            struct.pack(">I", 0x12345678) + announce_1_peer,
        ),
    )
    add(
        "seq_connect_then_announce_src_mismatch",
        _udp_frame(action_connect, connect_ok),
        _udp_frame(action_announce | ctrl_src_mismatch, announce_1_peer),
    )
    add("announce_too_short_11", _udp_frame(action_announce, b"\x00" * 11))
    add("announce_too_short_0", _udp_frame(action_announce, b""))
    add(
        "announce_negativeish_fields",
        _udp_frame(
            action_announce,
            struct.pack(">I", 0xFFFFFFFF)
            + struct.pack(">I", 0x80000000)
            + struct.pack(">I", 0x7FFFFFFF),
        ),
    )
    add("error_empty_payload", _udp_frame(action_error, b""))
    add("error_large_payload", _udp_frame(action_error, b"A" * 512))
    add(
        "seq_mixed_actions",
        _udp_frame(action_announce, b"\x00"),
        _udp_frame(action_connect, connect_ok),
        _udp_frame(action_scrape, b"\x01\x02\x03\x04"),
        _udp_frame(action_announce, announce_2_peers),
        _udp_frame(action_error, b"oops"),
    )

    print(f"Generated {c.count} udp_tracker corpus files in {outdir}")


def generate_pe_crypto_state(outdir: str) -> None:
    c = Corpus(outdir)
    add = c.add

    # In fuzzers/src/pe_crypto_state.cpp, the first byte controls
    # crypto_receive_buffer::crypto_reset(packet_size) via data[0] % 32.
    # The remaining bytes feed key derivation and encrypted payload content.
    add("minimal_5_bytes", b"\x00\x00\x00\x00\x00")
    add("reset_zero", bytes([0x00, 0x01, 0x02, 0x03, 0x04]))
    add("reset_small", bytes([0x01, 0x01, 0x02, 0x1A, 0xE1]))
    add("reset_mid", bytes([0x10, 0x07, 0x07, 0x13, 0x37]) + b"userpass")
    add("reset_max_31", bytes([0x1F, 0x00, 0x00, 0xFF, 0xFF]) + b"\x00" * 32)

    add("zeros_64", bytes([0x00, 0x00, 0x00, 0x00, 0x00]) + b"\x00" * 59)
    add("ones_64", bytes([0x1F, 0x07, 0x07, 0x00, 0x50]) + b"\xff" * 59)
    add(
        "incrementing_256",
        bytes([0x18, 0x07, 0x06, 0x23, 0x45]) + bytes(i & 0xFF for i in range(251)),
    )
    add(
        "alternating_256",
        bytes([0x0C, 0x05, 0x04, 0xC0, 0xDE]) + bytes([0xAA, 0x55] * 125 + [0xAA]),
    )

    print(f"Generated {c.count} pe_crypto_state corpus files in {outdir}")


def generate_pe_conn(outdir: str) -> None:
    """Seed corpus for fuzzers/src/pe_conn.cpp.

    Each file exercises one path through the PE handshake state machine.

    Wire format (4-byte header + optional payload):
      Byte 0 : pad_a_size   -- PadA bytes before sync hash (0-255)
      Byte 1 : flags
                 bit 0 : corrupt_sync_hash  -> sync_hash_not_found
                 bit 1 : corrupt_skey       -> invalid_info_hash
                 bit 2 : corrupt_vc         -> invalid_encryption_constant
                 bits 3-4 : crypto_provide selector
                            00 -> pe_both (0x03)
                            01 -> pe_plaintext (0x01)
                            10 -> pe_rc4 (0x02)
                            11 -> 0x00 (invalid) -> unsupported_encryption_mode
      Byte 2 : len_pad_c * 2  -- encrypted padding bytes (0-510)
      Byte 3 : len_ia         -- IA size (0-255; server rejects > 68)
      Bytes 4+ : PadA content (first pad_a_size bytes), then IA content
    """
    c = Corpus(outdir)
    add = c.add

    # flags byte: bits 3-4 encode crypto_provide
    FLAGS_BOTH = 0x00  # pe_both (0x03)
    FLAGS_PLAIN = 0x08  # pe_plaintext (0x01)
    FLAGS_RC4 = 0x10  # pe_rc4 (0x02)
    FLAGS_INVALID_CP = 0x18  # crypto_provide = 0x00 -> unsupported_encryption_mode

    # Happy path: both crypto modes offered, no padding, no IA
    add("happy_both_no_ia", bytes([0, FLAGS_BOTH, 0, 0]))

    # Happy path: plaintext only
    add("happy_plain_no_ia", bytes([0, FLAGS_PLAIN, 0, 0]))

    # Happy path: RC4 only
    add("happy_rc4_no_ia", bytes([0, FLAGS_RC4, 0, 0]))

    # Happy path: with full BT handshake as IA (len_ia = 68)
    # IA content: "\x13BitTorrent protocol" + 8 zero extension bytes +
    #             20 zero info hash bytes + 20 zero peer ID bytes
    bt_hs = b"\x13BitTorrent protocol" + b"\x00" * 48
    assert len(bt_hs) == 68
    add("happy_both_with_bt_handshake", bytes([0, FLAGS_BOTH, 0, 68]) + bt_hs)

    # Happy path: RC4 with full BT handshake as IA
    add("happy_rc4_with_bt_handshake", bytes([0, FLAGS_RC4, 0, 68]) + bt_hs)

    # Error: invalid crypto_provide (0x00) -> unsupported_encryption_mode
    add("error_invalid_crypto_provide", bytes([0, FLAGS_INVALID_CP, 0, 0]))

    # Error: corrupt sync hash -> sync_hash_not_found
    add("error_corrupt_sync_hash", bytes([0, FLAGS_BOTH | 0x01, 0, 0]))

    # Error: corrupt skey hash -> invalid_info_hash
    add("error_corrupt_skey", bytes([0, FLAGS_BOTH | 0x02, 0, 0]))

    # Error: corrupt VC -> invalid_encryption_constant
    add("error_corrupt_vc", bytes([0, FLAGS_BOTH | 0x04, 0, 0]))

    # Error: len_ia > 68 -> invalid_encrypt_handshake
    add("error_len_ia_too_large", bytes([0, FLAGS_BOTH, 0, 200]) + b"\x00" * 200)

    # Error: len_pad_c > 512 -> invalid_pad_size
    # len_pad_c byte = 255, actual = 255 * 2 = 510 (within range, max valid)
    add("max_valid_pad_c", bytes([0, FLAGS_BOTH, 255, 0]))

    # PadA exercises the sync-hash scanning loop.
    # pad_a_size = 10 means 10 bytes are inserted between DH key and sync hash.
    add("pad_a_10_bytes", bytes([10, FLAGS_BOTH, 0, 0]) + b"\x00" * 10)

    # PadA = 255 bytes (maximum single-byte value)
    add("pad_a_255_bytes", bytes([255, FLAGS_BOTH, 0, 0]) + b"\x00" * 255)

    # len_ia = 0: no IA, server skips read_pe_ia and goes directly to
    # read_protocol_identifier (same effect as both cases where pad transitions
    # directly)
    add("zero_ia_both", bytes([0, FLAGS_BOTH, 0, 0]))

    # len_ia = 1: minimal non-zero IA (server reads read_pe_ia state)
    add("ia_1_byte", bytes([0, FLAGS_BOTH, 0, 1]) + b"\x00")

    # len_ia = 68: maximum valid IA (full BT handshake)
    add("ia_68_bytes_zeros", bytes([0, FLAGS_BOTH, 0, 68]) + b"\x00" * 68)

    # Encrypted padding (len_pad_c = 1 byte * 2 = 2 bytes)
    add("pad_c_2_bytes", bytes([0, FLAGS_BOTH, 1, 0]))

    # Combined: PadA + RC4 + encrypted padding + IA
    add(
        "combined_pad_a_rc4_pad_c_ia",
        bytes([5, FLAGS_RC4, 2, 20]) + b"\xab" * 5 + b"\x00" * 20,
    )

    # All corruption bits set (corrupt_sync + corrupt_skey + corrupt_vc)
    add("all_corruptions", bytes([0, FLAGS_BOTH | 0x07, 0, 0]))

    print(f"Generated {c.count} pe_conn corpus files in {outdir}")


def generate_web_seed(outdir: str) -> None:
    """Seed corpus for fuzzers/src/web_seed.cpp.

    Each file is the raw bytes that the fuzzer sends as an HTTP response to
    libtorrent's web-seed (BEP 19) GET request. There is no wrapper -- the
    bytes are written directly to the socket. The corpus exercises the HTTP
    response parser in web_peer_connection.cpp.

    libtorrent sends requests like:
      GET /test_file HTTP/1.1
      Host: 127.0.0.1:<port>
      Range: bytes=0-16383
    """
    c = Corpus(outdir)
    add = c.add

    CRLF = b"\r\n"

    def http_response(status: str, headers: dict, body: bytes = b"") -> bytes:
        lines = [f"HTTP/1.1 {status}".encode()]
        for k, v in headers.items():
            lines.append(f"{k}: {v}".encode())
        return CRLF.join(lines) + CRLF + CRLF + body

    # valid 206 Partial Content -- correct response for a Range request
    block = bytes(16384)
    add(
        "206_partial_content",
        http_response(
            "206 Partial Content",
            {
                "Content-Range": "bytes 0-16383/4194304",
                "Content-Length": "16384",
                "Content-Type": "application/octet-stream",
            },
            block,
        ),
    )

    # 206 with a second block (offset 16384)
    add(
        "206_second_block",
        http_response(
            "206 Partial Content",
            {
                "Content-Range": "bytes 16384-32767/4194304",
                "Content-Length": "16384",
            },
            block,
        ),
    )

    # 206 with wrong Content-Length (claims more than we sent)
    add(
        "206_content_length_too_large",
        http_response(
            "206 Partial Content",
            {
                "Content-Range": "bytes 0-16383/4194304",
                "Content-Length": "32768",
            },
            block,
        ),
    )

    # 206 with Content-Range end > total size
    add(
        "206_invalid_range_end",
        http_response(
            "206 Partial Content",
            {
                "Content-Range": "bytes 0-4194303/4194304",
                "Content-Length": "4194304",
            },
        ),
    )

    # 206 with missing Content-Range header
    add(
        "206_missing_content_range",
        http_response(
            "206 Partial Content",
            {"Content-Length": "16384"},
            block,
        ),
    )

    # 200 OK with full content (exercises non-range path)
    add(
        "200_ok_full_content",
        http_response(
            "200 OK",
            {"Content-Length": "16384"},
            block,
        ),
    )

    # 200 OK with Transfer-Encoding: chunked
    chunk_data = b"4000\r\n" + block + b"\r\n0\r\n\r\n"
    add(
        "200_chunked",
        http_response(
            "200 OK",
            {"Transfer-Encoding": "chunked"},
            chunk_data,
        ),
    )

    # 200 OK with no body
    add("200_no_body", http_response("200 OK", {"Content-Length": "0"}))

    # 200 OK with Connection: close (no keepalive)
    add(
        "200_connection_close",
        http_response(
            "200 OK",
            {"Content-Length": "16384", "Connection": "close"},
            block,
        ),
    )

    # 301 Moved Permanently (exercises redirect handling)
    add(
        "301_redirect",
        http_response(
            "301 Moved Permanently",
            {"Location": "http://127.0.0.1:1/test_file"},
        ),
    )

    # 302 Found (temporary redirect)
    add(
        "302_redirect",
        http_response(
            "302 Found",
            {"Location": "http://127.0.0.1:1/other_file"},
        ),
    )

    # 301 with missing Location header
    add(
        "301_no_location",
        http_response("301 Moved Permanently", {}),
    )

    # 304 Not Modified
    add("304_not_modified", http_response("304 Not Modified", {}))

    # 400 Bad Request
    add(
        "400_bad_request",
        http_response("400 Bad Request", {"Content-Length": "0"}),
    )

    # 403 Forbidden
    add(
        "403_forbidden",
        http_response(
            "403 Forbidden",
            {"Content-Length": "9"},
            b"Forbidden",
        ),
    )

    # 404 Not Found
    add(
        "404_not_found",
        http_response(
            "404 Not Found",
            {"Content-Length": "9"},
            b"Not Found",
        ),
    )

    # 503 Service Unavailable with Retry-After
    add(
        "503_retry_after",
        http_response(
            "503 Service Unavailable",
            {"Retry-After": "60", "Content-Length": "0"},
        ),
    )

    # 503 without Retry-After
    add(
        "503_no_retry",
        http_response("503 Service Unavailable", {"Content-Length": "0"}),
    )

    # HTTP/1.0 response (no keepalive)
    add(
        "http10_206",
        b"HTTP/1.0 206 Partial Content\r\n"
        b"Content-Range: bytes 0-16383/4194304\r\n"
        b"Content-Length: 16384\r\n"
        b"\r\n" + block,
    )

    # headers only (no body -- exercises truncated response)
    add(
        "headers_only_206",
        b"HTTP/1.1 206 Partial Content\r\n"
        b"Content-Range: bytes 0-16383/4194304\r\n"
        b"Content-Length: 16384\r\n"
        b"\r\n",
    )

    # malformed status line
    add("malformed_status_line", b"NOT HTTP\r\n\r\n")

    # empty response
    add("empty", b"")

    # only CRLF
    add("only_crlf", b"\r\n\r\n")

    # status line with no headers
    add("status_only", b"HTTP/1.1 200 OK\r\n\r\n")

    # garbage binary data
    add("garbage", bytes(range(256)))

    # 200 with very large Content-Length claim (no body)
    add(
        "200_huge_content_length",
        http_response(
            "200 OK",
            {"Content-Length": "999999999"},
        ),
    )

    # 206 with content-range starting past the end of the file
    add(
        "206_range_past_eof",
        http_response(
            "206 Partial Content",
            {
                "Content-Range": "bytes 9999999-10000000/4194304",
                "Content-Length": "1",
            },
            b"\x00",
        ),
    )

    # 206 with negative range (malformed Content-Range)
    add(
        "206_negative_range",
        http_response(
            "206 Partial Content",
            {
                "Content-Range": "bytes -1-16383/4194304",
                "Content-Length": "16384",
            },
            block,
        ),
    )

    print(f"Generated {c.count} web_seed corpus files in {outdir}")


# Constants matching fuzzers/src/utp_stream.cpp
_UTP_HDR_FMT = (
    ">BBHIIIHH"  # 20 bytes: type_ver, ext, conn_id, ts, ts_diff, wnd, seq, ack
)
_UTP_HDR_SIZE = struct.calcsize(_UTP_HDR_FMT)
_UTP_VERSION = 1
_UTP_SERVER_RECV_ID = 2  # must match SERVER_RECV_ID in utp_stream.cpp
_UTP_SERVER_SEND_ID = 1  # must match SERVER_SEND_ID in utp_stream.cpp
_UTP_CLIENT_ISN = 1  # must match CLIENT_ISN in utp_stream.cpp
# After bootstrap SYN (seq=CLIENT_ISN), next expected client seq is CLIENT_ISN+1
_UTP_CLIENT_NEXT_SEQ = _UTP_CLIENT_ISN + 1

# Packet types (utp_socket_state_t enum)
_ST_DATA = 0
_ST_FIN = 1
_ST_STATE = 2
_ST_RESET = 3
_ST_SYN = 4

# Extension types (utp_extensions_t enum)
_UTP_NO_EXT = 0
_UTP_SACK = 1
_UTP_CLOSE_REASON = 3


def _utp_pkt(
    pkt_type: int,
    seq_nr: int,
    ack_nr: int,
    connection_id: int = _UTP_SERVER_RECV_ID,
    wnd_size: int = 0x100000,
    timestamp: int = 0,
    timestamp_diff: int = 0,
    extension: int = _UTP_NO_EXT,
    payload: bytes = b"",
) -> bytes:
    """Build a raw uTP packet (header + payload) in network byte order."""
    type_ver = (pkt_type << 4) | _UTP_VERSION
    header = struct.pack(
        _UTP_HDR_FMT,
        type_ver,
        extension,
        connection_id & 0xFFFF,
        timestamp & 0xFFFFFFFF,
        timestamp_diff & 0xFFFFFFFF,
        wnd_size & 0xFFFFFFFF,
        seq_nr & 0xFFFF,
        ack_nr & 0xFFFF,
    )
    return header + payload


def _utp_action(raw: bytes) -> bytes:
    """Encode a raw uTP packet in corpus wire format: [ctrl:1][packet:ctrl].

    ctrl is the packet length and must be 1-255.
    """
    assert 1 <= len(raw) <= 255, f"packet must be 1-255 bytes, got {len(raw)}"
    return bytes([len(raw)]) + raw


def _utp_ticks(n: int = 1) -> bytes:
    """Encode n 1ms time advances (ctrl==0 each)."""
    return b"\x00" * n


def _utp_sack_ext(bitfield_len: int = 4, next_ext: int = _UTP_NO_EXT) -> bytes:
    """Build a SACK extension header payload: [next_type:1][len:1][bitfield:len]."""
    assert bitfield_len % 4 == 0 and bitfield_len >= 4
    return bytes([next_ext, bitfield_len]) + bytes(bitfield_len)


def generate_utp_stream(outdir: str) -> None:
    """Seed corpus for fuzzers/src/utp_stream.cpp.

    Wire format (repeated until EOF):
      1 byte: ctrl
        ctrl == 0  -> advance time 1ms, call tick()
        ctrl >  0  -> next ctrl bytes fed as one raw uTP packet

    The fuzzer bootstraps the socket to CONNECTED state via a manually crafted
    SYN before processing corpus bytes. After bootstrap:
      - Server recv_id = 2, send_id = 1
      - Packets with connection_id != 2 are rejected (except SYN which uses 1)
      - Next expected seq_nr from client = CLIENT_ISN + 1 = 2
    """
    c = Corpus(outdir)
    add = c.add

    a = _utp_action  # encode one packet as a corpus action
    t = _utp_ticks  # encode time advances

    # Shortcuts for common in-order packet builds
    def state(
        ack_nr: int = 1, seq_nr: int = _UTP_CLIENT_NEXT_SEQ, **kw: object
    ) -> bytes:
        return a(_utp_pkt(_ST_STATE, seq_nr, ack_nr, **kw))  # type: ignore[arg-type]

    def data(
        payload: bytes,
        seq_nr: int = _UTP_CLIENT_NEXT_SEQ,
        ack_nr: int = 1,
        **kw: object,
    ) -> bytes:
        p = _utp_pkt(
            _ST_DATA, seq_nr, ack_nr, payload=payload, **kw  # type: ignore[arg-type]
        )
        return a(p)

    def fin(seq_nr: int = _UTP_CLIENT_NEXT_SEQ, ack_nr: int = 1) -> bytes:
        return a(_utp_pkt(_ST_FIN, seq_nr, ack_nr))

    def reset(seq_nr: int = _UTP_CLIENT_NEXT_SEQ, ack_nr: int = 1) -> bytes:
        return a(_utp_pkt(_ST_RESET, seq_nr, ack_nr))

    small = bytes(i % 251 for i in range(8))  # 8-byte predictable payload
    medium = bytes(i % 251 for i in range(64))  # 64-byte predictable payload
    # max payload that fits in a single corpus action (255 - 20-byte header)
    large = bytes(i % 251 for i in range(235))

    # --- Time advances only ---
    add("tick_1ms", t(1))
    add("tick_10ms", t(10))
    add("tick_50ms", t(50))

    # --- Single bare packets ---

    # ST_STATE (pure ACK): the most common packet after connection established
    add("state_ack", state())
    add("state_ack_wnd_zero", state(wnd_size=0))  # zero window
    add("state_ack_wnd_max", state(wnd_size=0xFFFFFFFF))
    add(
        "state_ack_with_timestamps",
        a(
            _utp_pkt(
                _ST_STATE,
                _UTP_CLIENT_NEXT_SEQ,
                1,
                timestamp=12345678,
                timestamp_diff=50000,
            )
        ),
    )

    # ST_DATA packets
    add("data_empty", data(b""))  # no payload (header only)
    add("data_small", data(small))
    add("data_medium", data(medium))
    add("data_large_235", data(large))  # maximum single-action size

    # ST_FIN: remote peer closes the connection
    add("fin_bare", fin())
    add("fin_with_data", a(_utp_pkt(_ST_FIN, _UTP_CLIENT_NEXT_SEQ, 1, payload=small)))

    # ST_RESET
    add("reset_bare", reset())

    # ST_SYN re-injected after bootstrap (should be handled gracefully)
    add(
        "syn_reinjected",
        a(_utp_pkt(_ST_SYN, _UTP_CLIENT_ISN, 0, connection_id=_UTP_SERVER_SEND_ID)),
    )

    # --- Invalid / boundary connection_id values ---
    add(
        "wrong_conn_id_0",
        a(_utp_pkt(_ST_DATA, _UTP_CLIENT_NEXT_SEQ, 1, connection_id=0, payload=small)),
    )
    add(
        "wrong_conn_id_99",
        a(_utp_pkt(_ST_DATA, _UTP_CLIENT_NEXT_SEQ, 1, connection_id=99, payload=small)),
    )
    # SERVER_SEND_ID (1): valid for SYN but not for DATA
    add(
        "wrong_conn_id_1",
        a(_utp_pkt(_ST_DATA, _UTP_CLIENT_NEXT_SEQ, 1, connection_id=1, payload=small)),
    )

    # --- Truncated packets (fewer bytes than utp_header = 20) ---
    add("truncated_1", bytes([1, 0x01]))  # 1 byte
    add("truncated_10", bytes([10]) + bytes(10))  # 10 bytes
    add("truncated_19", bytes([19]) + bytes(19))  # 19 bytes (one short of header)

    # --- seq_nr edge cases ---
    # Out-of-order: seq_nr = CLIENT_NEXT_SEQ + 1 (gap of one)
    add("data_ooo_seq3", data(small, seq_nr=_UTP_CLIENT_NEXT_SEQ + 1))
    # Old packet: seq_nr before next expected
    add("data_old_seq", data(small, seq_nr=_UTP_CLIENT_ISN))
    # seq_nr wrap-around boundary
    add("data_seq_max", data(small, seq_nr=0xFFFF))
    add("data_seq_0", data(small, seq_nr=0))
    # ack_nr at extremes
    add("data_ack_max", data(small, ack_nr=0xFFFF))
    add("data_ack_0", data(small, ack_nr=0))

    # --- Extension headers ---
    # ST_STATE with SACK extension (4-byte bitfield)
    sack = _utp_sack_ext(4)
    add(
        "state_with_sack",
        a(
            _utp_pkt(
                _ST_STATE, _UTP_CLIENT_NEXT_SEQ, 1, extension=_UTP_SACK, payload=sack
            )
        ),
    )
    # ST_DATA with SACK extension before data payload
    add(
        "data_with_sack",
        a(
            _utp_pkt(
                _ST_DATA,
                _UTP_CLIENT_NEXT_SEQ,
                1,
                extension=_UTP_SACK,
                payload=sack + small,
            )
        ),
    )
    # Extension declared but no extension bytes follow (truncated)
    add(
        "state_sack_truncated_ext",
        a(_utp_pkt(_ST_STATE, _UTP_CLIENT_NEXT_SEQ, 1, extension=_UTP_SACK)),
    )
    # close_reason extension (4-byte reason code, network byte order)
    close_reason_ext = bytes([0, 4]) + struct.pack(">I", 1)  # reason = 1
    add(
        "state_close_reason",
        a(
            _utp_pkt(
                _ST_STATE,
                _UTP_CLIENT_NEXT_SEQ,
                1,
                extension=_UTP_CLOSE_REASON,
                payload=close_reason_ext,
            )
        ),
    )
    # Unknown extension type
    add(
        "state_unknown_ext",
        a(
            _utp_pkt(
                _ST_STATE,
                _UTP_CLIENT_NEXT_SEQ,
                1,
                extension=99,
                payload=bytes([0, 4]) + bytes(4),
            )
        ),
    )

    # --- Multi-action sequences ---

    # Time advance then data delivery (idle gap before packet)
    add("seq_tick_then_data", t(5), data(small))

    # In-order DATA stream: seq 2, 3, 4
    add(
        "seq_data_3_inorder",
        data(small, seq_nr=_UTP_CLIENT_NEXT_SEQ),
        data(small, seq_nr=_UTP_CLIENT_NEXT_SEQ + 1),
        data(small, seq_nr=_UTP_CLIENT_NEXT_SEQ + 2),
    )

    # Out-of-order then in-order (exercises reorder buffer)
    add(
        "seq_data_ooo_reorder",
        data(small, seq_nr=_UTP_CLIENT_NEXT_SEQ + 2),
        data(small, seq_nr=_UTP_CLIENT_NEXT_SEQ + 1),
        data(small, seq_nr=_UTP_CLIENT_NEXT_SEQ),
    )

    # Duplicate DATA (same seq_nr twice)
    add("seq_data_duplicate", data(small), data(small))

    # DATA + time advance + ACK (normal flow with RTT sample)
    add("seq_data_tick_ack", data(medium), t(5), state())

    # DATA + FIN (peer sends data then closes)
    add("seq_data_then_fin", data(small), fin(seq_nr=_UTP_CLIENT_NEXT_SEQ + 1))

    # Many ticks (exercises retransmit / keep-alive timers)
    add("seq_many_ticks_100ms", t(100))

    # DATA, tick, DATA, tick, ... (interleaved with time, exercises ACK coalescing)
    add(
        "seq_data_interleaved",
        data(small, seq_nr=_UTP_CLIENT_NEXT_SEQ),
        t(2),
        data(small, seq_nr=_UTP_CLIENT_NEXT_SEQ + 1),
        t(2),
        data(small, seq_nr=_UTP_CLIENT_NEXT_SEQ + 2),
    )

    # STATE with zero window then non-zero (window probe path)
    add("seq_zero_window_then_open", state(wnd_size=0), t(10), state(wnd_size=0x100000))

    # RESET after some data
    add("seq_data_then_reset", data(small), reset())

    # FIN then another packet (post-FIN handling)
    add("seq_fin_then_state", fin(), state())

    # Sequence of 10 in-order DATA packets
    add(
        "seq_data_10_inorder",
        *[data(small, seq_nr=_UTP_CLIENT_NEXT_SEQ + i) for i in range(10)],
    )

    print(f"Generated {c.count} utp_stream corpus files in {outdir}")


def _upnp_http_ok(body: bytes) -> bytes:
    return (
        b"HTTP/1.1 200 OK\r\n"
        b"Content-Type: text/xml; charset=utf-8\r\n"
        b"Content-Length: " + str(len(body)).encode() + b"\r\n"
        b"\r\n" + body
    )


def _upnp_soap_response(action: str, ns: str, content: str = "") -> bytes:
    body = (
        '<?xml version="1.0"?>'
        '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
        's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
        "<s:Body>"
        f'<u:{action}Response xmlns:u="{ns}">'
        f"{content}"
        f"</u:{action}Response>"
        "</s:Body>"
        "</s:Envelope>"
    )
    return body.encode()


def _upnp_soap_error(code: int, desc: str = "Error") -> bytes:
    body = (
        '<?xml version="1.0"?>'
        '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
        's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
        "<s:Body>"
        "<s:Fault>"
        "<faultcode>s:Client</faultcode>"
        "<faultstring>UPnPError</faultstring>"
        "<detail>"
        '<UPnPError xmlns="urn:schemas-upnp-org:control-1-0">'
        f"<errorCode>{code}</errorCode>"
        f"<errorDescription>{desc}</errorDescription>"
        "</UPnPError>"
        "</detail>"
        "</s:Fault>"
        "</s:Body>"
        "</s:Envelope>"
    )
    return body.encode()


def generate_upnp(outdir: str) -> None:
    """Generate seed corpus for the UPnP session-level fuzzer.

    Wire format: length-prefixed HTTP responses (2-byte big-endian length).
    The fuzzer always hard-codes the first HTTP connection (XML description)
    and uses these messages for the subsequent SOAP connections:
      [0]  GetExternalIPAddress response
      [1]  AddPortMapping response (mapping 0 -- tcp/6881)
      [2]  AddPortMapping response (mapping 1 -- udp/6882)
      [3+] Additional responses for retries or DeletePortMapping connections.
    """
    c = Corpus(outdir, frame=_len_prefixed)
    add = c.add

    ns = "urn:schemas-upnp-org:service:WANIPConnection:1"
    get_ip_ok = _upnp_http_ok(
        _upnp_soap_response(
            "GetExternalIPAddress",
            ns,
            "<NewExternalIPAddress>1.2.3.4</NewExternalIPAddress>",
        )
    )
    add_ok = _upnp_http_ok(_upnp_soap_response("AddPortMapping", ns))
    del_ok = _upnp_http_ok(_upnp_soap_response("DeletePortMapping", ns))

    # Happy path: all SOAP calls succeed
    add("upnp_all_ok", get_ip_ok, add_ok, add_ok, del_ok, del_ok)

    # GetExternalIPAddress returns HTTP 500; mapping still attempted
    add(
        "upnp_get_ip_fail",
        b"HTTP/1.1 500 Internal Server Error\r\n\r\n",
        add_ok,
        add_ok,
        del_ok,
        del_ok,
    )

    # Error 725: gateway only supports permanent leases -- retry without
    # lease duration.
    add(
        "upnp_error_725",
        get_ip_ok,
        _upnp_http_ok(_upnp_soap_error(725, "Only Permanent Leases Supported")),
        add_ok,  # retry for mapping 0
        add_ok,  # mapping 1
        del_ok,
        del_ok,
    )

    # Error 718: port-mapping conflict -- retry with random external port
    add(
        "upnp_error_718",
        get_ip_ok,
        _upnp_http_ok(_upnp_soap_error(718, "Port Mapping Conflict")),
        add_ok,
        add_ok,
        del_ok,
        del_ok,
    )

    # Error 501: action failed -- treated like 718, retry with random port
    add(
        "upnp_error_501",
        get_ip_ok,
        _upnp_http_ok(_upnp_soap_error(501, "Action Failed")),
        add_ok,
        add_ok,
        del_ok,
        del_ok,
    )

    # Error 727: external port must be wildcard -- report error immediately
    add(
        "upnp_error_727",
        get_ip_ok,
        _upnp_http_ok(_upnp_soap_error(727, "ExternalPort Must Be Wildcard")),
        add_ok,  # mapping 1 still proceeds
        del_ok,
    )

    # Malformed HTTP (exercises incomplete-header error paths)
    add("upnp_malformed", b"not http at all", b"also not http")

    # Empty connections (EOF with no data -- exercises missing-header paths)
    add("upnp_empty", b"", b"", b"", b"", b"")

    print(f"Generated {c.count} upnp corpus files in {outdir}")


def _merkle_num_leafs(n: int) -> int:
    if n <= 1:
        return 1
    p = 1
    while p < n:
        p <<= 1
    return p


def _merkle_root(leaves: list[bytes], pad: bytes = bytes(32)) -> bytes:
    """Mirror lt::merkle_root(): takes a span of hashes and computes the root.

    The C++ fuzzer passes a vector of size merkle_num_nodes(blocks_per_piece)
    where only the first blocks_per_piece entries are real block hashes.  This
    function replicates that behaviour exactly so the Python seeds agree with
    the C++ pre-built torrent.
    """
    import hashlib

    def h(a: bytes, b: bytes) -> bytes:
        return hashlib.sha256(a + b).digest()

    n = len(leaves)
    if n == 0:
        return bytes(32)
    if n == 1:
        return bytes(leaves[0])
    num_leafs = _merkle_num_leafs(n)
    cur: list[bytes] = list(leaves)
    while num_leafs > 1:
        nxt = []
        for j in range(len(cur) // 2):
            nxt.append(h(cur[j * 2], cur[j * 2 + 1]))
        if len(cur) & 1:
            nxt.append(h(cur[-1], pad))
        pad = h(pad, pad)
        cur = nxt
        num_leafs //= 2
    return bytes(cur[0])


def _piece_layers_valid_seed() -> bytes:
    """Compute the bencoded 'piece layers' dict that matches the torrent built
    by LLVMFuzzerInitialize in fuzzers/src/piece_layers.cpp.

    The torrent has 1 file, 4 pieces of 256 KiB each, 16 blocks per piece.
    Content of piece i: all bytes = i.
    The C++ code fills piece_tree[0..15] with block hashes, then passes the
    full 31-element vector (zeros in positions 16..30) to merkle_root().
    """
    import hashlib

    block_size = 16384
    blocks_per_piece = 16  # 256 KiB / 16 KiB
    merkle_nodes = 2 * blocks_per_piece - 1  # 31

    def sha256(data: bytes) -> bytes:
        return hashlib.sha256(data).digest()

    piece_roots = []
    for pi in range(4):
        block_hash = sha256(bytes([pi]) * block_size)
        # Replicate: piece_tree[0..bpp-1] = block_hash, [bpp..2bpp-2] = zeros
        piece_tree = [block_hash] * blocks_per_piece + [bytes(32)] * (
            merkle_nodes - blocks_per_piece
        )
        piece_roots.append(_merkle_root(piece_tree))

    file_root = _merkle_root(piece_roots)

    value = b"".join(piece_roots)
    # bencoded: d 32:<file_root> 128:<value> e
    return b"d" + b"32:" + file_root + (f"{len(value)}:").encode() + value + b"e"


def generate_piece_layers(outdir: str) -> None:
    """Seed corpus for fuzzers/src/piece_layers.cpp.

    The fuzz input is the raw bencoded value that sits after the
    '12:piece layers' key inside a torrent dict whose 'info' section
    is pre-built by LLVMFuzzerInitialize.
    """
    c = Corpus(outdir)
    add = c.add

    # empty bencoded dict -- parse_piece_layers called but dict is empty
    add("empty_dict", b"de")

    # not a dict: dict_find_dict() returns null, piece layers skipped
    add("integer_value", b"i0e")

    # dict with a key that is too short (31 bytes instead of 32)
    add("short_key", b"d31:" + b"A" * 31 + b"0:e")

    # dict with a 32-byte key but empty value (wrong piece count)
    add("key_only", b"d32:" + b"A" * 32 + b"0:e")

    # dict with 32-byte key but value not a multiple of 32 bytes
    add("bad_value_length", b"d32:" + b"A" * 32 + b"10:" + b"B" * 10 + b"e")

    # dict with 32-byte key and value exactly 4*32=128 bytes but wrong hashes
    # (key does not match any file root, so torrent_invalid_piece_layer)
    add("unknown_root", b"d32:" + b"A" * 32 + b"128:" + b"B" * 128 + b"e")

    # valid piece layers that exactly matches the torrent built by
    # LLVMFuzzerInitialize -- exercises the full happy path in parse_piece_layers
    add("valid", _piece_layers_valid_seed())

    print(f"Generated {c.count} piece_layers corpus files in {outdir}")


def _ti_v1_pieces(num_pieces: int) -> bytes:
    """A num_pieces*20-byte placeholder for the v1 'pieces' field. Content is
    not validated by torrent_info parsing -- only the length matters."""
    return bytes(num_pieces * 20)


def _ti_v2_root(seed: int) -> bytes:
    """A non-zero 32-byte 'pieces root'. extract_single_file2 rejects
    all-zero roots for non-empty files, so we mark byte 0 with the seed."""
    return bytes([seed & 0xFF]) + bytes(31)


def generate_torrent_info(outdir: str) -> None:
    """Seed corpus for fuzzers/src/torrent_info.cpp.

    The fuzz input is a raw bencoded .torrent file passed straight to
    load_torrent_buffer. These seeds cover the feature surface enumerated
    by the parser in src/torrent_info.cpp and src/load_torrent.cpp:
      v1 / v2 / hybrid; flat files list vs file tree; piece layers;
      collections / similar (in-info + out-of-info); trackers in one
      tier vs many; i2p trackers; nodes; url-list (str + list);
      symbolic links; filenames with invalid characters.
    """
    import hashlib

    c = Corpus(outdir)

    def add(name: str, torrent: dict) -> None:
        c.add(name, _bencode(torrent))

    # --- v1: single-file ---
    add(
        "v1_single_file",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
            }
        },
    )

    # --- v1: multi-file (flat 'files' list) ---
    add(
        "v1_multi_file",
        {
            "info": {
                "name": "bundle",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "files": [
                    {"length": 50, "path": ["a.txt"]},
                    {"length": 50, "path": ["sub", "b.txt"]},
                ],
            }
        },
    )

    # --- v1: file path with embedded NUL byte (sanitization path) ---
    add(
        "v1_invalid_char_in_path",
        {
            "info": {
                "name": "bundle",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "files": [
                    {"length": 50, "path": [b"bad\x00name.txt"]},
                ],
            }
        },
    )

    # --- v1: torrent name with control characters ---
    add(
        "v1_invalid_char_in_name",
        {
            "info": {
                "name": b"weird\x01\x1fname",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
            }
        },
    )

    # --- v1: path component is ".." (must be sanitized away) ---
    add(
        "v1_dotdot_path",
        {
            "info": {
                "name": "bundle",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "files": [
                    {"length": 50, "path": ["..", "..", "etc", "passwd"]},
                ],
            }
        },
    )

    # --- v1: symbolic link ---
    add(
        "v1_symlink",
        {
            "info": {
                "name": "bundle",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "files": [
                    {"length": 100, "path": ["target.dat"]},
                    {
                        "length": 0,
                        "path": ["link.dat"],
                        "attr": "l",
                        "symlink path": ["target.dat"],
                    },
                ],
            }
        },
    )

    # --- v2: single-file (file tree with one entry) ---
    add(
        "v2_single_file",
        {
            "info": {
                "name": "solo.dat",
                "meta version": 2,
                "piece length": 16384,
                "file tree": {
                    "solo.dat": {"": {"length": 100, "pieces root": _ti_v2_root(1)}},
                },
            }
        },
    )

    # --- v2: multi-file file tree (nested directory) ---
    add(
        "v2_multi_file",
        {
            "info": {
                "name": "bundle",
                "meta version": 2,
                "piece length": 16384,
                "file tree": {
                    "a.txt": {"": {"length": 50, "pieces root": _ti_v2_root(1)}},
                    "sub": {
                        "b.txt": {"": {"length": 50, "pieces root": _ti_v2_root(2)}},
                    },
                },
            }
        },
    )

    # --- v2: symbolic link entry ---
    add(
        "v2_symlink",
        {
            "info": {
                "name": "bundle",
                "meta version": 2,
                "piece length": 16384,
                "file tree": {
                    "target.dat": {"": {"length": 100, "pieces root": _ti_v2_root(1)}},
                    "link.dat": {
                        "": {
                            "length": 0,
                            "attr": "l",
                            "symlink path": ["target.dat"],
                        }
                    },
                },
            }
        },
    )

    # --- v2: file tree depth (multiple nested directories) ---
    add(
        "v2_deep_tree",
        {
            "info": {
                "name": "bundle",
                "meta version": 2,
                "piece length": 16384,
                "file tree": {
                    "a": {
                        "b": {
                            "c": {
                                "deep.dat": {
                                    "": {
                                        "length": 50,
                                        "pieces root": _ti_v2_root(1),
                                    }
                                }
                            }
                        }
                    },
                },
            }
        },
    )

    # --- hybrid v1+v2 (single file, same layout in both views) ---
    add(
        "hybrid_single_file",
        {
            "info": {
                "name": "solo.dat",
                "meta version": 2,
                "piece length": 16384,
                "length": 100,
                "pieces": _ti_v1_pieces(1),
                "file tree": {
                    "solo.dat": {"": {"length": 100, "pieces root": _ti_v2_root(1)}}
                },
            }
        },
    )

    # --- hybrid v1+v2 with valid 'piece layers' ---
    # File of 32 KiB (= 2 * 16 KiB pieces). With piece_length = block_size,
    # piece_root == block_hash, and file_root = SHA256(block0 || block1).
    # The piece layer is the concatenation of the per-piece roots.
    block_size = 16384
    block0 = hashlib.sha256(b"\xaa" * block_size).digest()
    block1 = hashlib.sha256(b"\xbb" * block_size).digest()
    file_root_pl = hashlib.sha256(block0 + block1).digest()
    add(
        "hybrid_with_piece_layers",
        {
            "info": {
                "name": "data.bin",
                "meta version": 2,
                "piece length": block_size,
                "length": 2 * block_size,
                "pieces": _ti_v1_pieces(2),
                "file tree": {
                    "data.bin": {
                        "": {"length": 2 * block_size, "pieces root": file_root_pl}
                    },
                },
            },
            "piece layers": {file_root_pl: block0 + block1},
        },
    )

    # --- collections inside the info dict ---
    add(
        "collections_in_info",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
                "collections": ["public-domain", "linux-isos"],
            }
        },
    )

    # --- collections outside the info dict ---
    add(
        "collections_outside_info",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
            },
            "collections": ["public-domain", "linux-isos"],
        },
    )

    # --- collections in both places ---
    add(
        "collections_both",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
                "collections": ["inside-1", "inside-2"],
            },
            "collections": ["outside-1"],
        },
    )

    # --- similar inside the info dict ---
    add(
        "similar_in_info",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
                "similar": [b"\x11" * 20, b"\x22" * 20],
            }
        },
    )

    # --- similar outside the info dict ---
    add(
        "similar_outside_info",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
            },
            "similar": [b"\x33" * 20, b"\x44" * 20],
        },
    )

    # --- single 'announce' tracker ---
    add(
        "tracker_announce",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
            },
            "announce": "http://tracker.example.com/announce",
        },
    )

    # --- announce-list, multiple trackers in one tier ---
    add(
        "tracker_one_tier",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
            },
            "announce": "http://tracker.example.com/announce",
            "announce-list": [
                [
                    "http://t1.example.com/announce",
                    "http://t2.example.com/announce",
                    "udp://t3.example.com:6969",
                ]
            ],
        },
    )

    # --- announce-list, one tracker per tier (multiple tiers) ---
    add(
        "tracker_multiple_tiers",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
            },
            "announce": "http://tracker.example.com/announce",
            "announce-list": [
                ["http://primary.example.com/announce"],
                ["http://backup1.example.com/announce"],
                ["udp://backup2.example.com:6969"],
            ],
        },
    )

    # --- i2p tracker (.i2p hostname triggers torrent_flags::i2p_torrent) ---
    add(
        "tracker_i2p",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
            },
            "announce": "http://tracker.i2p/announce",
            "announce-list": [
                ["http://primary.i2p/announce"],
                ["http://clearnet.example.com/announce"],
            ],
        },
    )

    # --- DHT bootstrap nodes ---
    add(
        "nodes",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
            },
            "nodes": [
                ["dht.example.com", 6881],
                ["192.0.2.10", 6881],
                ["2001:db8::1", 6881],
            ],
        },
    )

    # --- url-list as a single string (single-file form) ---
    add(
        "url_list_string",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
            },
            "url-list": "http://webseed.example.com/file",
        },
    )

    # --- url-list as a list (multi-file form requires trailing slash) ---
    add(
        "url_list_list",
        {
            "info": {
                "name": "bundle",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "files": [
                    {"length": 50, "path": ["a.txt"]},
                    {"length": 50, "path": ["b.txt"]},
                ],
            },
            "url-list": [
                "http://webseed1.example.com/bundle/",
                "http://webseed2.example.com/bundle/",
            ],
        },
    )

    # --- private flag + comment + creation date + created-by metadata ---
    add(
        "private_with_metadata",
        {
            "info": {
                "name": "test.dat",
                "piece length": 16384,
                "pieces": _ti_v1_pieces(1),
                "length": 100,
                "private": 1,
            },
            "comment": "fuzz seed",
            "created by": "gen_corpus.py",
            "creation date": 1700000000,
        },
    )

    # --- everything-at-once: hybrid with trackers, nodes, url-list,
    # collections in both places, similar in both places, piece layers ---
    add(
        "kitchen_sink",
        {
            "info": {
                "name": "kitchen",
                "meta version": 2,
                "piece length": block_size,
                "length": 2 * block_size,
                "pieces": _ti_v1_pieces(2),
                "file tree": {
                    "kitchen": {
                        "": {
                            "length": 2 * block_size,
                            "pieces root": file_root_pl,
                        }
                    },
                },
                "collections": ["info-side"],
                "similar": [b"\x55" * 20],
            },
            "piece layers": {file_root_pl: block0 + block1},
            "announce": "http://primary.i2p/announce",
            "announce-list": [
                [
                    "http://t1.example.com/announce",
                    "http://t2.example.com/announce",
                ],
                ["udp://backup.example.com:6969"],
            ],
            "nodes": [["dht.example.com", 6881]],
            "url-list": "http://webseed.example.com/kitchen",
            "collections": ["outside"],
            "similar": [b"\x66" * 20],
            "comment": "everything",
            "created by": "gen_corpus.py",
            "creation date": 1700000000,
        },
    )

    print(f"Generated {c.count} torrent_info corpus files in {outdir}")


def generate_lsd(outdir: str) -> None:
    """Generate seed corpus for the LSD session-level fuzzer.

    Wire format: length-prefixed UDP datagrams (2-byte big-endian length).
    Each datagram is sent verbatim to the lsd socket on 127.0.0.1:6771.
    """
    c = Corpus(outdir, frame=_len_prefixed)
    add = c.add

    info_hash = "0123456789abcdef0123456789abcdef01234567"
    other_ih = "fedcba9876543210fedcba9876543210fedcba98"

    # Well-formed BT-SEARCH announce
    valid = (
        b"BT-SEARCH * HTTP/1.1\r\n"
        b"Host: 239.192.152.143:6771\r\n"
        b"Port: 6881\r\n"
        b"Infohash: " + info_hash.encode() + b"\r\n"
        b"cookie: deadbeef\r\n"
        b"\r\n\r\n"
    )
    add("lsd_valid", valid)

    # Multiple infohashes in a single announce
    multi_ih = (
        b"BT-SEARCH * HTTP/1.1\r\n"
        b"Host: 239.192.152.143:6771\r\n"
        b"Port: 6881\r\n"
        b"Infohash: " + info_hash.encode() + b"\r\n"
        b"Infohash: " + other_ih.encode() + b"\r\n"
        b"cookie: cafebabe\r\n"
        b"\r\n\r\n"
    )
    add("lsd_multi_infohash", multi_ih)

    # Missing port header
    no_port = (
        b"BT-SEARCH * HTTP/1.1\r\n"
        b"Host: 239.192.152.143:6771\r\n"
        b"Infohash: " + info_hash.encode() + b"\r\n"
        b"\r\n\r\n"
    )
    add("lsd_no_port", no_port)

    # Out-of-range port
    bad_port = (
        b"BT-SEARCH * HTTP/1.1\r\n"
        b"Host: 239.192.152.143:6771\r\n"
        b"Port: 99999\r\n"
        b"Infohash: " + info_hash.encode() + b"\r\n"
        b"\r\n\r\n"
    )
    add("lsd_bad_port", bad_port)

    # Wrong HTTP method (not bt-search)
    wrong_method = (
        b"GET * HTTP/1.1\r\n"
        b"Host: 239.192.152.143:6771\r\n"
        b"Port: 6881\r\n"
        b"Infohash: " + info_hash.encode() + b"\r\n"
        b"\r\n\r\n"
    )
    add("lsd_wrong_method", wrong_method)

    # Infohash with wrong length (not 40 hex chars)
    short_ih = (
        b"BT-SEARCH * HTTP/1.1\r\n"
        b"Host: 239.192.152.143:6771\r\n"
        b"Port: 6881\r\n"
        b"Infohash: 0123456789abcdef\r\n"
        b"\r\n\r\n"
    )
    add("lsd_short_infohash", short_ih)

    # Zero infohash (rejected by is_all_zeros check)
    zero_ih = (
        b"BT-SEARCH * HTTP/1.1\r\n"
        b"Host: 239.192.152.143:6771\r\n"
        b"Port: 6881\r\n"
        b"Infohash: 0000000000000000000000000000000000000000\r\n"
        b"\r\n\r\n"
    )
    add("lsd_zero_infohash", zero_ih)

    # Non-hex infohash (40 chars but invalid hex)
    nonhex_ih = (
        b"BT-SEARCH * HTTP/1.1\r\n"
        b"Host: 239.192.152.143:6771\r\n"
        b"Port: 6881\r\n"
        b"Infohash: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz\r\n"
        b"\r\n\r\n"
    )
    add("lsd_nonhex_infohash", nonhex_ih)

    # Truncated header (no terminator)
    truncated = b"BT-SEARCH * HTTP/1.1\r\nHost: x\r\nPort: 6881\r\n"
    add("lsd_truncated", truncated)

    # Garbage / not HTTP at all
    add("lsd_garbage", b"\x00\x01\x02\x03not_http_at_all")

    # Empty datagram
    add("lsd_empty", b"")

    # Two valid announces in one input (exercises receive re-arming)
    add("lsd_two_announces", valid, multi_ih)

    print(f"Generated {c.count} lsd corpus files in {outdir}")


# === add_torrent =============================================================
#
# add_torrent fuzzer (fuzzers/src/add_torrent.cpp) builds an
# add_torrent_params from the input via the read_bits helper. read_bits
# consumes bits LSB-first within a byte; when a read spans multiple bytes,
# the earlier byte's bits form the high-order portion of the returned value
# (so byte-aligned 8/16/32-bit reads come out big-endian).
#
# Reads, in order:
#   2  bits : file_priorities count   (0..3)
#   per entry: 3 bits : download_priority value (0..7)
#   24 bits : flags (low 24 bits of torrent_flags_t)
#   4  bits : num_unfinished_pieces
#   per entry:
#     32 bits : piece_index_t (raw int)
#     5  bits : mask bit-count
#     N  bits : mask
#   6  bits : have_pieces bit-count
#   N  bits : have_pieces
#   6  bits : verified_pieces bit-count
#   N  bits : verified_pieces
#   6  bits : piece_priorities count
#   N  bits : per-priority (1 bit each)
#   1  bit  : merkle-tree mode (1 = use the valid g_tree; 0 = fuzz fields)
#   if mode == 0:
#     2 bits : merkle_trees count
#     per entry: 13 bits : tree size
#     2 bits : merkle_tree_mask count
#     per entry: 13 bits : mask size, then N bits
#     2 bits : verified_leaf_hashes count
#     per entry: 4 bits : size, then N bits
#   then eleven 32-bit fields:
#     max_uploads, max_connections, upload_limit, download_limit,
#     active_time, finished_time, seeding_time, last_seen_complete,
#     num_complete, num_incomplete, num_downloaded
#
# active_time, finished_time and seeding_time are added to elapsed session
# time inside torrent::active_time()/finished_time()/seeding_time(), so an
# input near INT_MAX triggers signed overflow. Other fields with non-trivial
# ranges (piece_index_t, num_complete, etc.) get the same boundary treatment.


class BitWriter:
    """Build a byte string matching the bit layout consumed by read_bits.hpp.

    Bits within each byte are written LSB-first. When a value spans multiple
    bytes its high-order chunk is placed in the earlier byte, so the reader
    (which left-shifts previously-read chunks) recovers the original value.
    """

    def __init__(self) -> None:
        self._bytes = bytearray()
        self._bit = 0  # bit offset within the last byte (0..7)

    def write(self, value: int, bits: int) -> None:
        if bits <= 0:
            return
        value &= (1 << bits) - 1
        while bits > 0:
            if self._bit == 0:
                self._bytes.append(0)
            chunk_bits = min(8 - self._bit, bits)
            # The high `chunk_bits` of the remaining value go in this byte
            # because the reader left-shifts the accumulator by chunk_bits
            # before OR'ing in the freshly read chunk.
            chunk = (value >> (bits - chunk_bits)) & ((1 << chunk_bits) - 1)
            self._bytes[-1] |= chunk << self._bit
            self._bit += chunk_bits
            bits -= chunk_bits
            if self._bit == 8:
                self._bit = 0

    def bytes(self) -> bytes:
        return bytes(self._bytes)


# Bit-widths matching add_torrent.cpp's generate_atp().
_ATP_BITS_FILE_PRIO_COUNT = 2
_ATP_BITS_FILE_PRIO = 3
_ATP_BITS_FLAGS = 24
_ATP_BITS_NUM_UNFINISHED = 4
_ATP_BITS_PIECE_INDEX = 32
_ATP_BITS_UNFINISHED_MASK_SIZE = 5
_ATP_BITS_PIECES_SIZE = 6
_ATP_BITS_PIECE_PRIO = 1
_ATP_BITS_MERKLE_MODE = 1
_ATP_BITS_MERKLE_COUNT = 2
_ATP_BITS_MERKLE_TREE_SIZE = 13
_ATP_BITS_VERIFIED_LEAF_SIZE = 4

# 32-bit signed boundary set, applied to every int field in turn.
INT32_MAX = (1 << 31) - 1
INT32_MIN = -(1 << 31)
_INT32_BOUNDS = {
    "zero": 0,
    "one": 1,
    "neg1": -1,
    "intmax": INT32_MAX,
    "intmin": INT32_MIN,
    "intmax_m1": INT32_MAX - 1,
    "intmin_p1": INT32_MIN + 1,
}

# Fields read as bits.read(32). Order matters only for documentation; each
# is exercised in isolation against _INT32_BOUNDS.
_ATP_INT_FIELDS = (
    "max_uploads",
    "max_connections",
    "upload_limit",
    "download_limit",
    "active_time",
    "finished_time",
    "seeding_time",
    "last_seen_complete",
    "num_complete",
    "num_incomplete",
    "num_downloaded",
)


def _build_atp(
    *,
    file_priorities: Sequence[int] = (),
    flags: int = 0,
    unfinished_pieces: Sequence[tuple] = (),
    have_pieces_size: int = 0,
    have_pieces_bits: int = 0,
    verified_pieces_size: int = 0,
    verified_pieces_bits: int = 0,
    piece_priorities: Sequence[int] = (),
    merkle_valid: bool = True,
    merkle_trees_sizes: Sequence[int] = (),
    merkle_tree_mask_sizes: Sequence[int] = (),
    merkle_tree_mask_bits: Sequence[int] = (),
    verified_leaf_sizes: Sequence[int] = (),
    verified_leaf_bits: Sequence[int] = (),
    max_uploads: int = 0,
    max_connections: int = 0,
    upload_limit: int = 0,
    download_limit: int = 0,
    active_time: int = 0,
    finished_time: int = 0,
    seeding_time: int = 0,
    last_seen_complete: int = 0,
    num_complete: int = 0,
    num_incomplete: int = 0,
    num_downloaded: int = 0,
) -> bytes:
    w = BitWriter()
    w.write(len(file_priorities), _ATP_BITS_FILE_PRIO_COUNT)
    for p in file_priorities:
        w.write(p, _ATP_BITS_FILE_PRIO)
    w.write(flags, _ATP_BITS_FLAGS)
    w.write(len(unfinished_pieces), _ATP_BITS_NUM_UNFINISHED)
    for piece_idx, mask_size, mask_value in unfinished_pieces:
        w.write(piece_idx, _ATP_BITS_PIECE_INDEX)
        w.write(mask_size, _ATP_BITS_UNFINISHED_MASK_SIZE)
        for i in range(mask_size):
            w.write((mask_value >> i) & 1, 1)
    w.write(have_pieces_size, _ATP_BITS_PIECES_SIZE)
    for i in range(have_pieces_size):
        w.write((have_pieces_bits >> i) & 1, 1)
    w.write(verified_pieces_size, _ATP_BITS_PIECES_SIZE)
    for i in range(verified_pieces_size):
        w.write((verified_pieces_bits >> i) & 1, 1)
    w.write(len(piece_priorities), _ATP_BITS_PIECES_SIZE)
    for p in piece_priorities:
        w.write(p, _ATP_BITS_PIECE_PRIO)
    if merkle_valid:
        w.write(1, _ATP_BITS_MERKLE_MODE)
    else:
        w.write(0, _ATP_BITS_MERKLE_MODE)
        w.write(len(merkle_trees_sizes), _ATP_BITS_MERKLE_COUNT)
        for s in merkle_trees_sizes:
            w.write(s, _ATP_BITS_MERKLE_TREE_SIZE)
        w.write(len(merkle_tree_mask_sizes), _ATP_BITS_MERKLE_COUNT)
        for s, b in zip(merkle_tree_mask_sizes, merkle_tree_mask_bits):
            w.write(s, _ATP_BITS_MERKLE_TREE_SIZE)
            for i in range(s):
                w.write((b >> i) & 1, 1)
        w.write(len(verified_leaf_sizes), _ATP_BITS_MERKLE_COUNT)
        for s, b in zip(verified_leaf_sizes, verified_leaf_bits):
            w.write(s, _ATP_BITS_VERIFIED_LEAF_SIZE)
            for i in range(s):
                w.write((b >> i) & 1, 1)
    w.write(max_uploads, 32)
    w.write(max_connections, 32)
    w.write(upload_limit, 32)
    w.write(download_limit, 32)
    w.write(active_time, 32)
    w.write(finished_time, 32)
    w.write(seeding_time, 32)
    w.write(last_seen_complete, 32)
    w.write(num_complete, 32)
    w.write(num_incomplete, 32)
    w.write(num_downloaded, 32)
    return w.bytes()


def _build_atp_ints(*, merkle_valid: bool, ints: dict) -> bytes:
    """Wrapper that forwards int-only overrides as typed kwargs to _build_atp.

    Lets callers compute a `{field_name: int}` mapping without tripping mypy
    on _build_atp's mixed Sequence/int signature.
    """
    return _build_atp(
        merkle_valid=merkle_valid,
        max_uploads=ints.get("max_uploads", 0),
        max_connections=ints.get("max_connections", 0),
        upload_limit=ints.get("upload_limit", 0),
        download_limit=ints.get("download_limit", 0),
        active_time=ints.get("active_time", 0),
        finished_time=ints.get("finished_time", 0),
        seeding_time=ints.get("seeding_time", 0),
        last_seen_complete=ints.get("last_seen_complete", 0),
        num_complete=ints.get("num_complete", 0),
        num_incomplete=ints.get("num_incomplete", 0),
        num_downloaded=ints.get("num_downloaded", 0),
    )


def generate_add_torrent(outdir: str) -> None:
    """Seed corpus for fuzzers/src/add_torrent.cpp.

    Covers boundary values for every field generate_atp() reads:
      - each 32-bit int field exercised in isolation across
        {0, 1, -1, INT_MAX, INT_MIN, INT_MAX-1, INT_MIN+1},
      - active_time/finished_time/seeding_time near INT_MAX (where adding
        elapsed session time overflows int),
      - max-size bitfields (have_pieces, verified_pieces, piece_priorities),
      - max-count unfinished_pieces with INT_MAX / INT_MIN piece indices,
      - both merkle paths (valid g_tree and fuzz-controlled merkle fields),
      - flags with all 24 reachable bits set.
    """
    c = Corpus(outdir)

    # Baseline: all-default field values, valid merkle tree path
    c.add("atp_baseline", b"", _build_atp(merkle_valid=True))
    c.add("atp_baseline_fuzz_merkle", b"", _build_atp(merkle_valid=False))

    # Each 32-bit int field exercised in isolation against the boundary set
    for field in _ATP_INT_FIELDS:
        for label, v in _INT32_BOUNDS.items():
            c.add(
                f"atp_{field}_{label}",
                b"",
                _build_atp_ints(merkle_valid=True, ints={field: v}),
            )

    # Triplet that drives the known active_time/finished_time/seeding_time
    # overflow at INT_MAX, plus near-boundary variants so libFuzzer can
    # bracket the threshold.
    for label, t in [
        ("intmax", INT32_MAX),
        ("intmax_m1", INT32_MAX - 1),
        ("intmax_m10", INT32_MAX - 10),
        ("intmax_m100", INT32_MAX - 100),
        ("intmin", INT32_MIN),
        ("intmin_p1", INT32_MIN + 1),
    ]:
        c.add(
            f"atp_times_{label}",
            b"",
            _build_atp(
                merkle_valid=True,
                active_time=t,
                finished_time=t,
                seeding_time=t,
            ),
        )

    # All eleven int fields at the same extreme
    for label, v in (("intmax", INT32_MAX), ("intmin", INT32_MIN), ("neg1", -1)):
        c.add(
            f"atp_all_ints_{label}",
            b"",
            _build_atp_ints(
                merkle_valid=True,
                ints={n: v for n in _ATP_INT_FIELDS},
            ),
        )

    # download_priority_t at its 8-bit max for every file priority entry
    c.add(
        "atp_file_priorities_max",
        b"",
        _build_atp(file_priorities=[7, 7, 7], merkle_valid=True),
    )
    c.add(
        "atp_file_priorities_mixed",
        b"",
        _build_atp(file_priorities=[0, 4, 7], merkle_valid=True),
    )

    # All reachable flag bits set (only the low 24 bits are read)
    c.add("atp_flags_all_bits", b"", _build_atp(flags=(1 << 24) - 1, merkle_valid=True))

    # unfinished_pieces at max count with extreme piece_index_t values and
    # max-size masks. piece_index_t is a strong-typed int so any 32-bit
    # value reaches the map insert as a distinct key.
    c.add(
        "atp_unfinished_pieces_intmax_idx",
        b"",
        _build_atp(
            unfinished_pieces=[(INT32_MAX, 31, (1 << 31) - 1)] * 15,
            merkle_valid=True,
        ),
    )
    c.add(
        "atp_unfinished_pieces_intmin_idx",
        b"",
        _build_atp(
            unfinished_pieces=[(INT32_MIN, 31, (1 << 31) - 1)] * 15,
            merkle_valid=True,
        ),
    )

    # Max-size bitfields. The fuzzer caps these at 6 bits (size 0..63), so
    # exhaust those to cover the longest inner loops.
    max_bits = (1 << 63) - 1
    c.add(
        "atp_have_pieces_max",
        b"",
        _build_atp(have_pieces_size=63, have_pieces_bits=max_bits, merkle_valid=True),
    )
    c.add(
        "atp_verified_pieces_max",
        b"",
        _build_atp(
            verified_pieces_size=63, verified_pieces_bits=max_bits, merkle_valid=True
        ),
    )
    c.add(
        "atp_piece_priorities_max",
        b"",
        _build_atp(piece_priorities=[1] * 63, merkle_valid=True),
    )

    # Fuzz-controlled merkle fields at max counts/sizes
    big = (1 << _ATP_BITS_MERKLE_TREE_SIZE) - 1  # 8191
    leaf = (1 << _ATP_BITS_VERIFIED_LEAF_SIZE) - 1  # 15
    c.add(
        "atp_merkle_max_sizes",
        b"",
        _build_atp(
            merkle_valid=False,
            merkle_trees_sizes=[big, big, big],
            merkle_tree_mask_sizes=[big, big, big],
            merkle_tree_mask_bits=[(1 << big) - 1] * 3,
            verified_leaf_sizes=[leaf, leaf, leaf],
            verified_leaf_bits=[(1 << leaf) - 1] * 3,
        ),
    )
    c.add(
        "atp_merkle_zero_sizes",
        b"",
        _build_atp(
            merkle_valid=False,
            merkle_trees_sizes=[0, 0, 0],
            merkle_tree_mask_sizes=[0, 0, 0],
            merkle_tree_mask_bits=[0, 0, 0],
            verified_leaf_sizes=[0, 0, 0],
            verified_leaf_bits=[0, 0, 0],
        ),
    )

    # Everything maxed: combines all the boundary cases in one input
    c.add(
        "atp_all_max",
        b"",
        _build_atp(
            file_priorities=[7] * 3,
            flags=(1 << 24) - 1,
            unfinished_pieces=[(INT32_MAX, 31, (1 << 31) - 1)] * 15,
            have_pieces_size=63,
            have_pieces_bits=max_bits,
            verified_pieces_size=63,
            verified_pieces_bits=max_bits,
            piece_priorities=[1] * 63,
            merkle_valid=False,
            merkle_trees_sizes=[big] * 3,
            merkle_tree_mask_sizes=[big] * 3,
            merkle_tree_mask_bits=[(1 << big) - 1] * 3,
            verified_leaf_sizes=[leaf] * 3,
            verified_leaf_bits=[(1 << leaf) - 1] * 3,
            max_uploads=INT32_MAX,
            max_connections=INT32_MAX,
            upload_limit=INT32_MAX,
            download_limit=INT32_MAX,
            active_time=INT32_MAX,
            finished_time=INT32_MAX,
            seeding_time=INT32_MAX,
            last_seen_complete=INT32_MAX,
            num_complete=INT32_MAX,
            num_incomplete=INT32_MAX,
            num_downloaded=INT32_MAX,
        ),
    )

    print(f"Generated {c.count} add_torrent corpus files in {outdir}")


def main() -> None:
    # Anchor output to the repo's fuzzers/corpus directory regardless of the
    # current working directory. This script lives in tools/, so the repo root
    # is one level up.
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    def corpus(name: str) -> str:
        return os.path.join(repo_root, "fuzzers", "corpus", name)

    generate_peer_conn(corpus("peer_conn"))
    generate_natpmp(corpus("natpmp"))
    generate_udp_tracker(corpus("udp_tracker"))
    generate_upnp(corpus("upnp"))
    generate_pe_crypto_state(corpus("pe_crypto_state"))
    generate_pe_conn(corpus("pe_conn"))
    generate_ut_metadata(corpus("ut_metadata"))
    generate_ut_pex(corpus("ut_pex"))
    generate_web_seed(corpus("web_seed"))
    generate_utp_stream(corpus("utp_stream"))
    generate_piece_layers(corpus("piece_layers"))
    generate_torrent_info(corpus("torrent_info"))
    generate_lsd(corpus("lsd"))
    generate_add_torrent(corpus("add_torrent"))


if __name__ == "__main__":
    main()
