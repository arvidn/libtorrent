/*

Copyright (c) 2007, 2015, 2017, 2019, Arvid Norberg
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

#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/settings_pack.hpp"

#include <functional>

namespace libtorrent { namespace aux {

	session_settings::session_settings() = default;

	session_settings::session_settings(settings_pack const& p)
	{
		apply_pack_impl(&p, m_store);
	}

	void session_settings::bulk_set(std::function<void(session_settings_single_thread&)> f)
	{
		std::unique_lock<std::mutex> l(m_mutex);
		f(m_store);
	}

	void session_settings::bulk_get(std::function<void(session_settings_single_thread const&)> f) const
	{
		std::unique_lock<std::mutex> l(m_mutex);
		f(m_store);
	}

	session_settings_single_thread::session_settings_single_thread()
	{
		initialize_default_settings(*this);
	}

} }

