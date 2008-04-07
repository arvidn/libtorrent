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

#include "libtorrent/size_type.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{

	class TORRENT_EXPORT stat
	{
	friend class invariant_access;
	public:
		enum { history = 10 };

		stat()
			: m_downloaded_payload(0)
			, m_uploaded_payload(0)
			, m_downloaded_protocol(0)
			, m_uploaded_protocol(0)
			, m_total_download_payload(0)
			, m_total_upload_payload(0)
			, m_total_download_protocol(0)
			, m_total_upload_protocol(0)
			, m_mean_download_rate(0)
			, m_mean_upload_rate(0)
			, m_mean_download_payload_rate(0)
			, m_mean_upload_payload_rate(0)
		{
			std::fill(m_download_rate_history, m_download_rate_history+history, 0.f);
			std::fill(m_upload_rate_history, m_upload_rate_history+history, 0.f);
			std::fill(m_download_payload_rate_history, m_download_payload_rate_history+history, 0.f);
			std::fill(m_upload_payload_rate_history, m_upload_payload_rate_history+history, 0.f);
		}

		void operator+=(const stat& s)
		{
			INVARIANT_CHECK;

			m_downloaded_payload += s.m_downloaded_payload;
			m_total_download_payload += s.m_downloaded_payload;
			m_downloaded_protocol += s.m_downloaded_protocol;
			m_total_download_protocol += s.m_downloaded_protocol;
			
			m_uploaded_payload += s.m_uploaded_payload;
			m_total_upload_payload += s.m_uploaded_payload;
			m_uploaded_protocol += s.m_uploaded_protocol;
			m_total_upload_protocol += s.m_uploaded_protocol;
		}

		void received_bytes(int bytes_payload, int bytes_protocol)
		{
			INVARIANT_CHECK;

			TORRENT_ASSERT(bytes_payload >= 0);
			TORRENT_ASSERT(bytes_protocol >= 0);

			m_downloaded_payload += bytes_payload;
			m_total_download_payload += bytes_payload;
			m_downloaded_protocol += bytes_protocol;
			m_total_download_protocol += bytes_protocol;
		}

		void sent_bytes(int bytes_payload, int bytes_protocol)
		{
			INVARIANT_CHECK;

			TORRENT_ASSERT(bytes_payload >= 0);
			TORRENT_ASSERT(bytes_protocol >= 0);

			m_uploaded_payload += bytes_payload;
			m_total_upload_payload += bytes_payload;
			m_uploaded_protocol += bytes_protocol;
			m_total_upload_protocol += bytes_protocol;
		}

		// should be called once every second
		void second_tick(float tick_interval);

		float upload_rate() const { return m_mean_upload_rate; }
		float download_rate() const { return m_mean_download_rate; }

		float upload_payload_rate() const { return m_mean_upload_payload_rate; }
		float download_payload_rate() const { return m_mean_download_payload_rate; }

		size_type total_payload_upload() const { return m_total_upload_payload; }
		size_type total_payload_download() const { return m_total_download_payload; }

		size_type total_protocol_upload() const { return m_total_upload_protocol; }
		size_type total_protocol_download() const { return m_total_download_protocol; }

		// this is used to offset the statistics when a
		// peer_connection is opened and have some previous
		// transfers from earlier connections.
		void add_stat(size_type downloaded, size_type uploaded)
		{
			TORRENT_ASSERT(downloaded >= 0);
			TORRENT_ASSERT(uploaded >= 0);
			m_total_download_payload += downloaded;
			m_total_upload_payload += uploaded;
		}

	private:

#ifndef NDEBUG
		void check_invariant() const
		{
			TORRENT_ASSERT(m_mean_upload_rate >= 0);
			TORRENT_ASSERT(m_mean_download_rate >= 0);
			TORRENT_ASSERT(m_mean_upload_payload_rate >= 0);
			TORRENT_ASSERT(m_mean_download_payload_rate >= 0);
			TORRENT_ASSERT(m_total_upload_payload >= 0);
			TORRENT_ASSERT(m_total_download_payload >= 0);
			TORRENT_ASSERT(m_total_upload_protocol >= 0);
			TORRENT_ASSERT(m_total_download_protocol >= 0);
		}
#endif

		// history of download/upload speeds a few seconds back
		float m_download_rate_history[history];
		float m_upload_rate_history[history];

		float m_download_payload_rate_history[history];
		float m_upload_payload_rate_history[history];

		// the accumulators we are adding the downloads/uploads
		// to this second. This only counts the actual payload
		// and ignores the bytes sent as protocol chatter.
		int m_downloaded_payload;
		int m_uploaded_payload;

		// the accumulators we are adding the downloads/uploads
		// to this second. This only counts the protocol
		// chatter and ignores the actual payload
		int m_downloaded_protocol;
		int m_uploaded_protocol;

		// total download/upload counters
		// only counting payload data
		size_type m_total_download_payload;
		size_type m_total_upload_payload;

		// total download/upload counters
		// only counting protocol chatter
		size_type m_total_download_protocol;
		size_type m_total_upload_protocol;

		// current mean download/upload rates
		float m_mean_download_rate;
		float m_mean_upload_rate;

		float m_mean_download_payload_rate;
		float m_mean_upload_payload_rate;
	};

}

#endif // TORRENT_STAT_HPP_INCLUDED
