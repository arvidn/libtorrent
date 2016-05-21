/*

Copyright (c) 2003-2016, Arvid Norberg
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
#include <iterator>
#include <algorithm>
#include <set>
#include <cctype>

#include "libtorrent/peer_id.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/session_call.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/announce_entry.hpp"

#if TORRENT_COMPLETE_TYPES_REQUIRED
#include "libtorrent/peer_info.hpp" // for peer_list_entry
#endif

#define TORRENT_ASYNC_CALL0(x) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return; \
	torrent* ptr = t.get(); \
	session_impl& ses = static_cast<session_impl&>(t->session()); \
	ses.get_io_service().dispatch([=] () { ptr->x(); } )

#define TORRENT_ASYNC_CALL(x, ...) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return; \
	torrent* ptr = t.get(); \
	session_impl& ses = static_cast<session_impl&>(t->session()); \
	ses.get_io_service().dispatch([=] () { ptr->x(__VA_ARGS__); } )

#define TORRENT_SYNC_CALL(x, ...) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	torrent* ptr = t.get(); \
	if (t) aux::sync_call_handle(t, [=](){ ptr->x(__VA_ARGS__); })

#define TORRENT_SYNC_CALL_RET0(type, def, x) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	torrent* ptr = t.get(); \
	type r = def; \
	if (t) aux::sync_call_ret_handle(t, r, std::function<type()>([=](){ return ptr->x(); }))

#define TORRENT_SYNC_CALL_RET(type, def, x, ...) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	torrent* ptr = t.get(); \
	type r = def; \
	if (t) aux::sync_call_ret_handle(t, r, std::function<type()>([=](){ return ptr->x(__VA_ARGS__); }))

using libtorrent::aux::session_impl;

namespace libtorrent
{

#ifndef BOOST_NO_EXCEPTIONS
	void throw_invalid_handle()
	{
		throw system_error(errors::invalid_torrent_handle);
	}
#endif

	sha1_hash torrent_handle::info_hash() const
	{
		boost::shared_ptr<torrent> t = m_torrent.lock();
		static const sha1_hash empty;
		if (!t) return empty;
		return t->info_hash();
	}

	int torrent_handle::max_uploads() const
	{
		TORRENT_SYNC_CALL_RET0(int, 0, max_uploads);
		return r;
	}

	void torrent_handle::set_max_uploads(int max_uploads) const
	{
		TORRENT_ASSERT_PRECOND(max_uploads >= 2 || max_uploads == -1);
		TORRENT_ASYNC_CALL(set_max_uploads, max_uploads, true);
	}

	int torrent_handle::max_connections() const
	{
		TORRENT_SYNC_CALL_RET0(int, 0, max_connections);
		return r;
	}

	void torrent_handle::set_max_connections(int max_connections) const
	{
		TORRENT_ASSERT_PRECOND(max_connections >= 2 || max_connections == -1);
		TORRENT_ASYNC_CALL(set_max_connections, max_connections, true);
	}

	void torrent_handle::set_upload_limit(int limit) const
	{
		TORRENT_ASSERT_PRECOND(limit >= -1);
		TORRENT_ASYNC_CALL(set_upload_limit, limit);
	}

	int torrent_handle::upload_limit() const
	{
		TORRENT_SYNC_CALL_RET0(int, 0, upload_limit);
		return r;
	}

	void torrent_handle::set_download_limit(int limit) const
	{
		TORRENT_ASSERT_PRECOND(limit >= -1);
		TORRENT_ASYNC_CALL(set_download_limit, limit);
	}

	int torrent_handle::download_limit() const
	{
		TORRENT_SYNC_CALL_RET0(int, 0, download_limit);
		return r;
	}

	void torrent_handle::move_storage(
		std::string const& save_path, int flags) const
	{
		TORRENT_ASYNC_CALL(move_storage, save_path, flags);
	}

#if TORRENT_USE_WSTRING
#ifndef TORRENT_NO_DEPRECATE
	void torrent_handle::move_storage(
		std::wstring const& save_path, int flags) const
	{
		std::string utf8;
		wchar_utf8(save_path, utf8);
		TORRENT_ASYNC_CALL(move_storage, utf8, flags);
	}

	void torrent_handle::rename_file(int index, std::wstring const& new_name) const
	{
		std::string utf8;
		wchar_utf8(new_name, utf8);
		TORRENT_ASYNC_CALL(rename_file, index, utf8);
	}
#endif // TORRENT_NO_DEPRECATE
#endif // TORRENT_USE_WSTRING

	void torrent_handle::rename_file(int index, std::string const& new_name) const
	{
		TORRENT_ASYNC_CALL(rename_file, index, new_name);
	}

	void torrent_handle::add_extension(
		boost::function<boost::shared_ptr<torrent_plugin>(torrent_handle const&, void*)> const& ext
		, void* userdata)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		TORRENT_ASYNC_CALL(add_extension, ext, userdata);
#else
		TORRENT_UNUSED(ext);
		TORRENT_UNUSED(userdata);
#endif
	}

	bool torrent_handle::set_metadata(char const* metadata, int size) const
	{
		TORRENT_SYNC_CALL_RET(bool, false, set_metadata, metadata, size);
		return r;
	}

	void torrent_handle::pause(int flags) const
	{
		TORRENT_ASYNC_CALL(pause, bool(flags & graceful_pause));
	}

	void torrent_handle::stop_when_ready(bool b) const
	{
		TORRENT_ASYNC_CALL(stop_when_ready, b);
	}

	void torrent_handle::apply_ip_filter(bool b) const
	{
		TORRENT_ASYNC_CALL(set_apply_ip_filter, b);
	}

	void torrent_handle::set_share_mode(bool b) const
	{
		TORRENT_ASYNC_CALL(set_share_mode, b);
	}

	void torrent_handle::set_upload_mode(bool b) const
	{
		TORRENT_ASYNC_CALL(set_upload_mode, b);
	}

	void torrent_handle::flush_cache() const
	{
		TORRENT_ASYNC_CALL0(flush_cache);
	}

	void torrent_handle::set_ssl_certificate(
		std::string const& certificate
		, std::string const& private_key
		, std::string const& dh_params
		, std::string const& passphrase)
	{
#ifdef TORRENT_USE_OPENSSL
		TORRENT_ASYNC_CALL(set_ssl_cert, certificate, private_key, dh_params, passphrase);
#else
		TORRENT_UNUSED(certificate);
		TORRENT_UNUSED(private_key);
		TORRENT_UNUSED(dh_params);
		TORRENT_UNUSED(passphrase);
#endif
	}

	void torrent_handle::set_ssl_certificate_buffer(
		std::string const& certificate
		, std::string const& private_key
		, std::string const& dh_params)
	{
#ifdef TORRENT_USE_OPENSSL
		TORRENT_ASYNC_CALL(set_ssl_cert_buffer, certificate, private_key, dh_params);
#else
		TORRENT_UNUSED(certificate);
		TORRENT_UNUSED(private_key);
		TORRENT_UNUSED(dh_params);
#endif
	}

	void torrent_handle::save_resume_data(int f) const
	{
		TORRENT_ASYNC_CALL(save_resume_data, f);
	}

	bool torrent_handle::need_save_resume_data() const
	{
		TORRENT_SYNC_CALL_RET0(bool, false, need_save_resume_data);
		return r;
	}

	void torrent_handle::force_recheck() const
	{
		TORRENT_ASYNC_CALL0(force_recheck);
	}

	void torrent_handle::resume() const
	{
		TORRENT_ASYNC_CALL0(resume);
	}

	void torrent_handle::auto_managed(bool m) const
	{
		TORRENT_ASYNC_CALL(auto_managed, m);
	}

	void torrent_handle::set_priority(int p) const
	{
		TORRENT_ASYNC_CALL(set_priority, p);
	}

	int torrent_handle::queue_position() const
	{
		TORRENT_SYNC_CALL_RET0(int, -1, queue_position);
		return r;
	}

	void torrent_handle::queue_position_up() const
	{
		TORRENT_ASYNC_CALL0(queue_up);
	}

	void torrent_handle::queue_position_down() const
	{
		TORRENT_ASYNC_CALL0(queue_down);
	}

	void torrent_handle::queue_position_top() const
	{
		TORRENT_ASYNC_CALL(set_queue_position, 0);
	}

	void torrent_handle::queue_position_bottom() const
	{
		TORRENT_ASYNC_CALL(set_queue_position, INT_MAX);
	}

	void torrent_handle::clear_error() const
	{
		TORRENT_ASYNC_CALL0(clear_error);
	}

#ifndef TORRENT_NO_DEPRECATE
	void torrent_handle::set_tracker_login(std::string const& name
		, std::string const& password) const
	{
		TORRENT_ASYNC_CALL(set_tracker_login, name, password);
	}
#endif

	void torrent_handle::file_progress(std::vector<boost::int64_t>& progress, int flags) const
	{
		auto progressr = std::ref(progress);
		TORRENT_SYNC_CALL(file_progress, progressr, flags);
	}

	torrent_status torrent_handle::status(boost::uint32_t flags) const
	{
		torrent_status st;
		auto stp = &st;
		TORRENT_SYNC_CALL(status, stp, flags);
		return st;
	}

	void torrent_handle::set_pinned(bool p) const
	{
		TORRENT_ASYNC_CALL(set_pinned, p);
	}

	void torrent_handle::set_sequential_download(bool sd) const
	{
		TORRENT_ASYNC_CALL(set_sequential_download, sd);
	}

	void torrent_handle::piece_availability(std::vector<int>& avail) const
	{
		auto availr = std::ref(avail);
		TORRENT_SYNC_CALL(piece_availability, availr);
	}

	void torrent_handle::piece_priority(int index, int priority) const
	{
		TORRENT_ASYNC_CALL(set_piece_priority, index, priority);
	}

	int torrent_handle::piece_priority(int index) const
	{
		TORRENT_SYNC_CALL_RET(int, 0, piece_priority, index);
		return r;
	}

	void torrent_handle::prioritize_pieces(std::vector<int> const& pieces) const
	{
		TORRENT_ASYNC_CALL(prioritize_pieces, pieces);
	}

	void torrent_handle::prioritize_pieces(std::vector<std::pair<int, int> > const& pieces) const
	{
		TORRENT_ASYNC_CALL(prioritize_piece_list, pieces);
	}

	std::vector<int> torrent_handle::piece_priorities() const
	{
		std::vector<int> ret;
		auto retp = &ret;
		TORRENT_SYNC_CALL(piece_priorities, retp);
		return ret;
	}

	void torrent_handle::file_priority(int index, int priority) const
	{
		TORRENT_ASYNC_CALL(set_file_priority, index, priority);
	}

	int torrent_handle::file_priority(int index) const
	{
		TORRENT_SYNC_CALL_RET(int, 0, file_priority, index);
		return r;
	}

	void torrent_handle::prioritize_files(std::vector<int> const& files) const
	{
		TORRENT_ASYNC_CALL(prioritize_files, files);
	}

	std::vector<int> torrent_handle::file_priorities() const
	{
		std::vector<int> ret;
		auto retp = &ret;
		TORRENT_SYNC_CALL(file_priorities, retp);
		return ret;
	}

#ifndef TORRENT_NO_DEPRECATE
// ============ start deprecation ===============

	int torrent_handle::get_peer_upload_limit(tcp::endpoint) const { return -1; }
	int torrent_handle::get_peer_download_limit(tcp::endpoint) const { return -1; }
	void torrent_handle::set_peer_upload_limit(tcp::endpoint, int /* limit */) const {}
	void torrent_handle::set_peer_download_limit(tcp::endpoint, int /* limit */) const {}
	void torrent_handle::set_ratio(float) const {}
	void torrent_handle::use_interface(const char* net_interface) const
	{
		TORRENT_ASYNC_CALL(use_interface, std::string(net_interface));
	}

