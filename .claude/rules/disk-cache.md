---
paths:
  - "src/disk_cache*"
  - "src/pread_disk_io*"
  - "include/libtorrent/aux_/disk_cache*"
---

## Disk Cache (`pread_disk_io`)

The disk cache (`src/disk_cache.cpp`, `include/libtorrent/aux_/disk_cache.hpp`) is used exclusively by `pread_disk_io`. It is a write-back cache for incoming blocks, deferred until pieces are complete or memory pressure forces a flush.

**Data structures:**

- `disk_cache` — the cache itself; holds a `boost::multi_index_container` of `cached_piece_entry` objects, protected by a single `std::mutex`. The container has five indices: by `piece_location` (ordered), by `cheap_to_flush()` (ordered descending), by `need_force_flush()` (ordered descending), by `piece_location` (hashed, for fast lookup), and by `needs_hasher_kick()` (ordered descending).
- `cached_piece_entry` — one entry per in-flight piece. Contains an array of `cached_block_entry` (one per block), an optional `piece_hasher` (`ph`, v1 only), an optional `block_hashes` array (`sha256_hash[]`, v2 only), and the cursors/flags described below.
- `cached_block_entry::write_state` — `std::variant` with three mutually exclusive states:
  - `std::monostate` — no data present (the block has never been written, or its data has been fully released).
  - `disk_job*` — a pending write job is queued. The buffer is owned by the job; `take_write_job()` consumes the job during flushing.
  - `disk_buffer_ref` — the write has been flushed. The buffer may be non-null (`has_buf()`, "held alive" for future hashing) or null (already released). `is_flushed()` is true in either case and contributes to `flushed_cursor`.

**Cursors:**

- `hasher_cursor` — index of the first block not yet hashed (v1 SHA-1 only; hashing is always contiguous from block 0). Owned by the hashing thread while `hashing_flag` is set.
- `flushed_cursor` — index of the first block not yet confirmed flushed to disk. Updated under the mutex after each flush completes.
- `cheap_to_flush()` returns `hasher_cursor - flushed_cursor`: blocks that have been hashed but not yet flushed can be flushed without ever needing read-back.

**Flags on `cached_piece_entry`:**

Flags are stored in `cached_piece_flags flags` (a `bitfield_flag<uint8_t>` bitfield). The mutex must be held to read or modify flags; updates go through `view.modify()` (required by `boost::multi_index`). Once a flag is set, the owning thread may access the protected state **without the mutex**:

- `hashing_flag` — set under the mutex by the thread about to hash blocks. While set, that thread has exclusive access to `ph` (the v1 `piece_hasher`) and may advance `hasher_cursor`. No other thread may touch these. Cleared under the mutex when hashing is complete.
- `flushing_flag` — set under the mutex by the thread about to write blocks to disk. While set, that thread has exclusive access to the block buffers being flushed. No other thread will attempt to flush the same piece. Cleared under the mutex when flushing is complete.
- `force_flush_flag` — set when the piece has all blocks populated; prioritises this piece for flushing. Cleared once all blocks are flushed.
- `piece_hash_returned_flag` — set once the piece hash has been computed and returned to the BitTorrent engine.
- `v1_hashes_flag` — set if this piece requires v1 SHA-1 hashing (`ph` is allocated).
- `v2_hashes_flag` — set if this piece requires v2 SHA-256 block hashing (`block_hashes` is allocated).
- `needs_hasher_kick_flag` — set when a block is inserted and the hasher thread should be woken; cleared when the hasher picks it up. Coalesces multiple insertions into one wakeup.

In both hashing and flushing cases, the lock is **released** for the duration of the actual I/O (hashing or `pwrite()`), then re-acquired to update cursors/flags and potentially free buffers.

**Deferred clear (`clearing_flag`):** `clear_piece` is *not* a storage fence. `try_clear_piece()` clears the piece's block state in place (aborting its pending write jobs) if the piece is idle. If `flushing_flag` or `hashing_flag` is set, it cannot reset the block state safely, so it stores the clear job in `disk_cache::m_pending_clears` (a map keyed by piece — kept out of `cached_piece_entry` since pending clears are rare), sets `clearing_flag`, and returns deferred. The deferred clear is run by whichever thread releases the *last* of `flushing_flag`/`hashing_flag` — `flush_piece_impl()`, `kick_hasher()`, and `hash_piece()` all call `run_deferred_clear_impl()`, which runs `clear_piece_impl()` and routes the aborted writes + clear job through `clear_piece_fun` only once neither flag remains set. While `clearing_flag` is set, `kick_pending_hashers()` will not start a new hash on the piece, so a pending clear can never race a freshly started hasher. The BitTorrent engine locks the piece in the picker (`lock_piece`/`write_failed`) before requesting the clear and only restores it in the completion handler, so no new writes arrive while a clear is pending.

**Held-alive buffers and the flush/hasher race:**

When `flush_piece_impl` flushes a block, it checks `needed_by_hasher = (hashing_flag && block_index >= hasher_cursor)`. If a hasher is mid-snapshot (the `kick_hasher` window between snapshot and re-lock), the pointer to the buffer is in `blocks_storage[]` and must stay live. In that case the block transitions to `disk_buffer_ref{buf}` (non-null), and `m_blocks` is *not* decremented. Otherwise the buffer is freed immediately and `m_blocks` is decremented.

`m_blocks` therefore counts both pending writes and held-alive buffers — both forms of memory pressure. `back_pressure` and `flush_request()` are driven off `m_blocks`.

The held-alive buffer is reclaimed when `kick_hasher` next advances the cursor past it (its cleanup loop frees blocks in `[old_cursor, new_cursor)` via `take_buf`). However, if v1 SHA-1 is blocked by a missing earlier block, the cursor cannot advance and the buffer stays held until either the missing block arrives or the piece is cleared. The "drop held-alive" flush pass (below) handles the case where no further inserts will arrive — without it, back-pressure can pin the cache above the low watermark indefinitely (the back-pressure observer is only notified once `m_blocks` drops to the low watermark, so the producer that tripped back-pressure would never be woken up). Note that v2 hashes for held-alive blocks are typically already in `block_hashes[]` (computed by the kick that triggered the keep-alive), so freeing the buffer only loses the v1 read-back optimization, not v2 hash work.

**Flush strategy (`flush_to_disk`), in order:**

1. Force-flush pass (`view`) — flush pieces with `force_flush` set (piece fully downloaded and hashed); ordered by `need_force_flush()`.
2. Cheap flush pass (`view2`) — flush blocks in the range `[flushed_cursor, hasher_cursor)` (already hashed, no future read-back needed); pieces ordered by `cheap_to_flush()` descending.
3. Drop held-alive pass (`view3`) — for each piece without `flushing_flag` or `hashing_flag`, free all blocks where `has_buf()` (held-alive `disk_buffer_ref{buf}`). No I/O — the data is already on disk. Done before the expensive pass because it's free; only skipped if the target is already met. Loses the v1 read-back optimization for these blocks; a subsequent `async_hash` will read them from disk.
4. Expensive flush pass (`view4`) — flush any dirty blocks even if they haven't been hashed yet, requiring a read-back later to complete the piece hash. Used only when the cache exceeds the high-water mark.

Passes 2-4 run only when `optimistic=false`. The optimistic flush at the top of each disk thread's loop runs only pass 1.
