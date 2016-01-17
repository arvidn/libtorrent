/*

Copyright (c) 2007-2016, Arvid Norberg
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
#include <boost/function/function4.hpp>
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
		// error codes for the upnp_error_category. They hold error codes
		// returned by UPnP routers when mapping ports
		enum error_code_enum
		{
			// No error
			no_error = 0,
			// One of the arguments in the request is invalid
			invalid_argument = 402,
			// The request failed
			action_failed = 501,
			// The specified value does not exist in the array
			value_not_in_array = 714,
			// The source IP address cannot be wild-carded, but
			// must be fully specified
			source_ip_cannot_be_wildcarded = 715,
			// The external port cannot be wildcarded, but must
			// be specified
			external_port_cannot_be_wildcarded = 716,
			// The port mapping entry specified conflicts with a
			// mapping assigned previously to another client
			port_mapping_conflict = 718,
			// Internal and external port value must be the same
			internal_port_must_match_external = 724,
			// The NAT implementation only supports permanent
			// lease times on port mappings
			only_permanent_leases_supported = 725,
			// RemoteHost must be a wildcard and cannot be a
			// specific IP addres or DNS name
			remote_host_must_be_wildcard = 726,
			// ExternalPort must be a wildcard and cannot be a
			// specific port
			external_port_must_be_wildcard = 727
		};

		// hidden
		TORRENT_EXPORT boost::system::error_code make_error_code(error_code_enum e);
	}

	// the boost.system error category for UPnP errors
	TORRENT_EXPORT boost::system::error_category& get_upnp_category();

// int: port-mapping index
// address: external address as queried from router
// int: external port
// std::string: error message
// an empty string as error means success
// a port-mapping index of -1 means it's
// an informational log message
typedef boost::function<void(int, address, int, error_code const&)> portmap_callback_t;
typedef boost::function<void(char const*)> log_callback_t;

// TODO: support using the windows API for UPnP operations as well
class TORRENT_EXTRA_EXPORT upnp : public intrusive_ptr_base<upnp>
{
public:
	upnp(io_service& ios, connection_queue& cc
		, address const& listen_interface, std::string const& user_agent
		, portmap_callback_t const& cb, log_callback_t const& lcb
		, bool ignore_nonrouters, void* state = 0);
	~upnp();

	void* drain_state();

	enum protocol_type { none = 0, udp = 1, tcp = 2 };

	// Attempts to add a port mapping for the specified protocol. Valid protocols are
	// ``upnp::tcp`` and ``upnp::udp`` for the UPnP class and ``natpmp::tcp`` and
	// ``natpmp::udp`` for the NAT-PMP class.
	// 
	// ``external_port`` is the port on the external address that will be mapped. This
	// is a hint, you are not guaranteed that this port will be available, and it may
	// end up being something else. In the portmap_alert_ notification, the actual
	// external port is reported.
	// 
	// ``local_port`` is the port in the local machine that the mapping should forward
	// to.
	// 
	// The return value is an index that identifies this port mapping. This is used
	// to refer to mappings that fails or succeeds in the portmap_error_alert_ and
	// portmap_alert_ respectively. If The mapping fails immediately, the return value
	// is -1, which means failure. There will not be any error alert notification for
	// mappings that fail with a -1 return value.
	int add_mapping(protocol_type p, int external_port, int local_port);

	// This function removes a port mapping. ``mapping_index`` is the index that refers
	// to the mapping you want to remove, which was returned from add_mapping().
	void delete_mapping(int mapping_index);

	bool get_mapping(int mapping_index, int& local_port, int& external_port, int& protocol) const;

	void discover_device();
	void close();

	// This is only available for UPnP routers. If the model is advertized by
	// the router, it can be queried through this function.
	std::string router_model()
	{
		mutex::scoped_lock l(m_mutex);
		return m_model;
	}

private:

	void map_timer(error_code const& ec);
	void try_map_upnp(mutex::scoped_lock& l, bool timer = false);
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
			, non_router(false)
		{
#if TORRENT_USE_ASSERTS
			magic = 1337;
#endif
		}

#if TORRENT_USE_ASSERTS
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

		// this is true if the IP of this device is not
		// one of our default routes. i.e. it may be someone
		// else's router, we just happen to have multicast
		// enabled across networks
		// this is only relevant if ignore_non_routers is set.
		bool non_router;

		mutable boost::shared_ptr<http_connection> upnp_connection;

#if TORRENT_USE_ASSERTS
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

	// this timer fires one second after the last UPnP response. This is the
	// point where we assume we have received most or all SSDP reponses. If we
	// are ignoring non-routers and at this point we still haven't received a
	// response from a router UPnP device, we override the ignoring behavior and
	// map them anyway.
	deadline_timer m_map_timer;
	
	bool m_disabled;
	bool m_closing;
	bool m_ignore_non_routers;

	connection_queue& m_cc;

	mutex m_mutex;

	std::string m_model;
};

}

#if BOOST_VERSION >= 103500

namespace boost { namespace system {

	template<> struct is_error_code_enum<libtorrent::upnp_errors::error_code_enum>
	{ static const bool value = true; };

	template<> struct is_error_condition_enum<libtorrent::upnp_errors::error_code_enum>
	{ static const bool value = true; };
} }

#endif // BOOST_VERSION

#endif

