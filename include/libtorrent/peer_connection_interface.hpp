/*

Copyright (c) 2013-2018, Arvid Norberg
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

#ifndef TORRENT_PEER_CONNECTION_INTERFACE_HPP
#define TORRENT_PEER_CONNECTION_INTERFACE_HPP

#include "libtorrent/fwd.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/operations.hpp" // for operation_t enum
#include "libtorrent/units.hpp"

namespace libtorrent {

	struct torrent_peer;
	class stat;

	using disconnect_severity_t = aux::strong_typedef<std::uint8_t, struct disconnect_severity_tag>;

	// TODO: make this interface smaller!
	struct TORRENT_EXTRA_EXPORT peer_connection_interface
	{
		static constexpr disconnect_severity_t normal{0};
		static constexpr disconnect_severity_t failure{1};
		static constexpr disconnect_severity_t peer_error{2};

		virtual tcp::endpoint const& remote() const = 0;
		virtual tcp::endpoint local_endpoint() const = 0;
		virtual void disconnect(error_code const& ec
			, operation_t op, disconnect_severity_t = peer_connection_interface::normal) = 0;
		virtual peer_id const& pid() const = 0;
		virtual peer_id our_pid() const = 0;
		virtual void set_holepunch_mode() = 0;
		virtual torrent_peer* peer_info_struct() const = 0;
		virtual void set_peer_info(torrent_peer* pi) = 0;
		virtual bool is_outgoing() const = 0;
		virtual void add_stat(std::int64_t downloaded, std::int64_t uploaded) = 0;
		virtual bool fast_reconnect() const = 0;
		virtual bool is_choked() const = 0;
		virtual bool failed() const = 0;
		virtual stat const& statistics() const = 0;
		virtual void get_peer_info(peer_info& p) const = 0;
#ifndef TORRENT_DISABLE_LOGGING
		virtual bool should_log(peer_log_alert::direction_t direction) const = 0;
		virtual void peer_log(peer_log_alert::direction_t direction
			, char const* event, char const* fmt = "", ...) const noexcept TORRENT_FORMAT(4,5) = 0;
#endif
	protected:
		~peer_connection_interface() {}
	};
}

#endif
