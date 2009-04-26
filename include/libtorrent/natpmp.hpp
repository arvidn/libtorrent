/*

Copyright (c) 2007, Arvid Norberg
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

#include "libtorrent/socket.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"

#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
#include <fstream>
#endif

namespace libtorrent
{

// int: port mapping index
// int: external port
// std::string: error message
typedef boost::function<void(int, int, std::string const&)> portmap_callback_t;

class natpmp : public intrusive_ptr_base<natpmp>
{
public:
	natpmp(io_service& ios, address const& listen_interface, portmap_callback_t const& cb);

	void rebind(address const& listen_interface);

	// maps the ports, if a port is set to 0
	// it will not be mapped
	enum protocol_type { none = 0, udp = 1, tcp = 2 };
	int add_mapping(protocol_type p, int external_port, int local_port);
	void delete_mapping(int mapping_index);

	void close();

private:
	
	void update_mapping(int i);
	void send_map_request(int i);
	void resend_request(int i, error_code const& e);
	void on_reply(error_code const& e
		, std::size_t bytes_transferred);
	void try_next_mapping(int i);
	void update_expiration_timer();
	void mapping_expired(error_code const& e, int i);

	void disable(char const* message);

	struct mapping_t
	{
		enum action_t { action_none, action_add, action_delete };
		mapping_t()
			: action(action_none)
			, local_port(0)
			, external_port(0)
			, protocol(none)
		{}

		// indicates that the mapping has changed
		// and needs an update
		int action;

		// the time the port mapping will expire
		ptime expires;

		// the local port for this mapping. If this is set
		// to 0, the mapping is not in use
		int local_port;

		// the external (on the NAT router) port
		// for the mapping. This is the port we
		// should announce to others
		int external_port;

		int protocol;
	};

	portmap_callback_t m_callback;

	std::vector<mapping_t> m_mappings;
	
	// the endpoint to the nat router
	udp::endpoint m_nat_endpoint;

	// this is the mapping that is currently
	// being updated. It is -1 in case no
	// mapping is being updated at the moment
	int m_currently_mapping;

	// current retry count
	int m_retry_count;

	// used to receive responses in	
	char m_response_buffer[16];

	// the endpoint we received the message from
	udp::endpoint m_remote;
	
	// the udp socket used to communicate
	// with the NAT router
	datagram_socket m_socket;

	// used to resend udp packets in case
	// they time out
	deadline_timer m_send_timer;

	// timer used to refresh mappings
	deadline_timer m_refresh_timer;

	// the mapping index that will expire next
	int m_next_refresh;
	
	bool m_disabled;

	bool m_abort;

	typedef boost::mutex mutex_t;
	mutex_t m_mutex;

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	std::ofstream m_log;
#endif
};

}


#endif

