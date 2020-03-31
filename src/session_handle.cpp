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

#include "libtorrent/session_handle.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/session_call.hpp"
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/peer_class.hpp"
#include "libtorrent/peer_class_type_filter.hpp"
#include "libtorrent/aux_/scope_end.hpp"

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/lazy_entry.hpp"
#endif

using libtorrent::aux::session_impl;

namespace libtorrent {

	constexpr peer_class_t session_handle::global_peer_class_id;
	constexpr peer_class_t session_handle::tcp_peer_class_id;
	constexpr peer_class_t session_handle::local_peer_class_id;

	constexpr save_state_flags_t session_handle::save_settings;
	constexpr save_state_flags_t session_handle::save_dht_settings;
	constexpr save_state_flags_t session_handle::save_dht_state;
#if TORRENT_ABI_VERSION == 1
	constexpr save_state_flags_t session_handle::save_encryption_settings;
	constexpr save_state_flags_t session_handle::save_as_map TORRENT_DEPRECATED_ENUM;
	constexpr save_state_flags_t session_handle::save_proxy TORRENT_DEPRECATED_ENUM;
	constexpr save_state_flags_t session_handle::save_i2p_proxy TORRENT_DEPRECATED_ENUM;
	constexpr save_state_flags_t session_handle::save_dht_proxy TORRENT_DEPRECATED_ENUM;
	constexpr save_state_flags_t session_handle::save_peer_proxy TORRENT_DEPRECATED_ENUM;
	constexpr save_state_flags_t session_handle::save_web_proxy TORRENT_DEPRECATED_ENUM;
	constexpr save_state_flags_t session_handle::save_tracker_proxy TORRENT_DEPRECATED_ENUM;
#endif

	constexpr session_flags_t session_handle::add_default_plugins;
#if TORRENT_ABI_VERSION == 1
	constexpr session_flags_t session_handle::start_default_features;
#endif

	constexpr remove_flags_t session_handle::delete_files;
	constexpr remove_flags_t session_handle::delete_partfile;

	constexpr reopen_network_flags_t session_handle::reopen_map_ports;

	template <typename Fun, typename... Args>
	void session_handle::async_call(Fun f, Args&&... a) const
	{
		std::shared_ptr<session_impl> s = m_impl.lock();
		if (!s) aux::throw_ex<system_error>(errors::invalid_session_handle);
		s->get_io_service().dispatch([=]() mutable
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				(s.get()->*f)(std::forward<Args>(a)...);
#ifndef BOOST_NO_EXCEPTIONS
			} catch (system_error const& e) {
				s->alerts().emplace_alert<session_error_alert>(e.code(), e.what());
			} catch (std::exception const& e) {
				s->alerts().emplace_alert<session_error_alert>(error_code(), e.what());
			} catch (...) {
				s->alerts().emplace_alert<session_error_alert>(error_code(), "unknown error");
			}
#endif
		});
	}

	template<typename Fun, typename... Args>
	void session_handle::sync_call(Fun f, Args&&... a) const
	{
		std::shared_ptr<session_impl> s = m_impl.lock();
		if (!s) aux::throw_ex<system_error>(errors::invalid_session_handle);

		// this is the flag to indicate the call has completed
		// capture them by pointer to allow everything to be captured by value
		// and simplify the capture expression
		bool done = false;

		std::exception_ptr ex;
		s->get_io_service().dispatch([=, &done, &ex]() mutable
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				(s.get()->*f)(std::forward<Args>(a)...);
#ifndef BOOST_NO_EXCEPTIONS
			} catch (...) {
				ex = std::current_exception();
			}
#endif
			std::unique_lock<std::mutex> l(s->mut);
			done = true;
			s->cond.notify_all();
		});

		aux::torrent_wait(done, *s);
		if (ex) std::rethrow_exception(ex);
	}

	template<typename Ret, typename Fun, typename... Args>
	Ret session_handle::sync_call_ret(Fun f, Args&&... a) const
	{
		std::shared_ptr<session_impl> s = m_impl.lock();
		if (!s) aux::throw_ex<system_error>(errors::invalid_session_handle);

		// this is the flag to indicate the call has completed
		// capture them by pointer to allow everything to be captured by value
		// and simplify the capture expression
		bool done = false;
		Ret r;
		std::exception_ptr ex;
		s->get_io_service().dispatch([=, &r, &done, &ex]() mutable
		{
#ifndef BOOST_NO_EXCEPTIONS
			try {
#endif
				r = (s.get()->*f)(std::forward<Args>(a)...);
#ifndef BOOST_NO_EXCEPTIONS
			} catch (...) {
				ex = std::current_exception();
			}
#endif
			std::unique_lock<std::mutex> l(s->mut);
			done = true;
			s->cond.notify_all();
		});

		aux::torrent_wait(done, *s);
		if (ex) std::rethrow_exception(ex);
		return r;
	}

	void session_handle::save_state(entry& e, save_state_flags_t const flags) const
	{
		entry* ep = &e;
		sync_call(&session_impl::save_state, ep, flags);
	}

	void session_handle::load_state(bdecode_node const& e
		, save_state_flags_t const flags)
	{
		// this needs to be synchronized since the lifespan
		// of e is tied to the caller
		sync_call(&session_impl::load_state, &e, flags);
	}

	std::vector<torrent_status> session_handle::get_torrent_status(
		std::function<bool(torrent_status const&)> const& pred
		, status_flags_t const flags) const
	{
		std::vector<torrent_status> ret;
		sync_call(&session_impl::get_torrent_status, &ret, pred, flags);
		return ret;
	}

