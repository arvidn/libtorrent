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

#include "libtorrent/pch.hpp"

#include "libtorrent/socket.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/xml_parse.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/escape_string.hpp"

#include <boost/bind.hpp>
#include <boost/ref.hpp>
#if BOOST_VERSION < 103500
#include <asio/ip/host_name.hpp>
#include <asio/ip/multicast.hpp>
#else
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/ip/multicast.hpp>
#endif
#include <boost/thread/mutex.hpp>
#include <cstdlib>

#if (defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)) && !defined(TORRENT_UPNP_LOGGING)
#define TORRENT_UPNP_LOGGING
#endif

using boost::bind;
using namespace libtorrent;

static error_code ec;

upnp::upnp(io_service& ios, connection_queue& cc
	, address const& listen_interface, std::string const& user_agent
	, portmap_callback_t const& cb, bool ignore_nonrouters, void* state)
	:  m_user_agent(user_agent)
	, m_callback(cb)
	, m_retry_count(0)
	, m_io_service(ios)
	, m_socket(ios, udp::endpoint(address_v4::from_string("239.255.255.250", ec), 1900)
		, bind(&upnp::on_reply, self(), _1, _2, _3), false)
	, m_broadcast_timer(ios)
	, m_refresh_timer(ios)
	, m_disabled(false)
	, m_closing(false)
	, m_ignore_non_routers(ignore_nonrouters)
	, m_cc(cc)
{
#ifdef TORRENT_UPNP_LOGGING
	m_log.open("upnp.log", std::ios::in | std::ios::out | std::ios::trunc);
#endif
	m_retry_count = 0;

	if (state)
	{
		upnp_state_t* s = (upnp_state_t*)state;
		m_devices.swap(s->devices);
		m_mappings.swap(s->mappings);
		delete s;
	}
}

void* upnp::drain_state()
{
	upnp_state_t* s = new upnp_state_t;
	s->mappings.swap(m_mappings);

	for (std::set<rootdevice>::iterator i = m_devices.begin()
		, end(m_devices.end()); i != end; ++i)
		i->upnp_connection.reset();
	s->devices.swap(m_devices);
	return s;
}

upnp::~upnp()
{
}

void upnp::discover_device()
{
	mutex_t::scoped_lock l(m_mutex);

	discover_device_impl();
}

void upnp::discover_device_impl()
{
	const char msearch[] = 
		"M-SEARCH * HTTP/1.1\r\n"
		"HOST: 239.255.255.250:1900\r\n"
		"ST:upnp:rootdevice\r\n"
		"MAN:\"ssdp:discover\"\r\n"
		"MX:3\r\n"
		"\r\n\r\n";

	error_code ec;
#ifdef TORRENT_DEBUG_UPNP
	// simulate packet loss
	if (m_retry_count & 1)
#endif
	m_socket.send(msearch, sizeof(msearch) - 1, ec);

	if (ec)
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " ==> Broadcast FAILED: " << ec.message() << std::endl
			<< "aborting" << std::endl;
#endif
		disable(ec.message().c_str());
		return;
	}

	++m_retry_count;
	m_broadcast_timer.expires_from_now(seconds(2 * m_retry_count), ec);
	m_broadcast_timer.async_wait(bind(&upnp::resend_request
		, self(), _1));

#ifdef TORRENT_UPNP_LOGGING
	m_log << time_now_string()
		<< " ==> Broadcasting search for rootdevice" << std::endl;
#endif
}

// returns a reference to a mapping or -1 on failure
int upnp::add_mapping(upnp::protocol_type p, int external_port, int local_port)
{
	mutex_t::scoped_lock l(m_mutex);

#ifdef TORRENT_UPNP_LOGGING
	m_log << time_now_string()
		<< " *** add mapping [ proto: " << (p == tcp?"tcp":"udp")
		<< " ext_port: " << external_port
		<< " local_port :" << local_port << " ]";
	if (m_disabled) m_log << " DISABLED";
	m_log << std::endl;
#endif
	if (m_disabled) return -1;

	std::vector<global_mapping_t>::iterator i = std::find_if(
		m_mappings.begin(), m_mappings.end()
		, boost::bind(&global_mapping_t::protocol, _1) == int(none));

	if (i == m_mappings.end())
	{
		m_mappings.push_back(global_mapping_t());
		i = m_mappings.end() - 1;
	}

	i->protocol = p;
	i->external_port = external_port;
	i->local_port = local_port;

	int mapping_index = i - m_mappings.begin();

	for (std::set<rootdevice>::iterator i = m_devices.begin()
		, end(m_devices.end()); i != end; ++i)
	{
		rootdevice& d = const_cast<rootdevice&>(*i);
		TORRENT_ASSERT(d.magic == 1337);

		if (int(d.mapping.size()) <= mapping_index)
			d.mapping.resize(mapping_index + 1);
		mapping_t& m = d.mapping[mapping_index];

		m.action = mapping_t::action_add;
		m.protocol = p;
		m.external_port = external_port;
		m.local_port = local_port;

		if (d.service_namespace) update_map(d, mapping_index);
	}

	return mapping_index;
}

