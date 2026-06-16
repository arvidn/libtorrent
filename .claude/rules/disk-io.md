---
paths:
  - "include/libtorrent/disk_interface.hpp"
  - "src/mmap_disk_io*"
  - "src/posix_disk_io*"
  - "src/pread_disk_io*"
  - "src/disk_job_fence*"
---

## `disk_interface` contract

`disk_interface` (`include/libtorrent/disk_interface.hpp`) is the session-level
customization point for disk I/O, selected via
`session_params::disk_io_constructor`. Built-in implementations: `mmap_disk_io`,
`posix_disk_io`, `pread_disk_io`.

**Threading.** Every `async_*` method is called from libtorrent's single network
thread. The work may be performed asynchronously on the implementation's own
threads, but each completion handler **must be posted back to the network
thread** via the `io_context` given to the constructor; handlers run on the
network thread. An implementation may use any number of threads and service
queued jobs concurrently and out of order, except where the contract below
requires ordering.

**`async_hash` reflects all prior `async_write`s.** For a given piece, the
engine posts all of the piece's blocks via `async_write`, then posts
`async_hash` for that piece. The hash returned **must be computed over the data
of every `async_write` for that piece that was posted before it.** This is the
key constraint for a multi-threaded back end that services jobs out of order: it
must order a piece's hash after that piece's writes and must never hash stale
or missing data. The piece picker treats the returned hash as authoritative for
the data it handed over. `async_hash2` (single v2 block hash) has the same
requirement for the block it covers.

**Fence jobs.** Several operations must run with exclusive access to a storage,
synchronized against all other I/O on it. These are *fence jobs*. A fence job:

- waits for all jobs currently outstanding **on that storage** to finish, then
  executes -- so it runs alone, with no other operation on the storage in
  flight;
- *fences* everything posted after it: any job posted to the same storage while
  the fence is raised is queued and not executed until the fence job completes.

Fences are per-storage -- a fence on one storage does not block jobs on another.
The fence operations are:

- `async_clear_piece` -- synchronize with all outstanding I/O to the piece
  before the engine re-requests its blocks
- `async_move_storage`
- `async_release_files`
- `async_stop_torrent`
- `async_rename_file`
- `async_delete_files`
- `async_set_file_priority`
- `async_check_files`

`async_read`, `async_write`, `async_hash`, and `async_hash2` are ordinary
(non-fence) jobs. The built-in back ends implement the fence semantics with
`disk_job_fence` (`src/disk_job_fence.cpp`). A storage's outstanding-job count
is what the fence waits to reach zero before it runs; a job stays counted from
the moment it is accepted until its completion is posted back.

**Every ordinary job must honor the fence -- including a buffered write.** Each
job, with no exceptions, has to participate in the outstanding-job count for its
whole lifetime, so the fence cannot run while any work on the storage is still
in flight. This is easy to overlook for `async_write` on a write-back back end:
even though the write is inserted into a cache and its handler is deferred, it
must be counted as outstanding from the moment it is accepted until the flush
that retires it has finished and its completion is posted -- *including the
window while a disk thread is mid-flush writing it out*. It must not take a fast
path that skips the count. If a buffered write (or the thread flushing it) is
invisible to the fence, the fence can be raised and complete while that write is
still being written to disk, and a teardown fence (`async_stop_torrent`,
`async_release_files`, ...) can then tear the storage down underneath the
in-flight flush -- a use-after-free. So the rule is symmetric: a write counts
exactly like a read or a hash.

### Fences and deferred (write-back) writes

The fence contract above is straightforward for an implementation that runs
each job to completion immediately. An implementation that *defers* writes --
reporting an `async_write` complete before the data is durably on disk, as a
write-back cache does -- has to uphold two further invariants. Both are easy to
get wrong, and a violation of either is a deadlock or silent data corruption,
not a degraded-performance bug.

**A fence must not wait on writes that nothing will flush (forward progress).**
A fence waits for every job outstanding on the storage to complete before it
runs. When a write is only reported complete once it is durably stored, that
write stays outstanding -- and keeps the fence waiting -- the whole time it sits
buffered. The flush that would retire it normally happens when the piece
completes (its `async_hash`) or under memory pressure; but the completing hash
is itself an ordinary job, now queued *behind* the very fence that is waiting
for it, and memory pressure may never arrive. The invariant: **raising a fence
must guarantee the storage's buffered writes get flushed, rather than waiting
passively for a flush trigger the fence is itself blocking.** Otherwise the
fence and its own backlog deadlock.

**Resuming a fenced backlog must preserve posting order against new jobs.** When
a fence completes, the jobs that queued behind it are resumed. If resuming a job
is observable -- a buffered write becomes visible, a read is served -- then a job
the network thread posts *during* that resume must be ordered after the whole
resumed backlog, never interleaved ahead of it. The invariant: **a job arriving
while the backlog is being resumed must not bypass the backlog.** Violate it and
a freshly-posted `async_read` overtakes a still-queued `async_write` for the
same block and returns stale data (or a hole).

**A read resumed from a fence must reflect writes resumed ahead of it.** A
corollary of the ordering invariant: a read unblocked from the backlog may be
preceded in that same backlog by a write to the very block it reads (both were
blocked by the fence). That read must observe the write -- i.e. it must be
served from the buffered data, not from disk where the superseding write has not
yet landed. Put plainly: the reads and writes a fence releases must take effect
in the order they were posted, exactly as if the fence had never been raised.

**Back-pressure.** `async_write` returns `true` when the write queue is over the
high watermark; the caller then stops issuing writes for that peer until the
`disk_observer` passed in is notified that the queue has drained. `false` means
the caller may keep writing.

**Batching.** `async_*` calls only enqueue work; `submit_jobs()` is called once
after a batch to wake the disk thread(s). An implementation may also wake on each
`async_*`, but should tolerate the batched notification.
