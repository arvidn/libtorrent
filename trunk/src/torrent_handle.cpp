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
	
	torrent_status torrent_handle::status() const
	{
		if (m_ses == 0) throw invalid_handle();

		assert(m_chk != 0);
		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0) return t->status();
		}

		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);

			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0)
			{
				torrent_status st;
				st.total_download = 0;
				st.total_upload = 0;
				st.download_rate = 0.f;
				st.upload_rate = 0.f;
				if (d == &m_chk->m_torrents.front())
					st.state = torrent_status::checking_files;
				else
					st.state = torrent_status::queued_for_checking;
				st.progress = d->progress;
				st.next_announce = boost::posix_time::time_duration();
				st.pieces.clear();
				st.pieces.resize(d->torrent_ptr->torrent_file().num_pieces(), false);
				st.total_done = 0;
				return st;
			}
		}

		throw invalid_handle();
	}

	const torrent_info& torrent_handle::get_torrent_info() const
	{
		if (m_ses == 0) throw invalid_handle();
	
		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0) return t->torrent_file();
		}

		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);
			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0) return d->torrent_ptr->torrent_file();
		}

		throw invalid_handle();
	}

	bool torrent_handle::is_valid() const
	{
		if (m_ses == 0) return false;

		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0) return true;
		}

		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);
			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0) return true;
		}

		return false;
	}

	boost::filesystem::path torrent_handle::save_path() const
	{
		if (m_ses == 0) throw invalid_handle();

		// copy the path into this local variable before
		// unlocking and returning. Since we could get really
		// unlucky and having the path removed after we
		// have unlocked the data but before the return
		// value has been copied into the destination
		boost::filesystem::path ret;

		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0)
			{
				ret = t->save_path();
				return ret;
			}
		}

		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);
			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0)
			{
				ret = d->save_path;
				return ret;
			}
		}

		throw invalid_handle();
	}

	void torrent_handle::get_peer_info(std::vector<peer_info>& v) const
	{
		v.clear();
		if (m_ses == 0) throw invalid_handle();
		assert(m_chk != 0);

		boost::mutex::scoped_lock l(m_ses->m_mutex);
		
		const torrent* t = m_ses->find_torrent(m_info_hash);
		if (t == 0) return;

		for (std::vector<peer_connection*>::const_iterator i = t->begin();
			i != t->end();
			++i)
		{
			peer_connection* peer = *i;

			// peers that hasn't finished the handshake should
			// not be included in this list
			if (peer->associated_torrent() == 0) continue;

			v.push_back(peer_info());
			peer_info& p = v.back();

			const stat& statistics = peer->statistics();
			p.down_speed = statistics.download_rate();
			p.up_speed = statistics.upload_rate();
			p.id = peer->get_peer_id();
			p.ip = peer->get_socket()->sender();

			// TODO: add the prev_amount_downloaded and prev_amount_uploaded
			// from the peer list in the policy
			p.total_download = statistics.total_download();
			p.total_upload = statistics.total_upload();

			p.upload_limit = peer->send_quota();

			p.flags = 0;
			if (peer->is_interesting()) p.flags |= peer_info::interesting;
			if (peer->is_choked()) p.flags |= peer_info::choked;
			if (peer->is_peer_interested()) p.flags |= peer_info::remote_interested;
			if (peer->has_peer_choked()) p.flags |= peer_info::remote_choked;

			p.pieces = peer->get_bitfield();
		}
	}

	void torrent_handle::get_download_queue(std::vector<partial_piece_info>& queue) const
	{
		queue.clear();

		if (m_ses == 0) throw invalid_handle();
		assert(m_chk != 0);
	
		boost::mutex::scoped_lock l(m_ses->m_mutex);
		torrent* t = m_ses->find_torrent(m_info_hash);
		if (t == 0) return;

		const piece_picker& p = t->picker();

		const std::vector<piece_picker::downloading_piece>& q
			= p.get_download_queue();

		for (std::vector<piece_picker::downloading_piece>::const_iterator i
			= q.begin();
			i != q.end();
			++i)
		{
			partial_piece_info pi;
			pi.finished_blocks = i->finished_blocks;
			pi.requested_blocks = i->requested_blocks;
			for (int j = 0; j < partial_piece_info::max_blocks_per_piece; ++j)
			{
				pi.peer[j] = i->info[j].peer;
				pi.num_downloads[j] = i->info[j].num_downloads;
			}
			pi.piece_index = i->index;
			pi.blocks_in_piece = p.blocks_in_piece(i->index);
			queue.push_back(pi);
		}
	}

}
