---
paths:
  - "fuzzers/**"
  - "tools/gen_corpus.py"
---

## Fuzzers

### Build

Fuzzers require clang and are built separately from the main library:

```sh
cd fuzzers && b2 clang
```

Built with ASAN + UBSAN (norecover), debug symbols, asserts on, debug
iterators (`_GLIBCXX_DEBUG`), and WebTorrent enabled (per the default-build
in `fuzzers/Jamfile`; note that `invariant-checks` is *not* turned on). Each
target produces a standalone binary under `fuzzers/bin/<toolset>/` (e.g.
`fuzzers/bin/clang-linux-20/...`). The explicit `stage` / `stage-large`
install targets copy the binaries into the `fuzzers/fuzzers/` subdirectory
(the `<location>fuzzers` in `fuzzers/Jamfile` is relative to the Jamfile's
own directory).

Two optional build features (defined in `fuzzers/Jamfile`):

- `debug-logging=on` -- defines `DEBUG_LOGGING=1`, enabling extra alert
  logging in the session-level fuzzers (see peer_conn below).
- `variant=build_coverage` -- a fast replay build with sanitizers, asserts,
  and invariant checks all off. Combine with `test-coverage=on` to measure
  code coverage from an existing corpus.

### Running a fuzzer

Run a specific fuzzer against a corpus (single job):

```sh
./fuzzers/bin/.../peer_conn fuzzers/corpus/peer_conn/
```

Useful libFuzzer flags:

| Flag | Purpose |
|------|---------|
| `-max_len=N` | Cap input size (default: 4096) |
| `-timeout=N` | Seconds before a single run is declared a timeout (default: 1200) |
| `-max_total_time=N` | Stop after N seconds total |
| `-artifact_prefix=dir/` | Write crash/timeout inputs to this directory |

### Running in parallel

libFuzzer supports in-process parallelism via `-jobs` and `-workers`:

```sh
./fuzzers/bin/.../peer_conn fuzzers/corpus/peer_conn/ \
    -jobs=8 -workers=8
```

- `-jobs=N` — total number of fuzzing jobs to complete before the driver exits
- `-workers=N` — number of concurrent fuzzer processes (defaults to
  `min(jobs, num_cpus/2)` when omitted)

Each worker writes its own log to `fuzz-N.log` in the current directory.
Crashes are written to the current directory by default; use
`-artifact_prefix=crashes/` to redirect them.

After parallel runs, merge all worker corpora back into the main corpus to
deduplicate and minimize:

```sh
./fuzzers/bin/.../peer_conn -merge=1 \
    fuzzers/corpus/peer_conn/ fuzzers/corpus/peer_conn/
```

(Passing the same directory as both source and destination is safe; libFuzzer
reads all inputs first, then writes the minimized set back.)

### Structure

Every fuzzer in `fuzzers/src/` implements `LLVMFuzzerTestOneInput`; the
session-level fuzzers additionally implement `LLVMFuzzerInitialize`:

- `LLVMFuzzerTestOneInput(data, size)` — implemented by every fuzzer;
  called for every fuzz input; must be stateless with respect to prior
  inputs. Returns 0 on success, -1 to signal libFuzzer that the input was
  uninteresting (used for timeouts, not errors).

- `LLVMFuzzerInitialize(argc, argv)` — implemented only by the
  session-level fuzzers (`add_torrent`, `pe_conn`, `peer_conn`,
  `piece_layers`, `rtc_peer_conn`, `ut_metadata`, `ut_pex`, `web_seed`).
  Called once at startup to set up global state (sessions, sockets,
  servers) that persists across all test inputs. The many pure-function
  fuzzers (e.g. `bdecode_node`, `parse_magnet_uri`, `gzip`,
  `torrent_info`) have no global state and omit it.

### peer_conn.cpp

Connects to a live libtorrent session as a BitTorrent peer and sends
fuzz-controlled protocol messages.

The session and torrent setup is shared via `peer_session.hpp`
(`configure_fuzz_session()`, `make_fuzz_torrent_params()`, and the
`peer_fuzz_session` struct `g_fz`), and reused by the rtc_peer_conn,
ut_metadata, and ut_pex fuzzers.

**Session setup (`LLVMFuzzerInitialize`):**

