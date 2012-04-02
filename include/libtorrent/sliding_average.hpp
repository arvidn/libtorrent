/*

Copyright (c) 2010, Arvid Norberg
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

#ifndef TORRENT_SLIDING_AVERAGE_HPP_INCLUDED
#define TORRENT_SLIDING_AVERAGE_HPP_INCLUDED

namespace libtorrent
{
// a sliding average accumulator. Add samples to it and it
// keeps track of a sliding mean value and an average deviation
// from that average.
template <int history_size>
struct sliding_average
{
	sliding_average(): m_mean(-1), m_average_deviation(-1) {}

	void add_sample(int s)
	{
		if (m_mean == -1)
		{
			m_mean = s;
			return;
		}
		int deviation = abs(m_mean - s);

		m_mean = m_mean - m_mean / history_size + s / history_size;

		if (m_average_deviation == -1)
		{
			m_average_deviation = deviation;
			return;
		}
		m_average_deviation = m_average_deviation - m_average_deviation
			/ history_size + deviation / history_size;
	}

	int mean() const { return m_mean != -1 ? m_mean : 0; }
	int avg_deviation() const { return m_average_deviation != -1 ? m_average_deviation : 0; }

private:
	int m_mean;
	int m_average_deviation;
};

struct average_accumulator
{
	average_accumulator()
		: m_num_samples(0)
		, m_sample_sum(0)
	{}

	void add_sample(int s)
	{
		++m_num_samples;
		m_sample_sum += s;
	}

	int mean()
	{
		int ret;
		if (m_num_samples == 0) ret = 0;
		else ret = int(m_sample_sum / m_num_samples);
		m_num_samples = 0;
		m_sample_sum = 0;
		return ret;
	}

	int m_num_samples;
	size_type m_sample_sum;
};

}

#endif

