/*

Copyright (c) 2010, 2014, 2016, 2018-2020, Arvid Norberg
Copyright (c) 2019, Amir Abrams
Copyright (c) 2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_SLIDING_AVERAGE_HPP_INCLUDED
#define TORRENT_SLIDING_AVERAGE_HPP_INCLUDED

#include <cstdint>
#include <cstdlib> // for std::abs
#include <limits>
#include <type_traits> // for is_integral

#include "libtorrent/assert.hpp"

namespace lt::aux {

// an exponential moving average accumulator. Add samples to it and it keeps
// track of a moving mean value and an average deviation
template <typename Int, Int inverted_gain>
struct sliding_average
{
	static_assert(std::is_integral<Int>::value, "template argument must be integral");

	sliding_average(): m_mean(0), m_average_deviation(0), m_num_samples(0) {}
	sliding_average(sliding_average const&) = default;
	sliding_average& operator=(sliding_average const&) = default;

	void add_sample(Int s)
	{
		TORRENT_ASSERT(s < std::numeric_limits<Int>::max() / 64);
		// fixed point
		s *= 64;
		Int const deviation = (m_num_samples > 0) ? std::abs(m_mean - s) : 0;

		if (m_num_samples < inverted_gain)
			++m_num_samples;

		m_mean += (s - m_mean) / m_num_samples;

		if (m_num_samples > 1) {
			// the exact same thing for deviation off the mean except -1 on
			// the samples, because the number of deviation samples always lags
			// behind by 1 (you need to actual samples to have a single deviation
			// sample).
			m_average_deviation += (deviation - m_average_deviation) / (m_num_samples - 1);
		}
	}

	Int mean() const { return m_num_samples > 0 ? (m_mean + 32) / 64 : 0; }
	Int avg_deviation() const { return m_num_samples > 1 ? (m_average_deviation + 32) / 64 : 0; }
	int num_samples() const { return m_num_samples; }

private:
	// both of these are fixed point values (* 64)
	Int m_mean = 0;
	Int m_average_deviation = 0;
	// the number of samples we have received, but no more than inverted_gain
	// this is the effective inverted_gain
	int m_num_samples = 0;
};

}

#endif
