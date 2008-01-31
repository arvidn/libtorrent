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

#include "libtorrent/pch.hpp"

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

#ifdef BOOST_NO_EXCEPTIONS

#define TORRENT_FORWARD(call) \
	if (m_ses == 0) return; \
	TORRENT_ASSERT(m_chk); \
	session_impl::mutex_t::scoped_lock l1(m_ses->m_mutex); \
	mutex::scoped_lock l2(m_chk->m_mutex); \
	torrent* t = find_torrent(m_ses, m_chk, m_info_hash); \
	if (t == 0) return; \
	t->call
	
#define TORRENT_FORWARD_RETURN(call, def) \
	if (m_ses == 0) return def; \
	TORRENT_ASSERT(m_chk); \
	session_impl::mutex_t::scoped_lock l1(m_ses->m_mutex); \
	mutex::scoped_lock l2(m_chk->m_mutex); \
	torrent* t = find_torrent(m_ses, m_chk, m_info_hash); \
	if (t == 0) return def; \
	return t->call

#define TORRENT_FORWARD_RETURN2(call, def) \
	if (m_ses == 0) return def; \
	TORRENT_ASSERT(m_chk); \
	session_impl::mutex_t::scoped_lock l1(m_ses->m_mutex); \
	mutex::scoped_lock l2(m_chk->m_mutex); \
	torrent* t = find_torrent(m_ses, m_chk, m_info_hash); \
	if (t == 0) return def; \
	t->call

#else

#define TORRENT_FORWARD(call) \
	if (m_ses == 0) throw_invalid_handle(); \
	TORRENT_ASSERT(m_chk); \
	session_impl::mutex_t::scoped_lock l1(m_ses->m_mutex); \
	mutex::scoped_lock l2(m_chk->m_mutex); \
	torrent* t = find_torrent(m_ses, m_chk, m_info_hash); \
	if (t == 0) throw_invalid_handle(); \
	t->call
	
#define TORRENT_FORWARD_RETURN(call, def) \
	if (m_ses == 0) throw_invalid_handle(); \
	TORRENT_ASSERT(m_chk); \
	session_impl::mutex_t::scoped_lock l1(m_ses->m_mutex); \
	mutex::scoped_lock l2(m_chk->m_mutex); \
	torrent* t = find_torrent(m_ses, m_chk, m_info_hash); \
	if (t == 0) return def; \
	return t->call

#define TORRENT_FORWARD_RETURN2(call, def) \
	if (m_ses == 0) throw_invalid_handle(); \
	TORRENT_ASSERT(m_chk); \
	session_impl::mutex_t::scoped_lock l1(m_ses->m_mutex); \
	mutex::scoped_lock l2(m_chk->m_mutex); \
	torrent* t = find_torrent(m_ses, m_chk, m_info_hash); \
	if (t == 0) return def; \
	t->call

#endif

namespace libtorrent
{
	namespace fs = boost::filesystem;

	namespace
	{
#ifndef BOOST_NO_EXCEPTIONS
		void throw_invalid_handle()
		{
			throw invalid_handle();
		}
#endif
			  
		torrent* find_torrent(
			session_impl* ses
			, aux::checker_impl* chk
			, sha1_hash const& hash)
		{
			aux::piece_checker_data* d = chk->find_torrent(hash);
			if (d != 0) return d->torrent_ptr.get();

			boost::shared_ptr<torrent> t = ses->find_torrent(hash).lock();
			if (t) return t.get();
			return 0;
		}
	}

#ifndef NDEBUG

	void torrent_handle::check_invariant() const
	{
		TORRENT_ASSERT((m_ses == 0 && m_chk == 0) || (m_ses != 0 && m_chk != 0));
	}

#endif