#if TORRENT_ABI_VERSION == 1
	void session_handle::get_torrent_status(std::vector<torrent_status>* ret
		, std::function<bool(torrent_status const&)> const& pred
		, status_flags_t const flags) const
	{
		sync_call(&session_impl::get_torrent_status, ret, pred, flags);
	}
#endif

	void session_handle::refresh_torrent_status(std::vector<torrent_status>* ret
		, status_flags_t const flags) const
	{
		sync_call(&session_impl::refresh_torrent_status, ret, flags);
	}

	void session_handle::post_torrent_updates(status_flags_t const flags)
	{
		async_call(&session_impl::post_torrent_updates, flags);
	}

	void session_handle::post_session_stats()
	{
		async_call(&session_impl::post_session_stats);
	}

	void session_handle::post_dht_stats()
	{
		async_call(&session_impl::post_dht_stats);
	}

	io_service& session_handle::get_io_service()
	{
		std::shared_ptr<session_impl> s = m_impl.lock();
		if (!s) aux::throw_ex<system_error>(errors::invalid_session_handle);
		return s->get_io_service();
	}

	torrent_handle session_handle::find_torrent(sha1_hash const& info_hash) const
	{
		return sync_call_ret<torrent_handle>(&session_impl::find_torrent_handle, info_hash);
	}

	std::vector<torrent_handle> session_handle::get_torrents() const
	{
		return sync_call_ret<std::vector<torrent_handle>>(&session_impl::get_torrents);
	}

#if TORRENT_ABI_VERSION == 1
namespace {

	void handle_backwards_compatible_resume_data(add_torrent_params& atp)
	{
		// if there's no resume data set, there's nothing to do. It's either
		// using the previous API without resume data, or the resume data has
		// already been parsed out into the add_torrent_params struct.
		if (atp.resume_data.empty()) return;

		error_code ec;
		add_torrent_params resume_data
			= read_resume_data(atp.resume_data, ec);

		resume_data.internal_resume_data_error = ec;
		if (ec) return;

		// now, merge resume_data into atp according to the merge flags
		if ((atp.flags & add_torrent_params::flag_use_resume_save_path)
			&& !resume_data.save_path.empty())
		{
			atp.save_path = std::move(resume_data.save_path);
		}

		if (!atp.ti)
		{
			atp.ti = std::move(resume_data.ti);
		}

		if (!resume_data.trackers.empty())
		{
			atp.tracker_tiers.resize(atp.trackers.size(), 0);
			atp.trackers.insert(atp.trackers.end()
				, resume_data.trackers.begin()
				, resume_data.trackers.end());
			atp.tracker_tiers.insert(atp.tracker_tiers.end()
				, resume_data.tracker_tiers.begin()
				, resume_data.tracker_tiers.end());
			if (!(resume_data.flags & add_torrent_params::flag_merge_resume_trackers))
				atp.flags |= add_torrent_params::flag_override_trackers;
		}

		if (!resume_data.url_seeds.empty())
		{
			if (!(atp.flags & add_torrent_params::flag_merge_resume_http_seeds))
				atp.url_seeds.clear();

			atp.url_seeds.insert(atp.url_seeds.end()
				, resume_data.url_seeds.begin()
				, resume_data.url_seeds.end());
			if (!(atp.flags & add_torrent_params::flag_merge_resume_http_seeds))
				atp.flags |= add_torrent_params::flag_override_web_seeds;
		}

		if (!resume_data.http_seeds.empty())
		{
			if (!(atp.flags & add_torrent_params::flag_merge_resume_http_seeds))
				atp.http_seeds.clear();

			atp.http_seeds.insert(atp.http_seeds.end()
				, resume_data.http_seeds.begin()
				, resume_data.http_seeds.end());
			if (!(atp.flags & add_torrent_params::flag_merge_resume_http_seeds))
				atp.flags |= add_torrent_params::flag_override_web_seeds;
		}

		atp.total_uploaded = resume_data.total_uploaded;
		atp.total_downloaded = resume_data.total_downloaded;
		atp.num_complete = resume_data.num_complete;
		atp.num_incomplete = resume_data.num_incomplete;
		atp.num_downloaded = resume_data.num_downloaded;
		atp.active_time = resume_data.active_time;
		atp.finished_time = resume_data.finished_time;
		atp.seeding_time = resume_data.seeding_time;

		atp.last_seen_complete = resume_data.last_seen_complete;
		atp.last_upload = resume_data.last_upload;
		atp.last_download = resume_data.last_download;
		atp.url = resume_data.url;
		atp.uuid = resume_data.uuid;

		atp.added_time = resume_data.added_time;
		atp.completed_time = resume_data.completed_time;

		atp.peers.swap(resume_data.peers);
		atp.banned_peers.swap(resume_data.banned_peers);

		atp.unfinished_pieces.swap(resume_data.unfinished_pieces);
		atp.have_pieces.swap(resume_data.have_pieces);
		atp.verified_pieces.swap(resume_data.verified_pieces);
		atp.piece_priorities.swap(resume_data.piece_priorities);

		atp.merkle_tree = std::move(resume_data.merkle_tree);

		atp.renamed_files = std::move(resume_data.renamed_files);

		if (!(atp.flags & add_torrent_params::flag_override_resume_data))
		{
			atp.download_limit = resume_data.download_limit;
			atp.upload_limit = resume_data.upload_limit;
			atp.max_connections = resume_data.max_connections;
			atp.max_uploads = resume_data.max_uploads;
			atp.trackerid = resume_data.trackerid;
			if (!resume_data.file_priorities.empty())
				atp.file_priorities = resume_data.file_priorities;

			torrent_flags_t const mask =
				add_torrent_params::flag_seed_mode
				| add_torrent_params::flag_super_seeding
				| add_torrent_params::flag_auto_managed
				| add_torrent_params::flag_sequential_download
				| add_torrent_params::flag_paused;

			atp.flags &= ~mask;
			atp.flags |= resume_data.flags & mask;
		}
		else
		{
			if (atp.file_priorities.empty())
				atp.file_priorities = resume_data.file_priorities;
		}
	}

} // anonymous namespace