void upnp::delete_mapping(int mapping)
{
	mutex_t::scoped_lock l(m_mutex);

	if (mapping >= int(m_mappings.size())) return;

	global_mapping_t& m = m_mappings[mapping];

#ifdef TORRENT_UPNP_LOGGING
	m_log << time_now_string()
		<< " *** delete mapping [ proto: " << (m.protocol == tcp?"tcp":"udp")
		<< " ext_port:" << m.external_port
		<< " local_port:" << m.local_port << " ]";
	m_log << std::endl;
#endif

	if (m.protocol == none) return;
	
	for (std::set<rootdevice>::iterator i = m_devices.begin()
		, end(m_devices.end()); i != end; ++i)
	{
		rootdevice& d = const_cast<rootdevice&>(*i);
		TORRENT_ASSERT(d.magic == 1337);

		TORRENT_ASSERT(mapping < int(d.mapping.size()));
		d.mapping[mapping].action = mapping_t::action_delete;

		if (d.service_namespace) update_map(d, mapping);
	}
}

void upnp::resend_request(error_code const& e)
{
	if (e) return;

	boost::intrusive_ptr<upnp> me(self());

	mutex_t::scoped_lock l(m_mutex);

	if (m_closing) return;

	if (m_retry_count < 12
		&& (m_devices.empty() || m_retry_count < 4))
	{
		discover_device_impl();
		return;
	}

	if (m_devices.empty())
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " *** Got no response in 12 retries. Giving up, "
			"disabling UPnP." << std::endl;
#endif
		disable("no UPnP router found");
		return;
	}
	
	for (std::set<rootdevice>::iterator i = m_devices.begin()
		, end(m_devices.end()); i != end; ++i)
	{
		if (i->control_url.empty() && !i->upnp_connection && !i->disabled)
		{
			// we don't have a WANIP or WANPPP url for this device,
			// ask for it
			rootdevice& d = const_cast<rootdevice&>(*i);
			TORRENT_ASSERT(d.magic == 1337);
			try
			{
#ifdef TORRENT_UPNP_LOGGING
				m_log << time_now_string()
					<< " ==> connecting to " << d.url << std::endl;
#endif
				if (d.upnp_connection) d.upnp_connection->close();
				d.upnp_connection.reset(new http_connection(m_io_service
					, m_cc, bind(&upnp::on_upnp_xml, self(), _1, _2
					, boost::ref(d), _5)));
				d.upnp_connection->get(d.url, seconds(30), 1);
			}
			catch (std::exception& e)
			{
				(void)e;
#ifdef TORRENT_UPNP_LOGGING
				m_log << time_now_string()
					<< " *** Connection failed to: " << d.url
					<< " " << e.what() << std::endl;
#endif
				d.disabled = true;
			}
		}
	}
}

