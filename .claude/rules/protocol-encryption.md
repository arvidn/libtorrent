# Protocol Encryption (MSE/PE) Handshake

libtorrent implements Message Stream Encryption (MSE), also called Protocol
Encryption (PE). The implementation lives in `src/bt_peer_connection.cpp` and
`src/pe_crypto.cpp` (crypto primitives in `include/libtorrent/aux_/pe_crypto.hpp`).

## Overview

MSE provides obfuscation (not strong security) against deep-packet inspection.
It uses a Diffie-Hellman key exchange to derive RC4 stream keys and wraps the
BitTorrent handshake. The torrent info hash is obfuscated in the handshake so
the connection cannot be trivially identified.

After the handshake, the connection body is RC4-encrypted only if
`crypto_select` ended on `pe_rc4`. If `pe_plaintext` was selected, only the
handshake is obfuscated and post-handshake traffic is sent in the clear. This
is tracked by the `m_rc4_encrypted` flag on `bt_peer_connection` and gates the
encrypt/decrypt path in every send and receive routine.

## Settings

| Setting | Meaning |
|---------|---------|
| `pe_forced`   | Always use PE (outgoing) / require PE (incoming) |
| `pe_enabled`  | Use PE if peer supports it, fall back to plaintext |
| `pe_disabled` | Never use PE |

`settings_pack::out_enc_policy` controls outgoing connections.
`settings_pack::in_enc_policy` controls acceptance of incoming connections.
`settings_pack::allowed_enc_level` is a bitmask (`pe_plaintext=1`, `pe_rc4=2`, `pe_both=3`).
`settings_pack::prefer_rc4` selects RC4 over plaintext when both are offered.

## Handshake States

The state machine lives in `bt_peer_connection::on_receive_impl()`. The
handshake differs between the initiating side (outgoing) and the accepting
side (incoming).

### Incoming (server) path

1. **`read_pe_dhkey`** (96 bytes) -- read client's DH public key, respond
   with our DH public key + random PadB, transition to `read_pe_synchash`.
2. **`read_pe_synchash`** (scans up to 512 bytes of PadA + the 20-byte hash) --
   scan received bytes for `hash('req1', S)` where S is the shared secret. The
   client may insert up to 512 bytes of PadA before this hash. Failure:
   `sync_hash_not_found`.
3. **`read_pe_skey_vc`** (28 bytes) -- read the obfuscated SKEY hash (20
   bytes) and the encrypted VC (8 bytes). The SKEY hash is
   `hash('req2', info_hash) XOR hash('req3', S)` which reveals which torrent
   the client wants. Failure if torrent not found: `invalid_info_hash`.
   Failure if VC decrypts to non-zero: `invalid_encryption_constant`.
4. **`read_pe_cryptofield`** (6 bytes) -- read encrypted
   `crypto_provide(4) + len_pad_c(2)`. Server selects a crypto mode and
   sends back its own VC + `crypto_select + pad`. Failure if no mode overlaps:
   `unsupported_encryption_mode`. Failure if `len_pad_c > 512`:
   `invalid_pad_size`.
5. **`read_pe_pad`** (`len_pad_c + 2` bytes) -- skip encrypted padding, read
   `len_ia` (2 bytes).
6. **`read_pe_ia`** (`len_ia` bytes) -- read Initial Application data (IA),
   the encrypted start of the BitTorrent handshake. Failure if `len_ia > 68`:
   `invalid_encrypt_handshake`.
7. Falls through to **`read_protocol_identifier`** (regular BT handshake).

### Outgoing (client) path

1. **`on_connected`** -- send DH public key + random PadA,
   transition to `read_pe_dhkey`.
2. **`read_pe_dhkey`** (96 bytes) -- read server's DH public key, then send
   sync hash + SKEY hash + encrypted(VC + crypto_provide + len_pad + PadC +
   len_IA) + encrypted(IA = BT handshake). Also send the BT handshake
   encrypted as IA. Transition to `read_pe_syncvc`.
