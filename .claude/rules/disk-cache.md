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
- `cached_block_entry` — holds either a pending `write_job` (the buffer is owned by the job) or a `buf_holder` (buffer kept alive after the write job has executed but before hashing is done). Once both flushed and hashed, the buffer is freed.

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

**Deferred clear:** If `try_clear_piece()` is called while `flushing_flag` or `hashing_flag` is set, the clear job is stored in `cached_piece_entry::clear_piece` and executed by the owning thread once it clears its flag.

**Flush strategy (`flush_to_disk`):**

1. Force-flush pass — flush pieces with `force_flush` set (piece fully downloaded and hashed); ordered by `need_force_flush()`.
2. Cheap flush pass — flush blocks in the range `[flushed_cursor, hasher_cursor)` (already hashed, no future read-back needed); pieces ordered by `cheap_to_flush()` descending.
3. Expensive flush pass — flush any dirty blocks even if they haven't been hashed yet, requiring a read-back later to complete the piece hash. Used only when the cache exceeds the high-water mark.