	void torrent_handle::set_max_uploads(int max_uploads) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(max_uploads >= 2 || max_uploads == -1);
		TORRENT_FORWARD(set_max_uploads(max_uploads));
	}

	void torrent_handle::use_interface(const char* net_interface) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(use_interface(net_interface));
	}

	void torrent_handle::set_max_connections(int max_connections) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(max_connections >= 2 || max_connections == -1);
		TORRENT_FORWARD(set_max_connections(max_connections));
	}

	void torrent_handle::set_peer_upload_limit(tcp::endpoint ip, int limit) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(limit >= -1);
		TORRENT_FORWARD(set_peer_upload_limit(ip, limit));
	}

	void torrent_handle::set_peer_download_limit(tcp::endpoint ip, int limit) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(limit >= -1);
		TORRENT_FORWARD(set_peer_download_limit(ip, limit));
	}

	void torrent_handle::set_upload_limit(int limit) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(limit >= -1);
		TORRENT_FORWARD(set_upload_limit(limit));
	}

	int torrent_handle::upload_limit() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(upload_limit(), 0);
	}

	void torrent_handle::set_download_limit(int limit) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(limit >= -1);
		TORRENT_FORWARD(set_download_limit(limit));
	}

	int torrent_handle::download_limit() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(download_limit(), 0);
	}

	void torrent_handle::move_storage(
		fs::path const& save_path) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(move_storage(save_path));
	}

	void torrent_handle::add_extension(
		boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> const& ext
		, void* userdata)
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(add_extension(ext, userdata));
	}

	bool torrent_handle::has_metadata() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(valid_metadata(), false);
	}

	bool torrent_handle::is_seed() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(is_seed(), false);
	}

	bool torrent_handle::is_paused() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(is_paused(), false);
	}

	void torrent_handle::pause() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(pause());
	}

	void torrent_handle::resume() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(resume());
	}

	void torrent_handle::set_tracker_login(std::string const& name
		, std::string const& password) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(resume());
	}

	void torrent_handle::file_progress(std::vector<float>& progress)
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(file_progress(progress));
	}

	torrent_status torrent_handle::status() const
	{
		INVARIANT_CHECK;

		if (m_ses == 0)
#ifdef BOOST_NO_EXCEPTIONS
			return torrent_status();
#else
			throw_invalid_handle();
#endif
		TORRENT_ASSERT(m_chk);
	
		session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
		mutex::scoped_lock l2(m_chk->m_mutex);

		aux::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
		if (d != 0)
		{
			torrent_status st = d->torrent_ptr->status();

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

		boost::shared_ptr<torrent> t = m_ses->find_torrent(m_info_hash).lock();
		if (t) return t->status();

#ifndef BOOST_NO_EXCEPTIONS
		throw_invalid_handle();
#endif
		return torrent_status();
	}

	void torrent_handle::set_sequential_download(bool sd) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_sequential_download(sd));
	}

	std::string torrent_handle::name() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(name(), "");
	}

	void torrent_handle::piece_availability(std::vector<int>& avail) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(piece_availability(avail));
	}

	void torrent_handle::piece_priority(int index, int priority) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_piece_priority(index, priority));
	}

	int torrent_handle::piece_priority(int index) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(piece_priority(index), 0);
	}

	void torrent_handle::prioritize_pieces(std::vector<int> const& pieces) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(prioritize_pieces(pieces));
	}

	std::vector<int> torrent_handle::piece_priorities() const
	{
		INVARIANT_CHECK;
		std::vector<int> ret;
		TORRENT_FORWARD_RETURN2(piece_priorities(ret), ret);
		return ret;
	}

	void torrent_handle::prioritize_files(std::vector<int> const& files) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(prioritize_files(files));
	}

// ============ start deprecation ===============

	void torrent_handle::filter_piece(int index, bool filter) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(filter_piece(index, filter));
	}

	void torrent_handle::filter_pieces(std::vector<bool> const& pieces) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(filter_pieces(pieces));
	}

	bool torrent_handle::is_piece_filtered(int index) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(is_piece_filtered(index), false);
	}

	std::vector<bool> torrent_handle::filtered_pieces() const
	{
		INVARIANT_CHECK;
		std::vector<bool> ret;
		TORRENT_FORWARD_RETURN2(filtered_pieces(ret), ret);
		return ret;
	}

	void torrent_handle::filter_files(std::vector<bool> const& files) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(filter_files(files));
	}

