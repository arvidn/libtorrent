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

#ifndef TORRENT_UPNP_HPP
#define TORRENT_UPNP_HPP

#include "libtorrent/socket.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/http_connection.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"

#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <set>


#if (defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)) && !defined (TORRENT_UPNP_LOGGING)
#define TORRENT_UPNP_LOGGING
#endif

#if defined(TORRENT_UPNP_LOGGING)
#include <fstream>
#endif

namespace libtorrent
{

// int: external tcp port
// int: external udp port
// std::string: error message
typedef boost::function<void(int, int, std::string const&)> portmap_callback_t;

class upnp : public intrusive_ptr_base<upnp>
{
public:
	upnp(io_service& ios, connection_queue& cc
		, address const& listen_interface, std::string const& user_agent
		, portmap_callback_t const& cb, bool ignore_nonrouters);
	~upnp();

	// maps the ports, if a port is set to 0
	// it will not be mapped
	void set_mappings(int tcp, int udp);

	void discover_device();
	void close();

private:

	static address_v4 upnp_multicast_address;
	static udp::endpoint upnp_multicast_endpoint;

	enum { num_mappings = 2 };
	enum { default_lease_time = 3600 };
	
	void update_mapping(int i, int port);
	void resend_request(asio::error_code const& e);
	void on_reply(udp::endpoint const& from, char* buffer
		, std::size_t bytes_transferred);

	struct rootdevice;
	
	void on_upnp_xml(asio::error_code const& e
		, libtorrent::http_parser const& p, rootdevice& d);
	void on_upnp_map_response(asio::error_code const& e
		, libtorrent::http_parser const& p, rootdevice& d
		, int mapping);
	void on_upnp_unmap_response(asio::error_code const& e
		, libtorrent::http_parser const& p, rootdevice& d
		, int mapping);
	void on_expire(asio::error_code const& e);

	void map_port(rootdevice& d, int i);
	void unmap_port(rootdevice& d, int i);
	void disable();

	void delete_port_mapping(rootdevice& d, int i);
	void create_port_mapping(http_connection& c, rootdevice& d, int i);
	void post(upnp::rootdevice const& d, std::string const& soap
		, std::string const& soap_action);

	struct mapping_t
	{
		mapping_t()
			: need_update(false)
			, local_port(0)
			, external_port(0)
			, protocol(1)
		{}

		// the time the port mapping will expire
		ptime expires;
		
		bool need_update;

		// the local port for this mapping. If this is set
		// to 0, the mapping is not in use
		int local_port;

		// the external (on the NAT router) port
		// for the mapping. This is the port we
		// should announce to others
		int external_port;

		// 1 = udp, 0 = tcp
		int protocol;
	};

	struct rootdevice
	{
		rootdevice(): service_namespace(0)
			, lease_duration(default_lease_time)
			, supports_specific_external(true)
			, disabled(false)
		{
			mapping[0].protocol = 0;
			mapping[1].protocol = 1;
#ifndef NDEBUG
			magic = 1337;
#endif
		}

#ifndef NDEBUG
		~rootdevice()
		{
			TORRENT_ASSERT(magic == 1337);
			magic = 0;
		}
#endif
		
		// the interface url, through which the list of
		// supported interfaces are fetched
		std::string url;
	
		// the url to the WANIP or WANPPP interface
		std::string control_url;
		// either the WANIP namespace or the WANPPP namespace
		char const* service_namespace;

		mapping_t mapping[num_mappings];
		
		std::string hostname;
		int port;
		std::string path;

		int lease_duration;
		// true if the device supports specifying a
		// specific external port, false if it doesn't
		bool supports_specific_external;
		
		bool disabled;

		mutable boost::shared_ptr<http_connection> upnp_connection;

#ifndef NDEBUG
		int magic;
#endif
		void close() const
		{
			TORRENT_ASSERT(magic == 1337);
			if (!upnp_connection) return;
			upnp_connection->close();
			upnp_connection.reset();
		}
		
		bool operator<(rootdevice const& rhs) const
		{ return url < rhs.url; }
	};
	
	int m_udp_local_port;
	int m_tcp_local_port;

	std::string const& m_user_agent;
	
	// the set of devices we've found
	std::set<rootdevice> m_devices;
	
	portmap_callback_t m_callback;

	// current retry count
	int m_retry_count;

	asio::io_service& m_io_service;

	asio::strand m_strand;	

	// the udp socket used to send and receive
	// multicast messages on the network
	broadcast_socket m_socket;

	// used to resend udp packets in case
	// they time out
	deadline_timer m_broadcast_timer;

	// timer used to refresh mappings
	deadline_timer m_refresh_timer;
	
	bool m_disabled;
	bool m_closing;
	bool m_ignore_outside_network;

	connection_queue& m_cc;

#ifdef TORRENT_UPNP_LOGGING
	std::ofstream m_log;
#endif
};

}


#endif

