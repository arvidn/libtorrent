/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2016, 2020-2021, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_PEER_CONNECTION_INTERFACE_HPP
#define TORRENT_PEER_CONNECTION_INTERFACE_HPP

#include "libtorrent/fwd.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/operations.hpp" // for operation_t enum
#include "libtorrent/units.hpp"

namespace lt {

	namespace aux {
		struct torrent_peer;
		class stat;
	}

	enum class connection_type : std::uint8_t
	{
		bittorrent,
		url_seed,
		http_seed
	};

	using disconnect_severity_t = aux::strong_typedef<std::uint8_t, struct disconnect_severity_tag>;

	// TODO: make this interface smaller!
	struct TORRENT_EXTRA_EXPORT peer_connection_interface
	{
		static inline constexpr disconnect_severity_t normal{0};
		static inline constexpr disconnect_severity_t failure{1};
		static inline constexpr disconnect_severity_t peer_error{2};

		virtual tcp::endpoint const& remote() const = 0;
		virtual tcp::endpoint local_endpoint() const = 0;
		virtual void disconnect(error_code const& ec
			, operation_t op, disconnect_severity_t = peer_connection_interface::normal) = 0;
		virtual peer_id const& pid() const = 0;
		virtual peer_id our_pid() const = 0;
		virtual void set_holepunch_mode() = 0;
		virtual aux::torrent_peer* peer_info_struct() const = 0;
		virtual void set_peer_info(aux::torrent_peer* pi) = 0;
		virtual bool is_outgoing() const = 0;
		virtual void add_stat(std::int64_t downloaded, std::int64_t uploaded) = 0;
		virtual bool fast_reconnect() const = 0;
		virtual bool is_choked() const = 0;
		virtual bool failed() const = 0;
		virtual aux::stat const& statistics() const = 0;
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
