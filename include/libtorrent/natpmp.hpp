/*

Copyright (c) 2007-2018, Arvid Norberg
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

#ifndef TORRENT_NATPMP_HPP
#define TORRENT_NATPMP_HPP

#include "libtorrent/io_service_fwd.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/aux_/portmap.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/enum_net.hpp" // for ip_interface

namespace libtorrent {

	namespace errors
	{
		// See RFC 6887 Section 7.4
		enum pcp_errors
		{
			pcp_success = 0,
			pcp_unsupp_version,
			pcp_not_authorized,
			pcp_malformed_request,
			pcp_unsupp_opcode,
			pcp_unsupp_option,
			pcp_malformed_option,
			pcp_network_failure,
			pcp_no_resources,
			pcp_unsupp_protocol,
			pcp_user_ex_quota,
			pcp_cannot_provide_external,
			pcp_address_mismatch,
			pcp_excessive_remote_peers,
		};

		boost::system::error_code make_error_code(pcp_errors e);
	}

	TORRENT_EXPORT boost::system::error_category& pcp_category();
}

namespace boost { namespace system {
	template<> struct is_error_code_enum<libtorrent::errors::pcp_errors>
	{ static const bool value = true; };
} }

namespace libtorrent {

struct TORRENT_EXTRA_EXPORT natpmp
	: std::enable_shared_from_this<natpmp>
	, single_threaded
{
	natpmp(io_service& ios, aux::portmap_callback& cb);

	void start(ip_interface const& ip);

	// maps the ports, if a port is set to 0
	// it will not be mapped
	port_mapping_t add_mapping(portmap_protocol p, int external_port, tcp::endpoint local_ep);
	void delete_mapping(port_mapping_t mapping_index);
	bool get_mapping(port_mapping_t mapping_index, int& local_port, int& external_port
		, portmap_protocol& protocol) const;

	void close();

private:
	static error_code from_result_code(int version, int result);

	std::shared_ptr<natpmp> self() { return shared_from_this(); }

	void update_mapping(port_mapping_t i);
	void send_map_request(port_mapping_t i);
	void send_get_ip_address_request();
	void on_resend_request(port_mapping_t i, error_code const& e);
	void resend_request(port_mapping_t);
	void on_reply(error_code const& e
		, std::size_t bytes_transferred);
	void try_next_mapping(port_mapping_t i);
	void update_expiration_timer();
	void mapping_expired(error_code const& e, port_mapping_t i);
	void close_impl();

	void disable(error_code const& ec);

	enum protocol_version
	{
		version_natpmp = 0,
		version_pcp = 2,
	};

	static char const* version_to_string(protocol_version version);

	// See RFC 6887 Section 19.2
	enum pcp_opcode
	{
		opcode_announce = 0,
		opcode_map,
		opcode_peer,
	};

	struct mapping_t : aux::base_mapping
	{
		// random identifier, used by PCP
		std::array<char, 12> nonce;

		// only valid if the router supports PCP
		address external_address;

		// the local port for this mapping. If this is set
		// to 0, the mapping is not in use
		int local_port = 0;

		// set to true when the first map request is sent
		bool map_sent = false;

		// set to true while we're waiting for a response
		bool outstanding_request = false;
	};

#ifndef TORRENT_DISABLE_LOGGING
	bool should_log() const;
	void log(char const* fmt, ...) const TORRENT_FORMAT(2, 3);
	void mapping_log(char const* op, mapping_t const& m) const;
#endif

	aux::portmap_callback& m_callback;

	protocol_version m_version = version_pcp;

	aux::vector<mapping_t, port_mapping_t> m_mappings;

	// the endpoint to the nat router
	udp::endpoint m_nat_endpoint;

	// this is the mapping that is currently
	// being updated. It is -1 in case no
	// mapping is being updated at the moment
	port_mapping_t m_currently_mapping{-1};

	// current retry count
	int m_retry_count = 0;

	// used to receive responses in
	// 1100 octets is the maximum size of a PCP packet
	char m_response_buffer[1100];

	// router external IP address
	// this is only used if the router does not support PCP
	// with PCP the external IP is stored with the mapping
	address m_external_ip;

	// the endpoint we received the message from
	udp::endpoint m_remote;

	// the udp socket used to communicate
	// with the NAT router
	udp::socket m_socket;

	// used to resend udp packets in case
	// they time out
	deadline_timer m_send_timer;

	// timer used to refresh mappings
	deadline_timer m_refresh_timer;

	// the mapping index that will expire next
	port_mapping_t m_next_refresh{-1};

	bool m_disabled = false;

	bool m_abort = false;
};

}

#endif