void upnp::on_reply(udp::endpoint const& from, char* buffer
	, std::size_t bytes_transferred)
{
	boost::intrusive_ptr<upnp> me(self());

	mutex_t::scoped_lock l(m_mutex);

	using namespace libtorrent::detail;

	// parse out the url for the device

/*
	the response looks like this:

	HTTP/1.1 200 OK
	ST:upnp:rootdevice
	USN:uuid:000f-66d6-7296000099dc::upnp:rootdevice
	Location: http://192.168.1.1:5431/dyndev/uuid:000f-66d6-7296000099dc
	Server: Custom/1.0 UPnP/1.0 Proc/Ver
	EXT:
	Cache-Control:max-age=180
	DATE: Fri, 02 Jan 1970 08:10:38 GMT

	a notification looks like this:

	NOTIFY * HTTP/1.1
	Host:239.255.255.250:1900
	NT:urn:schemas-upnp-org:device:MediaServer:1
	NTS:ssdp:alive
	Location:http://10.0.3.169:2869/upnphost/udhisapi.dll?content=uuid:c17f0c32-d19b-4938-ae94-65f945c3a26e
	USN:uuid:c17f0c32-d19b-4938-ae94-65f945c3a26e::urn:schemas-upnp-org:device:MediaServer:1
	Cache-Control:max-age=900
	Server:Microsoft-Windows-NT/5.1 UPnP/1.0 UPnP-Device-Host/1.0

*/
	error_code ec;
	if (!in_local_network(m_io_service, from.address(), ec))
	{
#ifdef TORRENT_UPNP_LOGGING
		if (ec)
		{
			m_log << time_now_string() << " <== (" << from << ") error: "
				<< ec.message() << std::endl;
		}
		else
		{
			m_log << time_now_string() << " <== (" << from << ") UPnP device "
				"ignored because it's not on our local network ";
			std::vector<ip_interface> net = enum_net_interfaces(m_io_service, ec);
			for (std::vector<ip_interface>::const_iterator i = net.begin()
				, end(net.end()); i != end; ++i)
			{
				m_log << "(" << i->interface_address << ", " << i->netmask << ") ";
			}
			m_log << std::endl;
		}
#endif
		return;
	} 

	if (m_ignore_non_routers)
	{
		std::vector<ip_route> routes = enum_routes(m_io_service, ec);
	
		if (std::find_if(routes.begin(), routes.end()
			, bind(&ip_route::gateway, _1) == from.address()) == routes.end())
		{
			// this upnp device is filtered because it's not in the
			// list of configured routers
#ifdef TORRENT_UPNP_LOGGING
			if (ec)
			{
				m_log << time_now_string() << " <== (" << from << ") error: "
					<< ec.message() << std::endl;
			}
			else
			{
				m_log << time_now_string() << " <== (" << from << ") UPnP device "
					"ignored because it's not a router on our network ";
				for (std::vector<ip_route>::const_iterator i = routes.begin()
					, end(routes.end()); i != end; ++i)
				{
					m_log << "(" << i->gateway << ", " << i->netmask << ") ";
				}
				m_log << std::endl;
			}
#endif
			return;
		}
	}

	http_parser p;
	bool error = false;
	p.incoming(buffer::const_interval(buffer
		, buffer + bytes_transferred), error);
	if (error)
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string() << " <== (" << from << ") Rootdevice "
			"responded with incorrect HTTP packet. Ignoring device" << std::endl;
#endif
		return;
	}

	if (p.status_code() != 200 && p.method() != "notify")
	{
#ifdef TORRENT_UPNP_LOGGING
		if (p.method().empty())
			m_log << time_now_string()
				<< " <== (" << from << ") Device responded with HTTP status: " << p.status_code()
				<< ". Ignoring device" << std::endl;
		else
			m_log << time_now_string()
				<< " <== (" << from << ") Device with HTTP method: " << p.method()
				<< ". Ignoring device" << std::endl;
#endif
		return;
	}

	if (!p.header_finished())
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== (" << from << ") Rootdevice responded with incomplete HTTP "
			"packet. Ignoring device" << std::endl;
#endif
		return;
	}

	std::string url = p.header("location");
	if (url.empty())
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== (" << from << ") Rootdevice response is missing a location header. "
			"Ignoring device" << std::endl;