#if !TORRENT_NO_FPU
	void torrent_handle::file_progress(std::vector<float>& progress) const
	{
		auto progressr = std::ref(progress);
		TORRENT_SYNC_CALL(file_progress, progressr);
	}
#endif

	bool torrent_handle::is_seed() const
	{
		TORRENT_SYNC_CALL_RET0(bool, false, is_seed);
		return r;
	}

	bool torrent_handle::is_finished() const
	{
		TORRENT_SYNC_CALL_RET0(bool, false, is_finished);
		return r;
	}

	bool torrent_handle::is_paused() const
	{
		TORRENT_SYNC_CALL_RET0(bool, false, is_torrent_paused);
		return r;
	}

	bool torrent_handle::is_sequential_download() const
	{
		TORRENT_SYNC_CALL_RET0(bool, false, is_sequential_download);
		return r;
	}

	bool torrent_handle::is_auto_managed() const
	{
		TORRENT_SYNC_CALL_RET0(bool, false, is_auto_managed);
		return r;
	}

	bool torrent_handle::has_metadata() const
	{
		TORRENT_SYNC_CALL_RET0(bool, false, valid_metadata);
		return r;
	}

	void torrent_handle::filter_piece(int index, bool filter) const
	{
		TORRENT_ASYNC_CALL(filter_piece, index, filter);
	}

	void torrent_handle::filter_pieces(std::vector<bool> const& pieces) const
	{
		TORRENT_ASYNC_CALL(filter_pieces, pieces);
	}

	bool torrent_handle::is_piece_filtered(int index) const
	{
		TORRENT_SYNC_CALL_RET(bool, false, is_piece_filtered, index);
		return r;
	}

	std::vector<bool> torrent_handle::filtered_pieces() const
	{
		std::vector<bool> ret;
		auto retr = std::ref(ret);
		TORRENT_SYNC_CALL(filtered_pieces, retr);
		return ret;
	}

	void torrent_handle::filter_files(std::vector<bool> const& files) const
	{
		auto filesr= std::ref(files);
		TORRENT_ASYNC_CALL(filter_files, filesr);
	}

	bool torrent_handle::super_seeding() const
	{
		TORRENT_SYNC_CALL_RET0(bool, false, super_seeding);
		return r;
	}

