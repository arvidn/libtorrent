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

#ifndef TORRENT_TORRENT_HANDLE_HPP_INCLUDED
#define TORRENT_TORRENT_HANDLE_HPP_INCLUDED

#include <vector>

#include "libtorrent/peer_id.hpp"
#include "libtorrent/peer_info.hpp"


namespace libtorrent
{
	namespace detail
	{
		struct session_impl;
	}

	struct torrent_handle
	{
		friend class session;

		torrent_handle(): m_ses(0) {}

		void get_peer_info(std::vector<peer_info>& v);
		void abort();
		enum state_t
		{
			invalid_handle,
			checking_files,
			connecting_to_tracker,
			downloading,
			seeding
		};
		std::pair<state_t, float> status() const;

	private:

		torrent_handle(detail::session_impl* s, const sha1_hash& h)
			: m_ses(s)
			, m_info_hash(h)
		{}

		detail::session_impl* m_ses;
		sha1_hash m_info_hash; // should be replaced with a torrent*?

	};


}

#endif // TORRENT_TORRENT_HANDLE_HPP_INCLUDED