#endif
		return;
	}

	rootdevice d;
	d.url = url;

	std::set<rootdevice>::iterator i = m_devices.find(d);

	if (i == m_devices.end())
	{

		std::string protocol;
		std::string auth;
		char const* error;
		// we don't have this device in our list. Add it
		boost::tie(protocol, auth, d.hostname, d.port, d.path, error)
			= parse_url_components(d.url);

		if (error)
		{
#ifdef TORRENT_UPNP_LOGGING
			m_log << time_now_string()
				<< " <== (" << from << ") Rootdevice advertized an invalid url: '" << d.url
				<< "'. " << error << ". Ignoring device" << std::endl;
#endif
			return;
		}

		// ignore the auth here. It will be re-parsed
		// by the http connection later

		if (protocol != "http")
		{
#ifdef TORRENT_UPNP_LOGGING
			m_log << time_now_string()
				<< " <== (" << from << ") Rootdevice uses unsupported protocol: '" << protocol
				<< "'. Ignoring device" << std::endl;
#endif
			return;
		}

		if (d.port == 0)
		{
#ifdef TORRENT_UPNP_LOGGING
			m_log << time_now_string()
				<< " <== (" << from << ") Rootdevice responded with a url with port 0. "
				"Ignoring device" << std::endl;
#endif
			return;
		}
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== (" << from << ") Found rootdevice: " << d.url
			<< " total: " << m_devices.size() << std::endl;
#endif

		if (m_devices.size() >= 50)
		{
#ifdef TORRENT_UPNP_LOGGING
			m_log << time_now_string()
				<< " <== (" << from << ") Too many devices (" << m_devices.size() << "), "
				"ignoring: " << d.url << std::endl;
#endif
			return;
		}

		TORRENT_ASSERT(d.mapping.empty());
		for (std::vector<global_mapping_t>::iterator j = m_mappings.begin()
			, end(m_mappings.end()); j != end; ++j)
		{
			mapping_t m;
			m.action = mapping_t::action_add;
			m.local_port = j->local_port;
			m.external_port = j->external_port;
			m.protocol = j->protocol;
			d.mapping.push_back(m);
		}
		boost::tie(i, boost::tuples::ignore) = m_devices.insert(d);
	}

	if (!m_devices.empty())
	{
		for (std::set<rootdevice>::iterator i = m_devices.begin()
			, end(m_devices.end()); i != end; ++i)
		{
			if (i->control_url.empty() && !i->upnp_connection && !i->disabled)
			{
				// we don't have a WANIP or WANPPP url for this device,
				// ask for it
				rootdevice& d = const_cast<rootdevice&>(*i);
				TORRENT_ASSERT(d.magic == 1337);
#ifndef BOOST_NO_EXCEPTIONS
				try
				{
#endif
#ifdef TORRENT_UPNP_LOGGING
					m_log << time_now_string()
						<< " ==> connecting to " << d.url << std::endl;
#endif
					if (d.upnp_connection) d.upnp_connection->close();
					d.upnp_connection.reset(new http_connection(m_io_service
						, m_cc, bind(&upnp::on_upnp_xml, self(), _1, _2
						, boost::ref(d), _5)));
					d.upnp_connection->get(d.url, seconds(30), 1);
#ifndef BOOST_NO_EXCEPTIONS
				}
				catch (std::exception& e)
				{
					(void)e;
#ifdef TORRENT_UPNP_LOGGING
					m_log << time_now_string()
						<< " *** Connection failed to: " << d.url
						<< " " << e.what() << std::endl;
#endif
					d.disabled = true;
				}
#endif
			}
		}
	}
}

void upnp::post(upnp::rootdevice const& d, std::string const& soap
	, std::string const& soap_action)
{
	TORRENT_ASSERT(d.magic == 1337);
	TORRENT_ASSERT(d.upnp_connection);

	std::stringstream header;
	
	header << "POST " << d.path << " HTTP/1.0\r\n"
		"Host: " << d.hostname << ":" << d.port << "\r\n"
		"Content-Type: text/xml; charset=\"utf-8\"\r\n"
		"Content-Length: " << soap.size() << "\r\n"
		"Soapaction: \"" << d.service_namespace << "#" << soap_action << "\"\r\n\r\n" << soap;

	d.upnp_connection->sendbuffer = header.str();

#ifdef TORRENT_UPNP_LOGGING
	m_log << time_now_string()
		<< " ==> sending: " << header.str() << std::endl;
#endif
	
}

void upnp::create_port_mapping(http_connection& c, rootdevice& d, int i)
{
	mutex_t::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);

	if (!d.upnp_connection)
	{
		TORRENT_ASSERT(d.disabled);
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string() << " *** mapping (" << i
			<< ") aborted" << std::endl;
#endif
		return;
	}
	
	std::string soap_action = "AddPortMapping";

	std::stringstream soap;
	
	soap << "<?xml version=\"1.0\"?>\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body><u:" << soap_action << " xmlns:u=\"" << d.service_namespace << "\">";

	error_code ec;
	soap << "<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>" << d.mapping[i].external_port << "</NewExternalPort>"
		"<NewProtocol>" << (d.mapping[i].protocol == udp ? "UDP" : "TCP") << "</NewProtocol>"
		"<NewInternalPort>" << d.mapping[i].local_port << "</NewInternalPort>"
		"<NewInternalClient>" << c.socket().local_endpoint(ec).address() << "</NewInternalClient>"
		"<NewEnabled>1</NewEnabled>"
		"<NewPortMappingDescription>" << m_user_agent << " at " <<
			c.socket().local_endpoint(ec).address() << ":" << to_string(d.mapping[i].local_port).elems
			<< "</NewPortMappingDescription>"
		"<NewLeaseDuration>" << d.lease_duration << "</NewLeaseDuration>";
	soap << "</u:" << soap_action << "></s:Body></s:Envelope>";

	post(d, soap.str(), soap_action);
}