- Creates a session on `127.0.0.1:0` with all outbound connections and
  ancillary services (outgoing TCP/uTP, DHT, LSD, UPnP, NAT-PMP, IP
  notifier) disabled, encryption disabled, and all peer timeouts set to
  1 second.
- Adds a single **hybrid (v1 + v2)** torrent built from four files (a
  0-byte file, a 100-byte file, a 50 KiB file, and a 97 MiB + 200 KiB
  file). After canonicalization (alphabetical sort + inserted pad files)
  this is exactly 100 pieces of 1 MiB. Piece hashes (SHA-1) and per-file
  block hashes (SHA-256) are filled with dummy values. `save_path` is `.`
  and no data is on disk, so the torrent stays in downloading state.
- Waits for both the TCP `listen_succeeded_alert` (recording
  `g_fz.listen_port`) and the `torrent_resumed_alert` before returning.
- Records `g_fz.info_hash` (an `info_hash_t` holding both the v1 SHA-1
  and v2 SHA-256 info-hashes).

**Per-input flow (`LLVMFuzzerTestOneInput`):**

1. Opens a TCP connection to `127.0.0.1:g_fz.listen_port`.
2. Sends a 68-byte BT handshake:
   - `data[0..7]` are used as the extension-flags (reserved) bytes.
   - Bits for BEP 10 (byte 5, bit 0x10) and BEP 6 FAST (byte 7, bit
     0x04) are always forced on so those handlers are always reachable.
   - Byte 0 bit 0x01 selects the peer's protocol version: clear = v2
     (advertises `g_fz.info_hash.get_best()`, `protocol_v2` true), set =
     v1 (advertises `g_fz.info_hash.v1`, `protocol_v2` false).
   - Peer-ID is all zeros.
3. Sends a fixed BEP 10 extended handshake (`k_extended_handshake`,
   ext_id 0) registering:

   | Extension   | ext_id |
   |-------------|--------|
   | ut_pex      | 1      |
   | ut_metadata | 2      |
   | upload_only | 3      |

4. Parses `data[8..]` as a sequence of messages (see **Corpus wire
   format** below) and sends each with a correct 4-byte BT length
   prefix.
5. Closes the socket and waits for a `peer_error_alert` or
   `peer_disconnected_alert` (3-second timeout).

**DEBUG_LOGGING:** defined by `fuzzers/Jamfile` via the `debug-logging`
feature (default `off` -> `DEBUG_LOGGING=0`). Build with
`b2 clang debug-logging=on` to set it to 1, which adds `peer_log` alerts to
the alert mask and makes `wait_for_disconnect` print the last 10 interesting
alerts (connect and routine peer-log noise excluded) before each disconnect
-- useful for tracing which message paths were hit.

### Corpus wire format (peer_conn)

Each corpus file is binary-structured:

```
[8 bytes : extension flags]
[repeated until EOF:
    1 byte  : msg_type
    2 bytes : payload_len  (big-endian, max 65535)
    N bytes : payload      (N = min(payload_len, bytes_remaining))
]
```

Minimum file size: 11 bytes (8 flag bytes + 3-byte message header).

For **msg_type 20** (BEP 10 extended protocol), the first byte of the
payload is the extended message ID and the rest is the extension body.
The fuzzer splits this automatically before calling
`send_extended_message`.

**Extension flag byte layout** (8 reserved bytes, big-endian):

| Byte | Bit  | Protocol            |
|------|------|---------------------|
| 0    | 0x01 | protocol version (set = v1 SHA-1, clear = v2) |
| 5    | 0x10 | BEP 10 extended     |
| 7    | 0x04 | BEP 6 FAST          |
| 7    | 0x01 | BEP 5 DHT           |

**libtorrent's fixed incoming ext_ids** (from `bt_peer_connection.hpp`
enum; these are the IDs libtorrent expects in messages FROM us):

| ext_id | Handler          | Notes                                      |
|--------|------------------|--------------------------------------------|
| 0      | extended HS      | re-negotiation supported                   |
| 1      | ut_pex plugin    | as registered in `k_extended_handshake`    |
| 2      | ut_metadata      | as registered in `k_extended_handshake`    |
| 3      | upload_only      | `upload_only_msg` constant                 |
| 4      | holepunch        | `holepunch_msg`; requires `m_holepunch_id` |
| 7      | dont_have        | `dont_have_msg`; 4-byte piece index        |
| 8      | share_mode       | `share_mode_msg`                           |
| other  | disconnect       | `errors::invalid_message`                  |

