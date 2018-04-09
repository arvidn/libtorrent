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
#include "libtorrent/torrent.hpp"
#include "libtorrent/lazy_entry.hpp"
#include "libtorrent/peer_class_type_filter.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-macros"
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif

#define TORRENT_ASYNC_CALL(x) \
	m_impl->get_io_service().dispatch(boost::bind(&session_impl:: x, m_impl))

#define TORRENT_ASYNC_CALL1(x, a1) \
	m_impl->get_io_service().dispatch(boost::bind(&session_impl:: x, m_impl, a1))

#define TORRENT_ASYNC_CALL2(x, a1, a2) \
	m_impl->get_io_service().dispatch(boost::bind(&session_impl:: x, m_impl, a1, a2))

#define TORRENT_ASYNC_CALL3(x, a1, a2, a3) \
	m_impl->get_io_service().dispatch(boost::bind(&session_impl:: x, m_impl, a1, a2, a3))

#define TORRENT_SYNC_CALL(x) \
	aux::sync_call(*m_impl, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl)))

#define TORRENT_SYNC_CALL1(x, a1) \
	aux::sync_call(*m_impl, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl, a1)))

#define TORRENT_SYNC_CALL2(x, a1, a2) \
	aux::sync_call(*m_impl, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl, a1, a2)))

#define TORRENT_SYNC_CALL3(x, a1, a2, a3) \
	aux::sync_call(*m_impl, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl, a1, a2, a3)))

#define TORRENT_SYNC_CALL4(x, a1, a2, a3, a4) \
	aux::sync_call(*m_impl, boost::function<void(void)>(boost::bind(&session_impl:: x, m_impl, a1, a2, a3, a4)))

#define TORRENT_SYNC_CALL_RET(type, x) \
	aux::sync_call_ret<type>(*m_impl, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl)))

#define TORRENT_SYNC_CALL_RET1(type, x, a1) \
	aux::sync_call_ret<type>(*m_impl, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl, a1)))

#define TORRENT_SYNC_CALL_RET2(type, x, a1, a2) \
	aux::sync_call_ret<type>(*m_impl, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl, a1, a2)))

#define TORRENT_SYNC_CALL_RET3(type, x, a1, a2, a3) \
	aux::sync_call_ret<type>(*m_impl, boost::function<type(void)>(boost::bind(&session_impl:: x, m_impl, a1, a2, a3)))

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

using libtorrent::aux::session_impl;

namespace libtorrent
{
	void session_handle::save_state(entry& e, boost::uint32_t flags) const
	{
		TORRENT_SYNC_CALL2(save_state, &e, flags);
	}

	void session_handle::load_state(bdecode_node const& e
		, boost::uint32_t const flags)
	{
		// this needs to be synchronized since the lifespan
		// of e is tied to the caller
		TORRENT_SYNC_CALL2(load_state, &e, flags);
	}

	void session_handle::get_torrent_status(std::vector<torrent_status>* ret
		, boost::function<bool(torrent_status const&)> const& pred
		, boost::uint32_t flags) const
	{
		TORRENT_SYNC_CALL3(get_torrent_status, ret, boost::ref(pred), flags);
	}

	void session_handle::refresh_torrent_status(std::vector<torrent_status>* ret
		, boost::uint32_t flags) const
	{
		TORRENT_SYNC_CALL2(refresh_torrent_status, ret, flags);
	}

	void session_handle::post_torrent_updates(boost::uint32_t flags)
	{
		TORRENT_ASYNC_CALL1(post_torrent_updates, flags);
	}

	void session_handle::post_session_stats()
	{
		TORRENT_ASYNC_CALL(post_session_stats);
	}

	void session_handle::post_dht_stats()
	{
		TORRENT_ASYNC_CALL(post_dht_stats);
	}

	io_service& session_handle::get_io_service()
	{
		return m_impl->get_io_service();
	}

	torrent_handle session_handle::find_torrent(sha1_hash const& info_hash) const
	{
		return TORRENT_SYNC_CALL_RET1(torrent_handle, find_torrent_handle, info_hash);
	}

	std::vector<torrent_handle> session_handle::get_torrents() const
	{
		return TORRENT_SYNC_CALL_RET(std::vector<torrent_handle>, get_torrents);
	}

	#ifndef BOOST_NO_EXCEPTIONS
	torrent_handle session_handle::add_torrent(add_torrent_params const& params)
	{
		error_code ec;
		torrent_handle r = TORRENT_SYNC_CALL_RET2(torrent_handle, add_torrent, params, boost::ref(ec));
		if (ec) throw libtorrent_exception(ec);
		return r;
	}
#endif

	torrent_handle session_handle::add_torrent(add_torrent_params const& params, error_code& ec)
	{
		ec.clear();
		return TORRENT_SYNC_CALL_RET2(torrent_handle, add_torrent, params, boost::ref(ec));
	}

