/*

Copyright (c) 2003-2018, Arvid Norberg
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

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/session_call.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/write_resume_data.hpp"
#include "libtorrent/torrent_flags.hpp"
#include "libtorrent/pex_flags.hpp"

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/peer_info.hpp" // for peer_list_entry
#endif

using libtorrent::aux::session_impl;

namespace libtorrent {

	constexpr resume_data_flags_t torrent_handle::flush_disk_cache;
	constexpr resume_data_flags_t torrent_handle::save_info_dict;
	constexpr resume_data_flags_t torrent_handle::only_if_modified;
	constexpr add_piece_flags_t torrent_handle::overwrite_existing;
	constexpr pause_flags_t torrent_handle::graceful_pause;
	constexpr pause_flags_t torrent_handle::clear_disk_cache;
	constexpr deadline_flags_t torrent_handle::alert_when_available;
	constexpr reannounce_flags_t torrent_handle::ignore_min_interval;

	constexpr status_flags_t torrent_handle::query_distributed_copies;
	constexpr status_flags_t torrent_handle::query_accurate_download_counters;
	constexpr status_flags_t torrent_handle::query_last_seen_complete;
	constexpr status_flags_t torrent_handle::query_pieces;
	constexpr status_flags_t torrent_handle::query_verified_pieces;
	constexpr status_flags_t torrent_handle::query_torrent_file;
	constexpr status_flags_t torrent_handle::query_name;
	constexpr status_flags_t torrent_handle::query_save_path;

#ifndef BOOST_NO_EXCEPTIONS
	[[noreturn]] void throw_invalid_handle()
	{
		throw system_error(errors::invalid_torrent_handle);
	}
#endif

	template<typename Fun, typename... Args>
	void torrent_handle::async_call(Fun f, Args&&... a) const
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) aux::throw_ex<system_error>(errors::invalid_torrent_handle);
		auto& ses = static_cast<session_impl&>(t->session());
		ses.get_io_service().dispatch([=,&ses] ()
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				(t.get()->*f)(a...);
#ifndef BOOST_NO_EXCEPTIONS
			} catch (system_error const& e) {
				ses.alerts().emplace_alert<torrent_error_alert>(torrent_handle(m_torrent)
					, e.code(), e.what());
			} catch (std::exception const& e) {
				ses.alerts().emplace_alert<torrent_error_alert>(torrent_handle(m_torrent)
					, error_code(), e.what());
			} catch (...) {
				ses.alerts().emplace_alert<torrent_error_alert>(torrent_handle(m_torrent)
					, error_code(), "unknown error");
			}
#endif
		} );
	}

	template<typename Fun, typename... Args>
	void torrent_handle::sync_call(Fun f, Args&&... a) const
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t) aux::throw_ex<system_error>(errors::invalid_torrent_handle);
		auto& ses = static_cast<session_impl&>(t->session());

		// this is the flag to indicate the call has completed
		bool done = false;

		std::exception_ptr ex;
		ses.get_io_service().dispatch([=,&done,&ses,&ex] ()
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				(t.get()->*f)(a...);
#ifndef BOOST_NO_EXCEPTIONS
			} catch (...) {
				ex = std::current_exception();
			}
#endif
			std::unique_lock<std::mutex> l(ses.mut);
			done = true;
			ses.cond.notify_all();
		} );

		aux::torrent_wait(done, ses);
		if (ex) std::rethrow_exception(ex);
	}

	template<typename Ret, typename Fun, typename... Args>
	Ret torrent_handle::sync_call_ret(Ret def, Fun f, Args&&... a) const
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		Ret r = def;
#ifndef BOOST_NO_EXCEPTIONS
		if (!t) throw_invalid_handle();
#else
		if (!t) return r;
#endif
		auto& ses = static_cast<session_impl&>(t->session());

		// this is the flag to indicate the call has completed
		bool done = false;

		std::exception_ptr ex;
		ses.get_io_service().dispatch([=,&r,&done,&ses,&ex] ()
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				r = (t.get()->*f)(a...);
#ifndef BOOST_NO_EXCEPTIONS
			} catch (...) {
				ex = std::current_exception();
			}
#endif
			std::unique_lock<std::mutex> l(ses.mut);
			done = true;
			ses.cond.notify_all();
		} );

		aux::torrent_wait(done, ses);

		if (ex) std::rethrow_exception(ex);
		return r;
	}

	sha1_hash torrent_handle::info_hash() const
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		return t ? t->info_hash() : sha1_hash();
	}

	int torrent_handle::max_uploads() const
	{
		return sync_call_ret<int>(0, &torrent::max_uploads);
	}

	void torrent_handle::set_max_uploads(int max_uploads) const
	{
		TORRENT_ASSERT_PRECOND(max_uploads >= 2 || max_uploads == -1);
		async_call(&torrent::set_max_uploads, max_uploads, true);
	}

	int torrent_handle::max_connections() const
	{
		return sync_call_ret<int>(0, &torrent::max_connections);
	}

	void torrent_handle::set_max_connections(int max_connections) const
	{
		TORRENT_ASSERT_PRECOND(max_connections >= 2 || max_connections == -1);
		async_call(&torrent::set_max_connections, max_connections, true);
	}

	void torrent_handle::set_upload_limit(int limit) const
	{
		TORRENT_ASSERT_PRECOND(limit >= -1);
		async_call(&torrent::set_upload_limit, limit);
	}

	int torrent_handle::upload_limit() const
	{
		return sync_call_ret<int>(0, &torrent::upload_limit);
	}

	void torrent_handle::set_download_limit(int limit) const
	{
		TORRENT_ASSERT_PRECOND(limit >= -1);
		async_call(&torrent::set_download_limit, limit);
	}

	int torrent_handle::download_limit() const
	{
		return sync_call_ret<int>(0, &torrent::download_limit);
	}

	void torrent_handle::move_storage(std::string const& save_path, move_flags_t flags) const
	{
		async_call(&torrent::move_storage, save_path, flags);
	}

#if TORRENT_ABI_VERSION == 1
	void torrent_handle::move_storage(
		std::string const& save_path, int const flags) const
	{
		async_call(&torrent::move_storage, save_path, static_cast<move_flags_t>(flags));
	}

	void torrent_handle::move_storage(
		std::wstring const& save_path, int flags) const
	{
		async_call(&torrent::move_storage, wchar_utf8(save_path), static_cast<move_flags_t>(flags));
	}

	void torrent_handle::rename_file(file_index_t index, std::wstring const& new_name) const
	{
		async_call(&torrent::rename_file, index, wchar_utf8(new_name));
	}
#endif // TORRENT_ABI_VERSION

	void torrent_handle::rename_file(file_index_t index, std::string const& new_name) const
	{
		async_call(&torrent::rename_file, index, new_name);
	}

	void torrent_handle::add_extension(
		std::function<std::shared_ptr<torrent_plugin>(torrent_handle const&, void*)> const& ext
		, void* userdata)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		async_call(&torrent::add_extension_fun, ext, userdata);
#else
		TORRENT_UNUSED(ext);
		TORRENT_UNUSED(userdata);
#endif
	}

	bool torrent_handle::set_metadata(span<char const> metadata) const
	{
		return sync_call_ret<bool>(false, &torrent::set_metadata, metadata);
	}

	void torrent_handle::pause(pause_flags_t const flags) const
	{
		async_call(&torrent::pause, flags & graceful_pause);
	}

	torrent_flags_t torrent_handle::flags() const
	{
		return sync_call_ret<torrent_flags_t>(torrent_flags_t{}, &torrent::flags);
	}

	void torrent_handle::set_flags(torrent_flags_t const flags
		, torrent_flags_t const mask) const
	{
		async_call(&torrent::set_flags, flags, mask);
	}

	void torrent_handle::set_flags(torrent_flags_t const flags) const
	{
		async_call(&torrent::set_flags, torrent_flags::all, flags);
	}

	void torrent_handle::unset_flags(torrent_flags_t const flags) const
	{
		async_call(&torrent::set_flags, torrent_flags_t{}, flags);
	}

#if TORRENT_ABI_VERSION == 1
	void torrent_handle::stop_when_ready(bool b) const
	{ async_call(&torrent::stop_when_ready, b); }

	void torrent_handle::set_upload_mode(bool b) const
	{ async_call(&torrent::set_upload_mode, b); }

	void torrent_handle::set_share_mode(bool b) const
	{
		TORRENT_UNUSED(b);
#ifndef TORRENT_DISABLE_SHARE_MODE
		async_call(&torrent::set_share_mode, b);
#endif
	}

	void torrent_handle::apply_ip_filter(bool b) const
	{ async_call(&torrent::set_apply_ip_filter, b); }

	void torrent_handle::auto_managed(bool m) const
	{ async_call(&torrent::auto_managed, m); }

	void torrent_handle::set_pinned(bool) const {}

	void torrent_handle::set_sequential_download(bool sd) const
	{ async_call(&torrent::set_sequential_download, sd); }
#endif

	void torrent_handle::flush_cache() const
	{
		async_call(&torrent::flush_cache);
	}

	void torrent_handle::set_ssl_certificate(
		std::string const& certificate
		, std::string const& private_key
		, std::string const& dh_params
		, std::string const& passphrase)
	{
#ifdef TORRENT_USE_OPENSSL
		async_call(&torrent::set_ssl_cert, certificate, private_key, dh_params, passphrase);
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
		async_call(&torrent::set_ssl_cert_buffer, certificate, private_key, dh_params);
#else
		TORRENT_UNUSED(certificate);
		TORRENT_UNUSED(private_key);
		TORRENT_UNUSED(dh_params);
#endif
	}

	void torrent_handle::save_resume_data(resume_data_flags_t f) const
	{
		async_call(&torrent::save_resume_data, f);
	}

	bool torrent_handle::need_save_resume_data() const
	{
		return sync_call_ret<bool>(false, &torrent::need_save_resume_data);
	}

	void torrent_handle::force_recheck() const
	{
		async_call(&torrent::force_recheck);
	}

	void torrent_handle::resume() const
	{
		async_call(&torrent::resume);
	}

	queue_position_t torrent_handle::queue_position() const
	{
		return sync_call_ret<queue_position_t>(no_pos
			, &torrent::queue_position);
	}

	void torrent_handle::queue_position_up() const
	{
		async_call(&torrent::queue_up);
	}

	void torrent_handle::queue_position_down() const
	{
		async_call(&torrent::queue_down);
	}

	void torrent_handle::queue_position_set(queue_position_t const p) const
	{
		TORRENT_ASSERT_PRECOND(p >= queue_position_t{});
		if (p < queue_position_t{}) return;
		async_call(&torrent::set_queue_position, p);
	}

	void torrent_handle::queue_position_top() const
	{
		async_call(&torrent::set_queue_position, queue_position_t{});
	}

	void torrent_handle::queue_position_bottom() const
	{
		async_call(&torrent::set_queue_position, last_pos);
	}

	void torrent_handle::clear_error() const
	{
		async_call(&torrent::clear_error);
	}

#if TORRENT_ABI_VERSION == 1
	void torrent_handle::set_priority(int const p) const
	{
		async_call(&torrent::set_priority, p);
	}

	void torrent_handle::set_tracker_login(std::string const& name
		, std::string const& password) const
	{
		async_call(&torrent::set_tracker_login, name, password);
	}
#endif

	void torrent_handle::file_progress(std::vector<std::int64_t>& progress, int flags) const
	{
		auto& arg = static_cast<aux::vector<std::int64_t, file_index_t>&>(progress);
		sync_call(&torrent::file_progress, std::ref(arg), flags);
	}

	torrent_status torrent_handle::status(status_flags_t const flags) const
	{
		torrent_status st;
		sync_call(&torrent::status, &st, flags);
		return st;
	}

	void torrent_handle::piece_availability(std::vector<int>& avail) const
	{
		auto availr = std::ref(static_cast<aux::vector<int, piece_index_t>&>(avail));
		sync_call(&torrent::piece_availability, availr);
	}

	void torrent_handle::piece_priority(piece_index_t index, download_priority_t priority) const
	{
		async_call(&torrent::set_piece_priority, index, priority);
	}

	download_priority_t torrent_handle::piece_priority(piece_index_t index) const
	{
		return sync_call_ret<download_priority_t>(dont_download, &torrent::piece_priority, index);
	}

	void torrent_handle::prioritize_pieces(std::vector<download_priority_t> const& pieces) const
	{
		async_call(&torrent::prioritize_pieces
			, static_cast<aux::vector<download_priority_t, piece_index_t> const&>(pieces));
	}

	void torrent_handle::prioritize_pieces(std::vector<std::pair<piece_index_t
		, download_priority_t>> const& pieces) const
	{
		async_call(&torrent::prioritize_piece_list, pieces);
	}

	std::vector<download_priority_t> torrent_handle::get_piece_priorities() const
	{
		aux::vector<download_priority_t, piece_index_t> ret;
		auto retp = &ret;
		sync_call(&torrent::piece_priorities, retp);
		return std::move(ret);
	}

#if TORRENT_ABI_VERSION == 1
	void torrent_handle::prioritize_pieces(std::vector<int> const& pieces) const
	{
		aux::vector<download_priority_t, piece_index_t> p;
		p.reserve(pieces.size());
		for (auto const prio : pieces) {
			p.push_back(download_priority_t(static_cast<std::uint8_t>(prio)));
		}
		async_call(&torrent::prioritize_pieces, p);
	}

	void torrent_handle::prioritize_pieces(std::vector<std::pair<piece_index_t, int>> const& pieces) const
	{
		std::vector<std::pair<piece_index_t, download_priority_t>> p;
		p.reserve(pieces.size());
		async_call(&torrent::prioritize_piece_list, std::move(p));
	}

	std::vector<int> torrent_handle::piece_priorities() const
	{
		aux::vector<download_priority_t, piece_index_t> prio;
		auto retp = &prio;
		sync_call(&torrent::piece_priorities, retp);
		std::vector<int> ret;
		ret.reserve(prio.size());
		for (auto p : prio)
			ret.push_back(int(static_cast<std::uint8_t>(p)));
		return ret;
	}
#endif

	void torrent_handle::file_priority(file_index_t index, download_priority_t priority) const
	{
		async_call(&torrent::set_file_priority, index, priority);
	}

	download_priority_t torrent_handle::file_priority(file_index_t index) const
	{
		return sync_call_ret<download_priority_t>(dont_download, &torrent::file_priority, index);
	}

	// TODO: support moving files into this call
	void torrent_handle::prioritize_files(std::vector<download_priority_t> const& files) const
	{
		async_call(&torrent::prioritize_files
			, static_cast<aux::vector<download_priority_t, file_index_t> const&>(files));
	}

	std::vector<download_priority_t> torrent_handle::get_file_priorities() const
	{
		aux::vector<download_priority_t, file_index_t> ret;
		auto retp = &ret;
		sync_call(&torrent::file_priorities, retp);
		return std::move(ret);
	}

#if TORRENT_ABI_VERSION == 1

// ============ start deprecation ===============

	void torrent_handle::prioritize_files(std::vector<int> const& files) const
	{
		aux::vector<download_priority_t, file_index_t> file_prio;
		file_prio.reserve(files.size());
		for (auto const p : files) {
			file_prio.push_back(download_priority_t(static_cast<std::uint8_t>(p)));
		}
		async_call(&torrent::prioritize_files, file_prio);
	}

	std::vector<int> torrent_handle::file_priorities() const
	{
		aux::vector<download_priority_t, file_index_t> prio;
		auto retp = &prio;
		sync_call(&torrent::file_priorities, retp);
		std::vector<int> ret;
		ret.reserve(prio.size());
		for (auto p : prio)
			ret.push_back(int(static_cast<std::uint8_t>(p)));
		return ret;
	}


	int torrent_handle::get_peer_upload_limit(tcp::endpoint) const { return -1; }
	int torrent_handle::get_peer_download_limit(tcp::endpoint) const { return -1; }
	void torrent_handle::set_peer_upload_limit(tcp::endpoint, int /* limit */) const {}
	void torrent_handle::set_peer_download_limit(tcp::endpoint, int /* limit */) const {}
	void torrent_handle::set_ratio(float) const {}
	void torrent_handle::use_interface(const char* net_interface) const
	{
		async_call(&torrent::use_interface, std::string(net_interface));
	}