#endif // TORRENT_ABI_VERSION

#ifndef BOOST_NO_EXCEPTIONS
	torrent_handle session_handle::add_torrent(add_torrent_params&& params)
	{
		TORRENT_ASSERT_PRECOND(!params.save_path.empty());

#if TORRENT_ABI_VERSION == 1
		handle_backwards_compatible_resume_data(params);
#endif
		error_code ec;
		auto ecr = std::ref(ec);
		torrent_handle r = sync_call_ret<torrent_handle>(&session_impl::add_torrent, std::move(params), ecr);
		if (ec) aux::throw_ex<system_error>(ec);
		return r;
	}

	torrent_handle session_handle::add_torrent(add_torrent_params const& params)
	{
		return add_torrent(add_torrent_params(params));
	}
#endif

	torrent_handle session_handle::add_torrent(add_torrent_params&& params, error_code& ec)
	{
		TORRENT_ASSERT_PRECOND(!params.save_path.empty());

		ec.clear();
#if TORRENT_ABI_VERSION == 1
		handle_backwards_compatible_resume_data(params);
#endif
		auto ecr = std::ref(ec);
		return sync_call_ret<torrent_handle>(&session_impl::add_torrent, std::move(params), ecr);
	}

	torrent_handle session_handle::add_torrent(add_torrent_params const& params, error_code& ec)
	{
		return add_torrent(add_torrent_params(params), ec);
	}

	void session_handle::async_add_torrent(add_torrent_params const& params)
	{
		async_add_torrent(add_torrent_params(params));
	}

	void session_handle::async_add_torrent(add_torrent_params&& params)
	{
		TORRENT_ASSERT_PRECOND(!params.save_path.empty());

		// we cannot capture a unique_ptr into a lambda in c++11, so we use a raw
		// pointer for now. async_call uses a lambda expression to post the call
		// to the main thread
		// TODO: in C++14, use unique_ptr and move it into the lambda
		auto* p = new add_torrent_params(std::move(params));
		auto guard = aux::scope_end([p]{ delete p; });
		p->save_path = complete(p->save_path);

#if TORRENT_ABI_VERSION == 1
		handle_backwards_compatible_resume_data(*p);
#endif

		async_call(&session_impl::async_add_torrent, p);
		guard.disarm();
	}

