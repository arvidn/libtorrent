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

#include "libtorrent/session_handle.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/session_call.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/lazy_entry.hpp"

#ifndef TORRENT_NO_DEPRECATE
#include "libtorrent/read_resume_data.hpp"
#endif

using libtorrent::aux::session_impl;

namespace libtorrent
{

	template <typename Fun, typename... Args>
	void session_handle::async_call(Fun f, Args&&... a) const
	{
		m_impl->get_io_service().dispatch([=]() mutable
		{ (m_impl->*f)(a...); });
	}

	template<typename Fun, typename... Args>
	void session_handle::sync_call(Fun f, Args&&... a) const
	{
		// this is the flag to indicate the call has completed
		// capture them by pointer to allow everything to be captured by value
		// and simplify the capture expression
		bool done = false;

		m_impl->get_io_service().dispatch([=,&done]() mutable
		{
			(m_impl->*f)(a...);
			std::unique_lock<std::mutex> l(m_impl->mut);
			done = true;
			m_impl->cond.notify_all();
		});

		aux::torrent_wait(done, *m_impl);
	}

	template<typename Ret, typename Fun, typename... Args>
	Ret session_handle::sync_call_ret(Fun f, Args&&... a) const
	{
		// this is the flag to indicate the call has completed
		// capture them by pointer to allow everything to be captured by value
		// and simplify the capture expression
		bool done = false;
		Ret r;
		m_impl->get_io_service().dispatch([=,&r,&done]() mutable
		{
			r = (m_impl->*f)(a...);
			std::unique_lock<std::mutex> l(m_impl->mut);
			done = true;
			m_impl->cond.notify_all();
		});

		aux::torrent_wait(done, *m_impl);
		return r;
	}

	void session_handle::save_state(entry& e, std::uint32_t flags) const
	{
		entry* ep = &e;
		sync_call(&session_impl::save_state, ep, flags);
	}

	void session_handle::load_state(bdecode_node const& e
		, std::uint32_t const flags)
	{
		// this needs to be synchronized since the lifespan
		// of e is tied to the caller
		sync_call(&session_impl::load_state, &e, flags);
	}

	void session_handle::get_torrent_status(std::vector<torrent_status>* ret
		, boost::function<bool(torrent_status const&)> const& pred
		, std::uint32_t flags) const
	{
		auto predr = std::ref(pred);
		sync_call(&session_impl::get_torrent_status, ret, predr, flags);
	}

	void session_handle::refresh_torrent_status(std::vector<torrent_status>* ret
		, std::uint32_t flags) const
	{
		sync_call(&session_impl::refresh_torrent_status, ret, flags);
	}

	void session_handle::post_torrent_updates(std::uint32_t flags)
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
		return m_impl->get_io_service();
	}

	torrent_handle session_handle::find_torrent(sha1_hash const& info_hash) const
	{
		return sync_call_ret<torrent_handle>(&session_impl::find_torrent_handle, info_hash);
	}

	std::vector<torrent_handle> session_handle::get_torrents() const
	{
		return sync_call_ret<std::vector<torrent_handle>>(&session_impl::get_torrents);
	}

