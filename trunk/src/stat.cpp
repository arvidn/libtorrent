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

// TODO: Use two algorithms to estimate transfer rate.
// one (simple) for transfer rates that are >= 1 packet
// per second and one (low pass-filter) for rates < 1
// packet per second.

#include <numeric>

#include "libtorrent/stat.hpp"

using namespace libtorrent;

void libtorrent::stat::second_tick()
{
	std::copy(m_download_per_second_history,
		m_download_per_second_history+history-1,
		m_download_per_second_history+1);

	std::copy(m_upload_per_second_history,
		m_upload_per_second_history+history-1,
		m_upload_per_second_history+1);

	m_download_per_second_history[0] = m_downloaded_payload + m_downloaded_protocol;
	m_upload_per_second_history[0] = m_uploaded_payload + m_uploaded_protocol;
	m_downloaded_payload = 0;
	m_uploaded_payload = 0;
	m_downloaded_protocol = 0;
	m_uploaded_protocol = 0;

	m_mean_download_per_second
		= std::accumulate(m_download_per_second_history,
		m_download_per_second_history+history, 0) / history;

	m_mean_upload_per_second
		= std::accumulate(m_upload_per_second_history,
		m_upload_per_second_history+history, 0) / history;

	if (m_mean_download_per_second > m_peak_downloaded_per_second)
		m_peak_downloaded_per_second = m_mean_download_per_second;
	if (m_mean_upload_per_second > m_peak_uploaded_per_second)
		m_peak_uploaded_per_second = m_mean_upload_per_second;
}
