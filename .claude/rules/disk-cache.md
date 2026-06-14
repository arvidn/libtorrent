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
- `notify_flushed_flag` — set by a `flush_storage()` caller that is waiting on a piece another thread is mid-flush on; `flush_piece_impl`'s `scope_end` checks it and signals `m_flushing_cv`. Only a wakeup signal supporting a single waiter, not a pin — the waiter re-looks-up the piece after each wakeup because it may have been flushed, hashed, or evicted while it slept.

In both hashing and flushing cases, the lock is **released** for the duration of the actual I/O (hashing or `pwrite()`), then re-acquired to update cursors/flags and potentially free buffers.

**Deferred clear:** If `try_clear_piece()` is called while `flushing_flag` or `hashing_flag` is set, the clear job is stored in `cached_piece_entry::clear_piece` and executed by the owning thread once it clears its flag.

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

**`flush_storage()`:** Called as a fence job during torrent teardown (`release_files`, `delete_files`, etc.) to flush every cached piece of one storage to disk. For each piece it flushes any still-dirty blocks (completing their write jobs via `flush_piece_impl`). It does **not** free or erase the piece — eviction is left to `flush_to_disk`. Keeping the piece preserves its partial hash state and lets another thread erase it concurrently without racing the flush. If a piece is mid-flush on another thread (`flushing_flag` set), it sets `notify_flushed_flag`, waits on `m_flushing_cv`, and re-looks-up the piece on each wakeup (it may be flushed, hashed, or evicted while waiting); if it is gone, it moves on. A concurrent `flush_storage()` already waiting on a piece (`notify_flushed_flag` already set) is left to that waiter, since the flag supports only one waiter.