#ifndef BOOST_NO_EXCEPTIONS
#if TORRENT_ABI_VERSION == 1
	// if the torrent already exists, this will throw duplicate_torrent
	torrent_handle session_handle::add_torrent(
		torrent_info const& ti
		, std::string const& save_path
		, entry const& resume_data
		, storage_mode_t storage_mode
		, bool paused
		, storage_constructor_type sc)
	{
		add_torrent_params p(std::move(sc));
		p.ti = std::make_shared<torrent_info>(ti);
		p.save_path = save_path;
		if (resume_data.type() != entry::undefined_t)
		{
			bencode(std::back_inserter(p.resume_data), resume_data);
		}
		p.storage_mode = storage_mode;
		if (paused) p.flags |= add_torrent_params::flag_paused;
		else p.flags &= ~add_torrent_params::flag_paused;
		return add_torrent(p);
	}

	torrent_handle session_handle::add_torrent(
		char const* tracker_url
		, sha1_hash const& info_hash
		, char const* name
		, std::string const& save_path
		, entry const& resume_data
		, storage_mode_t storage_mode
		, bool paused
		, storage_constructor_type sc
		, void* userdata)
	{
		TORRENT_ASSERT_PRECOND(!save_path.empty());

		add_torrent_params p(std::move(sc));
		p.trackers.push_back(tracker_url);
		p.info_hash = info_hash;
		p.save_path = save_path;
		p.storage_mode = storage_mode;

		if (paused) p.flags |= add_torrent_params::flag_paused;
		else p.flags &= ~add_torrent_params::flag_paused;

		p.userdata = userdata;
		p.name = name;
		if (resume_data.type() != entry::undefined_t)
		{
			bencode(std::back_inserter(p.resume_data), resume_data);
		}
		return add_torrent(p);
	}
#endif // TORRENT_ABI_VERSION
#endif // BOOST_NO_EXCEPTIONS

	void session_handle::pause()
	{
		async_call(&session_impl::pause);
	}

	void session_handle::resume()
	{
		async_call(&session_impl::resume);
	}

	bool session_handle::is_paused() const
	{
		return sync_call_ret<bool>(&session_impl::is_paused);
	}

#if TORRENT_ABI_VERSION == 1
	void session_handle::set_load_function(user_load_function_t fun)
	{
		async_call(&session_impl::set_load_function, fun);
	}

	session_status session_handle::status() const
	{
		return sync_call_ret<session_status>(&session_impl::status);
	}

	void session_handle::get_cache_info(sha1_hash const& ih
		, std::vector<cached_piece_info>& ret) const
	{
		cache_status st;
		get_cache_info(&st, find_torrent(ih));
		ret.swap(st.pieces);
	}

	cache_status session_handle::get_cache_status() const
	{
		cache_status st;
		get_cache_info(&st);
		return st;
	}
#endif

	void session_handle::get_cache_info(cache_status* ret
		, torrent_handle h, int flags) const
	{
		sync_call(&session_impl::get_cache_info, h, ret, flags);
	}

#if TORRENT_ABI_VERSION == 1
	void session_handle::start_dht()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_dht, true);
		apply_settings(std::move(p));
	}

	void session_handle::stop_dht()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_dht, false);
		apply_settings(std::move(p));
	}
#endif // TORRENT_ABI_VERSION

	void session_handle::set_dht_settings(dht::dht_settings const& settings)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::set_dht_settings, settings);
#else
		TORRENT_UNUSED(settings);
#endif
	}

	dht::dht_settings session_handle::get_dht_settings() const
	{
#ifndef TORRENT_DISABLE_DHT
		return sync_call_ret<dht::dht_settings>(&session_impl::get_dht_settings);
#else
		return dht::dht_settings();
#endif
	}

	bool session_handle::is_dht_running() const
	{
#ifndef TORRENT_DISABLE_DHT
		return sync_call_ret<bool>(&session_impl::is_dht_running);
#else
		return false;
#endif
	}

	void session_handle::set_dht_storage(dht::dht_storage_constructor_type sc)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::set_dht_storage, sc);
#else
		TORRENT_UNUSED(sc);
#endif
	}

	void session_handle::add_dht_node(std::pair<std::string, int> const& node)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::add_dht_node_name, node);
#else
		TORRENT_UNUSED(node);
#endif
	}

#if TORRENT_ABI_VERSION == 1
	void session_handle::add_dht_router(std::pair<std::string, int> const& node)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::add_dht_router, node);
#else
		TORRENT_UNUSED(node);
#endif
	}
#endif // TORRENT_ABI_VERSION

	void session_handle::dht_get_item(sha1_hash const& target)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::dht_get_immutable_item, target);
#else
		TORRENT_UNUSED(target);
#endif
	}

	void session_handle::dht_get_item(std::array<char, 32> key
		, std::string salt)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::dht_get_mutable_item, key, salt);
#else
		TORRENT_UNUSED(key);
		TORRENT_UNUSED(salt);
#endif
	}

	// TODO: 3 expose the sequence_number, public_key, secret_key and signature
	// types to the client
	sha1_hash session_handle::dht_put_item(entry data)
	{
		std::vector<char> buf;
		bencode(std::back_inserter(buf), data);
		sha1_hash const ret = hasher(buf).final();

#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::dht_put_immutable_item, data, ret);
#endif
		return ret;
	}

	void session_handle::dht_put_item(std::array<char, 32> key
		, std::function<void(entry&, std::array<char,64>&
			, std::int64_t&, std::string const&)> cb
		, std::string salt)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::dht_put_mutable_item, key, cb, salt);
