/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/back_pressure.hpp"
#include <functional>
#include <algorithm>
#include <limits>

namespace libtorrent::aux {

namespace {

	// this is posted to the network thread
	void watermark_callback(std::vector<std::weak_ptr<disk_observer>> const& cbs)
	{
		for (auto const& i : cbs)
		{
			std::shared_ptr<disk_observer> o = i.lock();
			if (o) o->on_disk();
		}
	}

} // anonymous namespace

// checks to see if we're no longer exceeding the high watermark,
// and if we're in fact below the low watermark. If so, we need to
// post the notification messages to the peers that are waiting for
// more buffers to received data into
void back_pressure::check_buffer_level(int const level)
{
	if (!m_exceeded_max_size || level > m_low_watermark) return;

	m_exceeded_max_size = false;

	std::vector<std::weak_ptr<disk_observer>> cbs;
	m_observers.swap(cbs);
	post(m_ios, std::bind(&watermark_callback, std::move(cbs)));
}

bool back_pressure::has_back_pressure(int const level, std::shared_ptr<disk_observer> o)
{
	if (level >= m_max_size)
	{
		m_exceeded_max_size = true;
		if (o) m_observers.push_back(std::move(o));
		return true;
	}
	return false;
}

std::optional<int> back_pressure::should_flush(int const level) const
{
	if (level >= m_high_watermark || m_exceeded_max_size)
		return m_low_watermark;
	else
		return std::nullopt;
}

void back_pressure::set_max_size(int const max_size)
{
	m_max_size = max_size;
	m_low_watermark = static_cast<int>(std::min(std::int64_t(max_size) / 4 * 3, std::int64_t(std::numeric_limits<int>::max() / 3)));
	m_high_watermark = static_cast<int>(std::min(std::int64_t(max_size) / 8 * 7, std::int64_t(std::numeric_limits<int>::max() / 2)));
}

}
