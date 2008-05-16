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
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/xml_parse.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/enum_net.hpp"

#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <asio/ip/host_name.hpp>
#include <asio/ip/multicast.hpp>
#include <boost/thread/mutex.hpp>
#include <cstdlib>

#if (defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)) && !defined(TORRENT_UPNP_LOGGING)
#define TORRENT_UPNP_LOGGING
#endif

using boost::bind;
using namespace libtorrent;

namespace libtorrent
{
	bool is_local(address const& a);
	address guess_local_address(asio::io_service&);
}

upnp::upnp(io_service& ios, connection_queue& cc
	, address const& listen_interface, std::string const& user_agent
	, portmap_callback_t const& cb, bool ignore_nonrouters)
	: m_udp_local_port(0)
	, m_tcp_local_port(0)
	, m_user_agent(user_agent)
	, m_callback(cb)
	, m_retry_count(0)
	, m_io_service(ios)
	, m_strand(ios)
	, m_socket(ios, udp::endpoint(address_v4::from_string("239.255.255.250"), 1900)
		, bind(&upnp::on_reply, self(), _1, _2, _3), false)
	, m_broadcast_timer(ios)
	, m_refresh_timer(ios)
	, m_disabled(false)
	, m_closing(false)
	, m_ignore_outside_network(ignore_nonrouters)
	, m_cc(cc)
{
#ifdef TORRENT_UPNP_LOGGING
	m_log.open("upnp.log", std::ios::in | std::ios::out | std::ios::trunc);
#endif
	m_retry_count = 0;
}

upnp::~upnp()
{
}

void upnp::discover_device() try
{
	const char msearch[] = 
		"M-SEARCH * HTTP/1.1\r\n"
		"HOST: 239.255.255.250:1900\r\n"
		"ST:upnp:rootdevice\r\n"
		"MAN:\"ssdp:discover\"\r\n"
		"MX:3\r\n"
		"\r\n\r\n";

	asio::error_code ec;
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
		disable();
		return;
	}

	++m_retry_count;
	m_broadcast_timer.expires_from_now(milliseconds(250 * m_retry_count));
	m_broadcast_timer.async_wait(bind(&upnp::resend_request
		, self(), _1));

#ifdef TORRENT_UPNP_LOGGING
	m_log << time_now_string()
		<< " ==> Broadcasting search for rootdevice" << std::endl;
#endif
}
catch (std::exception&)
{
	disable();
};

void upnp::set_mappings(int tcp, int udp)
{
#ifdef TORRENT_UPNP_LOGGING
	m_log << time_now_string()
		<< " *** set mappings " << tcp << " " << udp;
	if (m_disabled) m_log << " DISABLED";
	m_log << std::endl;
#endif

	if (m_disabled) return;
	if (udp != 0) m_udp_local_port = udp;
	if (tcp != 0) m_tcp_local_port = tcp;

	for (std::set<rootdevice>::iterator i = m_devices.begin()
		, end(m_devices.end()); i != end; ++i)
	{
		rootdevice& d = const_cast<rootdevice&>(*i);
		TORRENT_ASSERT(d.magic == 1337);
		if (d.mapping[0].local_port != m_tcp_local_port)
		{
			if (d.mapping[0].external_port == 0)
				d.mapping[0].external_port = m_tcp_local_port;
			d.mapping[0].local_port = m_tcp_local_port;
			d.mapping[0].need_update = true;
		}
		if (d.mapping[1].local_port != m_udp_local_port)
		{
			if (d.mapping[1].external_port == 0)
				d.mapping[1].external_port = m_udp_local_port;
			d.mapping[1].local_port = m_udp_local_port;
			d.mapping[1].need_update = true;
		}
		if (d.service_namespace
			&& (d.mapping[0].need_update || d.mapping[1].need_update))
			map_port(d, 0);
	}
}