#if !TORRENT_NO_FPU
	void torrent_handle::file_progress(std::vector<float>& progress) const
	{
		sync_call(&torrent::file_progress_float, std::ref(static_cast<aux::vector<float, file_index_t>&>(progress)));
	}
#endif

	bool torrent_handle::is_seed() const
	{ return sync_call_ret<bool>(false, &torrent::is_seed); }

	bool torrent_handle::is_finished() const
	{ return sync_call_ret<bool>(false, &torrent::is_finished); }

	bool torrent_handle::is_paused() const
	{ return sync_call_ret<bool>(false, &torrent::is_torrent_paused); }

	bool torrent_handle::is_sequential_download() const
	{ return sync_call_ret<bool>(false, &torrent::is_sequential_download); }

	bool torrent_handle::is_auto_managed() const
	{ return sync_call_ret<bool>(false, &torrent::is_auto_managed); }

	bool torrent_handle::has_metadata() const
	{ return sync_call_ret<bool>(false, &torrent::valid_metadata); }

	bool torrent_handle::super_seeding() const
	{
#ifndef TORRENT_DISABLE_SUPERSEEDING
		return sync_call_ret<bool>(false, &torrent::super_seeding);
#else
		return false;
#endif
	}

// ============ end deprecation ===============
#endif

	std::vector<announce_entry> torrent_handle::trackers() const
	{
		static const std::vector<announce_entry> empty;
		return sync_call_ret<std::vector<announce_entry>>(empty, &torrent::trackers);
	}

	void torrent_handle::add_url_seed(std::string const& url) const
	{
		async_call(&torrent::add_web_seed, url, web_seed_entry::url_seed
			, std::string(), web_seed_entry::headers_t(), web_seed_flag_t{});
	}

	void torrent_handle::remove_url_seed(std::string const& url) const
	{
		async_call(&torrent::remove_web_seed, url, web_seed_entry::url_seed);
	}

	std::set<std::string> torrent_handle::url_seeds() const
	{
		static const std::set<std::string> empty;
		return sync_call_ret<std::set<std::string>>(empty, &torrent::web_seeds, web_seed_entry::url_seed);
	}

	void torrent_handle::add_http_seed(std::string const& url) const
	{
		async_call(&torrent::add_web_seed, url, web_seed_entry::http_seed
			, std::string(), web_seed_entry::headers_t(), web_seed_flag_t{});
	}

	void torrent_handle::remove_http_seed(std::string const& url) const
	{
		async_call(&torrent::remove_web_seed, url, web_seed_entry::http_seed);
	}

	std::set<std::string> torrent_handle::http_seeds() const
	{
		static const std::set<std::string> empty;
		return sync_call_ret<std::set<std::string>>(empty, &torrent::web_seeds, web_seed_entry::http_seed);
	}

	void torrent_handle::replace_trackers(
		std::vector<announce_entry> const& urls) const
	{
		async_call(&torrent::replace_trackers, urls);
	}

	void torrent_handle::add_tracker(announce_entry const& url) const
	{
		async_call(&torrent::add_tracker, url);
	}

	void torrent_handle::add_piece(piece_index_t piece, char const* data, add_piece_flags_t const flags) const
	{
		sync_call(&torrent::add_piece, piece, data, flags);
	}

	void torrent_handle::read_piece(piece_index_t piece) const
	{
		async_call(&torrent::read_piece, piece);
	}

	bool torrent_handle::have_piece(piece_index_t piece) const
	{
		return sync_call_ret<bool>(false, &torrent::user_have_piece, piece);
	}

	storage_interface* torrent_handle::get_storage_impl() const
	{
		return sync_call_ret<storage_interface*>(nullptr, &torrent::get_storage_impl);
	}

	bool torrent_handle::is_valid() const
	{
		return !m_torrent.expired();
	}

	std::shared_ptr<const torrent_info> torrent_handle::torrent_file() const
	{
		return sync_call_ret<std::shared_ptr<const torrent_info>>(
			std::shared_ptr<const torrent_info>(), &torrent::get_torrent_copy);
	}

