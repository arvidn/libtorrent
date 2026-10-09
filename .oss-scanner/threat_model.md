# Untrusted input, by trust boundary

## Fully offline (no network position needed)
- `.torrent` / magnet parsing: `torrent_info.cpp` (`load_torrent_buffer`),
  `parse_magnet_uri.cpp`, bdecode (`bdecode_node`, `bdecode_entry`,
  `aux::parse_int`).
- v2 metadata: `piece_layers` parsing in `torrent_info.cpp`,
  `hash_picker.cpp`, `merkle_tree.cpp`/`merkle.cpp`.
- Filenames from torrent metadata hitting the filesystem:
  `sanitize_path`, `escape_path`/`escape_string`,
  `resolve_duplicate_filenames` (`file_storage.cpp`), `file_storage::add_file`.
  Path-traversal / arbitrary-write class. Existing DoS bound here:
  `max_duplicate_filenames`.
- Resume data / session params deserialization: `read_resume_data.cpp`,
  `session_params`. Attacker needs to plant a file, or control the authoring
  of saved resume data.

## Remote peer (anyone who connects, or whom we connect to)
- BT handshake + message dispatch: `bt_peer_connection.cpp`.
- MSE/PE handshake: `pe_crypto.cpp`, state machine in
  `bt_peer_connection.cpp`. See `.claude/rules/protocol-encryption.md`.
- Extension protocol: `ut_metadata` (`src/ut_metadata.cpp`), `ut_pex`
  (`src/ut_pex.cpp`), `smart_ban`, `i2p_pex`.
- Peer-id/fingerprint: `identify_client.cpp`.
- uTP transport, pre- and post-connection: `utp_stream.cpp`
  (`utp_socket_impl::incoming_packet`).
- Web seed HTTP response parsing: `web_peer_connection.cpp`. Note: web seed
  URL itself comes from the (possibly untrusted) `.torrent`, so this
  boundary is reachable without any live attacker if the seed is malicious.

## DHT (any node, including ones never queried)
- Raw incoming dispatch: `kademlia/dht_tracker.cpp` (`incoming_packet`,
  least-structured entry point).
- Reply handling for our own traversals: `get_peers`, `get_item`,
  `sample_infohashes` observers in `kademlia/*.cpp`. Includes the
  `obfuscated_get_peers::invoke` / `generate_prefix_mask` path
  (`kademlia/node_id.cpp`) — real past bug, see `harden generate_prefix_mask()`
  commit.
- Unsolicited writes: `node::incoming_request` `put` handling — token
  verification, ed25519 sig check, CAS/stale-sequence rejection
  (`kademlia/item.cpp`, `ed25519/`).
- Gap: no fuzzer isolates `announce_peer`/`ping`/`find_node` *request*
  handling individually (only caught generally via raw-packet dispatch).
  Don't assume full per-message-type coverage here.

## Trackers (HTTP/UDP/websocket; webtorrent adds websocket)
- `parse_tracker_response` (`http_tracker_connection.cpp`),
  `udp_tracker_connection.cpp`, `parse_websocket_tracker_response`
  (webtorrent). Chains into SDP parsing for RTC offers relayed by the
  tracker (see below).

## WebRTC / RTC datachannel (webtorrent=on only)
- SDP: `rtc::Description` (third-party `deps/libdatachannel`), reached via
  `rtc_signaling.cpp` from both tracker-relayed offers and direct peer
  signaling.
- `aux::rtc_parse_endpoint`.
- Note: this whole surface is inert if built `webtorrent=off`; scanning
  image here builds with it on (default), so in scope.

## Local network / gateway (attacker = anything on-LAN, or spoofing one)
- LSD: `aux::lsd::process_packet` (`lsd.cpp`), capped at 1500B multicast
  datagram.
- UPnP: `upnp.cpp` — SSDP response -> HTTP fetch of device description ->
  XML parse (`xml_parse.cpp`, UPnP-only consumer) -> further HTTP exchange.
- NAT-PMP: `natpmp.cpp`.

## Proxy (attacker = configured proxy, or on-path to it; conditional)
- `socks5_stream.cpp` handshake, `socks5_stream`/UDP associate
  (`socks5_udp` fuzzer flags note this is gated on
  `settings_pack::proxy_peer_connections` / `proxy_tracker_connections` —
  proxy-supplied bytes only reach peer/tracker code if those are set).
- No dedicated HTTP CONNECT-proxy fuzzer; rides through `http_parser.cpp`
  / `parse_url.cpp`.

## Encoding/parsing utilities in a remote path
`http_parser.cpp`, `parse_url.cpp` + `idna.cpp` (hostnames from
trackers/magnets), `gzip.cpp` (`inflate_gzip`, gzip-encoded HTTP bodies).
Lower-sensitivity pure transforms (`base32`, `base64`, `utf8_codepoint`,
`verify_encoding`) still matter where they validate attacker-supplied
filenames/metadata.

# Out of scope / lower priority
- `bindings/python`, `examples/`, `tools/` — not built by
  `.oss-scanner/Dockerfile`.
- `TORRENT_ABI_VERSION` 2/3/4-only code paths beyond what the default build
  (`deprecated-functions=on`) exercises.
- Bugs only reachable with `asserts=on`/`invariant-checks=full`
  (debug-only instrumentation, not present in the scanned build).
- `fuzzers/`, `test/`, `simulation/` harness code itself.
