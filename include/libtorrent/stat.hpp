/*

Copyright (c) 2003-2005, 2007-2012, 2014-2017, 2019, Arvid Norberg
Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2016, Alden Torres
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
#include <limits>
#include <cstdint>

#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent {

	class TORRENT_EXTRA_EXPORT stat_channel
	{
	public:

		stat_channel()
			: m_total_counter(0)
			, m_counter(0)
			, m_5_sec_average(0)
		{}

		void operator+=(stat_channel const& s)
		{
			TORRENT_ASSERT(m_counter < (std::numeric_limits<std::int32_t>::max)() - s.m_counter);
			m_counter += s.m_counter;
			TORRENT_ASSERT(m_total_counter < (std::numeric_limits<std::int64_t>::max)() - s.m_counter);
			m_total_counter += s.m_counter;
		}

		void add(int count)
		{
			TORRENT_ASSERT(count >= 0);

			TORRENT_ASSERT(m_counter < (std::numeric_limits<std::int32_t>::max)() - count);
			m_counter += count;
			TORRENT_ASSERT(m_total_counter < (std::numeric_limits<std::int64_t>::max)() - count);
			m_total_counter += count;
		}

		// should be called once every second
		void second_tick(int tick_interval_ms);
		std::int32_t rate() const { return m_5_sec_average; }
		std::int32_t low_pass_rate() const { return m_5_sec_average; }

		std::int64_t total() const { return m_total_counter; }

		void offset(std::int64_t c)
		{
			TORRENT_ASSERT(m_total_counter < (std::numeric_limits<std::int64_t>::max)() - c);
			m_total_counter += c;
		}

		std::int32_t counter() const { return m_counter; }

		void clear()
		{
			m_counter = 0;
			m_5_sec_average = 0;
			m_total_counter = 0;
		}

	private:

		// total counters
		std::int64_t m_total_counter;

		// the accumulator for this second.
		std::int32_t m_counter;

		// sliding average
		std::int32_t m_5_sec_average;
	};

	class TORRENT_EXTRA_EXPORT stat
	{
	public:
		void operator+=(const stat& s)
		{
			for (int i = 0; i < num_channels; ++i)
				m_stat[i] += s.m_stat[i];
		}

		void sent_syn(bool ipv6)
		{
			m_stat[upload_ip_protocol].add(ipv6 ? 60 : 40);
		}

		void received_synack(bool ipv6)
		{
			// we received SYN-ACK and also sent ACK back
			m_stat[download_ip_protocol].add(ipv6 ? 60 : 40);
			m_stat[upload_ip_protocol].add(ipv6 ? 60 : 40);
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
		}

		int upload_ip_overhead() const { return m_stat[upload_ip_protocol].counter(); }
		int download_ip_overhead() const { return m_stat[download_ip_protocol].counter(); }

		// should be called once every second
		void second_tick(int tick_interval_ms)
		{
			for (int i = 0; i < num_channels; ++i)
				m_stat[i].second_tick(tick_interval_ms);
		}

		int low_pass_upload_rate() const
		{
			return m_stat[upload_payload].low_pass_rate()
				+ m_stat[upload_protocol].low_pass_rate()
				+ m_stat[upload_ip_protocol].low_pass_rate();
		}

		int low_pass_download_rate() const
		{
			return m_stat[download_payload].low_pass_rate()
				+ m_stat[download_protocol].low_pass_rate()
				+ m_stat[download_ip_protocol].low_pass_rate();
		}

		int upload_rate() const
		{
			return m_stat[upload_payload].rate()
				+ m_stat[upload_protocol].rate()
				+ m_stat[upload_ip_protocol].rate();
		}

		int download_rate() const
		{
			return m_stat[download_payload].rate()
				+ m_stat[download_protocol].rate()
				+ m_stat[download_ip_protocol].rate();
		}

		std::int64_t total_upload() const
		{
			return m_stat[upload_payload].total()
				+ m_stat[upload_protocol].total()
				+ m_stat[upload_ip_protocol].total();
		}

		std::int64_t total_download() const
		{
			return m_stat[download_payload].total()
				+ m_stat[download_protocol].total()
				+ m_stat[download_ip_protocol].total();
		}

		int upload_payload_rate() const
		{ return m_stat[upload_payload].rate(); }
		int download_payload_rate() const
		{ return m_stat[download_payload].rate(); }

		std::int64_t total_payload_upload() const
		{ return m_stat[upload_payload].total(); }
		std::int64_t total_payload_download() const
		{ return m_stat[download_payload].total(); }

		std::int64_t total_protocol_upload() const
		{ return m_stat[upload_protocol].total(); }
		std::int64_t total_protocol_download() const
		{ return m_stat[download_protocol].total(); }

		std::int64_t total_transfer(int channel) const
		{ return m_stat[channel].total(); }
		int transfer_rate(int channel) const
		{ return m_stat[channel].rate(); }

		// this is used to offset the statistics when a
		// peer_connection is opened and have some previous
		// transfers from earlier connections.
		void add_stat(std::int64_t downloaded, std::int64_t uploaded)
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
			// TODO: 3 everything but payload counters and rates could probably be
			// removed from here
			upload_payload,
			upload_protocol,
			download_payload,
			download_protocol,
			upload_ip_protocol,
			download_ip_protocol,
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