#else
		TORRENT_UNUSED(key);
		TORRENT_UNUSED(cb);
		TORRENT_UNUSED(salt);
#endif
	}

	void session_handle::dht_get_peers(sha1_hash const& info_hash)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::dht_get_peers, info_hash);
#else
		TORRENT_UNUSED(info_hash);
#endif
	}

	void session_handle::dht_announce(sha1_hash const& info_hash, int port
		, dht::announce_flags_t const flags)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::dht_announce, info_hash, port, flags);
#else
		TORRENT_UNUSED(info_hash);
		TORRENT_UNUSED(port);
		TORRENT_UNUSED(flags);
#endif
	}

	void session_handle::dht_live_nodes(sha1_hash const& nid)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::dht_live_nodes, nid);
#else
		TORRENT_UNUSED(nid);
#endif
	}

	void session_handle::dht_sample_infohashes(udp::endpoint const& ep, sha1_hash const& target)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::dht_sample_infohashes, ep, target);
#else
		TORRENT_UNUSED(ep);
		TORRENT_UNUSED(target);
#endif
	}

	void session_handle::dht_direct_request(udp::endpoint const& ep, entry const& e, void* userdata)
	{
#ifndef TORRENT_DISABLE_DHT
		entry copy = e;
		async_call(&session_impl::dht_direct_request, ep, copy, userdata);
#else
		TORRENT_UNUSED(ep);
		TORRENT_UNUSED(e);
		TORRENT_UNUSED(userdata);
#endif
	}

#if TORRENT_ABI_VERSION == 1
	entry session_handle::dht_state() const
	{
#ifndef TORRENT_DISABLE_DHT
		return sync_call_ret<entry>(&session_impl::dht_state);
#else
		return entry();
#endif
	}

	void session_handle::start_dht(entry const& startup_state)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::start_dht_deprecated, startup_state);
#else
		TORRENT_UNUSED(startup_state);
#endif
	}
#endif // TORRENT_ABI_VERSION

	void session_handle::add_extension(std::function<std::shared_ptr<torrent_plugin>(torrent_handle const&, void*)> ext)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		async_call(&session_impl::add_extension, ext);
#else
		TORRENT_UNUSED(ext);
#endif
	}

	void session_handle::add_extension(std::shared_ptr<plugin> ext)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		async_call(&session_impl::add_ses_extension, ext);
#else
		TORRENT_UNUSED(ext);
#endif
	}

#if TORRENT_ABI_VERSION == 1
	void session_handle::load_asnum_db(char const*) {}
	void session_handle::load_country_db(char const*) {}

	int session_handle::as_for_ip(address const&)
	{ return 0; }

	void session_handle::load_asnum_db(wchar_t const*) {}
	void session_handle::load_country_db(wchar_t const*) {}

	void session_handle::load_state(entry const& ses_state
		, save_state_flags_t const flags)
	{
		if (ses_state.type() == entry::undefined_t) return;
		std::vector<char> buf;
		bencode(std::back_inserter(buf), ses_state);
		bdecode_node e;
		error_code ec;
#if TORRENT_USE_ASSERTS || !defined BOOST_NO_EXCEPTIONS
		int ret =
#endif
		bdecode(&buf[0], &buf[0] + buf.size(), e, ec);

		TORRENT_ASSERT(ret == 0);
#ifndef BOOST_NO_EXCEPTIONS
		if (ret != 0) aux::throw_ex<system_error>(ec);
#endif
		sync_call(&session_impl::load_state, &e, flags);
	}

	entry session_handle::state() const
	{
		entry ret;
		auto retp = &ret;
		sync_call(&session_impl::save_state, retp, save_state_flags_t::all());
		return ret;
	}

	void session_handle::load_state(lazy_entry const& ses_state
		, save_state_flags_t const flags)
	{
		if (ses_state.type() == lazy_entry::none_t) return;
		std::pair<char const*, int> buf = ses_state.data_section();
		bdecode_node e;
		error_code ec;
#if TORRENT_USE_ASSERTS || !defined BOOST_NO_EXCEPTIONS
		int ret =
#endif
		bdecode(buf.first, buf.first + buf.second, e, ec);

		TORRENT_ASSERT(ret == 0);
#ifndef BOOST_NO_EXCEPTIONS
		if (ret != 0) aux::throw_ex<system_error>(ec);
#endif
		sync_call(&session_impl::load_state, &e, flags);
	}
#endif // TORRENT_ABI_VERSION

	void session_handle::set_ip_filter(ip_filter const& f)
	{
		std::shared_ptr<ip_filter> copy = std::make_shared<ip_filter>(f);
		async_call(&session_impl::set_ip_filter, copy);
	}

	ip_filter session_handle::get_ip_filter() const
	{
		return sync_call_ret<ip_filter>(&session_impl::get_ip_filter);
	}

	void session_handle::set_port_filter(port_filter const& f)
	{
		async_call(&session_impl::set_port_filter, f);
	}

