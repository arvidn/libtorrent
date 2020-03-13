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

#ifndef TORRENT_UPNP_HPP
#define TORRENT_UPNP_HPP

#include "libtorrent/socket.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/resolver.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/string_util.hpp"
#include "libtorrent/aux_/portmap.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/openssl.hpp" // for ssl::context

#include <memory>
#include <functional>
#include <set>

namespace libtorrent {
	struct http_connection;
	class http_parser;

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
			// The external port cannot be a wildcard, but must
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
			// specific IP address or DNS name
			remote_host_must_be_wildcard = 726,
			// ExternalPort must be a wildcard and cannot be a
			// specific port
			external_port_must_be_wildcard = 727
		};

		// hidden
		TORRENT_EXPORT boost::system::error_code make_error_code(error_code_enum e);
	}

	// the boost.system error category for UPnP errors
	TORRENT_EXPORT boost::system::error_category& upnp_category();

#if TORRENT_ABI_VERSION == 1
	TORRENT_DEPRECATED
	inline boost::system::error_category& get_upnp_category()
	{ return upnp_category(); }
#endif

struct parse_state
{
	bool in_service = false;
	std::vector<string_view> tag_stack;
	std::string control_url;
	std::string service_type;
	std::string model;
	std::string url_base;
	bool top_tags(string_view str1, string_view str2)
	{
		auto i = tag_stack.rbegin();
		if (i == tag_stack.rend()) return false;
		if (!string_equal_no_case(*i, str2)) return false;
		++i;
		if (i == tag_stack.rend()) return false;
		if (!string_equal_no_case(*i, str1)) return false;
		return true;
	}
};

struct error_code_parse_state
{
	bool in_error_code = false;
	bool exit = false;
	int error_code = -1;
};

struct ip_address_parse_state: error_code_parse_state
{
	bool in_ip_address = false;
	std::string ip_address;
};

TORRENT_EXTRA_EXPORT void find_control_url(int type, string_view, parse_state& state);

TORRENT_EXTRA_EXPORT void find_error_code(int type, string_view string
	, error_code_parse_state& state);

TORRENT_EXTRA_EXPORT void find_ip_address(int type, string_view string
	, ip_address_parse_state& state);

