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
#include "libtorrent/error_code.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/http_connection.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/deadline_timer.hpp"

#include <boost/function/function1.hpp>
#include <boost/function/function3.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <set>


#if defined(TORRENT_UPNP_LOGGING)
#include <fstream>
#endif

namespace libtorrent
{

	namespace upnp_errors
	{
		enum error_code_enum
		{
			no_error = 0,
			invalid_argument = 402,
			action_failed = 501,
			value_not_in_array = 714,
			source_ip_cannot_be_wildcarded = 715,
			external_port_cannot_be_wildcarded = 716,
			port_mapping_conflict = 718,
			internal_port_must_match_external = 724,
			only_permanent_leases_supported = 725,
			remote_host_must_be_wildcard = 726,
			external_port_must_be_wildcard = 727
		};
	}

#if BOOST_VERSION < 103500
	extern asio::error::error_category upnp_category;
#else

	struct TORRENT_EXPORT upnp_error_category : boost::system::error_category
	{
		virtual const char* name() const;
		virtual std::string message(int ev) const;
		virtual boost::system::error_condition default_error_condition(int ev) const
		{ return boost::system::error_condition(ev, *this); }
	};

	extern TORRENT_EXPORT upnp_error_category upnp_category;
#endif

// int: port-mapping index
// address: external address as queried from router
// int: external port
// std::string: error message
// an empty string as error means success
// a port-mapping index of -1 means it's
// an informational log message
typedef boost::function<void(int, address, int, error_code const&)> portmap_callback_t;
typedef boost::function<void(char const*)> log_callback_t;

class TORRENT_EXPORT upnp : public intrusive_ptr_base<upnp>
{
public:
	upnp(io_service& ios, connection_queue& cc
		, address const& listen_interface, std::string const& user_agent
		, portmap_callback_t const& cb, log_callback_t const& lcb
		, bool ignore_nonrouters, void* state = 0);
	~upnp();

	void* drain_state();

	enum protocol_type { none = 0, udp = 1, tcp = 2 };
	int add_mapping(protocol_type p, int external_port, int local_port);
	void delete_mapping(int mapping_index);
	bool get_mapping(int mapping_index, int& local_port, int& external_port, int& protocol) const;

	void discover_device();
	void close();

	std::string router_model()
	{
		mutex::scoped_lock l(m_mutex);
		return m_model;
	}

private:

	void discover_device_impl(mutex::scoped_lock& l);
	static address_v4 upnp_multicast_address;
	static udp::endpoint upnp_multicast_endpoint;

	// there are routers that's don't support timed
	// port maps, without returning error 725. It seems
	// safer to always assume that we have to ask for
	// permanent leases
	enum { default_lease_time = 0 };
	
	void resend_request(error_code const& e);
	void on_reply(udp::endpoint const& from, char* buffer
		, std::size_t bytes_transferred);

	struct rootdevice;
	void next(rootdevice& d, int i, mutex::scoped_lock& l);
	void update_map(rootdevice& d, int i, mutex::scoped_lock& l);

	
	void on_upnp_xml(error_code const& e
		, libtorrent::http_parser const& p, rootdevice& d
		, http_connection& c);
	void on_upnp_get_ip_address_response(error_code const& e
		, libtorrent::http_parser const& p, rootdevice& d
		, http_connection& c);
	void on_upnp_map_response(error_code const& e
		, libtorrent::http_parser const& p, rootdevice& d
		, int mapping, http_connection& c);
	void on_upnp_unmap_response(error_code const& e
		, libtorrent::http_parser const& p, rootdevice& d
		, int mapping, http_connection& c);
	void on_expire(error_code const& e);

	void disable(error_code const& ec, mutex::scoped_lock& l);
	void return_error(int mapping, int code, mutex::scoped_lock& l);
	void log(char const* msg, mutex::scoped_lock& l);

	void get_ip_address(rootdevice& d);
	void delete_port_mapping(rootdevice& d, int i);
	void create_port_mapping(http_connection& c, rootdevice& d, int i);
	void post(upnp::rootdevice const& d, char const* soap
		, char const* soap_action, mutex::scoped_lock& l);

	int num_mappings() const { return int(m_mappings.size()); }

	struct global_mapping_t
	{
		global_mapping_t()
			: protocol(none)
			, external_port(0)
			, local_port(0)
		{}
		int protocol;
		int external_port;
		int local_port;
	};

	struct mapping_t
	{
		enum action_t { action_none, action_add, action_delete };
		mapping_t()
			: action(action_none)
			, local_port(0)
			, external_port(0)
			, protocol(none)
			, failcount(0)
		{}

		// the time the port mapping will expire
		ptime expires;
		
		int action;

		// the local port for this mapping. If this is set
		// to 0, the mapping is not in use
		int local_port;

		// the external (on the NAT router) port
		// for the mapping. This is the port we
		// should announce to others
		int external_port;

		// 2 = udp, 1 = tcp
		int protocol;

		// the number of times this mapping has failed
		int failcount;
	};

	struct rootdevice
	{
		rootdevice(): service_namespace(0)
			, port(0)
			, lease_duration(default_lease_time)
			, supports_specific_external(true)
			, disabled(false)
		{
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			magic = 1337;
#endif
		}

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
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

		std::vector<mapping_t> mapping;
		
		// this is the hostname, port and path
		// component of the url or the control_url
		// if it has been found
		std::string hostname;
		int port;
		std::string path;
		address external_ip;

		int lease_duration;
		// true if the device supports specifying a
		// specific external port, false if it doesn't
		bool supports_specific_external;
		
		bool disabled;

		mutable boost::shared_ptr<http_connection> upnp_connection;

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
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
	
	struct upnp_state_t
	{
		std::vector<global_mapping_t> mappings;
		std::set<rootdevice> devices;
	};

	std::vector<global_mapping_t> m_mappings;

	std::string const& m_user_agent;
	
	// the set of devices we've found
	std::set<rootdevice> m_devices;
	
	portmap_callback_t m_callback;
	log_callback_t m_log_callback;

	// current retry count
	int m_retry_count;

	io_service& m_io_service;

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
	bool m_ignore_non_routers;

	connection_queue& m_cc;

	mutex m_mutex;

	std::string m_model;
};

}


#endif

