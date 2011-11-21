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
#include <iterator>
#include <algorithm>
#include <set>
#include <cctype>
#include <algorithm>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

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
#include "libtorrent/utf8.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std
{
	using ::srand;
	using ::isalnum;
};
#endif

using libtorrent::aux::session_impl;

#ifdef BOOST_NO_EXCEPTIONS

#define TORRENT_FORWARD(call) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return; \
	session_impl::mutex_t::scoped_lock l(t->session().m_mutex); \
	t->call
	
#define TORRENT_FORWARD_RETURN(call, def) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return def; \
	session_impl::mutex_t::scoped_lock l(t->session().m_mutex); \
	return t->call

#define TORRENT_FORWARD_RETURN2(call, def) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return def; \
	session_impl::mutex_t::scoped_lock l(t->session().m_mutex); \
	t->call

#else

#define TORRENT_FORWARD(call) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) throw_invalid_handle(); \
	session_impl::mutex_t::scoped_lock l(t->session().m_mutex); \
	t->call
	
#define TORRENT_FORWARD_RETURN(call, def) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) throw_invalid_handle(); \
	session_impl::mutex_t::scoped_lock l(t->session().m_mutex); \
	return t->call

#define TORRENT_FORWARD_RETURN2(call, def) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) throw_invalid_handle(); \
	session_impl::mutex_t::scoped_lock l(t->session().m_mutex); \
	t->call

#endif

namespace libtorrent
{
	namespace fs = boost::filesystem;

#ifndef BOOST_NO_EXCEPTIONS
	void throw_invalid_handle()
	{
		throw libtorrent_exception(errors::invalid_torrent_handle);
	}
#endif

#ifdef TORRENT_DEBUG

	void torrent_handle::check_invariant() const
	{}

#endif

	sha1_hash torrent_handle::info_hash() const
	{
		INVARIANT_CHECK;
		const static sha1_hash empty;
		TORRENT_FORWARD_RETURN(torrent_file().info_hash(), empty);
	}

	int torrent_handle::max_uploads() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(max_uploads(), 0);
	}

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

	int torrent_handle::max_connections() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(max_connections(), 0);
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

#ifndef BOOST_FILESYSTEM_NARROW_ONLY
	void torrent_handle::move_storage(
		fs::wpath const& save_path) const
	{
		INVARIANT_CHECK;
		std::string utf8;
		wchar_utf8(save_path.string(), utf8);
		TORRENT_FORWARD(move_storage(utf8));
	}

	void torrent_handle::rename_file(int index, fs::wpath const& new_name) const
	{
		INVARIANT_CHECK;
		std::string utf8;
		wchar_utf8(new_name.string(), utf8);
		TORRENT_FORWARD(rename_file(index, utf8));
	}
#endif

	void torrent_handle::rename_file(int index, fs::path const& new_name) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(rename_file(index, new_name.string()));
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

	bool torrent_handle::set_metadata(char const* metadata, int size) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(set_metadata(metadata, size), false);
	}

	bool torrent_handle::is_seed() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(is_seed(), false);
	}

	bool torrent_handle::is_finished() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(is_finished(), false);
	}

	bool torrent_handle::is_paused() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(is_torrent_paused(), false);
	}

	void torrent_handle::pause() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(pause());
	}

	void torrent_handle::set_upload_mode(bool b) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_upload_mode(b));
	}

	void torrent_handle::flush_cache() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(flush_cache());
	}

	void torrent_handle::save_resume_data() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(save_resume_data());
	}

	void torrent_handle::force_recheck() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(force_recheck());
	}

	void torrent_handle::resume() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(resume());
	}

	bool torrent_handle::is_auto_managed() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(is_auto_managed(), true);
	}

	void torrent_handle::auto_managed(bool m) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(auto_managed(m));
	}

	void torrent_handle::set_priority(int p) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_priority(p));
	}

	int torrent_handle::queue_position() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(queue_position(), -1);
	}

	void torrent_handle::queue_position_up() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_queue_position(t->queue_position() == 0
			? t->queue_position() : t->queue_position() - 1));
	}

	void torrent_handle::queue_position_down() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_queue_position(t->queue_position() + 1));
	}

	void torrent_handle::queue_position_top() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_queue_position(0));
	}

	void torrent_handle::queue_position_bottom() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_queue_position((std::numeric_limits<int>::max)()));
	}

	void torrent_handle::clear_error() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(clear_error());
	}

	void torrent_handle::set_tracker_login(std::string const& name
		, std::string const& password) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_tracker_login(name, password));
	}

#ifndef TORRENT_NO_DEPRECATE
#if !TORRENT_NO_FPU
	void torrent_handle::file_progress(std::vector<float>& progress) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(file_progress(progress));
	}