#ifndef TORRENT_NO_DEPRECATE
	namespace
	{
		void handle_backwards_compatible_resume_data(add_torrent_params& atp)
		{
			// if there's no resume data set, there's nothing to do. It's either
			// using the previous API without resume data, or the resume data has
			// already been parsed out into the add_torrent_params struct.
			if (atp.resume_data.empty()) return;

			error_code ec;
			add_torrent_params resume_data
				= read_resume_data(&atp.resume_data[0], int(atp.resume_data.size()), ec);

			resume_data.internal_resume_data_error = ec;
			if (ec) return;

			// now, merge resume_data into atp according to the merge flags
			if (atp.flags & add_torrent_params::flag_use_resume_save_path
				&& !resume_data.save_path.empty())
			{
				atp.save_path = resume_data.save_path;
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
				if ((resume_data.flags & add_torrent_params::flag_merge_resume_trackers) == 0)
					atp.flags |= add_torrent_params::flag_override_trackers;
			}

			if (!resume_data.url_seeds.empty())
			{
				if ((atp.flags & add_torrent_params::flag_merge_resume_http_seeds) == 0)
					atp.url_seeds.clear();

				atp.url_seeds.insert(atp.url_seeds.end()
					, resume_data.url_seeds.begin()
					, resume_data.url_seeds.end());
				if ((atp.flags & add_torrent_params::flag_merge_resume_http_seeds) == 0)
					atp.flags |= add_torrent_params::flag_override_web_seeds;
			}

			if (!resume_data.http_seeds.empty())
			{
				if ((atp.flags & add_torrent_params::flag_merge_resume_http_seeds) == 0)
					atp.http_seeds.clear();

				atp.http_seeds.insert(atp.http_seeds.end()
					, resume_data.http_seeds.begin()
					, resume_data.http_seeds.end());
				if ((atp.flags & add_torrent_params::flag_merge_resume_http_seeds) == 0)
					atp.flags |= add_torrent_params::flag_override_web_seeds;
			}

			atp.total_uploaded = resume_data.total_uploaded;
			atp.total_downloaded = resume_data.total_downloaded;
			atp.num_complete = resume_data.num_complete;
			atp.num_incomplete = resume_data.num_incomplete;
			atp.num_downloaded = resume_data.num_downloaded;
			atp.total_uploaded = resume_data.total_uploaded;
			atp.total_downloaded = resume_data.total_downloaded;
			atp.active_time = resume_data.active_time;
			atp.finished_time = resume_data.finished_time;
			atp.seeding_time = resume_data.seeding_time;

			atp.last_seen_complete = resume_data.last_seen_complete;
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

			atp.merkle_tree.swap(resume_data.merkle_tree);

			atp.renamed_files.swap(resume_data.renamed_files);

			if ((atp.flags & add_torrent_params::flag_override_resume_data) == 0)
			{
				atp.download_limit = resume_data.download_limit;
				atp.upload_limit = resume_data.upload_limit;
				atp.max_connections = resume_data.max_connections;
				atp.max_uploads = resume_data.max_uploads;
				atp.trackerid = resume_data.trackerid;
				if (!resume_data.file_priorities.empty())
					atp.file_priorities = resume_data.file_priorities;

				std::uint64_t const mask =
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
	}
#endif

#ifndef BOOST_NO_EXCEPTIONS
	torrent_handle session_handle::add_torrent(add_torrent_params const& params)
	{
		TORRENT_ASSERT_PRECOND(!params.save_path.empty());

#ifndef TORRENT_NO_DEPRECATE
		add_torrent_params p = params;
		handle_backwards_compatible_resume_data(p);
#else
		add_torrent_params const& p = params;
#endif
		error_code ec;
		auto ecr = std::ref(ec);
		torrent_handle r = sync_call_ret<torrent_handle>(&session_impl::add_torrent, p, ecr);
		if (ec) throw system_error(ec);
		return r;
	}
#endif

	torrent_handle session_handle::add_torrent(add_torrent_params const& params, error_code& ec)
	{
		TORRENT_ASSERT_PRECOND(!params.save_path.empty());

		ec.clear();
#ifndef TORRENT_NO_DEPRECATE
		add_torrent_params p = params;
		handle_backwards_compatible_resume_data(p);
#else
		add_torrent_params const& p = params;
#endif
		auto ecr = std::ref(ec);
		return sync_call_ret<torrent_handle>(&session_impl::add_torrent, p, ecr);
	}

	void session_handle::async_add_torrent(add_torrent_params const& params)
	{
		TORRENT_ASSERT_PRECOND(!params.save_path.empty());

		add_torrent_params* p = new add_torrent_params(params);

#ifndef TORRENT_NO_DEPRECATE
		handle_backwards_compatible_resume_data(*p);
#endif

		async_call(&session_impl::async_add_torrent, p);
	}

#ifndef BOOST_NO_EXCEPTIONS
#ifndef TORRENT_NO_DEPRECATE
	// if the torrent already exists, this will throw duplicate_torrent
	torrent_handle session_handle::add_torrent(
		torrent_info const& ti
		, std::string const& save_path
		, entry const& resume_data
		, storage_mode_t storage_mode
		, bool paused
		, storage_constructor_type sc)
	{
		boost::shared_ptr<torrent_info> tip(boost::make_shared<torrent_info>(ti));
		add_torrent_params p(sc);
		p.ti = tip;
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

		add_torrent_params p(sc);
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
#endif // TORRENT_NO_DEPRECATE
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

	void session_handle::set_load_function(user_load_function_t fun)
	{
		async_call(&session_impl::set_load_function, fun);
	}

#ifndef TORRENT_NO_DEPRECATE
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
		piece_manager* st = nullptr;
		boost::shared_ptr<torrent> t = h.m_torrent.lock();
		if (t)
		{
			if (t->has_storage())
				st = &t->storage();
			else
				flags = session::disk_cache_no_pieces;
		}
		m_impl->disk_thread().get_cache_info(ret, flags & session::disk_cache_no_pieces, st);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_handle::start_dht()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_dht, true);
		apply_settings(p);
	}

	void session_handle::stop_dht()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_dht, false);
		apply_settings(p);
	}
