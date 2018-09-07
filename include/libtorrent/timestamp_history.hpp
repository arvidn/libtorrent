/*

Copyright (c) 2009-2018, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TIMESTAMP_HISTORY_HPP
#define TIMESTAMP_HISTORY_HPP

#include <cstdint>
#include <array>

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent {

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