// ============ end deprecation ===============
#endif

	std::vector<announce_entry> torrent_handle::trackers() const
	{
		static const std::vector<announce_entry> empty;
		TORRENT_SYNC_CALL_RET0(std::vector<announce_entry>, empty, trackers);
		return r;
	}

	void torrent_handle::add_url_seed(std::string const& url) const
	{
		TORRENT_ASYNC_CALL(add_web_seed, url, web_seed_entry::url_seed);
	}

	void torrent_handle::remove_url_seed(std::string const& url) const
	{
		TORRENT_ASYNC_CALL(remove_web_seed, url, web_seed_entry::url_seed);
	}

	std::set<std::string> torrent_handle::url_seeds() const
	{
		static const std::set<std::string> empty;
		TORRENT_SYNC_CALL_RET(std::set<std::string>, empty, web_seeds, web_seed_entry::url_seed);
		return r;
	}

	void torrent_handle::add_http_seed(std::string const& url) const
	{
		TORRENT_ASYNC_CALL(add_web_seed, url, web_seed_entry::http_seed);
	}

	void torrent_handle::remove_http_seed(std::string const& url) const
	{
		TORRENT_ASYNC_CALL(remove_web_seed, url, web_seed_entry::http_seed);
	}

	std::set<std::string> torrent_handle::http_seeds() const
	{
		static const std::set<std::string> empty;
		TORRENT_SYNC_CALL_RET(std::set<std::string>, empty, web_seeds, web_seed_entry::http_seed);
		return r;
	}

	void torrent_handle::replace_trackers(
		std::vector<announce_entry> const& urls) const
	{
		TORRENT_ASYNC_CALL(replace_trackers, urls);
	}

	void torrent_handle::add_tracker(announce_entry const& url) const
	{
		TORRENT_ASYNC_CALL(add_tracker, url);
	}

	void torrent_handle::add_piece(int piece, char const* data, int flags) const
	{
		TORRENT_SYNC_CALL(add_piece, piece, data, flags);
	}

	void torrent_handle::read_piece(int piece) const
	{
		TORRENT_ASYNC_CALL(read_piece, piece);
	}

	bool torrent_handle::have_piece(int piece) const
	{
		TORRENT_SYNC_CALL_RET(bool, false, have_piece, piece);
		return r;
	}

	storage_interface* torrent_handle::get_storage_impl() const
	{
		TORRENT_SYNC_CALL_RET0(storage_interface*, 0, get_storage);
		return r;
	}

	bool torrent_handle::is_valid() const
	{
		return !m_torrent.expired();
	}

	boost::shared_ptr<const torrent_info> torrent_handle::torrent_file() const
	{
		TORRENT_SYNC_CALL_RET0(boost::shared_ptr<const torrent_info>
			, boost::shared_ptr<const torrent_info>(), get_torrent_copy);
		return r;
	}