	void session_handle::async_add_torrent(add_torrent_params const& params)
	{
		add_torrent_params* p = new add_torrent_params(params);
		p->save_path = complete(p->save_path);
#ifndef TORRENT_NO_DEPRECATE
		if (params.tracker_url)
		{
			p->trackers.push_back(params.tracker_url);
			p->tracker_url = NULL;
		}
#endif
		TORRENT_ASYNC_CALL1(async_add_torrent, p);
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
		p.paused = paused;
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
		add_torrent_params p(sc);
		p.tracker_url = tracker_url;
		p.info_hash = info_hash;
		p.save_path = save_path;
		p.storage_mode = storage_mode;
		p.paused = paused;
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
		TORRENT_ASYNC_CALL(pause);
	}

	void session_handle::resume()
	{
		TORRENT_ASYNC_CALL(resume);
	}

	bool session_handle::is_paused() const
	{
		return TORRENT_SYNC_CALL_RET(bool, is_paused);
	}

	void session_handle::set_load_function(user_load_function_t fun)
	{
		TORRENT_ASYNC_CALL1(set_load_function, fun);
	}

#ifndef TORRENT_NO_DEPRECATE
	session_status session_handle::status() const
	{
		return TORRENT_SYNC_CALL_RET(session_status, status);
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
		piece_manager* st = 0;
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
	feed_handle session_handle::add_feed(feed_settings const& feed)
	{
		// if you have auto-download enabled, you must specify a download directory!
		TORRENT_ASSERT_PRECOND(!feed.auto_download || !feed.add_args.save_path.empty());
		return TORRENT_SYNC_CALL_RET1(feed_handle, add_feed, feed);
	}

	void session_handle::remove_feed(feed_handle h)
	{
		TORRENT_ASYNC_CALL1(remove_feed, h);
	}

	void session_handle::get_feeds(std::vector<feed_handle>& f) const
	{
		f.clear();
		TORRENT_SYNC_CALL1(get_feeds, &f);
	}

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
		TORRENT_ASYNC_CALL1(set_dht_settings, settings);
#else
		TORRENT_UNUSED(settings);
#endif
	}

	dht_settings session_handle::get_dht_settings() const
	{
#ifndef TORRENT_DISABLE_DHT
		return TORRENT_SYNC_CALL_RET(dht_settings, get_dht_settings);
#else
		return dht_settings();
#endif
	}

	bool session_handle::is_dht_running() const
	{
#ifndef TORRENT_DISABLE_DHT
		return TORRENT_SYNC_CALL_RET(bool, is_dht_running);
#else
		return false;
#endif
	}

	void session_handle::set_dht_storage(dht::dht_storage_constructor_type sc)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(set_dht_storage, sc);
#else
		TORRENT_UNUSED(sc);
#endif
	}

	void session_handle::add_dht_node(std::pair<std::string, int> const& node)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(add_dht_node_name, node);
#else
		TORRENT_UNUSED(node);
#endif
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_handle::add_dht_router(std::pair<std::string, int> const& node)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(add_dht_router, node);
#else
		TORRENT_UNUSED(node);
#endif
	}
#endif // TORRENT_NO_DEPRECATE

	void session_handle::dht_get_item(sha1_hash const& target)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(dht_get_immutable_item, target);
#else
		TORRENT_UNUSED(target);
#endif
	}

	void session_handle::dht_get_item(boost::array<char, 32> key
		, std::string salt)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL2(dht_get_mutable_item, key, salt);
#else
		TORRENT_UNUSED(key);
		TORRENT_UNUSED(salt);
#endif
	}

	sha1_hash session_handle::dht_put_item(entry data)
	{
		std::vector<char> buf;
		bencode(std::back_inserter(buf), data);
		sha1_hash ret = hasher(&buf[0], buf.size()).final();

#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL2(dht_put_immutable_item, data, ret);
#endif
		return ret;
	}

	void session_handle::dht_put_item(boost::array<char, 32> key
		, boost::function<void(entry&, boost::array<char,64>&
		, boost::uint64_t&, std::string const&)> cb
		, std::string salt)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL3(dht_put_mutable_item, key, cb, salt);
#else
		TORRENT_UNUSED(key);
		TORRENT_UNUSED(cb);
		TORRENT_UNUSED(salt);
#endif
	}

	void session_handle::dht_get_peers(sha1_hash const& info_hash)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(dht_get_peers, info_hash);
#else
		TORRENT_UNUSED(info_hash);
#endif
	}

	void session_handle::dht_announce(sha1_hash const& info_hash, int port, int flags)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL3(dht_announce, info_hash, port, flags);
