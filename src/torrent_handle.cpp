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

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/optional.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/peer_id.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/invariant_check.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std
{
	using ::srand;
	using ::isalnum;
};
#endif

namespace libtorrent
{

#ifndef NDEBUG

	void torrent_handle::check_invariant() const
	{
		assert((m_ses == 0 && m_chk == 0) || (m_ses != 0));
	}

#endif

	void torrent_handle::set_max_uploads(int max_uploads)
	{
		INVARIANT_CHECK;

		assert(max_uploads >= 2 || max_uploads == -1);

		if (m_ses == 0) throw invalid_handle();

		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0)
			{
				t->get_policy().set_max_uploads(max_uploads);
				return;
			}
		}


		if (m_chk)
		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);

			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0)
			{
				d->torrent_ptr->get_policy().set_max_uploads(max_uploads);
				return;
			}
		}
		throw invalid_handle();
	}

	void torrent_handle::use_interface(const char* net_interface)
	{
		INVARIANT_CHECK;

		if (m_ses == 0) throw invalid_handle();

		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0)
			{
				t->use_interface(net_interface);
				return;
			}
		}


		if (m_chk)
		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);

			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0)
			{
				d->torrent_ptr->use_interface(net_interface);
				return;
			}
		}
		throw invalid_handle();
	}

	void torrent_handle::set_max_connections(int max_connections)
	{
		INVARIANT_CHECK;

		assert(max_connections > 2 || max_connections == -1);

		if (m_ses == 0) throw invalid_handle();

		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0)
			{
				t->get_policy().set_max_connections(max_connections);
				return;
			}
		}


		if (m_chk)
		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);

			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0)
			{
				d->torrent_ptr->get_policy().set_max_connections(max_connections);
				return;
			}
		}
		throw invalid_handle();
	}
	
	torrent_status torrent_handle::status() const
	{
		INVARIANT_CHECK;

		if (m_ses == 0) throw invalid_handle();

		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0) return t->status();
		}

		if (m_chk)
		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);

			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0)
			{
				torrent_status st;

				if (d == &m_chk->m_torrents.front())
					st.state = torrent_status::checking_files;
				else
					st.state = torrent_status::queued_for_checking;
				st.progress = d->progress;
				st.next_announce = boost::posix_time::time_duration();
				return st;
			}
		}

		throw invalid_handle();
	}

	const torrent_info& torrent_handle::get_torrent_info() const
	{
		INVARIANT_CHECK;

		if (m_ses == 0) throw invalid_handle();
	
		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0) return t->torrent_file();
		}

		if (m_chk)
		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);
			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0) return d->torrent_ptr->torrent_file();
		}

		throw invalid_handle();
	}

	void torrent_handle::set_tracker_login(std::string const& name, std::string const& password)
	{
		INVARIANT_CHECK;

		if (m_ses == 0) throw invalid_handle();
	
		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0)
			{
				t->set_tracker_login(name, password);
				return;
			}
		}

		if (m_chk)
		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);
			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0)
			{
				d->torrent_ptr->set_tracker_login(name, password);
				return;
			}
		}

		throw invalid_handle();
	}

	bool torrent_handle::is_valid() const
	{
		INVARIANT_CHECK;

		if (m_ses == 0) return false;

		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0) return true;
		}

		if (m_chk)
		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);
			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0) return true;
		}

		return false;
	}

	entry torrent_handle::write_resume_data() const
	{
		INVARIANT_CHECK;

		std::vector<int> piece_index;
		if (m_ses == 0)
			throw invalid_handle();

		boost::mutex::scoped_lock l(m_ses->m_mutex);
		torrent* t = m_ses->find_torrent(m_info_hash);
		if (t == 0)
			throw invalid_handle();
			
		t->filesystem().export_piece_map(piece_index);

		entry ret(entry::dictionary_t);

		ret["file-format"] = "libtorrent resume file";
		ret["file-version"] = 1;

		const sha1_hash& info_hash = t->torrent_file().info_hash();
		ret["info-hash"] = std::string((char*)info_hash.begin(), (char*)info_hash.end());

		ret["slots"] = entry(entry::list_t);
		entry::list_type& slots = ret.dict()["slots"].list();
		std::copy(piece_index.begin(), piece_index.end(), std::back_inserter(slots));

		const piece_picker& p = t->picker();

		const std::vector<piece_picker::downloading_piece>& q
			= p.get_download_queue();

		// blocks per piece
		int num_blocks_per_piece =
			static_cast<int>(t->torrent_file().piece_length()) / t->block_size();
		ret.dict()["blocks per piece"] = num_blocks_per_piece;

		// num unfinished pieces
		int num_unfinished = (int)q.size();
		ret["unfinished"] = entry::list_type();
		entry::list_type& up = ret["unfinished"].list();

		// info for each unfinished piece
		for (std::vector<piece_picker::downloading_piece>::const_iterator i
			= q.begin();
			i != q.end();
			++i)
		{
			if (i->finished_blocks.count() == 0) continue;

			entry::dictionary_type piece_struct;

			// the unfinished piece's index
			piece_struct["piece"] = i->index;

			std::string bitmask;
			const int num_bitmask_bytes
				= std::max(num_blocks_per_piece / 8, 1);

			for (int j = 0; j < num_bitmask_bytes; ++j)
			{
				unsigned char v = 0;
				for (int k = 0; k < 8; ++k)
					v |= i->finished_blocks[j*8+k]?(1 << k):0;
				bitmask.insert(bitmask.end(), v);
			}
			piece_struct["bitmask"] = bitmask;

			assert(t->filesystem().slot_for_piece(i->index) >= 0);
			unsigned long adler
				= t->filesystem().piece_crc(
					t->filesystem().slot_for_piece(i->index)
					, t->block_size()
					, i->finished_blocks);

			piece_struct["adler32"] = adler;

			// push the struct onto the unfinished-piece list
			up.push_back(piece_struct);
		}

		// write local peers

		ret["peers"] = entry::list_type();
		entry::list_type& peer_list = ret["peers"].list();

		for (torrent::const_peer_iterator i = t->begin();
			i != t->end();
			++i)
		{
			// we cannot save remote connection
			// since we don't know their listen port
			if (!i->second->is_local()) continue;

			address ip = i->second->get_socket()->sender();
			entry::dictionary_type peer;
			peer["ip"] = ip.as_string();
			peer["port"] = ip.port;
			peer_list.push_back(peer);
		}

		std::vector<size_type> file_sizes
			= get_filesizes(t->torrent_file(), t->save_path());

		ret["file sizes"] = entry::list_type();
		std::copy(
			file_sizes.begin()
			, file_sizes.end()
			, std::back_inserter(ret["file sizes"].list()));

		return ret;
	}


	boost::filesystem::path torrent_handle::save_path() const
	{
		INVARIANT_CHECK;

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

		if (m_chk)
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

	void torrent_handle::connect_peer(const address& adr) const
	{
		INVARIANT_CHECK;

		if (m_ses == 0) throw invalid_handle();
	
		boost::mutex::scoped_lock l(m_ses->m_mutex);
		torrent* t = m_ses->find_torrent(m_info_hash);
		if (t == 0) throw invalid_handle();

		peer_id id;
		std::fill(id.begin(), id.end(), 0);
		t->get_policy().peer_from_tracker(adr, id);
	}

	void torrent_handle::force_reannounce() const
	{
		INVARIANT_CHECK;

		if (m_ses == 0) throw invalid_handle();
	
		boost::mutex::scoped_lock l(m_ses->m_mutex);
		torrent* t = m_ses->find_torrent(m_info_hash);
		if (t == 0) throw invalid_handle();

		t->force_tracker_request();
	}

	void torrent_handle::set_ratio(float ratio)
	{
		INVARIANT_CHECK;

		assert(ratio >= 0.f);

		if (m_ses == 0) throw invalid_handle();

		if (ratio < 1.f && ratio > 0.f)
			ratio = 1.f;

		{
			boost::mutex::scoped_lock l(m_ses->m_mutex);
			torrent* t = m_ses->find_torrent(m_info_hash);
			if (t != 0)
			{
				t->set_ratio(ratio);
			}
		}

		if (m_chk)
		{
			boost::mutex::scoped_lock l(m_chk->m_mutex);
			detail::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0)
			{
				d->torrent_ptr->set_ratio(ratio);
			}
		}
	}

	void torrent_handle::get_peer_info(std::vector<peer_info>& v) const
	{
		INVARIANT_CHECK;

		v.clear();
		if (m_ses == 0) throw invalid_handle();

		boost::mutex::scoped_lock l(m_ses->m_mutex);
		
		const torrent* t = m_ses->find_torrent(m_info_hash);
		if (t == 0) return;

		for (torrent::const_peer_iterator i = t->begin();
			i != t->end();
			++i)
		{
			peer_connection* peer = i->second;

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

			p.total_download = statistics.total_payload_download();
			p.total_upload = statistics.total_payload_upload();

			if (peer->upload_bandwidth.given == std::numeric_limits<int>::max())
				p.upload_limit = -1;
			else
				p.upload_limit = peer->upload_bandwidth.given;

			if (peer->upload_bandwidth.wanted == std::numeric_limits<int>::max())
				p.upload_ceiling = -1;
			else
				p.upload_ceiling = peer->upload_bandwidth.wanted;

			p.load_balancing = peer->total_free_upload();

			p.download_queue_length = (int)peer->download_queue().size();
			p.upload_queue_length = (int)peer->upload_queue().size();

			boost::optional<piece_block_progress> ret = peer->downloading_piece();
			if (ret)
			{
				p.downloading_piece_index = ret->piece_index;
				p.downloading_block_index = ret->block_index;
				p.downloading_progress = ret->bytes_downloaded;
				p.downloading_total = ret->full_block_bytes;
			}
			else
			{
				p.downloading_piece_index = -1;
				p.downloading_block_index = -1;
				p.downloading_progress = 0;
				p.downloading_total = 0;
			}

			p.flags = 0;
			if (peer->is_interesting()) p.flags |= peer_info::interesting;
			if (peer->is_choked()) p.flags |= peer_info::choked;
			if (peer->is_peer_interested()) p.flags |= peer_info::remote_interested;
			if (peer->has_peer_choked()) p.flags |= peer_info::remote_choked;
			if (peer->support_extensions()) p.flags |= peer_info::supports_extensions;
			if (peer->is_local()) p.flags |= peer_info::local_connection;

			p.pieces = peer->get_bitfield();
		}
	}

	void torrent_handle::get_download_queue(std::vector<partial_piece_info>& queue) const
	{
		INVARIANT_CHECK;

		queue.clear();

		if (m_ses == 0) throw invalid_handle();
	
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
