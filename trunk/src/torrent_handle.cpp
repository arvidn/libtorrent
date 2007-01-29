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
#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/peer_id.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/invariant_check.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std
{
	using ::srand;
	using ::isalnum;
};
#endif

using boost::bind;
using boost::mutex;
using libtorrent::aux::session_impl;

namespace libtorrent
{
	namespace
	{
		void throw_invalid_handle()
		{
			throw invalid_handle();
		}
			  
		template<class Ret, class F>
		Ret call_member(
			session_impl* ses
			, aux::checker_impl* chk
			, sha1_hash const& hash
			, F f)
		{
			if (ses == 0) throw_invalid_handle();

			if (chk)
			{
				mutex::scoped_lock l(chk->m_mutex);
				aux::piece_checker_data* d = chk->find_torrent(hash);
				if (d != 0) return f(*d->torrent_ptr);
			}

			{
				session_impl::mutex_t::scoped_lock l(ses->m_mutex);
				boost::shared_ptr<torrent> t = ses->find_torrent(hash).lock();
				if (t) return f(*t);
			}

			// throwing directly instead of calling
			// the throw_invalid_handle() function
			// avoids a warning in gcc
			throw invalid_handle();
		}
	}

#ifndef NDEBUG

	void torrent_handle::check_invariant() const
	{
		assert((m_ses == 0 && m_chk == 0) || (m_ses != 0));
	}

#endif

