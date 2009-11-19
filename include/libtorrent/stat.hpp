/*

Copyright (c) 2003, Arvid Norberg
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

#ifndef TORRENT_STAT_HPP_INCLUDED
#define TORRENT_STAT_HPP_INCLUDED

#include <algorithm>
#include <vector>
#include <assert.h>
#include <cstring>

#include "libtorrent/size_type.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	class TORRENT_EXPORT stat_channel
	{
	friend class invariant_access;
	public:
		enum { history = 10 };

		stat_channel()
			: m_counter(0)
			, m_total_counter(0)
			, m_rate_sum(0)
		{
			std::memset(m_rate_history, 0, sizeof(m_rate_history));
		}

		void operator+=(stat_channel const& s)
		{
			m_counter += s.m_counter;
			m_total_counter += s.m_counter;
		}

		void add(int count)
		{
			TORRENT_ASSERT(count >= 0);

			m_counter += count;
			m_total_counter += count;
		}

		// should be called once every second
		void second_tick(float tick_interval);
		float rate() const { return m_rate_sum / float(history); }
		size_type rate_sum() const { return m_rate_sum; }
		size_type total() const { return m_total_counter; }

		void offset(size_type counter)
		{
			TORRENT_ASSERT(counter >= 0);
			m_total_counter += counter;
		}

		size_type counter() const { return m_counter; }

		void clear()
		{
			std::memset(m_rate_history, 0, sizeof(m_rate_history));
			m_counter = 0;
			m_total_counter = 0;
			m_rate_sum = 0;
		}

	private:

#ifdef TORRENT_DEBUG
		void check_invariant() const
		{
			int sum = 0;
			for (int i = 0; i < history; ++i) sum += m_rate_history[i];
			TORRENT_ASSERT(m_rate_sum == sum);
			TORRENT_ASSERT(m_total_counter >= 0);
		}
#endif

		// history of rates a few seconds back
		int m_rate_history[history];

		// the accumulator for this second.
		int m_counter;

		// total counters
		size_type m_total_counter;

		// sum of all elements in m_rate_history
		size_type m_rate_sum;
	};

	class TORRENT_EXPORT stat
	{
	friend class invariant_access;
	public:
		void operator+=(const stat& s)
		{
			for (int i = 0; i < num_channels; ++i)
				m_stat[i] += s.m_stat[i];
		}

		void received_bytes(int bytes_payload, int bytes_protocol)
		{
			TORRENT_ASSERT(bytes_payload >= 0);
			TORRENT_ASSERT(bytes_protocol >= 0);

			m_stat[download_payload].add(bytes_payload);
			m_stat[download_protocol].add(bytes_protocol);
		}

		void sent_bytes(int bytes_payload, int bytes_protocol)
		{
			TORRENT_ASSERT(bytes_payload >= 0);
			TORRENT_ASSERT(bytes_protocol >= 0);

			m_stat[upload_payload].add(bytes_payload);
			m_stat[upload_protocol].add(bytes_protocol);
		}

		// calculate ip protocol overhead
		void calc_ip_overhead()
		{
			int uploaded = m_stat[upload_protocol].counter()
				+ m_stat[upload_payload].counter();
			int downloaded = m_stat[download_protocol].counter()
				+ m_stat[download_payload].counter();

			// IP + TCP headers are 40 bytes per MTU (1460)
			// bytes of payload, but at least 40 bytes
			m_stat[upload_ip_protocol].add((std::max)(uploaded / 1460, uploaded>0?40:0));
			m_stat[download_ip_protocol].add((std::max)(downloaded / 1460, downloaded>0?40:0));

			// also account for ACK traffic. That adds to the transfers
			// in the opposite direction. Even on connections with symmetric
			// transfer rates, it seems to add a penalty.
			m_stat[upload_ip_protocol].add((std::max)(downloaded * 40 / 1460, downloaded>0?40:0));
			m_stat[download_ip_protocol].add((std::max)(uploaded * 40 / 1460, uploaded>0?40:0));
		}

		int upload_ip_overhead() const { return m_stat[upload_ip_protocol].counter(); }
		int download_ip_overhead() const { return m_stat[download_ip_protocol].counter(); }

		// should be called once every second
		void second_tick(float tick_interval)
		{
			for (int i = 0; i < num_channels; ++i)
				m_stat[i].second_tick(tick_interval);
		}

		float upload_rate() const
		{
			return (m_stat[upload_payload].rate_sum()
				+ m_stat[upload_protocol].rate_sum()
				+ m_stat[upload_ip_protocol].rate_sum())
				/ float(stat_channel::history);
		}

		float download_rate() const
		{
			return (m_stat[download_payload].rate_sum()
				+ m_stat[download_protocol].rate_sum()
				+ m_stat[download_ip_protocol].rate_sum())
				/ float(stat_channel::history);
		}

		float upload_payload_rate() const
		{ return m_stat[upload_payload].rate(); }

		float download_payload_rate() const
		{ return m_stat[download_payload].rate(); }

		size_type total_payload_upload() const
		{ return m_stat[upload_payload].total(); }
		size_type total_payload_download() const
		{ return m_stat[download_payload].total(); }

		size_type total_protocol_upload() const
		{ return m_stat[upload_protocol].total(); }
		size_type total_protocol_download() const
		{ return m_stat[download_protocol].total(); }

		// this is used to offset the statistics when a
		// peer_connection is opened and have some previous
		// transfers from earlier connections.
		void add_stat(size_type downloaded, size_type uploaded)
		{
			TORRENT_ASSERT(downloaded >= 0);
			TORRENT_ASSERT(uploaded >= 0);
			m_stat[download_payload].offset(downloaded);
			m_stat[upload_payload].offset(uploaded);
		}

		size_type last_payload_downloaded() const
		{ return m_stat[download_payload].counter(); }
		size_type last_payload_uploaded() const
		{ return m_stat[upload_payload].counter(); }

		void clear()
		{
			for (int i = 0; i < num_channels; ++i)
				m_stat[i].clear();
		}

	private:

		// these are the channels we keep stats for
		enum
		{
			upload_payload,
			upload_protocol,
			upload_ip_protocol,
			download_payload,
			download_protocol,
			download_ip_protocol,
			num_channels
		};

		stat_channel m_stat[num_channels];
	};

}

#endif // TORRENT_STAT_HPP_INCLUDED