#ifndef TORRENT_NO_DEPRECATE
	// this function should either be removed, or return
	// reference counted handle to the torrent_info which
	// forces the torrent to stay loaded while the client holds it
	torrent_info const& torrent_handle::get_torrent_info() const
	{
		static boost::shared_ptr<const torrent_info> holder[4];
		static int cursor = 0;
		static std::mutex holder_mutex;

		boost::shared_ptr<const torrent_info> r = torrent_file();

		std::lock_guard<std::mutex> l(holder_mutex);
		holder[cursor++] = r;
		cursor = cursor % (sizeof(holder) / sizeof(holder[0]));
		return *r;
	}

	entry torrent_handle::write_resume_data() const
	{
		entry ret(entry::dictionary_t);
		auto retr = std::ref(ret);
		TORRENT_SYNC_CALL(write_resume_data, retr);
		return ret;
	}

	std::string torrent_handle::save_path() const
	{
		TORRENT_SYNC_CALL_RET0(std::string, "", save_path);
		return r;
	}

	std::string torrent_handle::name() const
	{
		TORRENT_SYNC_CALL_RET0(std::string, "", name);
		return r;
	}

#endif

	void torrent_handle::connect_peer(tcp::endpoint const& adr, int source, int flags) const
	{
		TORRENT_ASYNC_CALL(add_peer, adr, source, flags);
	}

