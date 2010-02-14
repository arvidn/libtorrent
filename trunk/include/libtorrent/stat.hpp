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

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
#include "libtorrent/debug.hpp" // for logger
#endif

namespace libtorrent
{
	class TORRENT_EXPORT stat_channel
	{
	friend class invariant_access;
	public:
		enum { history = 5 };

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING

#define PRINT_SIZEOF(x) l << "sizeof(" #x "): " << sizeof(x) << "\n";
#define PRINT_OFFSETOF(x, y) l << "  offsetof(" #x "," #y "): " << offsetof(x, y) << "\n";

		static void print_size(logger& l)
		{
			PRINT_SIZEOF(stat_channel)
			PRINT_OFFSETOF(stat_channel, m_rate_history)
			PRINT_OFFSETOF(stat_channel, m_window)
			PRINT_OFFSETOF(stat_channel, m_counter)
			PRINT_OFFSETOF(stat_channel, m_total_counter)
			PRINT_OFFSETOF(stat_channel, m_rate_sum)
		}
#undef PRINT_SIZEOF
#undef PRINT_OFFSETOF

#endif

		stat_channel()
			: m_window(3)
			, m_counter(0)
			, m_total_counter(0)
			, m_rate_sum(0)
		{
			std::memset(m_rate_history, 0, sizeof(m_rate_history));
		}

		void set_window(int w);

		void operator+=(stat_channel const& s)
		{
			TORRENT_ASSERT(m_counter >= 0);
			TORRENT_ASSERT(m_total_counter >= 0);
			TORRENT_ASSERT(s.m_counter >= 0);
			m_counter += s.m_counter;
			m_total_counter += s.m_counter;
			TORRENT_ASSERT(m_counter >= 0);
			TORRENT_ASSERT(m_total_counter >= 0);
		}

		void add(int count)
		{
			TORRENT_ASSERT(count >= 0);

			m_counter += count;
			TORRENT_ASSERT(m_counter >= 0);
			m_total_counter += count;
			TORRENT_ASSERT(m_total_counter >= 0);
		}

		// should be called once every second
		void second_tick(int tick_interval_ms);
		int rate() const { return int(m_rate_sum / m_window); }
		size_type rate_sum() const { return m_rate_sum; }
		size_type total() const { return m_total_counter; }

		void offset(size_type counter)
		{
			TORRENT_ASSERT(counter >= 0);
			TORRENT_ASSERT(m_total_counter >= 0);
			m_total_counter += counter;
			TORRENT_ASSERT(m_total_counter >= 0);
		}

		int counter() const { return m_counter; }

		void clear()
		{
			std::memset(m_rate_history, 0, sizeof(m_rate_history));
			m_counter = 0;
			m_total_counter = 0;
			m_rate_sum = 0;
		}

		int window() const { return m_window; }

	private:

#ifdef TORRENT_DEBUG
		void check_invariant() const
		{
			size_type sum = 0;
			for (int i = 0; i < m_window; ++i) sum += m_rate_history[i];
			TORRENT_ASSERT(m_rate_sum == sum);
			TORRENT_ASSERT(m_total_counter >= 0);
		}
#endif

		// history of rates a few seconds back
		int m_rate_history[history];

		// averaging window (seconds). 'history' is the max size
		int m_window;

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

		void sent_syn(bool ipv6)
		{
#ifndef TORRENT_DISABLE_FULL_STATS
			m_stat[upload_ip_protocol].add(ipv6 ? 60 : 40);
#endif
		}

		void received_synack(bool ipv6)
		{
#ifndef TORRENT_DISABLE_FULL_STATS
			// we received SYN-ACK and also sent ACK back
			m_stat[download_ip_protocol].add(ipv6 ? 60 : 40);
			m_stat[upload_ip_protocol].add(ipv6 ? 60 : 40);
#endif
		}

		void received_dht_bytes(int bytes)
		{
#ifndef TORRENT_DISABLE_FULL_STATS
			TORRENT_ASSERT(bytes >= 0);
			m_stat[download_dht_protocol].add(bytes);
#endif
		}

		void sent_dht_bytes(int bytes)
		{
#ifndef TORRENT_DISABLE_FULL_STATS
			TORRENT_ASSERT(bytes >= 0);
			m_stat[upload_dht_protocol].add(bytes);
#endif
		}

		void received_tracker_bytes(int bytes)
		{
#ifndef TORRENT_DISABLE_FULL_STATS
			TORRENT_ASSERT(bytes >= 0);
			m_stat[download_tracker_protocol].add(bytes);
#endif
		}