// ============ end deprecation ===============


	std::vector<announce_entry> const& torrent_handle::trackers() const
	{
		INVARIANT_CHECK;
		const static std::vector<announce_entry> empty;
		TORRENT_FORWARD_RETURN(trackers(), empty);
	}

	void torrent_handle::add_url_seed(std::string const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(add_url_seed(url));
	}

	void torrent_handle::remove_url_seed(std::string const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(remove_url_seed(url));
	}

	std::set<std::string> torrent_handle::url_seeds() const
	{
		INVARIANT_CHECK;
		const static std::set<std::string> empty;
		TORRENT_FORWARD_RETURN(url_seeds(), empty);
	}

	void torrent_handle::replace_trackers(
		std::vector<announce_entry> const& urls) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(replace_trackers(urls));
	}

	torrent_info const& torrent_handle::get_torrent_info() const
	{
		INVARIANT_CHECK;
#ifdef BOOST_NO_EXCEPTIONS
		const static torrent_info empty;
		if (m_ses == 0) return empty;
#else
		if (m_ses == 0) throw_invalid_handle();
#endif
		TORRENT_ASSERT(m_chk);
		session_impl::mutex_t::scoped_lock l1(m_ses->m_mutex);
		mutex::scoped_lock l2(m_chk->m_mutex);
		torrent* t = find_torrent(m_ses, m_chk, m_info_hash);
		if (t == 0 || !t->valid_metadata())
#ifdef BOOST_NO_EXCEPTIONS
			return empty;
#else
			throw_invalid_handle();
#endif
		return t->torrent_file();
	}

	bool torrent_handle::is_valid() const
	{
		INVARIANT_CHECK;
		if (m_ses == 0) return false;
		TORRENT_ASSERT(m_chk);
		session_impl::mutex_t::scoped_lock l1(m_ses->m_mutex);
		mutex::scoped_lock l2(m_chk->m_mutex);
		torrent* t = find_torrent(m_ses, m_chk, m_info_hash);
		return t;
	}

	entry torrent_handle::write_resume_data() const
	{
		INVARIANT_CHECK;

		if (m_ses == 0)
#ifdef BOOST_NO_EXCEPTIONS
			return entry();
#else
			throw_invalid_handle();
#endif
		TORRENT_ASSERT(m_chk);

		session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
		mutex::scoped_lock l2(m_chk->m_mutex);

		torrent* t = find_torrent(m_ses, m_chk, m_info_hash);
		if (!t || !t->valid_metadata())
#ifdef BOOST_NO_EXCEPTIONS
			return entry();
#else
			throw_invalid_handle();
#endif

		std::vector<bool> have_pieces = t->pieces();

		entry ret(entry::dictionary_t);

		ret["file-format"] = "libtorrent resume file";
		ret["file-version"] = 1;

		ret["allocation"] = t->filesystem().compact_allocation()?"compact":"full";

		const sha1_hash& info_hash = t->torrent_file().info_hash();
		ret["info-hash"] = std::string((char*)info_hash.begin(), (char*)info_hash.end());

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
				if (i->finished == 0) continue;

				entry piece_struct(entry::dictionary_t);

				// the unfinished piece's index
				piece_struct["piece"] = i->index;

				have_pieces[i->index] = true;

				std::string bitmask;
				const int num_bitmask_bytes
					= (std::max)(num_blocks_per_piece / 8, 1);

				for (int j = 0; j < num_bitmask_bytes; ++j)
				{
					unsigned char v = 0;
					int bits = (std::min)(num_blocks_per_piece - j*8, 8);
					for (int k = 0; k < bits; ++k)
						v |= (i->info[j*8+k].state == piece_picker::block_info::state_finished)
						? (1 << k) : 0;
					bitmask.insert(bitmask.end(), v);
					TORRENT_ASSERT(bits == 8 || j == num_bitmask_bytes - 1);
				}
				piece_struct["bitmask"] = bitmask;

				TORRENT_ASSERT(t->filesystem().slot_for(i->index) >= 0);
				unsigned long adler
					= t->filesystem().piece_crc(
						t->filesystem().slot_for(i->index)
						, t->block_size()
						, i->info);

				piece_struct["adler32"] = adler;

				// push the struct onto the unfinished-piece list
				up.push_back(piece_struct);
			}
		}

		std::vector<int> piece_index;
		t->filesystem().export_piece_map(piece_index, have_pieces);
		entry::list_type& slots = ret["slots"].list();
		std::copy(piece_index.begin(), piece_index.end(), std::back_inserter(slots));

		// write local peers

		entry::list_type& peer_list = ret["peers"].list();
		entry::list_type& banned_peer_list = ret["banned_peers"].list();
		
		policy& pol = t->get_policy();

		for (policy::iterator i = pol.begin_peer()
			, end(pol.end_peer()); i != end; ++i)
		{
			asio::error_code ec;
			if (i->second.banned)
			{
				tcp::endpoint ip = i->second.ip;
				entry peer(entry::dictionary_t);
				peer["ip"] = ip.address().to_string(ec);
				if (ec) continue;
				peer["port"] = ip.port();
				banned_peer_list.push_back(peer);
				continue;
			}
			// we cannot save remote connection
			// since we don't know their listen port
			// unless they gave us their listen port
			// through the extension handshake
			// so, if the peer is not connectable (i.e. we
			// don't know its listen port) or if it has
			// been banned, don't save it.
			if (i->second.type == policy::peer::not_connectable) continue;

			tcp::endpoint ip = i->second.ip;
			entry peer(entry::dictionary_t);
			peer["ip"] = ip.address().to_string(ec);
			if (ec) continue;
			peer["port"] = ip.port();
			peer_list.push_back(peer);
		}

		t->filesystem().write_resume_data(ret);

		return ret;
	}


	fs::path torrent_handle::save_path() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(save_path(), fs::path());
	}

	void torrent_handle::connect_peer(tcp::endpoint const& adr, int source) const
	{
		INVARIANT_CHECK;

		if (m_ses == 0)
#ifdef BOOST_NO_EXCEPTIONS
			return;
#else
			throw_invalid_handle();
#endif
		TORRENT_ASSERT(m_chk);
	
		session_impl::mutex_t::scoped_lock l(m_ses->m_mutex);
		boost::shared_ptr<torrent> t = m_ses->find_torrent(m_info_hash).lock();
		
		if (!t)
		{
			// the torrent is being checked. Add the peer to its
			// peer list. The entries in there will be connected
			// once the checking is complete.
			mutex::scoped_lock l2(m_chk->m_mutex);

			aux::piece_checker_data* d = m_chk->find_torrent(m_info_hash);
			if (d == 0)
#ifdef BOOST_NO_EXCEPTIONS
				return;
#else
				throw_invalid_handle();
#endif
			d->peers.push_back(adr);
			return;
		}

		peer_id id;
		std::fill(id.begin(), id.end(), 0);
		t->get_policy().peer_from_tracker(adr, id, source, 0);
	}

	void torrent_handle::force_reannounce(
		boost::posix_time::time_duration duration) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(force_tracker_request(time_now() + seconds(duration.total_seconds())));
	}

	void torrent_handle::force_reannounce() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(force_tracker_request());
	}

	void torrent_handle::scrape_tracker() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(scrape_tracker());
	}

	void torrent_handle::set_ratio(float ratio) const
	{
		INVARIANT_CHECK;
		
		TORRENT_ASSERT(ratio >= 0.f);
		if (ratio < 1.f && ratio > 0.f)
			ratio = 1.f;
		TORRENT_FORWARD(set_ratio(ratio));
	}

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
	void torrent_handle::resolve_countries(bool r)
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(resolve_countries(r));
	}

	bool torrent_handle::resolve_countries() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(resolving_countries(), false);
	}
#endif

	void torrent_handle::get_peer_info(std::vector<peer_info>& v) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(get_peer_info(v));
	}

	void torrent_handle::get_download_queue(std::vector<partial_piece_info>& queue) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(get_download_queue(queue));
	}

}

