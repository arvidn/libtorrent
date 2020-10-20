/*

Copyright (c) 2003-2004, 2006-2007, 2009-2010, 2013-2020, Arvid Norberg
Copyright (c) 2015, Steven Siloti
Copyright (c) 2016, Alden Torres
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

#include <thread>

#include "libtorrent/config.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/session_handle.hpp"
#include "libtorrent/kademlia/dht_storage.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/session_types.hpp" // for session_flags_t

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/fingerprint.hpp"
#include <cstdio> // for snprintf
#endif

namespace libtorrent {

TORRENT_VERSION_NAMESPACE_3
	struct plugin;
	struct session_params;
TORRENT_VERSION_NAMESPACE_3_END

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
	// This configuration is intended to be the starting point for embedded
	// devices. It will significantly reduce memory usage.
	//
	// ``high_performance_seed`` returns settings optimized for a seed box,
	// serving many peers and that doesn't do any downloading. It has a 128 MB
	// disk cache and has a limit of 400 files in its file pool. It support fast
	// upload rates by allowing large send buffers.
	TORRENT_EXPORT settings_pack min_memory_usage();
	TORRENT_EXPORT settings_pack high_performance_seed();
#if TORRENT_ABI_VERSION == 1
	TORRENT_DEPRECATED
	inline void min_memory_usage(settings_pack& set)
	{ set = min_memory_usage(); }
	TORRENT_DEPRECATED
	inline void high_performance_seed(settings_pack& set)
	{ set = high_performance_seed(); }
#endif

namespace aux {

	struct session_impl;
}

	struct disk_interface;
	struct counters;
	struct settings_interface;

	// the constructor function for the default storage. On systems that support
	// memory mapped files (and a 64 bit address space) the memory mapped storage
	// will be constructed, otherwise the portable posix storage.
	TORRENT_EXPORT std::unique_ptr<disk_interface> default_disk_io_constructor(
		io_context& ios, settings_interface const&, counters& cnt);

	// this is a holder for the internal session implementation object. Once the
	// session destruction is explicitly initiated, this holder is used to
	// synchronize the completion of the shutdown. The lifetime of this object
	// may outlive session, causing the session destructor to not block. The
	// session_proxy destructor will block however, until the underlying session
	// is done shutting down.
	struct TORRENT_EXPORT session_proxy
	{
		friend struct session;
		// default constructor, does not refer to any session
		// implementation object.
		session_proxy();
		~session_proxy();
		session_proxy(session_proxy const&);
		session_proxy& operator=(session_proxy const&) &;
		session_proxy(session_proxy&&) noexcept;
		session_proxy& operator=(session_proxy&&) & noexcept;
	private:
		session_proxy(
			std::shared_ptr<io_context> ios
			, std::shared_ptr<std::thread> t
			, std::shared_ptr<aux::session_impl> impl);

		std::shared_ptr<io_context> m_io_service;
		std::shared_ptr<std::thread> m_thread;
		std::shared_ptr<aux::session_impl> m_impl;
	};

	// The session holds all state that spans multiple torrents. Among other
	// things it runs the network loop and manages all torrents. Once it's
	// created, the session object will spawn the main thread that will do all
	// the work. The main thread will be idle as long it doesn't have any
	// torrents to participate in.
	//
	// You have some control over session configuration through the
	// ``session_handle::apply_settings()`` member function. To change one or more
	// configuration options, create a settings_pack. object and fill it with
	// the settings to be set and pass it in to ``session::apply_settings()``.
	//
	// see apply_settings().
	struct TORRENT_EXPORT session : session_handle
	{
		// Constructs the session objects which acts as the container of torrents.
		// In order to avoid a race condition between starting the session and
		// configuring it, you can pass in a session_params object. Its settings
		// will take effect before the session starts up.
		//
		// The overloads taking ``flags`` can be used to start a session in
		// paused mode (by passing in ``session::paused``). Note that
		// ``add_default_plugins`` do not have an affect on constructors that
		// take a session_params object. It already contains the plugins to use.
		explicit session(session_params const& params);
		explicit session(session_params&& params);
		session(session_params const& params, session_flags_t flags);
		session(session_params&& params, session_flags_t flags);
		session();

		// Overload of the constructor that takes an external io_context to run
		// the session object on. This is primarily useful for tests that may want
		// to run multiple sessions on a single io_context, or low resource
		// systems where additional threads are expensive and sharing an
		// io_context with other events is fine.
		//
		// .. warning::
		// 	The session object does not cleanly terminate with an external
		// 	``io_context``. The ``io_context::run()`` call *must* have returned
		// 	before it's safe to destruct the session. Which means you *MUST*
		// 	call session::abort() and save the session_proxy first, then
		// 	destruct the session object, then sync with the io_context, then
		// 	destruct the session_proxy object.
		session(session_params&& params, io_context& ios);
		session(session_params const& params, io_context& ios);
		session(session_params&& params, io_context& ios, session_flags_t);
		session(session_params const& params, io_context& ios, session_flags_t);

		// hidden
		session(session&&);
		session& operator=(session&&) &;

		// hidden
		session(session const&) = delete;
		session& operator=(session const&) = delete;

#if TORRENT_ABI_VERSION <= 2
#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

		// Constructs the session objects which acts as the container of torrents.
		// It provides configuration options across torrents (such as rate limits,
		// disk cache, ip filter etc.). In order to avoid a race condition between
		// starting the session and configuring it, you can pass in a
		// settings_pack object. Its settings will take effect before the session
		// starts up.
		//
		// The ``flags`` parameter can be used to start default features (UPnP &
		// NAT-PMP) and default plugins (ut_metadata, ut_pex and smart_ban). The
		// default is to start those features. If you do not want them to start,
		// pass 0 as the flags parameter.
		TORRENT_DEPRECATED
		session(settings_pack&& pack, session_flags_t const flags);
		TORRENT_DEPRECATED
		session(settings_pack const& pack, session_flags_t const flags);
		explicit session(settings_pack&& pack) : session(std::move(pack), add_default_plugins) {}
		explicit session(settings_pack const& pack) : session(pack, add_default_plugins) {}

		// overload of the constructor that takes an external io_context to run
		// the session object on. This is primarily useful for tests that may want
		// to run multiple sessions on a single io_context, or low resource
		// systems where additional threads are expensive and sharing an
		// io_context with other events is fine.
		//
		// .. warning::
		// 	The session object does not cleanly terminate with an external
		// 	``io_context``. The ``io_context::run()`` call _must_ have returned
		// 	before it's safe to destruct the session. Which means you *MUST*
		// 	call session::abort() and save the session_proxy first, then
		// 	destruct the session object, then sync with the io_context, then
		// 	destruct the session_proxy object.
		TORRENT_DEPRECATED
		session(settings_pack&&, io_context&, session_flags_t);
		TORRENT_DEPRECATED
		session(settings_pack const&, io_context&, session_flags_t);
		session(settings_pack&& pack, io_context& ios) : session(std::move(pack), ios, add_default_plugins) {}
		session(settings_pack const& pack, io_context& ios) : session(pack, ios, add_default_plugins) {}

#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif // TORRENT_ABI_VERSION

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

		TORRENT_DEPRECATED
		session(fingerprint const& print
			, session_flags_t const flags = start_default_features | add_default_plugins
			, alert_category_t const alert_mask = alert_category::error);

		TORRENT_DEPRECATED
		session(fingerprint const& print
			, std::pair<int, int> listen_port_range
			, char const* listen_interface = "0.0.0.0"
			, session_flags_t const flags = start_default_features | add_default_plugins
			, alert_category_t const alert_mask = alert_category::error);

#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif // TORRENT_ABI_VERSION

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
		// 	struct session_proxy {};
		session_proxy abort();

	private:

		void start(session_flags_t, session_params&& params, io_context* ios);

#if TORRENT_ABI_VERSION <= 2
		void start(session_flags_t flags, settings_pack&& sp, io_context* ios);
#endif

		void start(session_params const& params, io_context* ios) = delete;
#if TORRENT_ABI_VERSION <= 2
		void start(session_flags_t flags, settings_pack const& sp, io_context* ios) = delete;
#endif

		// data shared between the main thread
		// and the working thread
		std::shared_ptr<io_context> m_io_service;
		std::shared_ptr<std::thread> m_thread;
		std::shared_ptr<aux::session_impl> m_impl;
	};

}

#endif // TORRENT_SESSION_HPP_INCLUDED
