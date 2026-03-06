---
paths:
  - "src/kademlia/**"
  - "include/libtorrent/kademlia/**"
---

# DHT (Kademlia) Implementation

Source: `src/kademlia/`, `include/libtorrent/kademlia/`

## BEP Support

| BEP | Description | Status |
|-----|-------------|--------|
| BEP 5  | DHT Protocol (ping, find_node, get_peers, announce_peer) | Full |
| BEP 32 | IPv6 extension (dual-stack, nodes6) | Full |
| BEP 33 | DHT Scrape (bloom filters BFpe/BFsd) | Full |
| BEP 42 | Node ID Verification (IP-derived IDs) | Full |
| BEP 44 | Arbitrary data storage (immutable + mutable items, Ed25519) | Full |
| BEP 51 | DHT Infohash Indexing (sample_infohashes) | Full |

---

## File Overview

| File | Purpose |
|------|---------|
| `node.cpp` / `node.hpp` | Central DHT node — routing, dispatch, traversal lifecycle |
| `dht_tracker.cpp` / `dht_tracker.hpp` | Session-level coordinator, one `node` per listen socket |
| `routing_table.cpp` / `routing_table.hpp` | K-bucket routing table |
| `rpc_manager.cpp` / `rpc_manager.hpp` | Tracks outstanding RPCs, calls observers on response/timeout |
| `observer.hpp` | Base observer class for pending RPC state |
| `traversal_algorithm.cpp` / `.hpp` | Base iterative search algorithm |
| `find_data.cpp` / `.hpp` | Find-nodes traversal, base for get_peers and get_item |
| `get_peers.cpp` / `.hpp` | BEP 5 peer lookup traversal |
| `get_item.cpp` / `.hpp` | BEP 44 item retrieval traversal |
| `put_data.cpp` / `.hpp` | BEP 44 item storage traversal |
| `sample_infohashes.cpp` / `.hpp` | BEP 51 traversal |
| `refresh.cpp` / `.hpp` | Bootstrap / bucket-refresh traversal |
| `direct_request.hpp` | Single-message direct request |
| `node_id.cpp` / `node_id.hpp` | `node_id` type (alias for `sha1_hash`), XOR distance, BEP 42 generation |
| `node_entry.cpp` / `node_entry.hpp` | Single routing-table entry |
| `item.cpp` / `item.hpp` | Mutable/immutable item type |
| `types.hpp` | `public_key`, `secret_key`, `signature`, `sequence_number` |
| `msg.hpp` | Incoming message structure |
| `dht_storage.cpp` / `dht_storage.hpp` | Pure-virtual storage interface + default RAM implementation |
| `dht_state.cpp` / `dht_state.hpp` | Persistent state (node IDs, bootstrap nodes) |
| `dht_settings.hpp` | Per-DHT settings struct |
| `dht_observer.hpp` | Callbacks from DHT into `session_impl` |
| `dos_blocker.cpp` / `dos_blocker.hpp` | Per-IP rate limiting (5 pkt/s, 5-min block) |
| `ed25519.hpp` | Ed25519 sign/verify API (vendored impl in `src/ed25519/`) |
| `announce_flags.hpp` | Announce flags: `seed`, `implied_port`, `ssl_torrent` |

---

## Threading

All DHT code runs on the **session network thread** (boost.asio). No locking needed inside DHT classes. `rpc_manager` uses a mutex only for its observer-pool allocator.

---

## Core Data Structures

### `node_id`
Type alias for `sha1_hash` (160 bits).

Key functions:
- `distance(a, b)` — XOR metric
- `distance_exp(a, b)` — index of highest differing bit (0–160)
- `compare_ref(a, b, ref)` — compare distances to reference
- `generate_id(external_ip)` — BEP 42 derivation
- `verify_id(nid, source_ip)` — BEP 42 check

### `node_entry`
One routing-table peer entry:
- `node_id id`, `udp::endpoint endpoint`
- `uint16_t rtt` (0xffff = unknown)
- `uint8_t timeout_count` (0 = confirmed, 0xff = never pinged)
- `bool verified` (BEP 42)
- `time_point last_queried`

### `routing_table`
160 k-buckets (one per XOR distance bit). Extended routing table (default on) gives larger buckets close to us:

| Bucket index | Multiplier | Max live nodes (k=20) |
|---|---|---|
| 0 | 16× | 320 |
| 1 | 8× | 160 |
| 2 | 4× | 80 |
| 3 | 2× | 40 |
| 4–159 | 1× | 20 (k) |

