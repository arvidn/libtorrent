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

#include <ctime>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <algorithm>
#include <set>
#include <cctype>
#include <algorithm>

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>

#include "libtorrent/peer_id.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/url_handler.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std
{
	using ::srand;
	using ::isprint;
};
#endif


namespace libtorrent
{
	
	std::pair<torrent_handle::state_t, float> torrent_handle::status() const
	{
		if (m_ses == 0) return std::make_pair(invalid_handle, 0.f);

		boost::mutex::scoped_lock l(m_ses->m_mutex);

		torrent* t = m_ses->find_active_torrent(m_info_hash);
		if (t != 0) return t->status();

		detail::piece_checker_data* d = m_ses->find_checking_torrent(m_info_hash);

		if (d != 0)
		{
			boost::mutex::scoped_lock l(d->mutex);
			return std::make_pair(checking_files, d->progress);
		}

		return std::make_pair(invalid_handle, 0.f);
	}

	void torrent_handle::get_peer_info(std::vector<peer_info>& v)
	{
		v.clear();
		if (m_ses == 0) return;
		boost::mutex::scoped_lock l(m_ses->m_mutex);
		
		std::map<sha1_hash, boost::shared_ptr<torrent> >::iterator i = m_ses->m_torrents.find(m_info_hash);
		if (i == m_ses->m_torrents.end()) return;

		const torrent* t = boost::get_pointer(i->second);

		for (std::vector<peer_connection*>::const_iterator i = t->begin();
			i != t->end();
			++i)
		{
			peer_connection* peer = *i;
			v.push_back(peer_info());
			peer_info& p = v.back();

			const stat& statistics = peer->statistics();
			p.down_speed = statistics.download_rate();
			p.up_speed = statistics.upload_rate();
			p.id = peer->get_peer_id();
			p.ip = peer->get_socket()->sender();

			p.total_download = statistics.total_download();
			p.total_upload = statistics.total_upload();

			p.flags = 0;
			if (peer->is_interesting()) p.flags |= peer_info::interesting;
			if (peer->has_choked()) p.flags |= peer_info::choked;
			if (peer->is_peer_interested()) p.flags |= peer_info::remote_interested;
			if (peer->has_peer_choked()) p.flags |= peer_info::remote_choked;

			p.pieces = peer->get_bitfield();
		}
	}

	void torrent_handle::abort()
	{
		if (m_ses == 0) return;
		boost::mutex::scoped_lock l(m_ses->m_mutex);

		torrent* t = m_ses->find_active_torrent(m_info_hash);
		if (t != 0)
		{
			t->abort();
			m_ses = 0;
			return;
		}

		detail::piece_checker_data* d = m_ses->find_checking_torrent(m_info_hash);

		if (d != 0)
		{
			boost::mutex::scoped_lock l(d->mutex);
			d->abort = true;
			// remove the checker. It will abort itself and
			// close the thread now.
			m_ses->m_checkers.erase(m_ses->m_checkers.find(m_info_hash));
			m_ses = 0;
			return;
		}

		m_ses = 0;
	}

}