	void torrent_handle::set_max_uploads(int max_uploads) const
	{
		INVARIANT_CHECK;

		assert(max_uploads >= 2 || max_uploads == -1);

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::set_max_uploads, _1, max_uploads));
	}

	void torrent_handle::use_interface(const char* net_interface) const
	{
		INVARIANT_CHECK;

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::use_interface, _1, net_interface));
	}

	void torrent_handle::set_max_connections(int max_connections) const
	{
		INVARIANT_CHECK;

		assert(max_connections >= 2 || max_connections == -1);

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::set_max_connections, _1, max_connections));
	}

	void torrent_handle::set_peer_upload_limit(tcp::endpoint ip, int limit) const
	{
		INVARIANT_CHECK;
		assert(limit >= -1);

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::set_peer_upload_limit, _1, ip, limit));
	}

	void torrent_handle::set_peer_download_limit(tcp::endpoint ip, int limit) const
	{
		INVARIANT_CHECK;
		assert(limit >= -1);

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::set_peer_download_limit, _1, ip, limit));
	}

	void torrent_handle::set_upload_limit(int limit) const
	{
		INVARIANT_CHECK;

		assert(limit >= -1);

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::set_upload_limit, _1, limit));
	}

	void torrent_handle::set_download_limit(int limit) const
	{
		INVARIANT_CHECK;

		assert(limit >= -1);

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::set_download_limit, _1, limit));
	}

	bool torrent_handle::move_storage(
		boost::filesystem::path const& save_path) const
	{
		INVARIANT_CHECK;

		return call_member<bool>(m_ses, m_chk, m_info_hash
			, bind(&torrent::move_storage, _1, save_path));
	}

	bool torrent_handle::has_metadata() const
	{
		INVARIANT_CHECK;

		return call_member<bool>(m_ses, m_chk, m_info_hash
			, bind(&torrent::valid_metadata, _1));
	}

	bool torrent_handle::is_seed() const
	{
		INVARIANT_CHECK;

		return call_member<bool>(m_ses, m_chk, m_info_hash
			, bind(&torrent::is_seed, _1));
	}

	bool torrent_handle::is_paused() const
	{
		INVARIANT_CHECK;

		return call_member<bool>(m_ses, m_chk, m_info_hash
			, bind(&torrent::is_paused, _1));
	}

	void torrent_handle::pause() const
	{
		INVARIANT_CHECK;

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::pause, _1));
	}

	void torrent_handle::resume() const
	{
		INVARIANT_CHECK;

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::resume, _1));
	}

	void torrent_handle::set_tracker_login(std::string const& name
		, std::string const& password) const
	{
		INVARIANT_CHECK;

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::set_tracker_login, _1, name, password));
	}

	void torrent_handle::file_progress(std::vector<float>& progress)
	{
		INVARIANT_CHECK;
		
		if (m_ses == 0) throw_invalid_handle();

		if (m_chk)
		{
			mutex::scoped_lock l(m_chk->m_mutex);

			aux::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0)
			{
				if (!d->processing)
				{
					torrent_info const& info = d->torrent_ptr->torrent_file();
					progress.clear();
					progress.resize(info.num_files(), 0.f);
					return;
				}
				d->torrent_ptr->file_progress(progress);
				return;
			}
		}

		{
			session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
			boost::shared_ptr<torrent> t = m_ses->find_torrent(m_info_hash).lock();
			if (t) return t->file_progress(progress);
		}

		throw_invalid_handle();
	}

	torrent_status torrent_handle::status() const
	{
		INVARIANT_CHECK;

		if (m_ses == 0) throw_invalid_handle();

		if (m_chk)
		{
			mutex::scoped_lock l(m_chk->m_mutex);

			aux::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0)
			{
				torrent_status st;

				if (d->processing)
				{
					if (d->torrent_ptr->is_allocating())
						st.state = torrent_status::allocating;
					else
						st.state = torrent_status::checking_files;
				}
				else
					st.state = torrent_status::queued_for_checking;
				st.progress = d->progress;
				st.paused = d->torrent_ptr->is_paused();
				return st;
			}
		}

		{
			session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
			boost::shared_ptr<torrent> t = m_ses->find_torrent(m_info_hash).lock();
			if (t) return t->status();
		}

		throw_invalid_handle();
		return torrent_status();
	}

	void torrent_handle::set_sequenced_download_threshold(int threshold) const
	{
		INVARIANT_CHECK;
		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::set_sequenced_download_threshold, _1, threshold));
	}

	void torrent_handle::filter_piece(int index, bool filter) const
	{
		INVARIANT_CHECK;
		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::filter_piece, _1, index, filter));
	}

	void torrent_handle::filter_pieces(std::vector<bool> const& pieces) const
	{
		INVARIANT_CHECK;
		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::filter_pieces, _1, pieces));
	}

	bool torrent_handle::is_piece_filtered(int index) const
	{
		INVARIANT_CHECK;
		return call_member<bool>(m_ses, m_chk, m_info_hash
			, bind(&torrent::is_piece_filtered, _1, index));
	}

	std::string torrent_handle::name() const
	{
		INVARIANT_CHECK;
		return call_member<std::string>(m_ses, m_chk, m_info_hash
			, bind(&torrent::name, _1));
	}

	std::vector<bool> torrent_handle::filtered_pieces() const
	{
		INVARIANT_CHECK;
		std::vector<bool> ret;
		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::filtered_pieces, _1, boost::ref(ret)));
		return ret;
	}

	void torrent_handle::filter_files(std::vector<bool> const& files) const
	{
		INVARIANT_CHECK;
		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::filter_files, _1, files));
	}

	std::vector<announce_entry> const& torrent_handle::trackers() const
	{
		INVARIANT_CHECK;

		return call_member<std::vector<announce_entry> const&>(m_ses
			, m_chk, m_info_hash, bind(&torrent::trackers, _1));
	}

	void torrent_handle::add_url_seed(std::string const& url)
	{
		INVARIANT_CHECK;

		return call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::add_url_seed, _1, url));
	}

	void torrent_handle::replace_trackers(
		std::vector<announce_entry> const& urls) const
	{
		INVARIANT_CHECK;

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::replace_trackers, _1, urls));
	}

	torrent_info const& torrent_handle::get_torrent_info() const
	{
		INVARIANT_CHECK;

		if (!has_metadata()) throw_invalid_handle();
		return call_member<torrent_info const&>(m_ses, m_chk, m_info_hash
			, bind(&torrent::torrent_file, _1));
	}

	bool torrent_handle::is_valid() const
	{
		INVARIANT_CHECK;

		if (m_ses == 0) return false;

		if (m_chk)
		{
			mutex::scoped_lock l(m_chk->m_mutex);
			aux::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d != 0) return true;
		}

		{
			session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
			boost::weak_ptr<torrent> t = m_ses->find_torrent(m_info_hash);
			if (!t.expired()) return true;
		}

		return false;
	}

	entry torrent_handle::write_resume_data() const
	{
		INVARIANT_CHECK;

		std::vector<int> piece_index;
		if (m_ses == 0) return entry();

		session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
		boost::shared_ptr<torrent> t = m_ses->find_torrent(m_info_hash).lock();
		if (!t) return entry();

		if (!t->valid_metadata()) return entry();

		t->filesystem().export_piece_map(piece_index);

		entry ret(entry::dictionary_t);

		ret["file-format"] = "libtorrent resume file";
		ret["file-version"] = 1;

		const sha1_hash& info_hash = t->torrent_file().info_hash();
		ret["info-hash"] = std::string((char*)info_hash.begin(), (char*)info_hash.end());

		ret["slots"] = entry(entry::list_t);
		entry::list_type& slots = ret["slots"].list();
		std::copy(piece_index.begin(), piece_index.end(), std::back_inserter(slots));

		// blocks per piece
		int num_blocks_per_piece =
			static_cast<int>(t->torrent_file().piece_length()) / t->block_size();
		ret["blocks per piece"] = num_blocks_per_piece;

		// if this torrent is a seed, we won't have a piece picker
		// and there will be no half-finished pieces.
		if (!t->is_seed())
		{
			const piece_picker& p = t->picker();

			const std::vector<piece_picker::downloading_piece>& q
				= p.get_download_queue();

			// unfinished pieces
			ret["unfinished"] = entry::list_type();
			entry::list_type& up = ret["unfinished"].list();

			// info for each unfinished piece
			for (std::vector<piece_picker::downloading_piece>::const_iterator i
				= q.begin(); i != q.end(); ++i)
			{
				if (i->finished_blocks.count() == 0) continue;

				entry piece_struct(entry::dictionary_t);

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
		}
		// write local peers

		ret["peers"] = entry::list_type();
		entry::list_type& peer_list = ret["peers"].list();
		
		policy& pol = t->get_policy();

		for (policy::iterator i = pol.begin_peer()
			, end(pol.end_peer()); i != end; ++i)
		{
			// we cannot save remote connection
			// since we don't know their listen port
			// unless they gave us their listen port
			// through the extension handshake
			// so, if the peer is not connectable (i.e. we
			// don't know its listen port) or if it has
			// been banned, don't save it.
			if (i->type == policy::peer::not_connectable
				|| i->banned) continue;

			tcp::endpoint ip = i->ip;
			entry peer(entry::dictionary_t);
			peer["ip"] = ip.address().to_string();
			peer["port"] = ip.port();
			peer_list.push_back(peer);
		}

		std::vector<std::pair<size_type, std::time_t> > file_sizes
			= get_filesizes(t->torrent_file(), t->save_path());

		ret["file sizes"] = entry::list_type();
		entry::list_type& fl = ret["file sizes"].list();
		for (std::vector<std::pair<size_type, std::time_t> >::iterator i
			= file_sizes.begin(), end(file_sizes.end()); i != end; ++i)
		{
			entry::list_type p;
			p.push_back(entry(i->first));
			p.push_back(entry(i->second));
			fl.push_back(entry(p));
		}

		return ret;
	}


	boost::filesystem::path torrent_handle::save_path() const
	{
		INVARIANT_CHECK;

		return call_member<boost::filesystem::path>(m_ses, m_chk, m_info_hash
			, bind(&torrent::save_path, _1));
	}

	void torrent_handle::connect_peer(tcp::endpoint const& adr) const
	{
		INVARIANT_CHECK;

		if (m_ses == 0) throw_invalid_handle();
	
		session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
		boost::shared_ptr<torrent> t = m_ses->find_torrent(m_info_hash).lock();
		
		if (!t)
		{
			// the torrent is being checked. Add the peer to its
			// peer list. The entries in there will be connected
			// once the checking is complete.
			mutex::scoped_lock l2(m_chk->m_mutex);

			aux::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d == 0) throw_invalid_handle();
			d->peers.push_back(adr);
			return;
		}

		peer_id id;
		std::fill(id.begin(), id.end(), 0);
		t->get_policy().peer_from_tracker(adr, id);
	}

	void torrent_handle::force_reannounce(
		boost::posix_time::time_duration duration) const
	{
		INVARIANT_CHECK;

		if (m_ses == 0) throw_invalid_handle();
	
		session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
		boost::shared_ptr<torrent> t = m_ses->find_torrent(m_info_hash).lock();
		if (!t) throw_invalid_handle();

		using boost::posix_time::second_clock;
		t->force_tracker_request(second_clock::universal_time()
			+ duration);
	}

	void torrent_handle::force_reannounce() const
	{
		INVARIANT_CHECK;

		if (m_ses == 0) throw_invalid_handle();
	
		session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
		boost::shared_ptr<torrent> t = m_ses->find_torrent(m_info_hash).lock();
		if (!t) throw_invalid_handle();

		t->force_tracker_request();
	}

	void torrent_handle::set_ratio(float ratio) const
	{
		INVARIANT_CHECK;

		assert(ratio >= 0.f);

		if (ratio < 1.f && ratio > 0.f)
			ratio = 1.f;

		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::set_ratio, _1, ratio));
	}

	void torrent_handle::resolve_countries(bool r)
	{
		INVARIANT_CHECK;
		call_member<void>(m_ses, m_chk, m_info_hash
			, bind(&torrent::resolve_countries, _1, r));
	}

	bool torrent_handle::resolve_countries() const
	{
		INVARIANT_CHECK;
		return call_member<bool>(m_ses, m_chk, m_info_hash
			, bind(&torrent::resolving_countries, _1));
	}

	void torrent_handle::get_peer_info(std::vector<peer_info>& v) const
	{
		INVARIANT_CHECK;

		v.clear();
		if (m_ses == 0) throw_invalid_handle();

		session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
		
		boost::shared_ptr<const torrent> t = m_ses->find_torrent(m_info_hash).lock();
		if (!t) return;

		for (torrent::const_peer_iterator i = t->begin();
			i != t->end(); ++i)
		{
			peer_connection* peer = i->second;

			// peers that haven't finished the handshake should
			// not be included in this list
			if (peer->associated_torrent().expired()) continue;

			v.push_back(peer_info());
			peer_info& p = v.back();
			
			peer->get_peer_info(p);
			if (t->resolving_countries())
				t->resolve_peer_country(intrusive_ptr<peer_connection>(peer));
		}
	}

	void torrent_handle::get_download_queue(std::vector<partial_piece_info>& queue) const
	{
		INVARIANT_CHECK;

		if (m_ses == 0) throw_invalid_handle();
	
		session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
		boost::shared_ptr<torrent> t = m_ses->find_torrent(m_info_hash).lock();

		queue.clear();
		if (!t) return;
		if (!t->valid_metadata()) return;
		// if we're a seed, the piece picker has been removed
		if (t->is_seed()) return;

		const piece_picker& p = t->picker();

		const std::vector<piece_picker::downloading_piece>& q
			= p.get_download_queue();

		for (std::vector<piece_picker::downloading_piece>::const_iterator i
			= q.begin(); i != q.end(); ++i)
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