Key behaviours:
- Last (closest) bucket splits when it exceeds k entries
- Each bucket has a **replacement cache**; candidates promoted when live node is removed
- `ip_set` restricts one node per IP per bucket (BEP 42, `restrict_routing_ips`)
- Periodic refresh: random query in each bucket's range every ~15 min

### `item` (BEP 44)
- **Immutable** — key is `sha1(value)`, no signature
- **Mutable** — key is `sha1(salt || pk)`, Ed25519-signed, has `sequence_number`
- CAS: `cas` field lets clients do compare-and-swap on sequence number

### `dht_storage_interface`
Pure-virtual; default RAM implementation (`dht_default_storage_constructor`):

```
map<sha1_hash, torrent_entry>        m_torrents       (BEP 5 peers)
map<sha1_hash, dht_immutable_item>   m_immutable_items
map<sha1_hash, dht_mutable_item>     m_mutable_items
```

Eviction uses distance from our node IDs — items far from us are evicted first.
Limits: `max_torrents` (2000), `max_peers` (500), `max_dht_items` (700).
Peer entries expire after 30 min (announce_interval).

---

## Key Classes

### `node`
One per listen socket. Owns `routing_table` and `rpc_manager`.

Important methods:
```cpp
void bootstrap(endpoints, done_cb);
void get_peers(info_hash, callback, flags);
void announce(info_hash, port, flags, callback);
void get_item(target, callback);           // immutable
void get_item(pk, salt, callback);         // mutable
void put_item(data, callback);             // immutable
void put_item(pk, sk, salt, callback);     // mutable
void sample_infohashes(endpoint, target, callback);
void incoming(udp::endpoint, msg);         // dispatch received UDP
void tick();                               // token rotation, bucket refresh
```

Write tokens: `hash(requester_ip + secret + info_hash)`. Two secrets kept; rotated periodically to allow stale tokens for one rotation period.

### `dht_tracker`
Owns `std::map<listen_socket_handle, tracker_node>` (one `node` per socket).
Aggregates results across all nodes. Manages DOS blocking. Saves/restores `dht_state`.

### `rpc_manager`
- `invoke(entry, endpoint, observer_ptr)` — sends bencoded RPC, tracks by 2-byte transaction ID
- `incoming(msg, &sender_id)` — looks up observer by txn-id, calls `observer::reply()`
- `tick()` — fires `observer::timeout()` / `observer::short_timeout()` for stale requests
- Uses an object pool for observers to reduce heap fragmentation

### `observer`
One per pending RPC. Flags:

| Flag | Meaning |
|------|---------|
| `flag_queried` | Request sent |
| `flag_initial` | First attempt |
| `flag_short_timeout` | Intermediate timeout fired |
| `flag_failed` | Node failed |
| `flag_alive` | Response received |
| `flag_done` | Finished, no longer tracked |

Observer hierarchy:
```
observer
  ├─ traversal_observer          (parses "nodes"/"nodes6" from responses)
  │   ├─ find_data_observer
  │   │   ├─ get_peers_observer  (also parses "values", bloom filters)
  │   │   └─ get_item_observer   (parses v/k/sig/seq)
  │   ├─ put_data_observer
  │   └─ sample_infohashes_observer
  ├─ announce_observer
  ├─ direct_observer
  └─ null_observer
```

---

## Traversal Algorithms

All searches inherit from `traversal_algorithm`. The base implements the Kademlia iterative lookup:

1. Seed result set with k closest nodes from routing table
2. Issue α (default 3) concurrent requests (`add_requests()`)
3. Each response: add new closer nodes, sort result set
4. Repeat until k closest nodes all responded or timeout
5. Call `done()`

| Class | BEP | RPC sent | Callback delivers |
|-------|-----|----------|-------------------|
| `find_data` | 5 | `find_node` | closest nodes + write tokens |
| `get_peers` | 5 | `get_peers` | `vector<tcp::endpoint>` (peers) |
| `get_item` | 44 | `get` | `item`, `bool authoritative` |
| `put_data` | 44 | first `get`, then `put` | response count |
| `sample_infohashes` | 51 | `sample_infohashes` | samples, interval, total |
| `bootstrap` / `refresh` | 5 | `get_peers` (our own ID) | (fires bucket refresh) |
| `direct_traversal` | — | arbitrary | single `msg` |

`obfuscated_get_peers`: wraps `get_peers`, uses a random target until convergence, then switches to the real info_hash (BEP 5 privacy mode, controlled by `dht_privacy_lookups`).