#ifndef TORRENT_NO_DEPRECATE
	void torrent_handle::force_reannounce(
		boost::posix_time::time_duration duration) const
	{
		TORRENT_ASYNC_CALL(force_tracker_request, aux::time_now()
			+ seconds(duration.total_seconds()), -1);
	}
#endif

	void torrent_handle::force_dht_announce() const
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL0(dht_announce);
#endif
	}

	void torrent_handle::force_reannounce(int s, int idx) const
	{
		TORRENT_ASYNC_CALL(force_tracker_request, aux::time_now() + seconds(s), idx);
	}

	void torrent_handle::file_status(std::vector<pool_file_status>& status) const
	{
		status.clear();

		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t || !t->has_storage()) return;
		session_impl& ses = static_cast<session_impl&>(t->session());
		ses.disk_thread().files().get_status(&status, &t->storage());
	}

	void torrent_handle::scrape_tracker(int idx) const
	{
		TORRENT_ASYNC_CALL(scrape_tracker, idx, true);
	}

	void torrent_handle::super_seeding(bool on) const
	{
		TORRENT_ASYNC_CALL(super_seeding, on);
	}

	void torrent_handle::get_full_peer_list(std::vector<peer_list_entry>& v) const
	{
		auto vp = &v;
		TORRENT_SYNC_CALL(get_full_peer_list, vp);
	}

	void torrent_handle::get_peer_info(std::vector<peer_info>& v) const
	{
		auto vp = &v;
		TORRENT_SYNC_CALL(get_peer_info, vp);
	}

	void torrent_handle::get_download_queue(std::vector<partial_piece_info>& queue) const
	{
		auto queuep = &queue;
		TORRENT_SYNC_CALL(get_download_queue, queuep);
	}

	void torrent_handle::set_piece_deadline(int index, int deadline, int flags) const
	{
		TORRENT_ASYNC_CALL(set_piece_deadline, index, deadline, flags);
	}

	void torrent_handle::reset_piece_deadline(int index) const
	{
		TORRENT_ASYNC_CALL(reset_piece_deadline, index);
	}

	void torrent_handle::clear_piece_deadlines() const
	{
		TORRENT_ASYNC_CALL0(clear_time_critical);
	}

	boost::shared_ptr<torrent> torrent_handle::native_handle() const
	{
		return m_torrent.lock();
	}

	std::size_t hash_value(torrent_status const& ts)
	{
		return hash_value(ts.handle);
	}

	std::size_t hash_value(torrent_handle const& th)
	{
		// using the locked shared_ptr value as hash doesn't work
		// for expired weak_ptrs. So, we're left with a hack
		return std::size_t(*reinterpret_cast<void* const*>(&th.m_torrent));
	}

}