#else
		TORRENT_UNUSED(info_hash);
		TORRENT_UNUSED(port);
		TORRENT_UNUSED(flags);
#endif
	}

	void session_handle::dht_direct_request(udp::endpoint ep, entry const& e, void* userdata)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL3(dht_direct_request, ep, e, userdata);
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
		return TORRENT_SYNC_CALL_RET(entry, dht_state);
#else
		return entry();
#endif
	}

	void session_handle::start_dht(entry const& startup_state)
	{
#ifndef TORRENT_DISABLE_DHT
		TORRENT_ASYNC_CALL1(start_dht_deprecated, startup_state);
#else
		TORRENT_UNUSED(startup_state);
#endif
	}
#endif // TORRENT_NO_DEPRECATE

	void session_handle::add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent_handle const&, void*)> ext)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		TORRENT_ASYNC_CALL1(add_extension, ext);
#else
		TORRENT_UNUSED(ext);
#endif
	}

	void session_handle::add_extension(boost::shared_ptr<plugin> ext)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		TORRENT_ASYNC_CALL1(add_ses_extension, ext);
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
		, boost::uint32_t const flags)
	{
		if (ses_state.type() == entry::undefined_t) return;
		std::vector<char> buf;
		bencode(std::back_inserter(buf), ses_state);
		bdecode_node e;
		error_code ec;
#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS || !defined BOOST_NO_EXCEPTIONS
		int ret =
#endif
		bdecode(&buf[0], &buf[0] + buf.size(), e, ec);

		TORRENT_ASSERT(ret == 0);
#ifndef BOOST_NO_EXCEPTIONS
		if (ret != 0) throw libtorrent_exception(ec);
#endif
		TORRENT_SYNC_CALL2(load_state, &e, flags);
	}

	entry session_handle::state() const
	{
		entry ret;
		TORRENT_SYNC_CALL2(save_state, &ret, 0xffffffff);
		return ret;
	}

	void session_handle::load_state(lazy_entry const& ses_state
		, boost::uint32_t const flags)
	{
		if (ses_state.type() == lazy_entry::none_t) return;
		std::pair<char const*, int> buf = ses_state.data_section();
		bdecode_node e;
		error_code ec;
#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS || !defined BOOST_NO_EXCEPTIONS
		int ret =
#endif
		bdecode(buf.first, buf.first + buf.second, e, ec);

		TORRENT_ASSERT(ret == 0);
#ifndef BOOST_NO_EXCEPTIONS
		if (ret != 0) throw libtorrent_exception(ec);
#endif
		TORRENT_SYNC_CALL2(load_state, &e, flags);
	}