#if TORRENT_ABI_VERSION == 1
	// this function should either be removed, or return
	// reference counted handle to the torrent_info which
	// forces the torrent to stay loaded while the client holds it
	torrent_info const& torrent_handle::get_torrent_info() const
	{
		static aux::array<std::shared_ptr<const torrent_info>, 4> holder;
		static int cursor = 0;
		static std::mutex holder_mutex;

		std::shared_ptr<const torrent_info> r = torrent_file();

		std::lock_guard<std::mutex> l(holder_mutex);
		holder[cursor++] = r;
		cursor = cursor % holder.end_index();
		return *r;
	}

	entry torrent_handle::write_resume_data() const
	{
		add_torrent_params params;
		auto retr = std::ref(params);
		sync_call(&torrent::write_resume_data, retr);
		return libtorrent::write_resume_data(params);
	}

	std::string torrent_handle::save_path() const
	{
		return sync_call_ret<std::string>("", &torrent::save_path);
	}

	std::string torrent_handle::name() const
	{
		return sync_call_ret<std::string>("", &torrent::name);
	}

#endif

	void torrent_handle::connect_peer(tcp::endpoint const& adr
		, peer_source_flags_t const source, pex_flags_t const flags) const
	{
		async_call(&torrent::add_peer, adr, source, flags);
	}