// TODO: support using the windows API for UPnP operations as well
struct TORRENT_EXTRA_EXPORT upnp final
	: std::enable_shared_from_this<upnp>
	, single_threaded
{
	upnp(io_service& ios
		, aux::session_settings const& settings
		, aux::portmap_callback& cb
		, address_v4 const& listen_address
		, address_v4 const& netmask
		, std::string listen_device);
	~upnp();

	void start();

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
	port_mapping_t add_mapping(portmap_protocol p, int external_port, tcp::endpoint local_ep);

	// This function removes a port mapping. ``mapping_index`` is the index that refers
	// to the mapping you want to remove, which was returned from add_mapping().
	void delete_mapping(port_mapping_t mapping_index);

	bool get_mapping(port_mapping_t mapping_index, tcp::endpoint& local_ep, int& external_port
		, portmap_protocol& protocol) const;

	void close();

	// This is only available for UPnP routers. If the model is advertised by
	// the router, it can be queried through this function.
	std::string router_model()
	{
		TORRENT_ASSERT(is_single_thread());
		return m_model;
	}

private:

	std::shared_ptr<upnp> self() { return shared_from_this(); }

	void open_multicast_socket(udp::socket& s, error_code& ec);
	void open_unicast_socket(udp::socket& s, error_code& ec);

	void map_timer(error_code const& ec);
	void try_map_upnp();
	void discover_device_impl();

	void resend_request(error_code const& e);
	void on_reply(udp::socket& s, error_code const& ec);

	struct rootdevice;
	void next(rootdevice& d, port_mapping_t i);
	void update_map(rootdevice& d, port_mapping_t i);

	void connect(rootdevice& d);

	void on_upnp_xml(error_code const& e
		, libtorrent::http_parser const& p, rootdevice& d
		, http_connection& c);
	void on_upnp_get_ip_address_response(error_code const& e
		, libtorrent::http_parser const& p, rootdevice& d
		, http_connection& c);
	void on_upnp_map_response(error_code const& e
		, libtorrent::http_parser const& p, rootdevice& d
		, port_mapping_t mapping, http_connection& c);
	void on_upnp_unmap_response(error_code const& e
		, libtorrent::http_parser const& p, rootdevice& d
		, port_mapping_t mapping, http_connection& c);
	void on_expire(error_code const& e);

	void disable(error_code const& ec);
	void return_error(port_mapping_t mapping, int code);
#ifndef TORRENT_DISABLE_LOGGING
	bool should_log() const;
	void log(char const* msg, ...) const TORRENT_FORMAT(2,3);
#endif

	void get_ip_address(rootdevice& d);
	void delete_port_mapping(rootdevice& d, port_mapping_t i);
	void create_port_mapping(http_connection& c, rootdevice& d, port_mapping_t i);
	void post(upnp::rootdevice const& d, char const* soap
		, char const* soap_action);

	int num_mappings() const { return int(m_mappings.size()); }

	struct global_mapping_t
	{
		portmap_protocol protocol = portmap_protocol::none;
		int external_port = 0;
		tcp::endpoint local_ep;
	};

	struct mapping_t : aux::base_mapping
	{
		// the local port for this mapping. If this is set
		// to 0, the mapping is not in use
		tcp::endpoint local_ep;

		// the number of times this mapping has failed
		int failcount = 0;
	};

	struct rootdevice
	{
		rootdevice();
		~rootdevice();
		rootdevice(rootdevice const&);
		rootdevice& operator=(rootdevice const&);
		rootdevice(rootdevice&&);
		rootdevice& operator=(rootdevice&&);

		// the interface url, through which the list of
		// supported interfaces are fetched
		std::string url;

		// the url to the WANIP or WANPPP interface
		std::string control_url;
		// either the WANIP namespace or the WANPPP namespace
		std::string service_namespace;

		aux::vector<mapping_t, port_mapping_t> mapping;

		// this is the hostname, port and path
		// component of the url or the control_url
		// if it has been found
		std::string hostname;
		int port = 0;
		std::string path;
		address external_ip;

		// default lease duration for a port map.
		int lease_duration = 3600;

		// true if the device supports specifying a
		// specific external port, false if it doesn't
		bool supports_specific_external = true;

		bool disabled = false;

		mutable std::shared_ptr<http_connection> upnp_connection;

#if TORRENT_USE_ASSERTS
		int magic = 1337;
#endif

		bool operator<(rootdevice const& rhs) const
		{ return url < rhs.url; }
	};

	struct upnp_state_t
	{
		aux::vector<global_mapping_t, port_mapping_t> mappings;
		std::set<rootdevice> devices;
	};

	aux::vector<global_mapping_t, port_mapping_t> m_mappings;

	aux::session_settings const& m_settings;

	// the set of devices we've found
	std::set<rootdevice> m_devices;

	aux::portmap_callback& m_callback;

	// current retry count
	int m_retry_count = 0;

	io_service& m_io_service;

	resolver m_resolver;

	// the udp socket used to send and receive
	// multicast messages on the network
	udp::socket m_multicast_socket;
	udp::socket m_unicast_socket;

	// used to resend udp packets in case
	// they time out
	deadline_timer m_broadcast_timer;

	// timer used to refresh mappings
	deadline_timer m_refresh_timer;

	// this timer fires one second after the last UPnP response. This is the
	// point where we assume we have received most or all SSDP responses. If we
	// are ignoring non-routers and at this point we still haven't received a
	// response from a router UPnP device, we override the ignoring behavior and
	// map them anyway.
	deadline_timer m_map_timer;

	bool m_disabled = false;
	bool m_closing = false;

	std::string m_model;

	// the network this UPnP mapper is associated with. Don't talk to any other
	// network
	address_v4 m_listen_address;
	address_v4 m_netmask;
	std::string m_device;

#ifdef TORRENT_USE_OPENSSL
	ssl::context m_ssl_ctx;
#endif
};

}

namespace boost { namespace system {

	template<> struct is_error_code_enum<libtorrent::upnp_errors::error_code_enum>
	{ static const bool value = true; };

} }

#endif