void upnp::resend_request(asio::error_code const& e)
#ifndef NDEBUG
try
#endif
{
	if (e) return;
	if (m_retry_count < 9
		&& (m_devices.empty() || m_retry_count < 4))
	{
		discover_device();
		return;
	}

	if (m_devices.empty())
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " *** Got no response in 9 retries. Giving up, "
			"disabling UPnP." << std::endl;
#endif
		disable();
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
				d.upnp_connection->get(d.url);
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
#ifndef NDEBUG
catch (std::exception&)
{
	TORRENT_ASSERT(false);
};
#endif

void upnp::on_reply(udp::endpoint const& from, char* buffer
	, std::size_t bytes_transferred)
#ifndef NDEBUG
try
#endif
{
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
	asio::error_code ec;
	if (m_ignore_outside_network && !in_local_network(m_io_service, from.address(), ec))
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
			std::vector<ip_interface> const& net = enum_net_interfaces(m_io_service, ec);
			m_log << time_now_string() << " <== (" << from << ") UPnP device "
				"ignored because it's not on our network ";
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

	http_parser p;
	try
	{
		p.incoming(buffer::const_interval(buffer
			, buffer + bytes_transferred));
	}
	catch (std::exception& e)
	{
		(void)e;
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== (" << from << ") Rootdevice responded with incorrect HTTP packet. Ignoring device (" << e.what() << ")" << std::endl;
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
		// we don't have this device in our list. Add it
		try
		{
			boost::tie(protocol, auth, d.hostname, d.port, d.path)
				= parse_url_components(d.url);
		}
		catch (std::exception& e)
		{
#ifdef TORRENT_UPNP_LOGGING
			m_log << time_now_string()
				<< " <== (" << from << ") invalid url: '" << d.url
				<< "'. Ignoring device" << std::endl;
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

		if (m_tcp_local_port != 0)
		{
			d.mapping[0].need_update = true;
			d.mapping[0].local_port = m_tcp_local_port;
			if (d.mapping[0].external_port == 0)
				d.mapping[0].external_port = d.mapping[0].local_port;
#ifdef TORRENT_UPNP_LOGGING
			m_log << time_now_string() << " *** Mapping 0 will be updated" << std::endl;
#endif
		}
		if (m_udp_local_port != 0)
		{
			d.mapping[1].need_update = true;
			d.mapping[1].local_port = m_udp_local_port;
			if (d.mapping[1].external_port == 0)
				d.mapping[1].external_port = d.mapping[1].local_port;
#ifdef TORRENT_UPNP_LOGGING
			m_log << time_now_string() << " *** Mapping 1 will be updated" << std::endl;
#endif
		}
		boost::tie(i, boost::tuples::ignore) = m_devices.insert(d);
	}


	// since we're using udp, send the query 4 times
	// just to make sure we find all devices
	if (m_retry_count >= 4 && !m_devices.empty())
	{
		m_broadcast_timer.cancel();

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
					d.upnp_connection->get(d.url);
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
}
#ifndef NDEBUG
catch (std::exception&)
{
	TORRENT_ASSERT(false);
};
#endif

void upnp::post(upnp::rootdevice const& d, std::string const& soap
	, std::string const& soap_action)
{
	TORRENT_ASSERT(d.magic == 1337);
	TORRENT_ASSERT(d.upnp_connection);

	std::stringstream header;
	
	header << "POST " << d.control_url << " HTTP/1.1\r\n"
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

	soap << "<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>" << d.mapping[i].external_port << "</NewExternalPort>"
		"<NewProtocol>" << (d.mapping[i].protocol ? "UDP" : "TCP") << "</NewProtocol>"
		"<NewInternalPort>" << d.mapping[i].local_port << "</NewInternalPort>"
		"<NewInternalClient>" << c.socket().local_endpoint().address().to_string() << "</NewInternalClient>"
		"<NewEnabled>1</NewEnabled>"
		"<NewPortMappingDescription>" << m_user_agent << "</NewPortMappingDescription>"
		"<NewLeaseDuration>" << d.lease_duration << "</NewLeaseDuration>";
	soap << "</u:" << soap_action << "></s:Body></s:Envelope>";

	post(d, soap.str(), soap_action);
}

void upnp::map_port(rootdevice& d, int i)
{
	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection) return;

	if (!d.mapping[i].need_update)
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string() << " *** mapping (" << i
			<< ") does not need update, skipping" << std::endl;
#endif
		if (i < num_mappings - 1)
			map_port(d, i + 1);
		return;
	}
	d.mapping[i].need_update = false;
	TORRENT_ASSERT(!d.upnp_connection);
	TORRENT_ASSERT(d.service_namespace);

#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " ==> connecting to " << d.hostname << std::endl;
#endif
	if (d.upnp_connection) d.upnp_connection->close();
	d.upnp_connection.reset(new http_connection(m_io_service
		, m_cc, bind(&upnp::on_upnp_map_response, self(), _1, _2
		, boost::ref(d), i, _5), true
		, bind(&upnp::create_port_mapping, self(), _1, boost::ref(d), i)));

	d.upnp_connection->start(d.hostname, boost::lexical_cast<std::string>(d.port)
		, seconds(10));
}

void upnp::delete_port_mapping(rootdevice& d, int i)
{
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
		"<NewProtocol>" << (d.mapping[i].protocol ? "UDP" : "TCP") << "</NewProtocol>";
	soap << "</u:" << soap_action << "></s:Body></s:Envelope>";
	
	post(d, soap.str(), soap_action);
}

// requires the mutex to be locked
void upnp::unmap_port(rootdevice& d, int i)
{
	TORRENT_ASSERT(d.magic == 1337);
	if (d.mapping[i].external_port == 0
		|| d.disabled)
	{
		if (i < num_mappings - 1)
		{
			unmap_port(d, i + 1);
		}
		return;
	}
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " ==> connecting to " << d.hostname << std::endl;
#endif

	if (d.upnp_connection) d.upnp_connection->close();
	d.upnp_connection.reset(new http_connection(m_io_service
		, m_cc, bind(&upnp::on_upnp_unmap_response, self(), _1, _2
		, boost::ref(d), i, _5), true
		, bind(&upnp::delete_port_mapping, self(), boost::ref(d), i)));
	d.upnp_connection->start(d.hostname, boost::lexical_cast<std::string>(d.port)
		, seconds(10));
}

namespace
{
	struct parse_state
	{
		parse_state(): found_service(false), exit(false) {}
		void reset(char const* st)
		{
			found_service = false;
			exit = false;
			service_type = st;
		}
		bool found_service;
		bool exit;
		std::string top_tag;
		std::string control_url;
		char const* service_type;
	};
	
	void find_control_url(int type, char const* string, parse_state& state)
	{
		if (state.exit) return;

		if (type == xml_start_tag)
		{
			if ((!state.top_tag.empty() && state.top_tag == "service")
				|| !strcmp(string, "service"))
			{
				state.top_tag = string;
			}
		}
		else if (type == xml_end_tag)
		{
			if (!strcmp(string, "service"))
			{
				state.top_tag.clear();
				if (state.found_service) state.exit = true;
			}
			else if (!state.top_tag.empty() && state.top_tag != "service")
				state.top_tag = "service";
		}
		else if (type == xml_string)
		{
			if (state.top_tag == "serviceType")
			{
				if (!strcmp(string, state.service_type))
					state.found_service = true;
			}
			else if (state.top_tag == "controlURL")
			{
				state.control_url = string;
				if (state.found_service) state.exit = true;
			}
		}
	}

}

void upnp::on_upnp_xml(asio::error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d
	, http_connection& c) try
{
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
	if (s.found_service)
	{
		d.service_namespace = s.service_type;
	}
	else
	{
		// we didn't find the WAN IP connection, look for
		// a PPP connection
		s.reset("urn:schemas-upnp-org:service:WANPPPConnection:1");
		xml_parse((char*)p.get_body().begin, (char*)p.get_body().end
			, bind(&find_control_url, _1, _2, boost::ref(s)));
		if (s.found_service)
		{
			d.service_namespace = s.service_type;
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
	
#ifdef TORRENT_UPNP_LOGGING
	m_log << time_now_string()
		<< " <== (" << d.url << ") Rootdevice response, found control URL: " << s.control_url
		<< " namespace: " << d.service_namespace << std::endl;
#endif

	d.control_url = s.control_url;

	map_port(d, 0);
}
catch (std::exception&)
{
	disable();
};

void upnp::disable()
{
	m_disabled = true;
	m_devices.clear();
	m_broadcast_timer.cancel();
	m_refresh_timer.cancel();
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
		if (type == xml_start_tag && !strcmp("errorCode", string))
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

void upnp::on_upnp_map_response(asio::error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d, int mapping
	, http_connection& c) try
{
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
		d.disabled = true;
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
	
	if (s.error_code == 725)
	{
		// only permanent leases supported
		d.lease_duration = 0;
		d.mapping[mapping].need_update = true;
		map_port(d, mapping);
		return;
	}
	else if (s.error_code == 718)
	{
		// conflict in mapping, try next external port
		++d.mapping[mapping].external_port;
		d.mapping[mapping].need_update = true;
		map_port(d, mapping);
		return;
	}
	else if (s.error_code != -1)
	{
		int num_errors = sizeof(error_codes) / sizeof(error_codes[0]);
		error_code_t* end = error_codes + num_errors;
		error_code_t tmp = {s.error_code, 0};
		error_code_t* e = std::lower_bound(error_codes, end, tmp
			, bind(&error_code_t::code, _1) < bind(&error_code_t::code, _2));
		std::string error_string = "UPnP mapping error ";
		error_string += boost::lexical_cast<std::string>(s.error_code);
		if (e != end  && e->code == s.error_code)
		{
			error_string += ": ";
			error_string += e->msg;
		}
		m_callback(0, 0, error_string);
	}

#ifdef TORRENT_UPNP_LOGGING
	m_log << time_now_string()
		<< " <== map response: " << std::string(p.get_body().begin, p.get_body().end)
		<< std::endl;
#endif

	if (s.error_code == -1)
	{
		int tcp = 0;
		int udp = 0;
		
		if (mapping == 0)
			tcp = d.mapping[mapping].external_port;
		else
			udp = d.mapping[mapping].external_port;

		m_callback(tcp, udp, "");
		if (d.lease_duration > 0)
		{
			d.mapping[mapping].expires = time_now()
				+ seconds(int(d.lease_duration * 0.75f));
			ptime next_expire = m_refresh_timer.expires_at();
			if (next_expire < time_now()
				|| next_expire > d.mapping[mapping].expires)
			{
				m_refresh_timer.expires_at(d.mapping[mapping].expires);
				m_refresh_timer.async_wait(bind(&upnp::on_expire, self(), _1));
			}
		}
		else
		{
			d.mapping[mapping].expires = max_time();
		}
	}

	for (int i = 0; i < num_mappings; ++i)
	{
		if (d.mapping[i].need_update)
		{
			map_port(d, i);
			return;
		}
	}
}
catch (std::exception&)
{
	disable();
};

void upnp::on_upnp_unmap_response(asio::error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d, int mapping
	, http_connection& c) try
{
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
	}

	if (!p.header_finished())
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== error while deleting portmap: incomplete http message" << std::endl;
#endif
		return;
	}

	if (p.status_code() != 200)
	{
#ifdef TORRENT_UPNP_LOGGING
		m_log << time_now_string()
			<< " <== error while deleting portmap: " << p.message() << std::endl;
#endif
		d.disabled = true;
		return;
	}

#ifdef TORRENT_UPNP_LOGGING
	m_log << time_now_string()
		<< " <== unmap response: " << std::string(p.get_body().begin, p.get_body().end)
		<< std::endl;
#endif

	// ignore errors and continue with the next mapping for this device
	if (mapping < num_mappings - 1)
	{
		unmap_port(d, mapping + 1);
		return;
	}
}
catch (std::exception&)
{
	disable();
};

void upnp::on_expire(asio::error_code const& e) try
{
	if (e) return;

	ptime now = time_now();
	ptime next_expire = max_time();

	for (std::set<rootdevice>::iterator i = m_devices.begin()
		, end(m_devices.end()); i != end; ++i)
	{
		rootdevice& d = const_cast<rootdevice&>(*i);
		TORRENT_ASSERT(d.magic == 1337);
		for (int m = 0; m < num_mappings; ++m)
		{
			if (d.mapping[m].expires != max_time())
				continue;

			if (d.mapping[m].expires < now)
			{
				d.mapping[m].expires = max_time();
				map_port(d, m);
			}
			else if (d.mapping[m].expires < next_expire)
			{
				next_expire = d.mapping[m].expires;
			}
		}
	}
	if (next_expire != max_time())
	{
		m_refresh_timer.expires_at(next_expire);
		m_refresh_timer.async_wait(bind(&upnp::on_expire, self(), _1));
	}
}
catch (std::exception&)
{
	disable();
};

void upnp::close()
{
	m_refresh_timer.cancel();
	m_broadcast_timer.cancel();
	m_closing = true;
	m_socket.close();

	if (m_disabled)
	{
		m_devices.clear();
		return;
	}

	for (std::set<rootdevice>::iterator i = m_devices.begin()
		, end(m_devices.end()); i != end; ++i)
	{
		rootdevice& d = const_cast<rootdevice&>(*i);
		TORRENT_ASSERT(d.magic == 1337);
		if (d.control_url.empty()) continue;
		unmap_port(d, 0);
	}
}