#endif
#endif

	void torrent_handle::file_progress(std::vector<size_type>& progress, int flags) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(file_progress(progress, flags));
	}

	torrent_status torrent_handle::status() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(status(), torrent_status());
	}

	void torrent_handle::set_sequential_download(bool sd) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_sequential_download(sd));
	}

	bool torrent_handle::is_sequential_download() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(is_sequential_download(), false);
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

	void torrent_handle::file_priority(int index, int priority) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_file_priority(index, priority));
	}

	int torrent_handle::file_priority(int index) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(file_priority(index), 0);
	}

	void torrent_handle::prioritize_files(std::vector<int> const& files) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(prioritize_files(files));
	}

	std::vector<int> torrent_handle::file_priorities() const
	{
		INVARIANT_CHECK;
		std::vector<int> ret;
		TORRENT_FORWARD_RETURN2(file_priorities(ret), ret);
		return ret;
	}

#ifndef TORRENT_NO_DEPRECATE
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
#endif

	std::vector<announce_entry> torrent_handle::trackers() const
	{
		INVARIANT_CHECK;
		const static std::vector<announce_entry> empty;
		TORRENT_FORWARD_RETURN(trackers(), empty);
	}

	void torrent_handle::add_url_seed(std::string const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(add_web_seed(url, web_seed_entry::url_seed));
	}

	void torrent_handle::remove_url_seed(std::string const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(remove_web_seed(url, web_seed_entry::url_seed));
	}

	std::set<std::string> torrent_handle::url_seeds() const
	{
		INVARIANT_CHECK;
		const static std::set<std::string> empty;
		TORRENT_FORWARD_RETURN(web_seeds(web_seed_entry::url_seed), empty);
	}

	void torrent_handle::add_http_seed(std::string const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(add_web_seed(url, web_seed_entry::http_seed));
	}

	void torrent_handle::remove_http_seed(std::string const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(remove_web_seed(url, web_seed_entry::http_seed));
	}

	std::set<std::string> torrent_handle::http_seeds() const
	{
		INVARIANT_CHECK;
		const static std::set<std::string> empty;
		TORRENT_FORWARD_RETURN(web_seeds(web_seed_entry::http_seed), empty);
	}

	void torrent_handle::replace_trackers(
		std::vector<announce_entry> const& urls) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(replace_trackers(urls));
	}

	void torrent_handle::add_tracker(announce_entry const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(add_tracker(url));
	}

	void torrent_handle::add_piece(int piece, char const* data, int flags) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(add_piece(piece, data, flags));
	}

	void torrent_handle::read_piece(int piece) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(read_piece(piece));
	}

	storage_interface* torrent_handle::get_storage_impl() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(get_storage(), 0);
	}

	torrent_info const& torrent_handle::get_torrent_info() const
	{
		INVARIANT_CHECK;
#ifdef BOOST_NO_EXCEPTIONS
		const static torrent_info empty(sha1_hash(0));
#endif
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t)
#ifdef BOOST_NO_EXCEPTIONS
			return empty;
#else
			throw_invalid_handle();
#endif
		session_impl::mutex_t::scoped_lock l(t->session().m_mutex);
		if (!t->valid_metadata())
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
		return !m_torrent.expired();
	}

#ifndef TORRENT_NO_DEPRECATE
	entry torrent_handle::write_resume_data() const
	{
		INVARIANT_CHECK;

		entry ret(entry::dictionary_t);
		TORRENT_FORWARD_RETURN2(write_resume_data(ret), ret);
		t->filesystem().write_resume_data(ret);

		return ret;
	}
#endif

	fs::path torrent_handle::save_path() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(save_path(), fs::path());
	}

	void torrent_handle::connect_peer(tcp::endpoint const& adr, int source) const
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t)
#ifdef BOOST_NO_EXCEPTIONS
			return;
#else
			throw_invalid_handle();
#endif
		session_impl::mutex_t::scoped_lock l(t->session().m_mutex);
		
		peer_id id;
		std::fill(id.begin(), id.end(), 0);
		t->get_policy().add_peer(adr, id, source, 0);
	}

	void torrent_handle::force_reannounce(
		boost::posix_time::time_duration duration) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(force_tracker_request(time_now() + seconds(duration.total_seconds())));
	}

#ifndef TORRENT_DISABLE_DHT
	void torrent_handle::force_dht_announce() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(force_dht_announce());
	}
#endif

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

	bool torrent_handle::super_seeding() const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD_RETURN(super_seeding(), false);
	}

	void torrent_handle::super_seeding(bool on) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(super_seeding(on));
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

	void torrent_handle::get_full_peer_list(std::vector<peer_list_entry>& v) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(get_full_peer_list(v));
	}

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

	void torrent_handle::set_piece_deadline(int index, int deadline, int flags) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(set_piece_deadline(index, deadline, flags));
	}

	void torrent_handle::reset_piece_deadline(int index) const
	{
		INVARIANT_CHECK;
		TORRENT_FORWARD(reset_piece_deadline(index));
	}

}