void upnp::next(rootdevice& d, int i)
{
	if (i < num_mappings() - 1)
	{
		update_map(d, i + 1);
	}
	else
	{
		std::vector<mapping_t>::iterator i
			= std::find_if(d.mapping.begin(), d.mapping.end()
			, boost::bind(&mapping_t::action, _1) != int(mapping_t::action_none));
		if (i == d.mapping.end()) return;

		update_map(d, i - d.mapping.begin());
	}
}

void upnp::update_map(rootdevice& d, int i)
{
	TORRENT_ASSERT(d.magic == 1337);
	TORRENT_ASSERT(i < int(d.mapping.size()));
	TORRENT_ASSERT(d.mapping.size() == m_mappings.size());

	if (d.upnp_connection) return;

	boost::intrusive_ptr<upnp> me(self());

	mapping_t& m = d.mapping[i];

	if (m.action == mapping_t::action_none
		|| m.protocol == none)
	{
#ifdef TORRENT_UPNP_LOGGING
		if (m.protocol != none)
			m_log << time_now_string() << " *** mapping (" << i
				<< ") does not need update, skipping" << std::endl;
#endif
		m.action = mapping_t::action_none;
		next(d, i);
		return;
	}

	TORRENT_ASSERT(!d.upnp_connection);
	TORRENT_ASSERT(d.service_namespace);

#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " ==> connecting to " << d.hostname << std::endl;
#endif
	if (m.action == mapping_t::action_add)
	{
		if (m.failcount > 5)
		{
			m.action = mapping_t::action_none;
			// giving up
			next(d, i);
			return;
		}

		if (d.upnp_connection) d.upnp_connection->close();
		d.upnp_connection.reset(new http_connection(m_io_service
			, m_cc, bind(&upnp::on_upnp_map_response, self(), _1, _2
			, boost::ref(d), i, _5), true
			, bind(&upnp::create_port_mapping, self(), _1, boost::ref(d), i)));

		d.upnp_connection->start(d.hostname, to_string(d.port).elems
			, seconds(10), 1);
	}
	else if (m.action == mapping_t::action_delete)
	{
		if (d.upnp_connection) d.upnp_connection->close();
		d.upnp_connection.reset(new http_connection(m_io_service
			, m_cc, bind(&upnp::on_upnp_unmap_response, self(), _1, _2
			, boost::ref(d), i, _5), true
			, bind(&upnp::delete_port_mapping, self(), boost::ref(d), i)));
		d.upnp_connection->start(d.hostname, to_string(d.port).elems
			, seconds(10), 1);
	}

	m.action = mapping_t::action_none;
}

void upnp::delete_port_mapping(rootdevice& d, int i)
{
	mutex_t::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);

	if (!d.upnp_connection)
	{
		TORRENT_ASSERT(d.disabled);
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string() << " *** unmapping (" << i
			<< ") aborted" << std::endl;
#endif
		return;
	}

	std::stringstream soap;
	
	std::string soap_action = "DeletePortMapping";

	soap << "<?xml version=\"1.0\"?>\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body><u:" << soap_action << " xmlns:u=\"" << d.service_namespace << "\">";

	soap << "<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>" << d.mapping[i].external_port << "</NewExternalPort>"
		"<NewProtocol>" << (d.mapping[i].protocol == udp ? "UDP" : "TCP") << "</NewProtocol>";
	soap << "</u:" << soap_action << "></s:Body></s:Envelope>";
	
	post(d, soap.str(), soap_action);
}

namespace
{
	char tolower(char c)
	{
		if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
		return c;
	}

	void copy_tolower(std::string& dst, char const* src)
	{
		dst.clear();
		while (*src) dst.push_back(tolower(*src++));
	}
	
	bool string_equal_nocase(char const* lhs, char const* rhs)
	{
		while (tolower(*lhs) == tolower(*rhs))
		{
			if (*lhs == 0) return true;
			++lhs;
			++rhs;
		}
		return false;
	}
}

struct parse_state
{
	parse_state(): in_service(false) {}
	void reset(char const* st)
	{
		in_service = false;
		service_type = st;
		tag_stack.clear();
		control_url.clear();
		model.clear();
		url_base.clear();
	}
	bool in_service;
	std::list<std::string> tag_stack;
	std::string control_url;
	char const* service_type;
	std::string model;
	std::string url_base;
	bool top_tags(const char* str1, const char* str2)
	{
		std::list<std::string>::reverse_iterator i = tag_stack.rbegin();
		if (i == tag_stack.rend()) return false;
		if (!string_equal_nocase(i->c_str(), str2)) return false;
		++i;
		if (i == tag_stack.rend()) return false;
		if (!string_equal_nocase(i->c_str(), str1)) return false;
		return true;
	}
};