		void sent_tracker_bytes(int bytes)
		{
#ifndef TORRENT_DISABLE_FULL_STATS
			TORRENT_ASSERT(bytes >= 0);
			m_stat[upload_tracker_protocol].add(bytes);
#endif
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

		// and IP packet was received or sent
		// account for the overhead caused by it
		void trancieve_ip_packet(int bytes_transferred, bool ipv6)
		{
#ifndef TORRENT_DISABLE_FULL_STATS
			// one TCP/IP packet header for the packet
			// sent or received, and one for the ACK
			// The IPv4 header is 20 bytes
			// and IPv6 header is 40 bytes
			const int header = (ipv6 ? 40 : 20) + 20;
			const int mtu = 1500;
			const int packet_size = mtu - header;
			const int overhead = (std::max)(1, (bytes_transferred + packet_size - 1) / packet_size) * header;
			m_stat[download_ip_protocol].add(overhead);
			m_stat[upload_ip_protocol].add(overhead);
#endif
		}

#ifndef TORRENT_DISABLE_FULL_STATS
		int upload_ip_overhead() const { return m_stat[upload_ip_protocol].counter(); }
		int download_ip_overhead() const { return m_stat[download_ip_protocol].counter(); }
		int upload_dht() const { return m_stat[upload_dht_protocol].counter(); }
		int download_dht() const { return m_stat[download_dht_protocol].counter(); }
		int download_tracker() const { return m_stat[download_tracker_protocol].counter(); }
		int upload_tracker() const { return m_stat[upload_tracker_protocol].counter(); }
#else
		int upload_ip_overhead() const { return 0; }
		int download_ip_overhead() const { return 0; }
		int upload_dht() const { return 0; }
		int download_dht() const { return 0; }
		int download_tracker() const { return 0; }
		int upload_tracker() const { return 0; }
#endif

		void set_window(int w)
		{
			for (int i = 0; i < num_channels; ++i)
				m_stat[i].set_window(w);
		}

		// should be called once every second
		void second_tick(int tick_interval_ms)
		{
			for (int i = 0; i < num_channels; ++i)
				m_stat[i].second_tick(tick_interval_ms);
		}

		int upload_rate() const
		{
			return int((m_stat[upload_payload].rate_sum()
				+ m_stat[upload_protocol].rate_sum()
#ifndef TORRENT_DISABLE_FULL_STATS
				+ m_stat[upload_ip_protocol].rate_sum()
				+ m_stat[upload_dht_protocol].rate_sum()
#endif
				)
				/ m_stat[0].window());
		}

		int download_rate() const
		{
			return int((m_stat[download_payload].rate_sum()
				+ m_stat[download_protocol].rate_sum()
#ifndef TORRENT_DISABLE_FULL_STATS
				+ m_stat[download_ip_protocol].rate_sum()
				+ m_stat[download_dht_protocol].rate_sum()
#endif
				)
				/ m_stat[0].window());
		}

		size_type total_upload() const
		{
			return m_stat[upload_payload].total()
				+ m_stat[upload_protocol].total()
#ifndef TORRENT_DISABLE_FULL_STATS
				+ m_stat[upload_ip_protocol].total()
				+ m_stat[upload_dht_protocol].total()
				+ m_stat[upload_tracker_protocol].total()
#endif
				;
		}

		size_type total_download() const
		{
			return m_stat[download_payload].total()
				+ m_stat[download_protocol].total()
#ifndef TORRENT_DISABLE_FULL_STATS
				+ m_stat[download_ip_protocol].total()
				+ m_stat[download_dht_protocol].total()
				+ m_stat[download_tracker_protocol].total()
#endif
				;
		}

		int upload_payload_rate() const
		{ return m_stat[upload_payload].rate(); }
		int download_payload_rate() const
		{ return m_stat[download_payload].rate(); }

		size_type total_payload_upload() const
		{ return m_stat[upload_payload].total(); }
		size_type total_payload_download() const
		{ return m_stat[download_payload].total(); }

		size_type total_protocol_upload() const
		{ return m_stat[upload_protocol].total(); }
		size_type total_protocol_download() const
		{ return m_stat[download_protocol].total(); }

		size_type total_transfer(int channel) const
		{ return m_stat[channel].total(); }
		int transfer_rate(int channel) const
		{ return m_stat[channel].rate(); }

		// this is used to offset the statistics when a
		// peer_connection is opened and have some previous
		// transfers from earlier connections.
		void add_stat(size_type downloaded, size_type uploaded)
		{
			m_stat[download_payload].offset(downloaded);
			m_stat[upload_payload].offset(uploaded);
		}

		int last_payload_downloaded() const
		{ return m_stat[download_payload].counter(); }
		int last_payload_uploaded() const
		{ return m_stat[upload_payload].counter(); }
		int last_protocol_downloaded() const
		{ return m_stat[download_protocol].counter(); }
		int last_protocol_uploaded() const
		{ return m_stat[upload_protocol].counter(); }

		// these are the channels we keep stats for
		enum
		{
			upload_payload,
			upload_protocol,
			download_payload,
			download_protocol,
#ifndef TORRENT_DISABLE_FULL_STATS
			upload_ip_protocol,
			upload_dht_protocol,
			upload_tracker_protocol,
			download_ip_protocol,
			download_dht_protocol,
			download_tracker_protocol,
#endif
			num_channels
		};

		void clear()
		{
			for (int i = 0; i < num_channels; ++i)
				m_stat[i].clear();
		}

		stat_channel const& operator[](int i) const
		{
			TORRENT_ASSERT(i >= 0 && i < num_channels);
			return m_stat[i];
		}

	private:

		stat_channel m_stat[num_channels];
	};

}

#endif // TORRENT_STAT_HPP_INCLUDED