Holepunch (ext_id 4) silently returns if `m_holepunch_id == 0`. To
reach the holepunch handler body, a corpus file must first send an
additional extended handshake (ext_id 0) that includes
`"ut_holepunch": <nonzero>` in its `m` dict.

**BEP 78 hash messages** (21 = hash_request, 22 = hashes, 23 =
hash_reject) require the peer to have advertised v2 support via the
info-hash in the handshake (`protocol_v2` flag). They are rejected with
`errors::invalid_message` otherwise.

### Corpus generation tool

`tools/gen_corpus.py` generates seed corpora for all session-level fuzzers.
Run it from the repo root:

```sh
python3 tools/gen_corpus.py
```

This writes into:
- `fuzzers/corpus/peer_conn/`
- `fuzzers/corpus/natpmp/`
- `fuzzers/corpus/udp_tracker/`
- `fuzzers/corpus/upnp/`
- `fuzzers/corpus/pe_crypto_state/`
- `fuzzers/corpus/pe_conn/`
- `fuzzers/corpus/ut_metadata/`
- `fuzzers/corpus/ut_pex/`
- `fuzzers/corpus/web_seed/`
- `fuzzers/corpus/utp_stream/`
- `fuzzers/corpus/piece_layers/`
- `fuzzers/corpus/torrent_info/`
- `fuzzers/corpus/lsd/`
- `fuzzers/corpus/add_torrent/`

**Sync requirement:** the Python constants `EXT_UT_PEX`,
`EXT_UT_METADATA`, `EXT_UPLOAD_ONLY`, and `FUZZER_EXT_HANDSHAKE` in
the tool must match `k_extended_handshake` in `peer_conn.cpp`. If the
C++ extended handshake is changed, update both files together.

The tool also uses `NUM_PIECES = 100`, `PIECE_SIZE = 1 MiB`, and
`BLOCK_SIZE = 16 KiB` -- these must match the torrent that
`make_fuzz_torrent_params()` in `fuzzers/src/peer_session.hpp` builds.

### pe_conn.cpp

Connects to a live libtorrent session and drives the full PE (protocol
encryption / MSE) handshake as the initiating (client) side. Requires
the session to have `in_enc_policy = pe_enabled` and
`allowed_enc_level = pe_both`, which pe_conn's `LLVMFuzzerInitialize`
sets.

The fuzzer performs a real DH key exchange (so the server can locate the
torrent), then uses fuzz-controlled bytes to drive all encrypted fields.
Every server-side error path is reachable by setting the appropriate bit
in the flags byte.

**Session setup (`LLVMFuzzerInitialize`):**

- Same hybrid v1+v2 torrent as peer_conn (100 pieces, 1 MiB each).
- `in_enc_policy = pe_enabled`, `allowed_enc_level = pe_both`.
- All outbound services disabled (same as peer_conn).

**Per-input flow (`LLVMFuzzerTestOneInput`):**

1. Connect TCP socket to session.
2. Generate a fresh DH key pair; send DH public key + PadA (from fuzz bytes).
3. Read the server's DH public key (96 bytes, blocking).
4. Compute shared secret, RC4 keys, sync hash, and obfuscated SKEY hash.
5. Build the PE payload with fuzz-controlled fields and optional corruptions.
6. Send the payload, close socket, wait for disconnect.

**Corpus wire format:**

```
Byte 0 : pad_a_size      (PadA bytes before sync hash, 0-255)
Byte 1 : flags
           bit 0 : corrupt_sync_hash  -> sync_hash_not_found
           bit 1 : corrupt_skey       -> invalid_info_hash
           bit 2 : corrupt_vc         -> invalid_encryption_constant
           bits 3-4 : crypto_provide
                      00 -> pe_both (0x03)
                      01 -> pe_plaintext (0x01)
                      10 -> pe_rc4 (0x02)
                      11 -> 0x00 (invalid) -> unsupported_encryption_mode
Byte 2 : len_pad_c * 2  (encrypted padding, 0-510 bytes)
Byte 3 : len_ia         (IA size 0-255; server rejects > 68)
Bytes 4+ : first pad_a_size bytes = PadA content
           then up to len_ia bytes = IA (BT handshake portion)
```

See `.claude/rules/protocol-encryption.md` for details of the PE protocol.