void find_control_url(int type, char const* string, parse_state& state)
{
	if (type == xml_start_tag)
	{
		std::string tag;
		copy_tolower(tag, string);
		state.tag_stack.push_back(tag);
//		std::copy(state.tag_stack.begin(), state.tag_stack.end(), std::ostream_iterator<std::string>(std::cout, " "));
//		std::cout << std::endl;
	}
	else if (type == xml_end_tag)
	{
		if (!state.tag_stack.empty())
		{
			if (state.in_service && state.tag_stack.back() == "service")
				state.in_service = false;
			state.tag_stack.pop_back();
		}
	}
	else if (type == xml_string)
	{
		if (state.tag_stack.empty()) return;
//		std::cout << " " << string << std::endl;
		if (!state.in_service && state.top_tags("service", "servicetype"))
		{
			if (string_equal_nocase(string, state.service_type))
				state.in_service = true;
		}
		else if (state.in_service && state.top_tags("service", "controlurl"))
		{
			state.control_url = string;
		}
		else if (state.model.empty() && state.top_tags("device", "modelname"))
		{
			state.model = string;
		}
		else if (state.tag_stack.back() == "urlbase")
		{
			state.url_base = string;
		}
	}
}

void upnp::on_upnp_xml(error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d
	, http_connection& c)
{
	boost::intrusive_ptr<upnp> me(self());

	mutex_t::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection && d.upnp_connection.get() == &c)
	{
		d.upnp_connection->close();
		d.upnp_connection.reset();
	}

	if (e && e != asio::error::eof)
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== (" << d.url << ") error while fetching control url: "
			<< e.message() << std::endl;
#endif
		d.disabled = true;
		return;
	}

	if (!p.header_finished())
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== (" << d.url << ") error while fetching control url: incomplete http message" << std::endl;
#endif
		d.disabled = true;
		return;
	}

	if (p.status_code() != 200)
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== (" << d.url << ") error while fetching control url: " << p.message() << std::endl;
#endif
		d.disabled = true;
		return;
	}

	parse_state s;
	s.reset("urn:schemas-upnp-org:service:WANIPConnection:1");
	xml_parse((char*)p.get_body().begin, (char*)p.get_body().end
		, bind(&find_control_url, _1, _2, boost::ref(s)));
	if (!s.control_url.empty())
	{
		d.service_namespace = s.service_type;
		if (!s.model.empty()) m_model = s.model;
	}
	else
	{
		// we didn't find the WAN IP connection, look for
		// a PPP connection
		s.reset("urn:schemas-upnp-org:service:WANPPPConnection:1");
		xml_parse((char*)p.get_body().begin, (char*)p.get_body().end
			, bind(&find_control_url, _1, _2, boost::ref(s)));
		if (!s.control_url.empty())
		{
			d.service_namespace = s.service_type;
			if (!s.model.empty()) m_model = s.model;
		}
		else
		{
#ifdef TORRENT_UPNP_LOGGING
			m_log << time_now_string()
				<< " <== (" << d.url << ") Rootdevice response, did not find "
				"a port mapping interface" << std::endl;
#endif
			d.disabled = true;
			return;
		}
	}
	
	if (!s.url_base.empty() && s.control_url.substr(0, 7) != "http://")
	{
		// avoid double slashes in path
		if (s.url_base[s.url_base.size()-1] == '/'
			&& !s.control_url.empty()
			&& s.control_url[0] == '/')
			s.url_base.erase(s.url_base.end()-1);
		d.control_url = s.url_base + s.control_url;
	}
	else d.control_url = s.control_url;

	std::string protocol;
	std::string auth;
	char const* error;
	if (!d.control_url.empty() && d.control_url[0] == '/')
	{
		boost::tie(protocol, auth, d.hostname, d.port, d.path, error)
			= parse_url_components(d.url);
		d.control_url = protocol + "://" + d.hostname + ":"
			+ to_string(d.port).elems + s.control_url;
	}

#ifdef TORRENT_UPNP_LOGGING
	m_log << time_now_string()
		<< " <== (" << d.url << ") Rootdevice response, found control URL: " << d.control_url
		<< " urlbase: " << s.url_base << " namespace: " << d.service_namespace << std::endl;