#if TORRENT_ABI_VERSION == 1
	void session_handle::set_peer_id(peer_id const& id)
	{
		settings_pack p;
		p.set_str(settings_pack::peer_fingerprint, id.to_string());
		apply_settings(std::move(p));
	}

	void session_handle::set_key(std::uint32_t)
	{
		// this is just a dummy function now, as we generate the key automatically
		// per listen interface
	}

	peer_id session_handle::id() const
	{
		return sync_call_ret<peer_id>(&session_impl::deprecated_get_peer_id);
	}
#endif

	unsigned short session_handle::listen_port() const
	{
		return sync_call_ret<unsigned short, unsigned short(session_impl::*)() const>
			(&session_impl::listen_port);
	}

	unsigned short session_handle::ssl_listen_port() const
	{
		return sync_call_ret<unsigned short, unsigned short(session_impl::*)() const>
			(&session_impl::ssl_listen_port);
	}

	bool session_handle::is_listening() const
	{
		return sync_call_ret<bool>(&session_impl::is_listening);
	}

	void session_handle::set_peer_class_filter(ip_filter const& f)
	{
		async_call(&session_impl::set_peer_class_filter, f);
	}

	ip_filter session_handle::get_peer_class_filter() const
	{
		return sync_call_ret<ip_filter>(&session_impl::get_peer_class_filter);
	}

	void session_handle::set_peer_class_type_filter(peer_class_type_filter const& f)
	{
		async_call(&session_impl::set_peer_class_type_filter, f);
	}

	peer_class_type_filter session_handle::get_peer_class_type_filter() const
	{
		return sync_call_ret<peer_class_type_filter>(&session_impl::get_peer_class_type_filter);
	}

	peer_class_t session_handle::create_peer_class(char const* name)
	{
		return sync_call_ret<peer_class_t>(&session_impl::create_peer_class, name);
	}

	void session_handle::delete_peer_class(peer_class_t cid)
	{
		async_call(&session_impl::delete_peer_class, cid);
	}

	peer_class_info session_handle::get_peer_class(peer_class_t cid) const
	{
		return sync_call_ret<peer_class_info>(&session_impl::get_peer_class, cid);
	}

	void session_handle::set_peer_class(peer_class_t cid, peer_class_info const& pci)
	{
		async_call(&session_impl::set_peer_class, cid, pci);
	}

#if TORRENT_ABI_VERSION == 1
	void session_handle::use_interfaces(char const* interfaces)
	{
		settings_pack p;
		p.set_str(settings_pack::outgoing_interfaces, interfaces);
		apply_settings(std::move(p));
	}

	void session_handle::listen_on(
		std::pair<int, int> const& port_range
		, error_code& ec
		, const char* net_interface, int flags)
	{
		settings_pack p;
		std::string interfaces_str;
		if (net_interface == nullptr || strlen(net_interface) == 0)
			net_interface = "0.0.0.0";

		interfaces_str = print_endpoint(tcp::endpoint(make_address(net_interface, ec), std::uint16_t(port_range.first)));
		if (ec) return;

		p.set_str(settings_pack::listen_interfaces, interfaces_str);
		p.set_int(settings_pack::max_retry_port_bind, port_range.second - port_range.first);
		p.set_bool(settings_pack::listen_system_port_fallback, (flags & session::listen_no_system_port) == 0);
		apply_settings(std::move(p));
	}
#endif

	void session_handle::remove_torrent(const torrent_handle& h, remove_flags_t const options)
	{
		if (!h.is_valid())
#ifdef BOOST_NO_EXCEPTIONS
			return;
#else
			throw_invalid_handle();
#endif
		async_call(&session_impl::remove_torrent, h, options);
	}

#if TORRENT_ABI_VERSION == 1
	void session_handle::set_pe_settings(pe_settings const& r)
	{
		settings_pack p;
		p.set_bool(settings_pack::prefer_rc4, r.prefer_rc4);
		p.set_int(settings_pack::out_enc_policy, r.out_enc_policy);
		p.set_int(settings_pack::in_enc_policy, r.in_enc_policy);
		p.set_int(settings_pack::allowed_enc_level, r.allowed_enc_level);

		apply_settings(std::move(p));
	}

	pe_settings session_handle::get_pe_settings() const
	{
		settings_pack sett = get_settings();

		pe_settings r;
		r.prefer_rc4 = sett.get_bool(settings_pack::prefer_rc4);
		r.out_enc_policy = std::uint8_t(sett.get_int(settings_pack::out_enc_policy));
		r.in_enc_policy = std::uint8_t(sett.get_int(settings_pack::in_enc_policy));
		r.allowed_enc_level = std::uint8_t(sett.get_int(settings_pack::allowed_enc_level));
		return r;
	}
