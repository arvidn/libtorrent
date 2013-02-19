/*

Copyright (c) 2012-2013, Arvid Norberg
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

#ifndef TORRENT_TORRENT_INTERFACE_HPP_INCLUDED
#define TORRENT_TORRENT_INTERFACE_HPP_INCLUDED

namespace libtorrent
{

	class peer_connection;
	class torrent_info;
	struct torrent_handle;
	class alert_manager;

	namespace aux
	{
		struct session_settings;
	}

	struct torrent_interface
	{
		virtual piece_picker& picker() = 0;
		virtual int num_peers() const = 0;
		virtual aux::session_settings const& settings() const = 0;
		virtual external_ip const& external_address() const = 0;
		virtual int listen_port() const = 0;
		virtual bool apply_ip_filter() const = 0;
		virtual bool is_i2p() const = 0;

		virtual int port_filter_access(int port) const = 0;
		virtual int ip_filter_access(address const& addr) const = 0;

		virtual torrent_peer* allocate_peer_entry(int type) = 0;
		virtual void free_peer_entry(torrent_peer* p) = 0;

		virtual alert_manager& alerts() const = 0;

		virtual bool has_picker() const = 0;

		// this is only used to determine the max peer list size. That could be controlled externally
		virtual bool is_paused() const = 0;

		// some of these properties are really only used to determine which kinds of peers are connect
		// candidates. raise the abstraction level to hand the peer to the torrent and ask it if
		// it's a connect candidate or not.. maybe?
		virtual bool is_finished() const = 0;

		// this is only used when recalculating or altering the number of connect candidates.
		// it could be done by the caller instead
		virtual void update_want_peers() = 0;

		// this is used when we add a new connection, and it succeeds to actually be added
		virtual void state_updated() = 0;

		// this is only used to post alerts. raise the abstraction level, don't do that from within policy
		virtual torrent_handle get_handle() = 0;
#ifndef TORRENT_DISABLE_EXTENSIONS
		virtual void notify_extension_add_peer(tcp::endpoint const& ip, int src, int flags) = 0;
#endif
		virtual bool connect_to_peer(torrent_peer* peerinfo, bool ignore_limit = false) = 0;
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		virtual std::string name() const = 0;
		virtual void debug_log(const char* fmt, ...) const = 0;
		virtual void session_log(char const* fmt, ...) const = 0;
#endif
	};

}

#endif

