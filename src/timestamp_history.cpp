/*

Copyright (c) 2010, 2014, 2016, 2018-2020, Arvid Norberg
Copyright (c) 2016-2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/


#include "libtorrent/aux_/timestamp_history.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

namespace lt::aux {

constexpr std::uint32_t TIME_MASK = 0xffffffff;

// defined in utp_stream.cpp
bool compare_less_wrap(std::uint32_t lhs, std::uint32_t rhs
	, std::uint32_t mask);

std::uint32_t timestamp_history::add_sample(std::uint32_t sample, bool step)
{
	if (!initialized())
	{
		m_history.fill(sample);
		m_base = sample;
		m_num_samples = 0;
	}

	// don't let the counter wrap
	if (m_num_samples < 0xfffe) ++m_num_samples;

	// if sample is less than base, update the base
	// and update the history entry (because it will
	// be less than that too)
	if (compare_less_wrap(sample, m_base, TIME_MASK))
	{
		m_base = sample;
		m_history[m_index] = sample;
	}
	// if sample is less than our history entry, update it
	else if (compare_less_wrap(sample, m_history[m_index], TIME_MASK))
	{
		m_history[m_index] = sample;
	}

	std::uint32_t ret = sample - m_base;

	// don't step base delay history unless we have at least 120
	// samples. Anything less would suggest that the connection is
	// essentially idle and the samples are probably not very reliable
	if (step && m_num_samples > 120)
	{
		m_num_samples = 0;
		m_index = (m_index + 1) % history_size;

		m_history[m_index] = sample;
		// update m_base
		m_base = sample;
		for (auto& h : m_history)
		{
			if (compare_less_wrap(h, m_base, TIME_MASK))
				m_base = h;
		}
	}
	return ret;
}

void timestamp_history::adjust_base(int change)
{
	TORRENT_ASSERT(initialized());
	m_base += aux::numeric_cast<std::uint32_t>(change);
	// make sure this adjustment sticks by updating all history slots
	for (auto& h : m_history)
	{
		if (compare_less_wrap(h, m_base, TIME_MASK))
			h = m_base;
	}
}

}