#endif

	void session_handle::apply_settings(settings_pack const& s)
	{
		TORRENT_ASSERT_PRECOND(!s.has_val(settings_pack::out_enc_policy)
			|| s.get_int(settings_pack::out_enc_policy)
				<= settings_pack::pe_disabled);
		TORRENT_ASSERT_PRECOND(!s.has_val(settings_pack::in_enc_policy)
			|| s.get_int(settings_pack::in_enc_policy)
				<= settings_pack::pe_disabled);
		TORRENT_ASSERT_PRECOND(!s.has_val(settings_pack::allowed_enc_level)
			|| s.get_int(settings_pack::allowed_enc_level)
				<= settings_pack::pe_both);

		auto copy = std::make_shared<settings_pack>(s);
		async_call(&session_impl::apply_settings_pack, copy);
	}

	void session_handle::apply_settings(settings_pack&& s)
	{
		TORRENT_ASSERT_PRECOND(!s.has_val(settings_pack::out_enc_policy)
			|| s.get_int(settings_pack::out_enc_policy)
				<= settings_pack::pe_disabled);
		TORRENT_ASSERT_PRECOND(!s.has_val(settings_pack::in_enc_policy)
			|| s.get_int(settings_pack::in_enc_policy)
				<= settings_pack::pe_disabled);
		TORRENT_ASSERT_PRECOND(!s.has_val(settings_pack::allowed_enc_level)
			|| s.get_int(settings_pack::allowed_enc_level)
				<= settings_pack::pe_both);

		auto copy = std::make_shared<settings_pack>(std::move(s));
		async_call(&session_impl::apply_settings_pack, copy);
	}

	settings_pack session_handle::get_settings() const
	{
		return sync_call_ret<settings_pack>(&session_impl::get_settings);
	}

#if TORRENT_ABI_VERSION == 1
	void session_handle::set_i2p_proxy(proxy_settings const& s)
	{
		settings_pack pack;
		pack.set_str(settings_pack::i2p_hostname, s.hostname);
		pack.set_int(settings_pack::i2p_port, s.port);

		apply_settings(pack);
	}

	proxy_settings session_handle::i2p_proxy() const
	{
		proxy_settings ret;
		settings_pack sett = get_settings();
		ret.hostname = sett.get_str(settings_pack::i2p_hostname);
		ret.port = std::uint16_t(sett.get_int(settings_pack::i2p_port));
		return ret;
	}

	void session_handle::set_proxy(proxy_settings const& s)
	{
		settings_pack p;
		p.set_str(settings_pack::proxy_hostname, s.hostname);
		p.set_str(settings_pack::proxy_username, s.username);
		p.set_str(settings_pack::proxy_password, s.password);
		p.set_int(settings_pack::proxy_type, s.type);
		p.set_int(settings_pack::proxy_port, s.port);
		p.set_bool(settings_pack::proxy_hostnames,s.proxy_hostnames);
		p.set_bool(settings_pack::proxy_peer_connections, s.proxy_peer_connections);

		apply_settings(std::move(p));
	}

	proxy_settings session_handle::proxy() const
	{
		settings_pack sett = get_settings();
		return proxy_settings(sett);
	}

	int session_handle::num_uploads() const
	{
		return sync_call_ret<int>(&session_impl::num_uploads);
	}

	int session_handle::num_connections() const
	{
		return sync_call_ret<int>(&session_impl::num_connections);
	}

	void session_handle::set_peer_proxy(proxy_settings const& s)
	{
		set_proxy(s);
	}

	void session_handle::set_web_seed_proxy(proxy_settings const&)
	{
		// NO-OP
	}

	void session_handle::set_tracker_proxy(proxy_settings const& s)
	{
		// if the tracker proxy is enabled, set the "proxy_tracker_connections"
		// setting
		settings_pack pack;
		pack.set_bool(settings_pack::proxy_tracker_connections
			, s.type != settings_pack::none);
		apply_settings(pack);
	}

	proxy_settings session_handle::peer_proxy() const
	{
		return proxy();
	}

	proxy_settings session_handle::web_seed_proxy() const
	{
		return proxy();
	}

	proxy_settings session_handle::tracker_proxy() const
	{
		settings_pack const sett = get_settings();
		return sett.get_bool(settings_pack::proxy_tracker_connections)
			? proxy_settings(sett) : proxy_settings();
	}

	void session_handle::set_dht_proxy(proxy_settings const&)
	{
		// NO-OP
	}

	proxy_settings session_handle::dht_proxy() const
	{
		return proxy();
	}

	int session_handle::upload_rate_limit() const
	{
		return sync_call_ret<int>(&session_impl::upload_rate_limit_depr);
	}

	int session_handle::download_rate_limit() const
	{
		return sync_call_ret<int>(&session_impl::download_rate_limit_depr);
	}

	int session_handle::local_upload_rate_limit() const
	{
		return sync_call_ret<int>(&session_impl::local_upload_rate_limit);
	}

	int session_handle::local_download_rate_limit() const
	{
		return sync_call_ret<int>(&session_impl::local_download_rate_limit);
	}

	int session_handle::max_half_open_connections() const { return 8; }

	void session_handle::set_local_upload_rate_limit(int bytes_per_second)
	{
		async_call(&session_impl::set_local_upload_rate_limit, bytes_per_second);
	}

	void session_handle::set_local_download_rate_limit(int bytes_per_second)
	{
		async_call(&session_impl::set_local_download_rate_limit, bytes_per_second);
	}

	void session_handle::set_upload_rate_limit(int bytes_per_second)
	{
		async_call(&session_impl::set_upload_rate_limit_depr, bytes_per_second);
	}

	void session_handle::set_download_rate_limit(int bytes_per_second)
	{
		async_call(&session_impl::set_download_rate_limit_depr, bytes_per_second);
	}

	void session_handle::set_max_connections(int limit)
	{
		async_call(&session_impl::set_max_connections, limit);
	}

	void session_handle::set_max_uploads(int limit)
	{
		async_call(&session_impl::set_max_uploads, limit);
	}

	void session_handle::set_max_half_open_connections(int) {}

	int session_handle::max_uploads() const
	{
		return sync_call_ret<int>(&session_impl::max_uploads);
	}

	int session_handle::max_connections() const
	{
		return sync_call_ret<int>(&session_impl::max_connections);
	}