#endif

	boost::tie(protocol, auth, d.hostname, d.port, d.path, error)
		= parse_url_components(d.control_url);

	if (error)
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " *** Failed to parse URL '" << d.control_url << "': " << error << std::endl;
#endif
		d.disabled = true;
		return;
	}

	if (num_mappings() > 0) update_map(d, 0);
}

void upnp::disable(char const* msg)
{
	m_disabled = true;

	// kill all mappings
	for (std::vector<global_mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol == none) continue;
		i->protocol = none;
		m_callback(i - m_mappings.begin(), 0, msg);
	}
	
//	m_devices.clear();
	error_code ec;
	m_broadcast_timer.cancel(ec);
	m_refresh_timer.cancel(ec);
	m_socket.close();
}

namespace
{
	struct error_code_parse_state
	{
		error_code_parse_state(): in_error_code(false), exit(false), error_code(-1) {}
		bool in_error_code;
		bool exit;
		int error_code;
	};
	
	void find_error_code(int type, char const* string, error_code_parse_state& state)
	{
		if (state.exit) return;
		if (type == xml_start_tag && !std::strcmp("errorCode", string))
		{
			state.in_error_code = true;
		}
		else if (type == xml_string && state.in_error_code)
		{
			state.error_code = std::atoi(string);
			state.exit = true;
		}
	}
}

namespace
{
	struct error_code_t
	{
		int code;
		char const* msg;
	};
	
	error_code_t error_codes[] =
	{
		{402, "Invalid Arguments"}
		, {501, "Action Failed"}
		, {714, "The specified value does not exist in the array"}
		, {715, "The source IP address cannot be wild-carded"}
		, {716, "The external port cannot be wild-carded"}
		, {718, "The port mapping entry specified conflicts with "
			"a mapping assigned previously to another client"}
		, {724, "Internal and External port values must be the same"}
		, {725, "The NAT implementation only supports permanent "
			"lease times on port mappings"}
		, {726, "RemoteHost must be a wildcard and cannot be a "
			"specific IP address or DNS name"}
		, {727, "ExternalPort must be a wildcard and cannot be a specific port "}
	};

}

void upnp::on_upnp_map_response(error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d, int mapping
	, http_connection& c)
{
	boost::intrusive_ptr<upnp> me(self());

	mutex_t::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection && d.upnp_connection.get() == &c)
	{
		d.upnp_connection->close();
		d.upnp_connection.reset();
	}

	if (e && e != asio::error::eof)
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== error while adding portmap: " << e.message() << std::endl;
#endif
		d.disabled = true;
		return;
	}
	
	if (m_closing) return;
	
//	 error code response may look like this:
//	<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
//		s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
//	 <s:Body>
//	  <s:Fault>
//		<faultcode>s:Client</faultcode>
//		<faultstring>UPnPError</faultstring>
//		<detail>
//		 <UPnPErrorxmlns="urn:schemas-upnp-org:control-1-0">
//		  <errorCode>402</errorCode>
//		  <errorDescription>Invalid Args</errorDescription>
//		 </UPnPError>
//		</detail>
//	  </s:Fault>
//	 </s:Body>
//	</s:Envelope>

	if (!p.header_finished())
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== error while adding portmap: incomplete http message" << std::endl;
#endif
		next(d, mapping);
		return;
	}

	// We don't want to ignore responses with return codes other than 200
	// since those might contain valid UPnP error codes

	error_code_parse_state s;
	xml_parse((char*)p.get_body().begin, (char*)p.get_body().end
		, bind(&find_error_code, _1, _2, boost::ref(s)));

#ifdef TORRENT_UPNP_LOGGING
	if (s.error_code != -1)
	{
		m_log << time_now_string()
			<< " <== got error message: " << s.error_code << std::endl;
	}
#endif
	
	mapping_t& m = d.mapping[mapping];

	if (s.error_code == 725)
	{
		// only permanent leases supported
		d.lease_duration = 0;
		m.action = mapping_t::action_add;
		++m.failcount;
		update_map(d, mapping);
		return;
	}
	else if (s.error_code == 718 || s.error_code == 727)
	{
		if (m.external_port != 0)
		{
			// conflict in mapping, set port to wildcard
			// and let the router decide
			m.external_port = 0;
			m.action = mapping_t::action_add;
			++m.failcount;
			update_map(d, mapping);
			return;
		}
		return_error(mapping, s.error_code);
	}
	else if (s.error_code == 716)
	{
		// The external port cannot be wildcarder
		// pick a random port
		m.external_port = 40000 + (std::rand() % 10000);
		m.action = mapping_t::action_add;
		++m.failcount;
		update_map(d, mapping);
		return;
	}
	else if (s.error_code != -1)
	{
		return_error(mapping, s.error_code);
	}