#endif // TORRENT_NO_DEPRECATE

	void session_handle::set_ip_filter(ip_filter const& f)
	{
		boost::shared_ptr<ip_filter> copy = boost::make_shared<ip_filter>(f);
		TORRENT_ASYNC_CALL1(set_ip_filter, copy);
	}

	ip_filter session_handle::get_ip_filter() const
	{
		return TORRENT_SYNC_CALL_RET(ip_filter, get_ip_filter);
	}

	void session_handle::set_port_filter(port_filter const& f)
	{
		TORRENT_ASYNC_CALL1(set_port_filter, f);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_handle::set_peer_id(peer_id const& id)
	{
		settings_pack p;
		p.set_str(settings_pack::peer_fingerprint, id.to_string());
		apply_settings(p);
	}

	peer_id session_handle::id() const
	{
		return TORRENT_SYNC_CALL_RET(peer_id, deprecated_get_peer_id);
	}
#endif

	void session_handle::set_key(int key)
	{
		TORRENT_ASYNC_CALL1(set_key, key);
	}

	unsigned short session_handle::listen_port() const
	{
		return TORRENT_SYNC_CALL_RET(unsigned short, listen_port);
	}

	unsigned short session_handle::ssl_listen_port() const
	{
		return TORRENT_SYNC_CALL_RET(unsigned short, ssl_listen_port);
	}

	bool session_handle::is_listening() const
	{
		return TORRENT_SYNC_CALL_RET(bool, is_listening);
	}

	void session_handle::set_peer_class_filter(ip_filter const& f)
	{
		TORRENT_ASYNC_CALL1(set_peer_class_filter, f);
	}

	ip_filter session_handle::get_peer_class_filter() const
	{
		return TORRENT_SYNC_CALL_RET(ip_filter, get_peer_class_filter);
	}

	void session_handle::set_peer_class_type_filter(peer_class_type_filter const& f)
	{
		TORRENT_ASYNC_CALL1(set_peer_class_type_filter, f);
	}

	peer_class_type_filter session_handle::get_peer_class_type_filter() const
	{
		return TORRENT_SYNC_CALL_RET(peer_class_type_filter, get_peer_class_type_filter);
	}

	int session_handle::create_peer_class(char const* name)
	{
		return TORRENT_SYNC_CALL_RET1(int, create_peer_class, name);
	}

	void session_handle::delete_peer_class(int cid)
	{
		TORRENT_ASYNC_CALL1(delete_peer_class, cid);
	}

	peer_class_info session_handle::get_peer_class(int cid)
	{
		return TORRENT_SYNC_CALL_RET1(peer_class_info, get_peer_class, cid);
	}

	void session_handle::set_peer_class(int cid, peer_class_info const& pci)
	{
		TORRENT_ASYNC_CALL2(set_peer_class, cid, pci);
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
		if (net_interface == NULL || strlen(net_interface) == 0)
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
		TORRENT_ASYNC_CALL2(remove_torrent, h, options);
	}

#ifndef TORRENT_NO_DEPRECATE
	void session_handle::set_settings(session_settings const& s)
	{
		TORRENT_ASYNC_CALL1(set_settings, s);
	}

	session_settings session_handle::settings() const
	{
		return TORRENT_SYNC_CALL_RET(session_settings, deprecated_settings);
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
		TORRENT_ASYNC_CALL1(apply_settings_pack, copy);
	}

	settings_pack session_handle::get_settings() const
	{
		return TORRENT_SYNC_CALL_RET(settings_pack, get_settings);
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
		return TORRENT_SYNC_CALL_RET(int, num_uploads);
	}

	int session_handle::num_connections() const
	{
		return TORRENT_SYNC_CALL_RET(int, num_connections);
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
			, s.type != aux::proxy_settings::none);
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
		return TORRENT_SYNC_CALL_RET(int, upload_rate_limit);
	}

	int session_handle::download_rate_limit() const
	{
		return TORRENT_SYNC_CALL_RET(int, download_rate_limit);
	}

	int session_handle::local_upload_rate_limit() const
	{
		return TORRENT_SYNC_CALL_RET(int, local_upload_rate_limit);
	}

	int session_handle::local_download_rate_limit() const
	{
		return TORRENT_SYNC_CALL_RET(int, local_download_rate_limit);
	}

	int session_handle::max_half_open_connections() const { return 8; }

	void session_handle::set_local_upload_rate_limit(int bytes_per_second)
	{
		TORRENT_ASYNC_CALL1(set_local_upload_rate_limit, bytes_per_second);
	}

	void session_handle::set_local_download_rate_limit(int bytes_per_second)
	{
		TORRENT_ASYNC_CALL1(set_local_download_rate_limit, bytes_per_second);
	}

	void session_handle::set_upload_rate_limit(int bytes_per_second)
	{
		TORRENT_ASYNC_CALL1(set_upload_rate_limit, bytes_per_second);
	}

	void session_handle::set_download_rate_limit(int bytes_per_second)
	{
		TORRENT_ASYNC_CALL1(set_download_rate_limit, bytes_per_second);
	}

	void session_handle::set_max_connections(int limit)
	{
		TORRENT_ASYNC_CALL1(set_max_connections, limit);
	}

	void session_handle::set_max_uploads(int limit)
	{
		TORRENT_ASYNC_CALL1(set_max_uploads, limit);
	}

	void session_handle::set_max_half_open_connections(int) {}

	int session_handle::max_uploads() const
	{
		return TORRENT_SYNC_CALL_RET(int, max_uploads);
	}

	int session_handle::max_connections() const
	{
		return TORRENT_SYNC_CALL_RET(int, max_connections);
	}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	std::auto_ptr<alert> session_handle::pop_alert()
	{
		alert const* a = m_impl->pop_alert();
		if (a == NULL) return std::auto_ptr<alert>();
		return a->clone();
	}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

	void session_handle::pop_alerts(std::deque<alert*>* alerts)
	{
		m_impl->pop_alerts(alerts);
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
		return TORRENT_SYNC_CALL_RET1(size_t, set_alert_queue_size_limit, queue_size_limit_);
	}

	void session_handle::set_alert_mask(boost::uint32_t m)
	{
		settings_pack p;
		p.set_int(settings_pack::alert_mask, m);
		apply_settings(p);
	}

	boost::uint32_t session_handle::get_alert_mask() const
	{
		return get_settings().get_int(settings_pack::alert_mask);
	}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	void session_handle::set_alert_dispatch(boost::function<void(std::auto_ptr<alert>)> const& fun)
	{
		m_impl->alerts().set_dispatch_function(fun);
	}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

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
		return TORRENT_SYNC_CALL_RET3(int, add_port_mapping, int(t), external_port, local_port);
	}

	void session_handle::delete_port_mapping(int handle)
	{
		TORRENT_ASYNC_CALL1(delete_port_mapping, handle);
	}

} // namespace libtorrent