#endif // TORRENT_ABI_VERSION

	// the alerts are const, they may not be deleted by the client
	void session_handle::pop_alerts(std::vector<alert*>* alerts)
	{
		std::shared_ptr<session_impl> s = m_impl.lock();
		if (!s) aux::throw_ex<system_error>(errors::invalid_session_handle);
		s->pop_alerts(alerts);
	}

	alert* session_handle::wait_for_alert(time_duration max_wait)
	{
		std::shared_ptr<session_impl> s = m_impl.lock();
		if (!s) aux::throw_ex<system_error>(errors::invalid_session_handle);
		return s->wait_for_alert(max_wait);
	}

	void session_handle::set_alert_notify(std::function<void()> const& fun)
	{
		std::shared_ptr<session_impl> s = m_impl.lock();
		if (!s) aux::throw_ex<system_error>(errors::invalid_session_handle);
		s->alerts().set_notify_function(fun);
	}

#if TORRENT_ABI_VERSION == 1
	void session_handle::set_severity_level(alert::severity_t s)
	{
		alert_category_t m = {};
		switch (s)
		{
			case alert::debug: m = alert_category::all; break;
			case alert::info: m = alert_category::all & ~(alert::debug_notification
				| alert::progress_notification | alert_category::dht); break;
			case alert::warning: m = alert_category::all & ~(alert::debug_notification
				| alert_category::status | alert::progress_notification
				| alert_category::dht); break;
			case alert::critical: m = alert_category::error | alert_category::storage; break;
			case alert::fatal: m = alert_category::error; break;
			case alert::none: m = {}; break;
		}

		settings_pack p;
		p.set_int(settings_pack::alert_mask, m);
		apply_settings(std::move(p));
	}

	size_t session_handle::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		return sync_call_ret<size_t>(&session_impl::set_alert_queue_size_limit, queue_size_limit_);
	}

	void session_handle::set_alert_mask(std::uint32_t m)
	{
		settings_pack p;
		p.set_int(settings_pack::alert_mask, int(m));
		apply_settings(std::move(p));
	}

	std::uint32_t session_handle::get_alert_mask() const
	{
		return std::uint32_t(get_settings().get_int(settings_pack::alert_mask));
	}

	void session_handle::start_lsd()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_lsd, true);
		apply_settings(std::move(p));
	}

	void session_handle::stop_lsd()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_lsd, false);
		apply_settings(std::move(p));
	}

	void session_handle::start_upnp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_upnp, true);
		apply_settings(std::move(p));
	}

	void session_handle::stop_upnp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_upnp, false);
		apply_settings(std::move(p));
	}

	void session_handle::start_natpmp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_natpmp, true);
		apply_settings(std::move(p));
	}

	void session_handle::stop_natpmp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_natpmp, false);
		apply_settings(std::move(p));
	}
#endif // TORRENT_ABI_VERSION

	std::vector<port_mapping_t> session_handle::add_port_mapping(portmap_protocol const t
		, int external_port, int local_port)
	{
		return sync_call_ret<std::vector<port_mapping_t>>(&session_impl::add_port_mapping, t, external_port, local_port);
	}

	void session_handle::delete_port_mapping(port_mapping_t handle)
	{
		async_call(&session_impl::delete_port_mapping, handle);
	}

	void session_handle::reopen_network_sockets(reopen_network_flags_t const options)
	{
		async_call(&session_impl::reopen_network_sockets, options);
	}

} // namespace libtorrent