#if TORRENT_ABI_VERSION == 1
	void torrent_handle::force_reannounce(
		boost::posix_time::time_duration duration) const
	{
		async_call(&torrent::force_tracker_request, aux::time_now()
			+ seconds(duration.total_seconds()), -1, reannounce_flags_t{});
	}

	void torrent_handle::file_status(std::vector<open_file_state>& status) const
	{
		status.clear();

		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t || !t->has_storage()) return;
		auto& ses = static_cast<session_impl&>(t->session());
		status = ses.disk_thread().get_status(t->storage());
	}
#endif

	void torrent_handle::force_dht_announce() const
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&torrent::dht_announce);
#endif
	}

	void torrent_handle::force_reannounce(int s, int idx, reannounce_flags_t const flags) const
	{
		async_call(&torrent::force_tracker_request, aux::time_now() + seconds(s), idx, flags);
	}

	std::vector<open_file_state> torrent_handle::file_status() const
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t || !t->has_storage()) return {};
		auto& ses = static_cast<session_impl&>(t->session());
		return ses.disk_thread().get_status(t->storage());
	}

	void torrent_handle::scrape_tracker(int idx) const
	{
		async_call(&torrent::scrape_tracker, idx, true);
	}

#if TORRENT_ABI_VERSION == 1
	void torrent_handle::super_seeding(bool on) const
	{
		TORRENT_UNUSED(on);
#ifndef TORRENT_DISABLE_SUPERSEEDING
		async_call(&torrent::set_super_seeding, on);
#endif
	}

	void torrent_handle::get_full_peer_list(std::vector<peer_list_entry>& v) const
	{
		auto vp = &v;
		sync_call(&torrent::get_full_peer_list, vp);
	}
