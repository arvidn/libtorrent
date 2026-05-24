/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_BACK_PRESSURE
#define TORRENT_BACK_PRESSURE

#include <optional>
#include <memory>
#include <atomic>
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/disk_observer.hpp"

namespace libtorrent {
	struct disk_observer;
}

namespace libtorrent::aux {

struct TORRENT_EXTRA_EXPORT back_pressure
{
	back_pressure(io_context& ios) : m_ios(ios) {}
	void set_max_size(int max_size);
	std::optional<int> should_flush(int const level) const;
	bool has_back_pressure(int level, std::shared_ptr<disk_observer> o);
	void check_buffer_level(int level);

	// returns true once we have exceeded the max size and have engaged
	// back-pressure (i.e. peers are being held back from sending more data).
	// It stays true until we have flushed back down to the low watermark, so
	// there is hysteresis between when it becomes true and when it clears
	// again. While true, writes can only make progress by flushing down to the
	// low watermark, so flushing must not be deferred behind read jobs.
	// This is read without holding the disk_cache mutex (see
	// disk_cache::should_force_flush()), so the flag is atomic. This flag is
	// not independent of the rest of the cache state; it summarizes it. A
	// reader that observes the flag change must also observe the state changes
	// the writer made before flipping it, so this load must synchronize-with
	// the store under the mutex. We therefore use the default (sequentially
	// consistent) ordering rather than relaxed.
	bool exceeded() const { return m_exceeded_max_size.load(); }

private:

	// apply back-pressure to peers when m_blocks reach this level
	int m_max_size = 9;

	// start flushing when m_blocks hits the high watermark
	int m_high_watermark = 0;

	// stop flushing when m_blocks hits the low watermark
	int m_low_watermark = 0;

	// after we exceed the high watermark, we set this to true and start
	// flushing, we don't stop flushing until we hit the low watermark. Then we
	// set this to false. Writes are always made while holding the disk_cache
	// mutex; it is atomic only so exceeded() can be read without that mutex.
	std::atomic<bool> m_exceeded_max_size{false};

	// if we exceed the max number of buffers, we start
	// adding up callbacks to this queue. Once the number
	// of buffers in use drops below the low watermark,
	// we start calling these functions back
	std::vector<std::weak_ptr<disk_observer>> m_observers;

	// this is the main thread io_context. Callbacks are
	// posted on this in order to have them execute in
	// the main thread.
	io_context& m_ios;
};

}

#endif // TORRENT_BACK_PRESSURE