---

## Message Format (bencode)

All messages are bencoded dicts with mandatory keys `"t"` (transaction id), `"y"` (`"q"/"r"/"e"`), optional `"v"` (version), `"ip"` (external address, BEP 42), `"ro"` (read-only, BEP 5).

**Queries** (`y="q"`): `"q"` = method name, `"a"` = argument dict with `"id"` (our node id, 20 bytes).

**Responses** (`y="r"`): `"r"` = response dict.

**Errors** (`y="e"`): `"e"` = `[error_code, "message"]`.

### Method summary

| Method | Key request fields | Key response fields |
|--------|--------------------|---------------------|
| `ping` | `id` | `id` |
| `find_node` | `id`, `target`, `want` | `id`, `nodes`, `nodes6` |
| `get_peers` | `id`, `info_hash`, `noseed`, `scrape`, `want` | `id`, `token`, `values`\|`nodes`, `BFpe`, `BFsd` |
| `announce_peer` | `id`, `info_hash`, `port`, `token`, `implied_port`, `seed` | `id` |
| `get` (BEP 44) | `id`, `target`, [`seq`] | `id`, `v`, [`k`, `sig`, `seq`] |
| `put` (BEP 44) | `id`, `v`, [`k`, `sig`, `seq`, `salt`, `cas`] | `id` |
| `sample_infohashes` | `id`, `target` | `id`, `samples`, `num`, `interval`, `nodes` |

Compact node encoding: 26 bytes per IPv4 node (20-byte id + 4-byte IP + 2-byte port), 38 bytes for IPv6.

---

## Bootstrap

1. Load `dht_state` (saved node IDs + endpoints)
2. Assign node ID: saved → BEP 42 from external IP → random
3. Add router nodes from settings (`router.bittorrent.com:6881`, etc.)
4. Run `bootstrap` traversal against saved/configured endpoints (queries `get_peers` for own ID)
5. On completion, refresh all non-contiguous buckets
6. `dht_observer::get_peers()` notifies `session_impl` to query for all active torrents

---

## Integration with `session_impl`

`session_impl` owns `shared_ptr<dht_tracker> m_dht` and implements `dht_observer`:

| Observer method | When called |
|-----------------|-------------|
| `set_external_address()` | DHT detects our external IP via `"ip"` field |
| `get_listen_port(socket)` | Node asks for our announce port |
| `get_peers(info_hash, peer)` | Another node announced a peer to us |
| `announce(info_hash, endpoint)` | We received a get_peers for one of our torrents |
| `on_dht_request(method, msg, reply)` | Custom extension message handler |

Session calls:
- `dht_tracker::incoming_packet(socket, endpoint, buffer)` — for each received DHT UDP packet
- `dht_tracker::new_socket(handle)` / `delete_socket(handle)` — listen port changes
- `dht_tracker::tick()` — periodic maintenance

Relevant `settings_pack` keys:

| Setting | Effect |
|---------|--------|
| `enable_dht` | Toggle DHT |
| `dht_search_branching` | Alpha (concurrent requests) |
| `dht_max_peers_reply` | Peers returned per get_peers response |
| `dht_enforce_node_id` | Reject nodes failing BEP 42 check |
| `dht_restrict_routing_ips` | One node per /24 IPv4 per bucket |
| `dht_restrict_search_ips` | Sybil mitigation in traversals |
| `dht_privacy_lookups` | Enable obfuscated_get_peers |
| `dht_read_only` | Set `ro=1` flag, don't accept queries |
| `dht_upload_rate_limit` | Response bytes/sec cap |
| `dht_item_lifetime` | BEP 44 item expiry (0 = never) |
| `dht_sample_infohashes_interval` | BEP 51 cache TTL |

---

## Constants and Tunables

| Parameter | Value | Notes |
|-----------|-------|-------|
| Node ID | 160 bits | SHA-1 |
| k (bucket size) | 20 | Configurable |
| Alpha | 3 | Concurrent requests per traversal |
| RPC full timeout | 15 s | |
| RPC short timeout | ~6 s | Triggers next request early |
| Token rotation | ~7 min | Two tokens kept |
| Bucket refresh | 15 min | Per-bucket, measured from last query |
| Peer announce validity | 30 min | |
| DOS block rate | 5 pkt/s | Per source IP |
| DOS block duration | 5 min | |
| DOS tracker size | 20 | Most-recent offenders |
