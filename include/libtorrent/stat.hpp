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

namespace libtorrent
{

	class stat
	{
	enum { history = 5 };
	public:

		stat()
			: m_downloaded_payload(0)
			, m_uploaded_payload(0)
			, m_downloaded_protocol(0)
			, m_uploaded_protocol(0)
			, m_total_download_payload(0)
			, m_total_upload_payload(0)
			, m_total_download_protocol(0)
			, m_total_upload_protocol(0)
			, m_peak_downloaded_per_second(0)
			, m_peak_uploaded_per_second(0)
			, m_mean_download_per_second(0)
			, m_mean_upload_per_second(0)
		{
			std::fill(m_download_per_second_history, m_download_per_second_history+history, 0);
			std::fill(m_upload_per_second_history, m_upload_per_second_history+history, 0);
		}

		void operator+=(const stat& s)
		{
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
			m_downloaded_payload += bytes_payload;
			m_total_download_payload += bytes_payload;

			m_downloaded_protocol += bytes_protocol;
			m_total_download_protocol += bytes_protocol;
		}
		void sent_bytes(int bytes_payload, int bytes_protocol)
		{
			m_uploaded_payload += bytes_payload;
			m_total_upload_payload += bytes_payload;

			m_uploaded_protocol += bytes_protocol;
			m_total_upload_protocol += bytes_protocol;
		}

		// should be called once every second
		void second_tick();

		// only counts the payload data!
		float upload_rate() const { return m_mean_upload_per_second; }
		float download_rate() const { return m_mean_download_per_second; }

		float down_peak() const { return m_peak_downloaded_per_second; }
		float up_peak() const { return m_peak_uploaded_per_second; }

		unsigned int total_payload_upload() const { return m_total_upload_payload; }
		unsigned int total_payload_download() const { return m_total_download_payload; }

		unsigned int total_protocol_upload() const { return m_total_upload_protocol; }
		unsigned int total_protocol_download() const { return m_total_download_protocol; }

	private:



		// history of download/upload speeds a few seconds back
		unsigned int m_download_per_second_history[history];
		unsigned int m_upload_per_second_history[history];

		// the accumulators we are adding the downloads/upploads
		// to this second. This only counts the actual payload
		// and ignores the bytes sent as protocol chatter.
		unsigned int m_downloaded_payload;
		unsigned int m_uploaded_payload;

		// the accumulators we are adding the downloads/upploads
		// to this second. This only counts the protocol
		// chatter and ignores the actual payload
		unsigned int m_downloaded_protocol;
		unsigned int m_uploaded_protocol;

		// total download/upload counters
		// only counting payload data
		unsigned int m_total_download_payload;
		unsigned int m_total_upload_payload;

		// total download/upload counters
		// only counting protocol chatter
		unsigned int m_total_download_protocol;
		unsigned int m_total_upload_protocol;

		// peak mean download/upload rates
		unsigned int m_peak_downloaded_per_second;
		unsigned int m_peak_uploaded_per_second;

		// current mean download/upload rates
		unsigned int m_mean_download_per_second;
		unsigned int m_mean_upload_per_second;
	};

}

#endif // TORRENT_STAT_HPP_INCLUDED