#endif // TORRENT_NO_DEPRECATE

	void session_handle::set_dht_settings(dht_settings const& settings)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::set_dht_settings, settings);
#else
		TORRENT_UNUSED(settings);
#endif
	}

	dht_settings session_handle::get_dht_settings() const
	{
#ifndef TORRENT_DISABLE_DHT
		return sync_call_ret<dht_settings>(&session_impl::get_dht_settings);
#else
		return dht_settings();
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

	void session_handle::add_dht_router(std::pair<std::string, int> const& node)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::add_dht_router, node);
#else
		TORRENT_UNUSED(node);
#endif
	}

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
		, boost::function<void(entry&, std::array<char,64>&
		, std::uint64_t&, std::string const&)> cb
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

	void session_handle::dht_announce(sha1_hash const& info_hash, int port, int flags)
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&session_impl::dht_announce, info_hash, port, flags);
#else
		TORRENT_UNUSED(info_hash);
		TORRENT_UNUSED(port);
		TORRENT_UNUSED(flags);
#endif
	}

	void session_handle::dht_direct_request(udp::endpoint ep, entry const& e, void* userdata)
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

#ifndef TORRENT_NO_DEPRECATE
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
#endif // TORRENT_NO_DEPRECATE

	void session_handle::add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent_handle const&, void*)> ext)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		async_call(&session_impl::add_extension, ext);
#else
		TORRENT_UNUSED(ext);
#endif
	}

	void session_handle::add_extension(boost::shared_ptr<plugin> ext)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		async_call(&session_impl::add_ses_extension, ext);
#else
		TORRENT_UNUSED(ext);
#endif
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_handle::load_asnum_db(char const*) {}
	void session_handle::load_country_db(char const*) {}

	int session_handle::as_for_ip(address const&)
	{ return 0; }

#if TORRENT_USE_WSTRING
	void session_handle::load_asnum_db(wchar_t const*) {}
	void session_handle::load_country_db(wchar_t const*) {}
#endif // TORRENT_USE_WSTRING

	void session_handle::load_state(entry const& ses_state
		, std::uint32_t const flags)
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
		if (ret != 0) throw system_error(ec);
#endif
		sync_call(&session_impl::load_state, &e, flags);
	}

	entry session_handle::state() const
	{
		entry ret;
		auto retp = &ret;
		sync_call(&session_impl::save_state, retp, 0xffffffff);
		return ret;
	}

	void session_handle::load_state(lazy_entry const& ses_state
		, std::uint32_t const flags)
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
		if (ret != 0) throw system_error(ec);
#endif
		sync_call(&session_impl::load_state, &e, flags);
	}