#endif

	void torrent_handle::get_peer_info(std::vector<peer_info>& v) const
	{
		auto vp = &v;
		sync_call(&torrent::get_peer_info, vp);
	}

	void torrent_handle::get_download_queue(std::vector<partial_piece_info>& queue) const
	{
		auto queuep = &queue;
		sync_call(&torrent::get_download_queue, queuep);
	}

	void torrent_handle::set_piece_deadline(piece_index_t index, int deadline
		, deadline_flags_t const flags) const
	{
#ifndef TORRENT_DISABLE_STREAMING
		async_call(&torrent::set_piece_deadline, index, deadline, flags);
#else
		TORRENT_UNUSED(deadline);
		if (flags & alert_when_available)
			async_call(&torrent::read_piece, index);
#endif
	}

	void torrent_handle::reset_piece_deadline(piece_index_t index) const
	{
#ifndef TORRENT_DISABLE_STREAMING
		async_call(&torrent::reset_piece_deadline, index);
#else
		TORRENT_UNUSED(index);
#endif
	}

	void torrent_handle::clear_piece_deadlines() const
	{
#ifndef TORRENT_DISABLE_STREAMING
		async_call(&torrent::clear_time_critical);
#endif
	}

	std::shared_ptr<torrent> torrent_handle::native_handle() const
	{
		return m_torrent.lock();
	}

	std::size_t hash_value(torrent_handle const& th)
	{
		// using the locked shared_ptr value as hash doesn't work
		// for expired weak_ptrs. So, we're left with a hack
		return std::size_t(*reinterpret_cast<void* const*>(&th.m_torrent));
	}

	static_assert(std::is_nothrow_move_constructible<torrent_handle>::value
		, "should be nothrow move constructible");
	static_assert(std::is_nothrow_move_assignable<torrent_handle>::value
		, "should be nothrow move assignable");
	static_assert(std::is_nothrow_default_constructible<torrent_handle>::value
		, "should be nothrow default constructible");
}
