---
paths:
  - "src/piece_picker.cpp"
  - "include/libtorrent/aux_/piece_picker.hpp"
  - "test/test_piece_picker.cpp"
---

## Piece Picker

The piece picker (`src/piece_picker.cpp`, `include/libtorrent/aux_/piece_picker.hpp`) decides
which blocks to request from which peers. It tracks availability (how many peers have each piece),
per-piece priority, and the state of every block in every partially-downloaded piece.

### Core data structures

**`piece_pos`** — 8 bytes per piece, tightly packed:
- `peer_count` (26 bits) — number of peers that have this piece
- `download_state` (3 bits) — one of the `download_queue_t` constants below
- `piece_priority` (3 bits) — 0 = filtered (dont-download), 1–7 user priority (4 = default)
- `index` (`prio_index_t`) — position in `m_pieces`, or `we_have_index` (-1) if we have it

**`m_piece_map`** — `aux::vector<piece_pos, piece_index_t>`, one entry per piece.

**`m_pieces`** — flat `aux::vector<piece_index_t, prio_index_t>` of all pickable (not filtered,
not have) pieces, sorted by effective priority. Within each priority band pieces are in random
order (rarest-first is approximated by the priority formula, not by sorting on availability alone).

**`m_priority_boundaries`** — cumulative end-indices into `m_pieces`. Priority band `p` spans
`[m_priority_boundaries[p-1], m_priority_boundaries[p])`. Priority 0 starts at index 0.

**`m_downloads`** — `aux::array` of 4 sorted `vector<downloading_piece>` (one per download
category), indexed by `download_queue_t`:
- `piece_downloading` (0) — some blocks still open (not yet requested)
- `piece_full` (1) — all blocks requested, at least one not yet writing/finished
- `piece_finished` (2) — all blocks writing or finished
- `piece_zero_prio` (3) — downloading but priority = 0

`piece_downloading_reverse` and `piece_full_reverse` are pseudo-states stored in `download_state`
to de-prioritize pieces only requested from reverse peers; they map to the same download vectors
as their non-reverse counterparts.

**`downloading_piece`** — one per in-flight piece:
- `info_idx` (`uint16_t`) — index into `m_block_info`; the slice `[info_idx * blocks_per_piece, (info_idx+1) * blocks_per_piece)` holds the block states for this piece
- `finished`, `writing`, `requested` (15 bits each) — block state counters
- `passed_hash_check` (1 bit) — hash job returned OK (piece may not yet be on disk)
- `locked` (1 bit) — blocks cannot be picked; set during error recovery, cleared by `restore_piece()`
- `hashing` (1 bit) — outstanding hash request in flight

**`m_block_info`** — flat `aux::vector<block_info>`. Each downloading piece owns
`blocks_per_piece()` consecutive entries identified by `info_idx`. Free ranges are recycled
via `m_free_block_infos` (a free-list of `uint16_t` indices).

**`block_info`** — per block:
- `peer` — last peer to request/write this block
- `num_peers` (14 bits) — peers with this block outstanding
- `state` (2 bits) — `state_none → state_requested → state_writing → state_finished`

### Priority formula

`piece_pos::priority(picker)` returns -1 (not pickable) if: filtered, have, no availability
(`peer_count + m_seeds == 0`), `piece_full`, or `piece_finished`.

Otherwise:
```
priority = availability * (priority_levels - piece_priority) * prio_factor + adjustment
```
where `priority_levels = 8`, `prio_factor = 3`, and:
- `adjustment = -3` for actively downloading pieces (non-reverse)
- `adjustment = -2` for open pieces
- `adjustment = -1` for reverse-downloading pieces

Lower value = picked first. The `prio_factor = 3` creates three sub-levels per availability
tier, letting downloading pieces beat open ones at the same availability.

### Seeds optimization

Peers with all pieces are tracked in `m_seeds` rather than incrementing every `peer_count`.
True availability of a piece is `peer_count + m_seeds`. When a seed sends DONT_HAVE,
`break_one_seed()` decrements `m_seeds` and increments every `piece_pos::peer_count`.

### Lazy rebuild (`m_dirty`)

`m_pieces` and `m_priority_boundaries` are rebuilt lazily. When `m_dirty` is true, they are
stale; `update_pieces()` (called automatically before any pick) rebuilds them:
1. Walk `m_piece_map`, count pieces per priority band → `m_priority_boundaries` holds deltas
2. Make boundaries cumulative, resize `m_pieces`
3. Fill `m_pieces` using per-piece relative offsets stored in `piece_pos::index`
4. Shuffle each priority band randomly
5. Fix up `piece_pos::index` to point back into the final positions in `m_pieces`

Bulk operations (e.g. `inc_refcount(bitfield)` touching many pieces) set `m_dirty = true`
rather than updating incrementally. For small bitfield changes (< 50 pieces), the picker
updates incrementally via `update()` / `add()` / `remove()` without dirtying.

### Sequential download cursors

`m_cursor` — lowest piece index not yet had.
`m_reverse_cursor` — one past the highest piece index not yet had (all pieces from here to end
are had). `set_sequential_range(first, last)` constrains the cursor window.

### Piece lifecycle

```
open → downloading → full → finished → (removed, piece_pos::have set)
```

- `mark_as_downloading(block, peer)` — block: none→requested; piece may transition open→downloading
- `mark_as_writing(block, peer)` — block: requested→writing
- `mark_as_finished(block, peer)` — block: writing→finished; calls `update_piece_state()` which
  may advance the piece from downloading→full→finished
- `piece_passed(index)` — hash check passed; sets `passed_hash_check`, calls `account_have()`,
  calls `piece_flushed()` if all blocks are also finished
- `piece_flushed(index)` — piece is on disk; removes from `m_downloads`, removes from `m_pieces`
  via `remove()`, sets `piece_pos::index = we_have_index`
- `restore_piece(index, blocks)` — after hash failure or write error; clears `locked`, optionally
  resets specific blocks back to `state_none`, re-opens the piece for downloading

### `pick_pieces` options

`picker_options_t` flags passed to `pick_pieces()`:
- `rarest_first` — pick lowest-availability pieces (the default; driven by the priority formula)
- `reverse` — pick most-common first, or last pieces if sequential
- `sequential` — pick within the `[m_cursor, m_reverse_cursor)` window in order
- `on_parole` — only pick pieces exclusively requested by this peer (after suspected bad data)
- `prioritize_partials` — prefer pieces already in `m_downloads` over fresh ones
- `piece_extent_affinity` — create affinity for 4 MiB extents of adjacent pieces
- `align_expanded_pieces` — align contiguous-block expansion to natural boundaries

Auto partial prioritization: if `num_partials > num_peers * 3 / 2` or
`num_partials * blocks_per_piece > 2048`, `prioritize_partials` is forced on and
`prefer_contiguous_blocks` is set to 0 to avoid unbounded partial piece growth.

`prefer_contiguous_blocks` (int, not a flag) — request this many consecutive blocks from the
same piece; used by web peers to pipeline larger requests.
