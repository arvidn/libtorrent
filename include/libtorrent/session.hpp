/*

Copyright (c) 2006-2016, Arvid Norberg
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

#ifndef TORRENT_SESSION_HPP_INCLUDED
#define TORRENT_SESSION_HPP_INCLUDED

#include <algorithm>
#include <vector>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/limits.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/config.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/build_config.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/io_service.hpp"

#include "libtorrent/storage.hpp"
#include "libtorrent/session_settings.hpp"
#include "libtorrent/session_handle.hpp"
#include "libtorrent/thread.hpp"

#ifndef TORRENT_NO_DEPRECATE
#include "libtorrent/rss.hpp"
#include "libtorrent/fingerprint.hpp"
#endif

#ifdef TORRENT_USE_OPENSSL
// this is a nasty openssl macro
#ifdef set_key
#undef set_key
#endif
#endif

namespace libtorrent
{
	// The default values of the session settings are set for a regular
	// bittorrent client running on a desktop system. There are functions that
	// can set the session settings to pre set settings for other environments.
	// These can be used for the basis, and should be tweaked to fit your needs
	// better.
	// 
	// ``min_memory_usage`` returns settings that will use the minimal amount of
	// RAM, at the potential expense of upload and download performance. It
	// adjusts the socket buffer sizes, disables the disk cache, lowers the send
	// buffer watermarks so that each connection only has at most one block in
	// use at any one time. It lowers the outstanding blocks send to the disk
	// I/O thread so that connections only have one block waiting to be flushed
	// to disk at any given time. It lowers the max number of peers in the peer
	// list for torrents. It performs multiple smaller reads when it hashes
	// pieces, instead of reading it all into memory before hashing.
	// 
	// This configuration is inteded to be the starting point for embedded
	// devices. It will significantly reduce memory usage.
	// 
	// ``high_performance_seed`` returns settings optimized for a seed box,
	// serving many peers and that doesn't do any downloading. It has a 128 MB
	// disk cache and has a limit of 400 files in its file pool. It support fast
	// upload rates by allowing large send buffers.
	TORRENT_EXPORT void min_memory_usage(settings_pack& set);
	TORRENT_EXPORT void high_performance_seed(settings_pack& set);

#ifndef TORRENT_NO_DEPRECATE
	TORRENT_DEPRECATED
	TORRENT_EXPORT session_settings min_memory_usage();
	TORRENT_DEPRECATED
	TORRENT_EXPORT session_settings high_performance_seed();
#endif

#ifndef TORRENT_CFG
#error TORRENT_CFG is not defined!
#endif

	void TORRENT_EXPORT TORRENT_CFG();

	namespace aux
	{
		struct session_impl;
	}

	// this is a holder for the internal session implementation object. Once the
	// session destruction is explicitly initiated, this holder is used to
	// synchronize the completion of the shutdown. The lifetime of this object
	// may outlive session, causing the session destructor to not block. The
	// session_proxy destructor will block however, until the underlying session
	// is done shutting down.
	class TORRENT_EXPORT session_proxy
	{
		friend class session;
	public:
		// default constructor, does not refer to any session
		// implementation object.
		session_proxy() {}
		~session_proxy();
#if __cplusplus >= 201103L
		session_proxy(session_proxy const&) = default;
		session_proxy& operator=(session_proxy const&) = default;
#endif
	private:
		session_proxy(
			boost::shared_ptr<io_service> ios
			, boost::shared_ptr<thread> t
			, boost::shared_ptr<aux::session_impl> impl)
			: m_io_service(ios)
			, m_thread(t)
			, m_impl(impl)
		{}
		boost::shared_ptr<io_service> m_io_service;
		boost::shared_ptr<thread> m_thread;
		boost::shared_ptr<aux::session_impl> m_impl;
	};

	// The session holds all state that spans multiple torrents. Among other
	// things it runs the network loop and manages all torrents. Once it's
	// created, the session object will spawn the main thread that will do all
	// the work. The main thread will be idle as long it doesn't have any
	// torrents to participate in.
	//
	// You have some control over session configuration through the
	// ``session::apply_settings()`` member function. To change one or more
	// configuration options, create a settings_pack. object and fill it with
	// the settings to be set and pass it in to ``session::apply_settings()``.
	// 
	// see apply_settings().
	class TORRENT_EXPORT session: public boost::noncopyable, public session_handle
	{
	public:

		// Constructs the session obects which acts as the container of torrents.
		// It provides configuration options across torrents (such as rate limits,
		// disk cache, ip filter etc.). In order to avoid a race condition between
		// starting the session and configuring it, you can pass in a
		// settings_pack object. Its settings will take effect before the session
		// starts up.
		// 
		// The ``flags`` parameter can be used to start default features (upnp &
		// nat-pmp) and default plugins (ut_metadata, ut_pex and smart_ban). The
		// default is to start those features. If you do not want them to start,
		// pass 0 as the flags parameter.
		session(settings_pack const& pack = settings_pack()
			, int flags = start_default_features | add_default_plugins)
			: session_handle(NULL)
		{
			TORRENT_CFG();
			start(flags, pack, NULL);
		}

		// overload of the constructor that takes an external io_service to run
		// the session object on. This is primarily useful for tests that may want
		// to run multiple sessions on a single io_service, or low resource
		// systems where additional threads are expensive and sharing an
		// io_service with other events is fine.
		// 
		// .. warning::
		// 	The session object does not cleanly terminate with an external
		// 	``io_service``. The ``io_service::run()`` call _must_ have returned
		// 	before it's safe to destruct the session. Which means you *MUST*
		// 	call session::abort() and save the session_proxy first, then
		// 	destruct the session object, then sync with the io_service, then
		// 	destruct the session_proxy object.
		session(settings_pack const& pack
			, io_service& ios
			, int flags = start_default_features | add_default_plugins)
			: session_handle(NULL)
		{
			TORRENT_CFG();
			start(flags, pack, &ios);
		}

#ifndef TORRENT_NO_DEPRECATE
		TORRENT_DEPRECATED
		session(fingerprint const& print
			, int flags = start_default_features | add_default_plugins
			, boost::uint32_t alert_mask = alert::error_notification)
			: session_handle(NULL)
		{
			TORRENT_CFG();
			settings_pack pack;
			pack.set_int(settings_pack::alert_mask, alert_mask);
			pack.set_str(settings_pack::peer_fingerprint, print.to_string());
			if ((flags & start_default_features) == 0)
			{
				pack.set_bool(settings_pack::enable_upnp, false);
				pack.set_bool(settings_pack::enable_natpmp, false);
				pack.set_bool(settings_pack::enable_lsd, false);
				pack.set_bool(settings_pack::enable_dht, false);
			}

			start(flags, pack, NULL);
		}

		TORRENT_DEPRECATED
		session(fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = "0.0.0.0"
			, int flags = start_default_features | add_default_plugins
			, int alert_mask = alert::error_notification)
			: session_handle(NULL)
		{
			TORRENT_CFG();
			TORRENT_ASSERT(listen_port_range.first > 0);
			TORRENT_ASSERT(listen_port_range.first <= listen_port_range.second);

			settings_pack pack;
			pack.set_int(settings_pack::alert_mask, alert_mask);
			pack.set_int(settings_pack::max_retry_port_bind, listen_port_range.second - listen_port_range.first);
			pack.set_str(settings_pack::peer_fingerprint, print.to_string());
			char if_string[100];

			if (listen_interface == NULL) listen_interface = "0.0.0.0";
			snprintf(if_string, sizeof(if_string), "%s:%d", listen_interface, listen_port_range.first);
			pack.set_str(settings_pack::listen_interfaces, if_string);

			if ((flags & start_default_features) == 0)
			{
				pack.set_bool(settings_pack::enable_upnp, false);
				pack.set_bool(settings_pack::enable_natpmp, false);
				pack.set_bool(settings_pack::enable_lsd, false);
				pack.set_bool(settings_pack::enable_dht, false);
			}
			start(flags, pack, NULL);
		}
#endif // TORRENT_NO_DEPRECATE

		// The destructor of session will notify all trackers that our torrents
		// have been shut down. If some trackers are down, they will time out.
		// All this before the destructor of session returns. So, it's advised
		// that any kind of interface (such as windows) are closed before
		// destructing the session object. Because it can take a few second for
		// it to finish. The timeout can be set with apply_settings().
		~session();

		// In case you want to destruct the session asynchronously, you can
		// request a session destruction proxy. If you don't do this, the
		// destructor of the session object will block while the trackers are
		// contacted. If you keep one ``session_proxy`` to the session when
		// destructing it, the destructor will not block, but start to close down
		// the session, the destructor of the proxy will then synchronize the
		// threads. So, the destruction of the session is performed from the
		// ``session`` destructor call until the ``session_proxy`` destructor
		// call. The ``session_proxy`` does not have any operations on it (since
		// the session is being closed down, no operations are allowed on it).
		// The only valid operation is calling the destructor::
		// 
		// 	class session_proxy
		// 	{
		// 	public:
		// 		session_proxy();
		// 		~session_proxy()
		// 	};
		session_proxy abort() { return session_proxy(m_io_service, m_thread, m_impl); }

	private:

		void start(int flags, settings_pack const& pack, io_service* ios);

		// data shared between the main thread
		// and the working thread
		boost::shared_ptr<io_service> m_io_service;
		boost::shared_ptr<thread> m_thread;
		boost::shared_ptr<aux::session_impl> m_impl;
	};

}

#endif // TORRENT_SESSION_HPP_INCLUDED

