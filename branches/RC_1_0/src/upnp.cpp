/*

Copyright (c) 2007-2014, Arvid Norberg
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

#include "libtorrent/socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/xml_parse.hpp"
#include "libtorrent/connection_queue.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/random.hpp"

#if defined TORRENT_ASIO_DEBUGGING
#include "libtorrent/debug.hpp"
#endif

#include <boost/bind.hpp>
#include <boost/ref.hpp>
#if BOOST_VERSION < 103500
#include <asio/ip/host_name.hpp>
#include <asio/ip/multicast.hpp>
#else
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/ip/multicast.hpp>
#endif
#include <cstdlib>

namespace libtorrent {

namespace upnp_errors
{
	boost::system::error_code make_error_code(error_code_enum e)
	{
		return error_code(e, get_upnp_category());
	}

} // upnp_errors namespace

static error_code ec;

// TODO: listen_interface is not used. It's meant to bind the broadcast socket
upnp::upnp(io_service& ios, connection_queue& cc
	, address const& listen_interface, std::string const& user_agent
	, portmap_callback_t const& cb, log_callback_t const& lcb
	, bool ignore_nonrouters, void* state)
	: m_user_agent(user_agent)
	, m_callback(cb)
	, m_log_callback(lcb)
	, m_retry_count(0)
	, m_io_service(ios)
	, m_socket(udp::endpoint(address_v4::from_string("239.255.255.250", ec), 1900)
		, boost::bind(&upnp::on_reply, self(), _1, _2, _3))
	, m_broadcast_timer(ios)
	, m_refresh_timer(ios)
	, m_map_timer(ios)
	, m_disabled(false)
	, m_closing(false)
	, m_ignore_non_routers(ignore_nonrouters)
	, m_cc(cc)
{
	TORRENT_ASSERT(cb);

	error_code ec;
	m_socket.open(ios, ec);

	if (state)
	{
		upnp_state_t* s = (upnp_state_t*)state;
		m_devices.swap(s->devices);
		m_mappings.swap(s->mappings);
		delete s;
	}

	m_mappings.reserve(10);
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
	mutex::scoped_lock l(m_mutex);
	if (m_socket.num_send_sockets() == 0)
		log("No network interfaces to broadcast to", l);

	discover_device_impl(l);
}

void upnp::log(char const* msg, mutex::scoped_lock& l)
{
	l.unlock();
	m_log_callback(msg);
	l.lock();
}

void upnp::discover_device_impl(mutex::scoped_lock& l)
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
		char msg[500];
		snprintf(msg, sizeof(msg), "broadcast failed: %s. Aborting."
			, convert_from_native(ec.message()).c_str());
		log(msg, l);
		disable(ec, l);
		return;
	}

#if defined TORRENT_ASIO_DEBUGGING
	add_outstanding_async("upnp::resend_request");
#endif
	++m_retry_count;
	m_broadcast_timer.expires_from_now(seconds(2 * m_retry_count), ec);
	m_broadcast_timer.async_wait(boost::bind(&upnp::resend_request
		, self(), _1));

	log("broadcasting search for rootdevice", l);
}

// returns a reference to a mapping or -1 on failure
int upnp::add_mapping(upnp::protocol_type p, int external_port, int local_port)
{
	// external port 0 means _every_ port
	TORRENT_ASSERT(external_port != 0);

	mutex::scoped_lock l(m_mutex);

	char msg[500];
	snprintf(msg, sizeof(msg), "adding port map: [ protocol: %s ext_port: %u "
		"local_port: %u ] %s", (p == tcp?"tcp":"udp"), external_port
		, local_port, m_disabled ? "DISABLED": "");
	log(msg, l);
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

		if (d.service_namespace) update_map(d, mapping_index, l);
	}

	return mapping_index;
}

void upnp::delete_mapping(int mapping)
{
	mutex::scoped_lock l(m_mutex);

	if (mapping >= int(m_mappings.size())) return;

	global_mapping_t& m = m_mappings[mapping];

	char msg[500];
	snprintf(msg, sizeof(msg), "deleting port map: [ protocol: %s ext_port: %u "
		"local_port: %u ]", (m.protocol == tcp?"tcp":"udp"), m.external_port
		, m.local_port);
	log(msg, l);

	if (m.protocol == none) return;
	
	for (std::set<rootdevice>::iterator i = m_devices.begin()
		, end(m_devices.end()); i != end; ++i)
	{
		rootdevice& d = const_cast<rootdevice&>(*i);
		TORRENT_ASSERT(d.magic == 1337);

		TORRENT_ASSERT(mapping < int(d.mapping.size()));
		d.mapping[mapping].action = mapping_t::action_delete;

		if (d.service_namespace) update_map(d, mapping, l);
	}
}

bool upnp::get_mapping(int index, int& local_port, int& external_port, int& protocol) const
{
	TORRENT_ASSERT(index < int(m_mappings.size()) && index >= 0);
	if (index >= int(m_mappings.size()) || index < 0) return false;
	global_mapping_t const& m = m_mappings[index];
	if (m.protocol == none) return false;
	local_port = m.local_port;
	external_port = m.external_port;
	protocol = m.protocol;
	return true;
}

void upnp::resend_request(error_code const& ec)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("upnp::resend_request");
#endif
	if (ec) return;

	boost::intrusive_ptr<upnp> me(self());

	mutex::scoped_lock l(m_mutex);

	if (m_closing) return;

	if (m_retry_count < 12
		&& (m_devices.empty() || m_retry_count < 4))
	{
		discover_device_impl(l);
		return;
	}

	if (m_devices.empty())
	{
		disable(errors::no_router, l);
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
			TORRENT_TRY
			{
				char msg[500];
				snprintf(msg, sizeof(msg), "connecting to: %s", d.url.c_str());
				log(msg, l);
				if (d.upnp_connection) d.upnp_connection->close();
				d.upnp_connection.reset(new http_connection(m_io_service
					, m_cc, boost::bind(&upnp::on_upnp_xml, self(), _1, _2
					, boost::ref(d), _5)));
				d.upnp_connection->get(d.url, seconds(30), 1);
			}
			TORRENT_CATCH (std::exception& exc)
			{
				TORRENT_DECLARE_DUMMY(std::exception, exc);
				char msg[500];
				snprintf(msg, sizeof(msg), "connection failed to: %s %s", d.url.c_str(), exc.what());
				log(msg, l);
				d.disabled = true;
			}
		}
	}
}

void upnp::on_reply(udp::endpoint const& from, char* buffer
	, std::size_t bytes_transferred)
{
	boost::intrusive_ptr<upnp> me(self());

	mutex::scoped_lock l(m_mutex);

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
		if (ec)
		{
			char msg[500];
			snprintf(msg, sizeof(msg), "when receiving response from: %s: %s"
				, print_endpoint(from).c_str(), convert_from_native(ec.message()).c_str());
			log(msg, l);
		}
		else
		{
			char msg[400];
			int num_chars = snprintf(msg, sizeof(msg)
				, "ignoring response from: %s. IP is not on local network. "
				, print_endpoint(from).c_str());

			std::vector<ip_interface> net = enum_net_interfaces(m_io_service, ec);
			for (std::vector<ip_interface>::const_iterator i = net.begin()
				, end(net.end()); i != end && num_chars < int(sizeof(msg)); ++i)
			{
				num_chars += snprintf(msg + num_chars, sizeof(msg) - num_chars, "(%s,%s) "
					, print_address(i->interface_address).c_str(), print_address(i->netmask).c_str());
			}
			log(msg, l);
			return;
		}
	} 

	bool non_router = false;
	if (m_ignore_non_routers)
	{
		std::vector<ip_route> routes = enum_routes(m_io_service, ec);
		if (std::find_if(routes.begin(), routes.end()
			, boost::bind(&ip_route::gateway, _1) == from.address()) == routes.end())
		{
			// this upnp device is filtered because it's not in the
			// list of configured routers
			if (ec)
			{
				char msg[500];
				snprintf(msg, sizeof(msg), "failed to enumerate routes when "
					"receiving response from: %s: %s"
					, print_endpoint(from).c_str(), convert_from_native(ec.message()).c_str());
				log(msg, l);
			}
			else
			{
				char msg[400];
				int num_chars = snprintf(msg, sizeof(msg), "SSDP response from: "
					"%s: IP is not a router. "
					, print_endpoint(from).c_str());
				for (std::vector<ip_route>::const_iterator i = routes.begin()
					, end(routes.end()); i != end && num_chars < int(sizeof(msg)); ++i)
				{
					num_chars += snprintf(msg + num_chars, sizeof(msg) - num_chars, "(%s,%s) "
						, print_address(i->gateway).c_str(), print_address(i->netmask).c_str());
				}
				log(msg, l);
				non_router = true;
			}
		}
	}

	http_parser p;
	bool error = false;
	p.incoming(buffer::const_interval(buffer
		, buffer + bytes_transferred), error);
	if (error)
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "received malformed HTTP from: %s"
			, print_endpoint(from).c_str());
		log(msg, l);
		return;
	}

	if (p.status_code() != 200 && p.method() != "notify")
	{
		if (p.method().empty())
		{
			char msg[500];
			snprintf(msg, sizeof(msg), "HTTP status %u from %s"
				, p.status_code(), print_endpoint(from).c_str());
			log(msg, l);
		}
		else
		{
			char msg[500];
			snprintf(msg, sizeof(msg), "HTTP method %s from %s"
				, p.method().c_str(), print_endpoint(from).c_str());
			log(msg, l);
		}
		return;
	}

	if (!p.header_finished())
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "incomplete HTTP packet from %s"
			, print_endpoint(from).c_str());
		log(msg, l);
		return;
	}

	std::string url = p.header("location");
	if (url.empty())
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "missing location header from %s"
			, print_endpoint(from).c_str());
		log(msg, l);
		return;
	}

	rootdevice d;
	d.url = url;

	std::set<rootdevice>::iterator i = m_devices.find(d);

	if (i == m_devices.end())
	{
		std::string protocol;
		std::string auth;
		error_code ec;
		// we don't have this device in our list. Add it
		boost::tie(protocol, auth, d.hostname, d.port, d.path)
			= parse_url_components(d.url, ec);
		if (d.port == -1) d.port = protocol == "http" ? 80 : 443;

		if (ec)
		{
			char msg[500];
			snprintf(msg, sizeof(msg), "invalid URL %s from %s: %s"
				, d.url.c_str(), print_endpoint(from).c_str(), convert_from_native(ec.message()).c_str());
			log(msg, l);
			return;
		}

		// ignore the auth here. It will be re-parsed
		// by the http connection later

		if (protocol != "http")
		{
			char msg[500];
			snprintf(msg, sizeof(msg), "unsupported protocol %s from %s"
				, protocol.c_str(), print_endpoint(from).c_str());
			log(msg, l);
			return;
		}

		if (d.port == 0)
		{
			char msg[500];
			snprintf(msg, sizeof(msg), "URL with port 0 from %s"
				, print_endpoint(from).c_str());
			log(msg, l);
			return;
		}

		char msg[500];
		snprintf(msg, sizeof(msg), "found rootdevice: %s (%d)"
			, d.url.c_str(), int(m_devices.size()));
		log(msg, l);

		if (m_devices.size() >= 50)
		{
			char msg[500];
			snprintf(msg, sizeof(msg), "too many rootdevices: (%d). Ignoring %s"
				, int(m_devices.size()), d.url.c_str());
			log(msg, l);
			return;
		}
		d.non_router = non_router;

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


	// iterate over the devices we know and connect and issue the mappings
	try_map_upnp(l);

	if (m_ignore_non_routers)
	{
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("upnp::map_timer");
#endif
		// check back in in a little bit to see if we have seen any
		// devices at one of our default routes. If not, we want to override
		// ignoring them and use them instead (better than not working).
		m_map_timer.expires_from_now(seconds(1), ec);
		m_map_timer.async_wait(boost::bind(&upnp::map_timer
			, self(), _1));
	}
}

void upnp::map_timer(error_code const& ec)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("upnp::map_timer");
#endif
	if (ec) return;
	if (m_closing) return;

	mutex::scoped_lock l(m_mutex);
	try_map_upnp(l, true);
}

void upnp::try_map_upnp(mutex::scoped_lock& l, bool timer)
{
	if (m_devices.empty()) return;

	bool override_ignore_non_routers = false;
	if (m_ignore_non_routers && timer)
	{
		// if we don't ave any devices that match our default route, we
		// should try to map with the ones we did hear from anyway,
		// regardless of if they are not running at our gateway.
		override_ignore_non_routers = std::find_if(m_devices.begin()
			, m_devices.end(), boost::bind(&rootdevice::non_router, _1) == false)
			== m_devices.end();
		if (override_ignore_non_routers)
		{
			char msg[500];
			snprintf(msg, sizeof(msg), "overriding ignore non-routers");
			log(msg, l);
		}
	}

	for (std::set<rootdevice>::iterator i = m_devices.begin()
		, end(m_devices.end()); i != end; ++i)
	{
		// if we're ignoring non-routers, skip them. If on_timer is
		// set, we expect to have received all responses and if we don't
		// have any devices at our default route, then issue requests
		// to any device we found.
		if (m_ignore_non_routers && i->non_router
			&& !override_ignore_non_routers)
			continue;

		if (i->control_url.empty() && !i->upnp_connection && !i->disabled)
		{
			// we don't have a WANIP or WANPPP url for this device,
			// ask for it
			rootdevice& d = const_cast<rootdevice&>(*i);
			TORRENT_ASSERT(d.magic == 1337);
			TORRENT_TRY
			{
				char msg[500];
				snprintf(msg, sizeof(msg), "connecting to: %s"
					, d.url.c_str());
				log(msg, l);

				if (d.upnp_connection) d.upnp_connection->close();
				d.upnp_connection.reset(new http_connection(m_io_service
					, m_cc, boost::bind(&upnp::on_upnp_xml, self(), _1, _2
					, boost::ref(d), _5)));
				d.upnp_connection->get(d.url, seconds(30), 1);
			}
			TORRENT_CATCH (std::exception& exc)
			{
				TORRENT_DECLARE_DUMMY(std::exception, exc);
				char msg[500];
				snprintf(msg, sizeof(msg), "connection failed to: %s %s"
					, d.url.c_str(), exc.what());
				log(msg, l);
				d.disabled = true;
			}
		}
	}
}

void upnp::post(upnp::rootdevice const& d, char const* soap
	, char const* soap_action, mutex::scoped_lock& l)
{
	TORRENT_ASSERT(d.magic == 1337);
	TORRENT_ASSERT(d.upnp_connection);

	char header[2048];
	snprintf(header, sizeof(header), "POST %s HTTP/1.0\r\n"
		"Host: %s:%u\r\n"
		"Content-Type: text/xml; charset=\"utf-8\"\r\n"
		"Content-Length: %d\r\n"
		"Soapaction: \"%s#%s\"\r\n\r\n"
		"%s"
		, d.path.c_str(), d.hostname.c_str(), d.port
		, int(strlen(soap)), d.service_namespace, soap_action
		, soap);

	d.upnp_connection->sendbuffer = header;

	char msg[1024];
	snprintf(msg, sizeof(msg), "sending: %s", header);
	log(msg, l);
}

void upnp::create_port_mapping(http_connection& c, rootdevice& d, int i)
{
	mutex::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);

	if (!d.upnp_connection)
	{
		TORRENT_ASSERT(d.disabled);
		char msg[500];
		snprintf(msg, sizeof(msg), "mapping %u aborted", i);
		log(msg, l);
		return;
	}
	
	char const* soap_action = "AddPortMapping";

	std::string local_endpoint = print_address(c.socket().local_endpoint(ec).address());

	char soap[2048];
	error_code ec;
	snprintf(soap, sizeof(soap), "<?xml version=\"1.0\"?>\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body><u:%s xmlns:u=\"%s\">"
		"<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>%u</NewExternalPort>"
		"<NewProtocol>%s</NewProtocol>"
		"<NewInternalPort>%u</NewInternalPort>"
		"<NewInternalClient>%s</NewInternalClient>"
		"<NewEnabled>1</NewEnabled>"
		"<NewPortMappingDescription>%s at %s:%d</NewPortMappingDescription>"
		"<NewLeaseDuration>%u</NewLeaseDuration>"
		"</u:%s></s:Body></s:Envelope>"
		, soap_action, d.service_namespace, d.mapping[i].external_port
		, (d.mapping[i].protocol == udp ? "UDP" : "TCP")
		, d.mapping[i].local_port
		, local_endpoint.c_str()
		, m_user_agent.c_str(), local_endpoint.c_str(), d.mapping[i].local_port
		, d.lease_duration, soap_action);

	post(d, soap, soap_action, l);
}

void upnp::next(rootdevice& d, int i, mutex::scoped_lock& l)
{
	if (i < num_mappings() - 1)
	{
		update_map(d, i + 1, l);
	}
	else
	{
		std::vector<mapping_t>::iterator j
			= std::find_if(d.mapping.begin(), d.mapping.end()
			, boost::bind(&mapping_t::action, _1) != int(mapping_t::action_none));
		if (j == d.mapping.end()) return;

		update_map(d, j - d.mapping.begin(), l);
	}
}

void upnp::update_map(rootdevice& d, int i, mutex::scoped_lock& l)
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
		char msg[500];
		snprintf(msg, sizeof(msg), "mapping %u does not need updating, skipping", i);
		log(msg, l);
		m.action = mapping_t::action_none;
		next(d, i, l);
		return;
	}

	TORRENT_ASSERT(!d.upnp_connection);
	TORRENT_ASSERT(d.service_namespace);

	char msg[500];
	snprintf(msg, sizeof(msg), "connecting to %s", d.hostname.c_str());
	log(msg, l);
	if (m.action == mapping_t::action_add)
	{
		if (m.failcount > 5)
		{
			m.action = mapping_t::action_none;
			// giving up
			next(d, i, l);
			return;
		}

		if (d.upnp_connection) d.upnp_connection->close();
		d.upnp_connection.reset(new http_connection(m_io_service
			, m_cc, boost::bind(&upnp::on_upnp_map_response, self(), _1, _2
			, boost::ref(d), i, _5), true, default_max_bottled_buffer_size
			, boost::bind(&upnp::create_port_mapping, self(), _1, boost::ref(d), i)));

		d.upnp_connection->start(d.hostname, to_string(d.port).elems
			, seconds(10), 1);
	}
	else if (m.action == mapping_t::action_delete)
	{
		if (d.upnp_connection) d.upnp_connection->close();
		d.upnp_connection.reset(new http_connection(m_io_service
			, m_cc, boost::bind(&upnp::on_upnp_unmap_response, self(), _1, _2
			, boost::ref(d), i, _5), true, default_max_bottled_buffer_size
			, boost::bind(&upnp::delete_port_mapping, self(), boost::ref(d), i)));
		d.upnp_connection->start(d.hostname, to_string(d.port).elems
			, seconds(10), 1);
	}

	m.action = mapping_t::action_none;
}

void upnp::delete_port_mapping(rootdevice& d, int i)
{
	mutex::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);

	if (!d.upnp_connection)
	{
		TORRENT_ASSERT(d.disabled);
		char msg[500];
		snprintf(msg, sizeof(msg), "unmapping %u aborted", i);
		log(msg, l);
		return;
	}

	char const* soap_action = "DeletePortMapping";

	char soap[2048];
	error_code ec;
	snprintf(soap, sizeof(soap), "<?xml version=\"1.0\"?>\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body><u:%s xmlns:u=\"%s\">"
		"<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>%u</NewExternalPort>"
		"<NewProtocol>%s</NewProtocol>"
		"</u:%s></s:Body></s:Envelope>"
		, soap_action, d.service_namespace
		, d.mapping[i].external_port
		, (d.mapping[i].protocol == udp ? "UDP" : "TCP")
		, soap_action);
	
	post(d, soap, soap_action, l);
}

namespace
{
	void copy_tolower(std::string& dst, char const* src)
	{
		dst.clear();
		while (*src) dst.push_back(to_lower(*src++));
	}
}

struct parse_state
{
	parse_state(): in_service(false), service_type(0) {}
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
		if (!string_equal_no_case(i->c_str(), str2)) return false;
		++i;
		if (i == tag_stack.rend()) return false;
		if (!string_equal_no_case(i->c_str(), str1)) return false;
		return true;
	}
};

TORRENT_EXTRA_EXPORT void find_control_url(int type, char const* string, parse_state& state)
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
			if (string_equal_no_case(string, state.service_type))
				state.in_service = true;
		}
		else if (state.control_url.empty() && state.in_service && state.top_tags("service", "controlurl"))
		{
			// default to the first (or only) control url in the router's listing
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

	mutex::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection && d.upnp_connection.get() == &c)
	{
		d.upnp_connection->close();
		d.upnp_connection.reset();
	}

	if (e && e != asio::error::eof)
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "error while fetching control url from: %s: %s"
			, d.url.c_str(), convert_from_native(e.message()).c_str());
		log(msg, l);
		d.disabled = true;
		return;
	}

	if (!p.header_finished())
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "error while fetching control url from: %s: incomplete HTTP message"
			, d.url.c_str());
		log(msg, l);
		d.disabled = true;
		return;
	}

	if (p.status_code() != 200)
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "error while fetching control url from: %s: %s"
			, d.url.c_str(), convert_from_native(p.message()).c_str());
		log(msg, l);
		d.disabled = true;
		return;
	}

	parse_state s;
	s.reset("urn:schemas-upnp-org:service:WANIPConnection:1");
	xml_parse((char*)p.get_body().begin, (char*)p.get_body().end
		, boost::bind(&find_control_url, _1, _2, boost::ref(s)));
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
			, boost::bind(&find_control_url, _1, _2, boost::ref(s)));
		if (!s.control_url.empty())
		{
			d.service_namespace = s.service_type;
			if (!s.model.empty()) m_model = s.model;
		}
		else
		{
			char msg[500];
			snprintf(msg, sizeof(msg), "could not find a port mapping interface in response from: %s"
				, d.url.c_str());
			log(msg, l);
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
	error_code ec;
	if (!d.control_url.empty() && d.control_url[0] == '/')
	{
		boost::tie(protocol, auth, d.hostname, d.port, d.path)
			= parse_url_components(d.url, ec);
		if (d.port == -1) d.port = protocol == "http" ? 80 : 443;
		d.control_url = protocol + "://" + d.hostname + ":"
			+ to_string(d.port).elems + s.control_url;
	}

	char msg[500];
	snprintf(msg, sizeof(msg), "found control URL: %s namespace %s "
		"urlbase: %s in response from %s"
		, d.control_url.c_str(), d.service_namespace
		, s.url_base.c_str(), d.url.c_str());
	log(msg, l);

	boost::tie(protocol, auth, d.hostname, d.port, d.path)
		= parse_url_components(d.control_url, ec);
	if (d.port == -1) d.port = protocol == "http" ? 80 : 443;

	if (ec)
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "failed to parse URL '%s': %s"
			, d.control_url.c_str(), convert_from_native(ec.message()).c_str());
		log(msg, l);
		d.disabled = true;
		return;
	}

	d.upnp_connection.reset(new http_connection(m_io_service
		, m_cc, boost::bind(&upnp::on_upnp_get_ip_address_response, self(), _1, _2
		, boost::ref(d), _5), true, default_max_bottled_buffer_size 
		, boost::bind(&upnp::get_ip_address, self(), boost::ref(d))));
	d.upnp_connection->start(d.hostname, to_string(d.port).elems
		, seconds(10), 1);
}

void upnp::get_ip_address(rootdevice& d)
{
	mutex::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);

	if (!d.upnp_connection)
	{
		TORRENT_ASSERT(d.disabled);
		char msg[500];
		snprintf(msg, sizeof(msg), "getting external IP address");
		log(msg, l);
		return;
	}

	char const* soap_action = "GetExternalIPAddress";

	char soap[2048];
	error_code ec;
	snprintf(soap, sizeof(soap), "<?xml version=\"1.0\"?>\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body><u:%s xmlns:u=\"%s\">"
		"</u:%s></s:Body></s:Envelope>"
		, soap_action, d.service_namespace
		, soap_action);

	post(d, soap, soap_action, l);
}

void upnp::disable(error_code const& ec, mutex::scoped_lock& l)
{
	m_disabled = true;

	// kill all mappings
	for (std::vector<global_mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol == none) continue;
		i->protocol = none;
		l.unlock();
		m_callback(i - m_mappings.begin(), address(), 0, ec);
		l.lock();
	}
	
	// we cannot clear the devices since there
	// might be outstanding requests relying on
	// the device entry being present when they
	// complete
	error_code e;
	m_broadcast_timer.cancel(e);
	m_refresh_timer.cancel(e);
	m_map_timer.cancel(e);
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

	struct ip_address_parse_state: public error_code_parse_state
	{
		ip_address_parse_state(): in_ip_address(false) {}
		bool in_ip_address;
		std::string ip_address;
	};

	void find_ip_address(int type, char const* string, ip_address_parse_state& state)
	{
		find_error_code(type, string, state);
		if (state.exit) return;

		if (type == xml_start_tag && !std::strcmp("NewExternalIPAddress", string))
		{
			state.in_ip_address = true;
		}
		else if (type == xml_string && state.in_ip_address)
		{
			state.ip_address = string;
			state.exit = true;
		}
	}

	struct error_code_t
	{
		int code;
		char const* msg;
	};
	
	error_code_t error_codes[] =
	{
		{0, "no error"}
		, {402, "Invalid Arguments"}
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

#if BOOST_VERSION >= 103500

struct upnp_error_category : boost::system::error_category
{
	virtual const char* name() const BOOST_SYSTEM_NOEXCEPT
	{
		return "UPnP error";
	}

	virtual std::string message(int ev) const BOOST_SYSTEM_NOEXCEPT
	{
		int num_errors = sizeof(error_codes) / sizeof(error_codes[0]);
		error_code_t* end = error_codes + num_errors;
		error_code_t tmp = {ev, 0};
		error_code_t* e = std::lower_bound(error_codes, end, tmp
			, boost::bind(&error_code_t::code, _1) < boost::bind(&error_code_t::code, _2));
		if (e != end && e->code == ev)
		{
			return e->msg;
		}
		char msg[500];
		snprintf(msg, sizeof(msg), "unknown UPnP error (%d)", ev);
		return msg;
	}

	virtual boost::system::error_condition default_error_condition(
		int ev) const BOOST_SYSTEM_NOEXCEPT
	{
		return boost::system::error_condition(ev, *this);
	}
};

boost::system::error_category& get_upnp_category()
{
	static upnp_error_category cat;
	return cat;
}

#else

boost::system::error_category& get_upnp_category()
{
	static ::asio::error::error_category cat(21);
	return cat;
}

#endif

void upnp::on_upnp_get_ip_address_response(error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d
	, http_connection& c)
{
	boost::intrusive_ptr<upnp> me(self());

	mutex::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection && d.upnp_connection.get() == &c)
	{
		d.upnp_connection->close();
		d.upnp_connection.reset();
	}

	if (m_closing) return;

	if (e && e != asio::error::eof)
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "error while getting external IP address: %s"
			, convert_from_native(e.message()).c_str());
		log(msg, l);
		if (num_mappings() > 0) update_map(d, 0, l);
		return;
	}

	if (!p.header_finished())
	{
		log("error while getting external IP address: incomplete http message", l);
		if (num_mappings() > 0) update_map(d, 0, l);
		return;
	}

	if (p.status_code() != 200)
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "error while getting external IP address: %s"
			, convert_from_native(p.message()).c_str());
		log(msg, l);
		if (num_mappings() > 0) update_map(d, 0, l);
		return;
	}

	// response may look like
	// <?xml version="1.0"?>
	// <s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
	// <s:Body><u:GetExternalIPAddressResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1">
	// <NewExternalIPAddress>192.168.160.19</NewExternalIPAddress>
	// </u:GetExternalIPAddressResponse>
	// </s:Body>
	// </s:Envelope>

	char msg[500];
	snprintf(msg, sizeof(msg), "get external IP address response: %s"
		, std::string(p.get_body().begin, p.get_body().end).c_str());
	log(msg, l);

	ip_address_parse_state s;
	xml_parse((char*)p.get_body().begin, (char*)p.get_body().end
		, boost::bind(&find_ip_address, _1, _2, boost::ref(s)));
	if (s.error_code != -1)
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "error while getting external IP address, code: %u"
			, s.error_code);
		log(msg, l);
	}

	if (!s.ip_address.empty()) {
		snprintf(msg, sizeof(msg), "got router external IP address %s", s.ip_address.c_str());
		log(msg, l);
		d.external_ip = address::from_string(s.ip_address.c_str(), ec);
	} else {
		log("failed to find external IP address in response", l);
	}

	if (num_mappings() > 0) update_map(d, 0, l);
}

void upnp::on_upnp_map_response(error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d, int mapping
	, http_connection& c)
{
	boost::intrusive_ptr<upnp> me(self());

	mutex::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection && d.upnp_connection.get() == &c)
	{
		d.upnp_connection->close();
		d.upnp_connection.reset();
	}

	if (e && e != asio::error::eof)
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "error while adding port map: %s"
			, convert_from_native(e.message()).c_str());
		log(msg, l);
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
		log("error while adding port map: incomplete http message", l);
		next(d, mapping, l);
		return;
	}

	std::string ct = p.header("content-type");
	if (!ct.empty()
		&& ct.find_first_of("text/xml") == std::string::npos
		&& ct.find_first_of("text/soap+xml") == std::string::npos
		&& ct.find_first_of("application/xml") == std::string::npos
		&& ct.find_first_of("application/soap+xml") == std::string::npos
		)
	{
		char msg[300];
		snprintf(msg, sizeof(msg), "error while adding port map: invalid content-type, \"%s\". Expected text/xml or application/soap+xml"
			, ct.c_str());
		log(msg, l);
		next(d, mapping, l);
		return;
	}

	// We don't want to ignore responses with return codes other than 200
	// since those might contain valid UPnP error codes

	error_code_parse_state s;
	xml_parse((char*)p.get_body().begin, (char*)p.get_body().end
		, boost::bind(&find_error_code, _1, _2, boost::ref(s)));

	if (s.error_code != -1)
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "error while adding port map, code: %u"
			, s.error_code);
		log(msg, l);
	}
	
	mapping_t& m = d.mapping[mapping];

	if (s.error_code == 725)
	{
		// only permanent leases supported
		d.lease_duration = 0;
		m.action = mapping_t::action_add;
		++m.failcount;
		update_map(d, mapping, l);
		return;
	}
	else if (s.error_code == 727)
	{
		return_error(mapping, s.error_code, l);
	}
	else if ((s.error_code == 718 || s.error_code == 501) && m.failcount < 4)
	{
		// some routers return 501 action failed, instead of 716
		// The external port conflicts with another mapping
		// pick a random port
		m.external_port = 40000 + (random() % 10000);
		m.action = mapping_t::action_add;
		++m.failcount;
		update_map(d, mapping, l);
		return;
	}
	else if (s.error_code != -1)
	{
		return_error(mapping, s.error_code, l);
	}

	char msg[500];
	snprintf(msg, sizeof(msg), "map response: %s"
		, std::string(p.get_body().begin, p.get_body().end).c_str());
	log(msg, l);

	if (s.error_code == -1)
	{
		l.unlock();
		m_callback(mapping, d.external_ip, m.external_port, error_code());
		l.lock();
		if (d.lease_duration > 0)
		{
			m.expires = time_now()
				+ seconds(int(d.lease_duration * 0.75f));
			ptime next_expire = m_refresh_timer.expires_at();
			if (next_expire < time_now()
				|| next_expire > m.expires)
			{
#if defined TORRENT_ASIO_DEBUGGING
				add_outstanding_async("upnp::on_expire");
#endif
				error_code ec;
				m_refresh_timer.expires_at(m.expires, ec);
				m_refresh_timer.async_wait(boost::bind(&upnp::on_expire, self(), _1));
			}
		}
		else
		{
			m.expires = max_time();
		}
		m.failcount = 0;
	}

	next(d, mapping, l);
}

void upnp::return_error(int mapping, int code, mutex::scoped_lock& l)
{
	int num_errors = sizeof(error_codes) / sizeof(error_codes[0]);
	error_code_t* end = error_codes + num_errors;
	error_code_t tmp = {code, 0};
	error_code_t* e = std::lower_bound(error_codes, end, tmp
		, boost::bind(&error_code_t::code, _1) < boost::bind(&error_code_t::code, _2));
	std::string error_string = "UPnP mapping error ";
	error_string += to_string(code).elems;
	if (e != end && e->code == code)
	{
		error_string += ": ";
		error_string += e->msg;
	}
	l.unlock();
	m_callback(mapping, address(), 0, error_code(code, get_upnp_category()));
	l.lock();
}

void upnp::on_upnp_unmap_response(error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d, int mapping
	, http_connection& c)
{
	boost::intrusive_ptr<upnp> me(self());

	mutex::scoped_lock l(m_mutex);

	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection && d.upnp_connection.get() == &c)
	{
		d.upnp_connection->close();
		d.upnp_connection.reset();
	}

	if (e && e != asio::error::eof)
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "error while deleting portmap: %s"
			, convert_from_native(e.message()).c_str());
		log(msg, l);
	}
	else if (!p.header_finished())
	{
		log("error while deleting portmap: incomplete http message", l);
	}
	else if (p.status_code() != 200)
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "error while deleting portmap: %s"
			, convert_from_native(p.message()).c_str());
		log(msg, l);
	}
	else
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "unmap response: %s"
			, std::string(p.get_body().begin, p.get_body().end).c_str());
		log(msg, l);
	}

	error_code_parse_state s;
	if (p.header_finished())
	{
		xml_parse((char*)p.get_body().begin, (char*)p.get_body().end
			, boost::bind(&find_error_code, _1, _2, boost::ref(s)));
	}

	l.unlock();
	m_callback(mapping, address(), 0, p.status_code() != 200
		? error_code(p.status_code(), get_http_category())
		: error_code(s.error_code, get_upnp_category()));
	l.lock();

	d.mapping[mapping].protocol = none;

	next(d, mapping, l);
}

void upnp::on_expire(error_code const& ec)
{
#if defined TORRENT_ASIO_DEBUGGING
	complete_async("upnp::on_expire");
#endif
	if (ec) return;

	ptime now = time_now();
	ptime next_expire = max_time();

	mutex::scoped_lock l(m_mutex);

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
				update_map(d, m, l);
			}
			else if (d.mapping[m].expires < next_expire)
			{
				next_expire = d.mapping[m].expires;
			}
		}
	}
	if (next_expire != max_time())
	{
#if defined TORRENT_ASIO_DEBUGGING
		add_outstanding_async("upnp::on_expire");
#endif
		error_code e;
		m_refresh_timer.expires_at(next_expire, e);
		m_refresh_timer.async_wait(boost::bind(&upnp::on_expire, self(), _1));
	}
}

void upnp::close()
{
	mutex::scoped_lock l(m_mutex);

	error_code ec;
	m_refresh_timer.cancel(ec);
	m_broadcast_timer.cancel(ec);
	m_map_timer.cancel(ec);
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
		if (num_mappings() > 0) update_map(d, 0, l);
	}
}

}