#ifdef TORRENT_UPNP_LOGGING
	m_log << time_now_string()
		<< " <== map response: " << std::string(p.get_body().begin, p.get_body().end)
		<< std::endl;
#endif

	if (s.error_code == -1)
	{
		m_callback(mapping, m.external_port, "");
		if (d.lease_duration > 0)
		{
			m.expires = time_now()
				+ seconds(int(d.lease_duration * 0.75f));
			ptime next_expire = m_refresh_timer.expires_at();
			if (next_expire < time_now()
				|| next_expire > m.expires)
			{
				error_code ec;
				m_refresh_timer.expires_at(m.expires, ec);
				m_refresh_timer.async_wait(bind(&upnp::on_expire, self(), _1));
			}
		}
		else
		{
			m.expires = max_time();
		}
		m.failcount = 0;
	}

	next(d, mapping);
}

void upnp::return_error(int mapping, int code)
{
	int num_errors = sizeof(error_codes) / sizeof(error_codes[0]);
	error_code_t* end = error_codes + num_errors;
	error_code_t tmp = {code, 0};
	error_code_t* e = std::lower_bound(error_codes, end, tmp
		, bind(&error_code_t::code, _1) < bind(&error_code_t::code, _2));
	std::string error_string = "UPnP mapping error ";
	error_string += to_string(code).elems;
	if (e != end && e->code == code)
	{
		error_string += ": ";
		error_string += e->msg;
	}
	m_callback(mapping, 0, error_string);
}

void upnp::on_upnp_unmap_response(error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d, int mapping
	, http_connection& c)
{
	boost::intrusive_ptr<upnp> me(self());

	mutex_t::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection && d.upnp_connection.get() == &c)
	{
		d.upnp_connection->close();
		d.upnp_connection.reset();
	}

	if (e && e != asio::error::eof)
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== error while deleting portmap: " << e.message() << std::endl;
#endif
	} else if (!p.header_finished())
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== error while deleting portmap: incomplete http message" << std::endl;
#endif
	}
	else if (p.status_code() != 200)
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== error while deleting portmap: " << p.message() << std::endl;
#endif
	}
	else
	{

#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== unmap response: " << std::string(p.get_body().begin, p.get_body().end)
			<< std::endl;
#endif
	}

	d.mapping[mapping].protocol = none;

	next(d, mapping);
}

void upnp::on_expire(error_code const& e)
{
	if (e) return;

	ptime now = time_now();
	ptime next_expire = max_time();

	mutex_t::scoped_lock l(m_mutex);

	for (std::set<rootdevice>::iterator i = m_devices.begin()
		, end(m_devices.end()); i != end; ++i)
	{
		rootdevice& d = const_cast<rootdevice&>(*i);
		TORRENT_ASSERT(d.magic == 1337);
		for (int m = 0; m < num_mappings(); ++m)
		{
			if (d.mapping[m].expires != max_time())
				continue;

			if (d.mapping[m].expires < now)
			{
				d.mapping[m].expires = max_time();
				update_map(d, m);
			}
			else if (d.mapping[m].expires < next_expire)
			{
				next_expire = d.mapping[m].expires;
			}
		}
	}
	if (next_expire != max_time())
	{
		error_code ec;
		m_refresh_timer.expires_at(next_expire, ec);
		m_refresh_timer.async_wait(bind(&upnp::on_expire, self(), _1));
	}
}

void upnp::close()
{
	mutex_t::scoped_lock l(m_mutex);

	error_code ec;
	m_refresh_timer.cancel(ec);
	m_broadcast_timer.cancel(ec);
	m_closing = true;
	m_socket.close();

	for (std::set<rootdevice>::iterator i = m_devices.begin()
		, end(m_devices.end()); i != end; ++i)
	{
		rootdevice& d = const_cast<rootdevice&>(*i);
		TORRENT_ASSERT(d.magic == 1337);
		if (d.control_url.empty()) continue;
		for (std::vector<mapping_t>::iterator j = d.mapping.begin()
			, end(d.mapping.end()); j != end; ++j)
		{
			if (j->protocol == none) continue;
			if (j->action == mapping_t::action_add)
			{
				j->action = mapping_t::action_none;
				continue;
			}
			j->action = mapping_t::action_delete;
			m_mappings[j - d.mapping.begin()].protocol = none;
		}
		if (num_mappings() > 0) update_map(d, 0);
	}
}

