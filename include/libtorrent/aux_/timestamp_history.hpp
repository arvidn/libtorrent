/*

Copyright (c) 2010-2012, 2014-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TIMESTAMP_HISTORY_HPP
#define TIMESTAMP_HISTORY_HPP

#include <cstdint>
#include <array>

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

namespace lt::aux {

// timestamp history keeps a history of the lowest timestamps we've
// seen in the last 20 minutes
struct TORRENT_EXTRA_EXPORT timestamp_history
{
	static constexpr int history_size = 20;

	timestamp_history() = default;
	bool initialized() const { return m_num_samples != not_initialized; }

	// add a sample to the timestamp history. If step is true, it's been
	// a minute since the last step
	std::uint32_t add_sample(std::uint32_t sample, bool step);
	std::uint32_t base() const { TORRENT_ASSERT(initialized()); return m_base; }
	void adjust_base(int change);

private:

	// this is a circular buffer
	std::array<std::uint32_t, history_size> m_history;

	// this is the lowest sample seen in the
	// last 'history_size' minutes
	std::uint32_t m_base = 0;

	// and this is the index we're currently at
	// in the circular buffer
	std::uint16_t m_index = 0;

	static constexpr std::uint16_t not_initialized = 0xffff;

	// this is the number of samples since the
	// last time we stepped one minute. If we
	// don't have enough samples, we won't step
	// if this is set to 'not_initialized' we
	// have bit seen any samples at all yet
	// and m_base is not initialized yet
	std::uint16_t m_num_samples = not_initialized;
};

}

#endif