#endif // TORRENT_NO_DEPRECATE

	void session_handle::set_ip_filter(ip_filter const& f)
	{
		boost::shared_ptr<ip_filter> copy = boost::make_shared<ip_filter>(f);
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

#ifndef TORRENT_NO_DEPRECATE
	void session_handle::set_peer_id(peer_id const& id)
	{
		settings_pack p;
		p.set_str(settings_pack::peer_fingerprint, id.to_string());
		apply_settings(p);
	}
#endif

	peer_id session_handle::id() const
	{
		return sync_call_ret<peer_id>(&session_impl::get_peer_id);
	}

	void session_handle::set_key(int key)
	{
		async_call(&session_impl::set_key, key);
	}

	unsigned short session_handle::listen_port() const
	{
		return sync_call_ret<unsigned short>(&session_impl::listen_port);
	}

	unsigned short session_handle::ssl_listen_port() const
	{
		return sync_call_ret<unsigned short>(&session_impl::ssl_listen_port);
	}

	bool session_handle::is_listening() const
	{
		return sync_call_ret<bool>(&session_impl::is_listening);
	}

	void session_handle::set_peer_class_filter(ip_filter const& f)
	{
		async_call(&session_impl::set_peer_class_filter, f);
	}

	void session_handle::set_peer_class_type_filter(peer_class_type_filter const& f)
	{
		async_call(&session_impl::set_peer_class_type_filter, f);
	}

	int session_handle::create_peer_class(char const* name)
	{
		return sync_call_ret<int>(&session_impl::create_peer_class, name);
	}

	void session_handle::delete_peer_class(int cid)
	{
		async_call(&session_impl::delete_peer_class, cid);
	}

	peer_class_info session_handle::get_peer_class(int cid)
	{
		return sync_call_ret<peer_class_info>(&session_impl::get_peer_class, cid);
	}

	void session_handle::set_peer_class(int cid, peer_class_info const& pci)
	{
		async_call(&session_impl::set_peer_class, cid, pci);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_handle::use_interfaces(char const* interfaces)
	{
		settings_pack pack;
		pack.set_str(settings_pack::outgoing_interfaces, interfaces);
		apply_settings(pack);
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

		interfaces_str = print_endpoint(tcp::endpoint(address::from_string(net_interface, ec), port_range.first));
		if (ec) return;

		p.set_str(settings_pack::listen_interfaces, interfaces_str);
		p.set_int(settings_pack::max_retry_port_bind, port_range.second - port_range.first);
		p.set_bool(settings_pack::listen_system_port_fallback, (flags & session::listen_no_system_port) == 0);
		apply_settings(p);
	}
#endif

	void session_handle::remove_torrent(const torrent_handle& h, int options)
	{
		if (!h.is_valid())
#ifdef BOOST_NO_EXCEPTIONS
			return;
#else
			throw_invalid_handle();
#endif
		async_call(&session_impl::remove_torrent, h, options);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_handle::set_settings(session_settings const& s)
	{
		async_call(&session_impl::set_settings, s);
	}

	session_settings session_handle::settings() const
	{
		return sync_call_ret<session_settings>(&session_impl::deprecated_settings);
	}

	void session_handle::set_pe_settings(pe_settings const& r)
	{
		settings_pack pack;
		pack.set_bool(settings_pack::prefer_rc4, r.prefer_rc4);
		pack.set_int(settings_pack::out_enc_policy, r.out_enc_policy);
		pack.set_int(settings_pack::in_enc_policy, r.in_enc_policy);
		pack.set_int(settings_pack::allowed_enc_level, r.allowed_enc_level);

		apply_settings(pack);
	}

	pe_settings session_handle::get_pe_settings() const
	{
		settings_pack sett = get_settings();

		pe_settings r;
		r.prefer_rc4 = sett.get_bool(settings_pack::prefer_rc4);
		r.out_enc_policy = sett.get_int(settings_pack::out_enc_policy);
		r.in_enc_policy = sett.get_int(settings_pack::in_enc_policy);
		r.allowed_enc_level = sett.get_int(settings_pack::allowed_enc_level);
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

		boost::shared_ptr<settings_pack> copy = boost::make_shared<settings_pack>(s);
		async_call(&session_impl::apply_settings_pack, copy);
	}

	settings_pack session_handle::get_settings() const
	{
		return sync_call_ret<settings_pack>(&session_impl::get_settings);
	}

#ifndef TORRENT_NO_DEPRECATE
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
		ret.port = sett.get_int(settings_pack::i2p_port);
		return ret;
	}

	void session_handle::set_proxy(proxy_settings const& s)
	{
		settings_pack pack;
		pack.set_str(settings_pack::proxy_hostname, s.hostname);
		pack.set_str(settings_pack::proxy_username, s.username);
		pack.set_str(settings_pack::proxy_password, s.password);
		pack.set_int(settings_pack::proxy_type, s.type);
		pack.set_int(settings_pack::proxy_port, s.port);
		pack.set_bool(settings_pack::proxy_hostnames,s.proxy_hostnames);
		pack.set_bool(settings_pack::proxy_peer_connections, s.proxy_peer_connections);

		apply_settings(pack);
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

	void session_handle::set_web_seed_proxy(proxy_settings const& s)
	{
		set_proxy(s);
	}

	void session_handle::set_tracker_proxy(proxy_settings const& s)
	{
		set_proxy(s);
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
		return proxy();
	}

	void session_handle::set_dht_proxy(proxy_settings const& s)
	{
		set_proxy(s);
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

#endif // TORRENT_NO_DEPRECATE

	// the alerts are const, they may not be deleted by the client
	void session_handle::pop_alerts(std::vector<alert*>* alerts)
	{
		m_impl->pop_alerts(alerts);
	}

	alert* session_handle::wait_for_alert(time_duration max_wait)
	{
		return m_impl->wait_for_alert(max_wait);
	}

	void session_handle::set_alert_notify(boost::function<void()> const& fun)
	{
		m_impl->alerts().set_notify_function(fun);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_handle::set_severity_level(alert::severity_t s)
	{
		int m = 0;
		switch (s)
		{
			case alert::debug: m = alert::all_categories; break;
			case alert::info: m = alert::all_categories & ~(alert::debug_notification
				| alert::progress_notification | alert::dht_notification); break;
			case alert::warning: m = alert::all_categories & ~(alert::debug_notification
				| alert::status_notification | alert::progress_notification
				| alert::dht_notification); break;
			case alert::critical: m = alert::error_notification | alert::storage_notification; break;
			case alert::fatal: m = alert::error_notification; break;
			case alert::none: m = 0; break;
		}

		settings_pack p;
		p.set_int(settings_pack::alert_mask, m);
		apply_settings(p);
	}

	size_t session_handle::set_alert_queue_size_limit(size_t queue_size_limit_)
	{
		return sync_call_ret<size_t>(&session_impl::set_alert_queue_size_limit, queue_size_limit_);
	}

	void session_handle::set_alert_mask(std::uint32_t m)
	{
		settings_pack p;
		p.set_int(settings_pack::alert_mask, m);
		apply_settings(p);
	}

	std::uint32_t session_handle::get_alert_mask() const
	{
		return get_settings().get_int(settings_pack::alert_mask);
	}

	void session_handle::start_lsd()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_lsd, true);
		apply_settings(p);
	}

	void session_handle::stop_lsd()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_lsd, false);
		apply_settings(p);
	}

	void session_handle::start_upnp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_upnp, true);
		apply_settings(p);
	}

	void session_handle::stop_upnp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_upnp, false);
		apply_settings(p);
	}

	void session_handle::start_natpmp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_natpmp, true);
		apply_settings(p);
	}

	void session_handle::stop_natpmp()
	{
		settings_pack p;
		p.set_bool(settings_pack::enable_natpmp, false);
		apply_settings(p);
	}
#endif // TORRENT_NO_DEPRECATE

	int session_handle::add_port_mapping(session::protocol_type t, int external_port, int local_port)
	{
		return sync_call_ret<int>(&session_impl::add_port_mapping, int(t), external_port, local_port);
	}

	void session_handle::delete_port_mapping(int handle)
	{
		async_call(&session_impl::delete_port_mapping, handle);
	}

} // namespace libtorrent
