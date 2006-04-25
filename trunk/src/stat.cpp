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
#include "libtorrent/invariant_check.hpp"
#include <algorithm>

#if defined _MSC_VER && _MSC_VER <= 1200
#define for if (false) {} else for
#endif

using namespace libtorrent;

void libtorrent::stat::second_tick(float tick_interval)
{
	INVARIANT_CHECK;

	for (int i = history - 2; i >= 0; --i)
	{
		m_download_rate_history[i + 1] = m_download_rate_history[i];
		m_upload_rate_history[i + 1] = m_upload_rate_history[i];
		m_download_payload_rate_history[i + 1] = m_download_payload_rate_history[i];
		m_upload_payload_rate_history[i + 1] = m_upload_payload_rate_history[i];
	}

	m_download_rate_history[0] = (m_downloaded_payload + m_downloaded_protocol)
		/ tick_interval;
	m_upload_rate_history[0] = (m_uploaded_payload + m_uploaded_protocol)
		/ tick_interval;
	m_download_payload_rate_history[0] = m_downloaded_payload / tick_interval;
	m_upload_payload_rate_history[0] = m_uploaded_payload / tick_interval;

	m_downloaded_payload = 0;
	m_uploaded_payload = 0;
	m_downloaded_protocol = 0;
	m_uploaded_protocol = 0;

	m_mean_download_rate = 0;
	m_mean_upload_rate = 0;
	m_mean_download_payload_rate = 0;
	m_mean_upload_payload_rate = 0;

	for (int i = 0; i < history; ++i)
	{
		m_mean_download_rate += m_download_rate_history[i];
		m_mean_upload_rate += m_upload_rate_history[i];
		m_mean_download_payload_rate += m_download_payload_rate_history[i];
		m_mean_upload_payload_rate += m_upload_payload_rate_history[i];
	}

	m_mean_download_rate /= history;
	m_mean_upload_rate /= history;
	m_mean_download_payload_rate /= history;
	m_mean_upload_payload_rate /= history;
}