3. **`read_pe_syncvc`** (scans up to 512 bytes of PadB + the 8-byte VC) --
   scan for the encrypted 8-byte verification constant (all zeros encrypted
   with RC4). Failure if not found within the 512-byte pad window:
   `invalid_encryption_constant`.
4. **`read_pe_cryptofield`** (6 bytes) -- read server's `crypto_select(4) +
   len_pad(2)`. Failure if selected mode not in our `allowed_enc_level`:
   `unsupported_encryption_mode_selected`.
5. **`read_pe_pad`** (`len_pad` bytes) -- skip server's padding.
6. Falls through to **`read_protocol_identifier`** (regular BT handshake).

## Crypto Primitives

### DH Key Exchange (`dh_key_exchange`)

768-bit Diffie-Hellman with a fixed prime (MSE standard prime from BEP 10/MSE
spec). `key = 2^random % prime`.

- `dh_key_exchange()` -- generate local key pair
- `compute_secret(remote_pubkey)` -- compute shared secret S; also computes
  `m_xor_mask = hash('req3', S)` used for SKEY obfuscation
- `get_local_key()` -- 96-byte DH public key to send
- `get_secret()` -- shared secret S
- `get_hash_xor_mask()` -- `hash('req3', S)` for SKEY obfuscation

### RC4 Key Derivation

For outgoing connections (client):
```
encrypt_key = hash("keyA", S, SKEY)
decrypt_key = hash("keyB", S, SKEY)
```
For incoming connections (server):
```
encrypt_key = hash("keyB", S, SKEY)
decrypt_key = hash("keyA", S, SKEY)
```
where SKEY is the SHA-1 info hash of the torrent (or first 20 bytes of SHA-256
for v2 torrents). The RC4 stream is initialized by discarding the first 1024
bytes (standard MSE requirement).

### Message Layout (client -> server)

```
[96 bytes]  client DH public key
[0-512 bytes] PadA (random, encrypted sync hash follows)
[20 bytes]  sync_hash = hash('req1', S)
[20 bytes]  skey_obfusc = hash('req2', SKEY) XOR hash('req3', S)
encrypt([8 bytes]  VC = 0x00 * 8
        [4 bytes]  crypto_provide (bit flags: 1=plaintext, 2=RC4)
        [2 bytes]  len_PadC
        [len_PadC] PadC (random)
        [2 bytes]  len_IA)
encrypt([len_IA bytes] IA = start of BT handshake, always RC4 encrypted)
```

### Message Layout (server -> client)

```
[96 bytes]  server DH public key
[0-512 bytes] PadB (random)
encrypt([8 bytes]  VC = 0x00 * 8
        [4 bytes]  crypto_select (chosen mode: 1 or 2)
        [2 bytes]  len_PadD
        [len_PadD] PadD (random))
[regular BT handshake, encrypted or not depending on crypto_select]
```

## Key Files

| File | Purpose |
|------|---------|
| `src/pe_crypto.cpp` + `include/libtorrent/aux_/pe_crypto.hpp` | DH exchange, RC4 handler, encryption_handler |
| `src/bt_peer_connection.cpp` | PE state machine and write helpers |
| `fuzzers/src/pe_conn.cpp` | Session-level fuzzer for the PE handshake |
| `fuzzers/src/pe_crypto_state.cpp` | Low-level fuzzer for DH + RC4 primitives |

## Error Codes

| Error | Trigger |
|-------|---------|
| `sync_hash_not_found` | sync hash not found within 512-byte window |
| `invalid_info_hash` | SKEY hash doesn't match any known torrent |
| `invalid_encryption_constant` | VC decrypts to non-zero bytes |
| `unsupported_encryption_mode` | No overlap in crypto_provide vs allowed_enc_level |
| `unsupported_encryption_mode_selected` | Server selected a mode not in our allowed list |
| `invalid_pad_size` | len_pad_c > 512 |
| `invalid_encrypt_handshake` | len_ia > 68 |
