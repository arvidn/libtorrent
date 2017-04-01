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

#include "libtorrent/socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/xml_parse.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/aux_/time.hpp" // for aux::time_now()
#include "libtorrent/aux_/escape_string.hpp" // for convert_from_native
#include "libtorrent/debug.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/ip/multicast.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <cstdlib>
#include <cstdio> // for snprintf
#include <cstdarg>
#include <functional>

using namespace std::placeholders;

namespace lt {
LIBTORRENT_VERSION_NAMESPACE {

using namespace aux;

namespace upnp_errors
{
	boost::system::error_code make_error_code(error_code_enum e)
	{
		return error_code(e, upnp_category());
	}

} // upnp_errors namespace

static error_code ignore_error;

// TODO: 3 bind the broadcast socket. it would probably have to be changed to a vector of interfaces to
// bind to, since the broadcast socket opens one socket per local
// interface by default
upnp::upnp(io_service& ios
	, std::string const& user_agent
	, aux::portmap_callback& cb
	, bool ignore_nonrouters)
	: m_user_agent(user_agent)
	, m_callback(cb)
	, m_retry_count(0)
	, m_io_service(ios)
	, m_resolver(ios)
	, m_socket(udp::endpoint(address_v4::from_string("239.255.255.250"
		, ignore_error), 1900))
	, m_broadcast_timer(ios)
	, m_refresh_timer(ios)
	, m_map_timer(ios)
	, m_disabled(false)
	, m_closing(false)
	, m_ignore_non_routers(ignore_nonrouters)
	, m_last_if_update(min_time())
{
}

void upnp::start()
{
	TORRENT_ASSERT(is_single_thread());

	error_code ec;
	m_socket.open(std::bind(&upnp::on_reply, self(), _1, _2, _3)
		, m_refresh_timer.get_io_service(), ec);

	m_mappings.reserve(10);
}

upnp::~upnp() = default;

void upnp::discover_device()
{
	TORRENT_ASSERT(is_single_thread());
#ifndef TORRENT_DISABLE_LOGGING
	if (m_socket.num_send_sockets() == 0)
		log("No network interfaces to broadcast to");
#endif

	discover_device_impl();
}

#ifndef TORRENT_DISABLE_LOGGING
bool upnp::should_log() const
{
	return m_callback.should_log_portmap(aux::portmap_transport::upnp);
}

TORRENT_FORMAT(2,3)
void upnp::log(char const* fmt, ...) const
{
	TORRENT_ASSERT(is_single_thread());
	if (!should_log()) return;
	va_list v;
	va_start(v, fmt);
	char msg[500];
	std::vsnprintf(msg, sizeof(msg), fmt, v);
	va_end(v);
	m_callback.log_portmap(aux::portmap_transport::upnp, msg);
}
#endif

void upnp::discover_device_impl()
{
	TORRENT_ASSERT(is_single_thread());
	static const char msearch[] =
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
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("broadcast failed: %s. Aborting."
				, convert_from_native(ec.message()).c_str());
		}
#endif
		disable(ec);
		return;
	}

	ADD_OUTSTANDING_ASYNC("upnp::resend_request");
	++m_retry_count;
	m_broadcast_timer.expires_from_now(seconds(2 * m_retry_count), ec);
	m_broadcast_timer.async_wait(std::bind(&upnp::resend_request
		, self(), _1));

#ifndef TORRENT_DISABLE_LOGGING
	log("broadcasting search for rootdevice");
#endif
}

// returns a reference to a mapping or -1 on failure
int upnp::add_mapping(portmap_protocol const p, int const external_port
	, int const local_port)
{
	TORRENT_ASSERT(is_single_thread());
	// external port 0 means _every_ port
	TORRENT_ASSERT(external_port != 0);

#ifndef TORRENT_DISABLE_LOGGING
	log("adding port map: [ protocol: %s ext_port: %u "
		"local_port: %u ] %s", (p == portmap_protocol::tcp?"tcp":"udp")
		, external_port
		, local_port, m_disabled ? "DISABLED": "");
#endif
	if (m_disabled) return -1;

	std::vector<global_mapping_t>::iterator mapping_it = std::find_if(
		m_mappings.begin(), m_mappings.end()
		, [](global_mapping_t const& m) { return m.protocol == portmap_protocol::none; });

	if (mapping_it == m_mappings.end())
	{
		m_mappings.push_back(global_mapping_t());
		mapping_it = m_mappings.end() - 1;
	}

	mapping_it->protocol = p;
	mapping_it->external_port = external_port;
	mapping_it->local_port = local_port;

	int const mapping_index = int(mapping_it - m_mappings.begin());

	for (auto const& dev : m_devices)
	{
		rootdevice& d = const_cast<rootdevice&>(dev);
		TORRENT_ASSERT(d.magic == 1337);

		if (d.mapping.end_index() <= mapping_index)
			d.mapping.resize(mapping_index + 1);
		mapping_t& m = d.mapping[mapping_index];

		m.act = mapping_t::action::add;
		m.protocol = p;
		m.external_port = external_port;
		m.local_port = local_port;

		if (!d.service_namespace.empty()) update_map(d, mapping_index);
	}

	return mapping_index;
}

void upnp::delete_mapping(int const mapping)
{
	TORRENT_ASSERT(is_single_thread());

	if (mapping >= int(m_mappings.size())) return;

	global_mapping_t& m = m_mappings[mapping];

#ifndef TORRENT_DISABLE_LOGGING
	log("deleting port map: [ protocol: %s ext_port: %u "
		"local_port: %u ]", (m.protocol == portmap_protocol::tcp?"tcp":"udp"), m.external_port
		, m.local_port);
#endif

	if (m.protocol == portmap_protocol::none) return;

	for (auto const& dev : m_devices)
	{
		rootdevice& d = const_cast<rootdevice&>(dev);
		TORRENT_ASSERT(d.magic == 1337);

		TORRENT_ASSERT(mapping < d.mapping.end_index());
		d.mapping[mapping].act = mapping_t::action::del;

		if (!d.service_namespace.empty()) update_map(d, mapping);
	}
}

bool upnp::get_mapping(int const index
	, int& local_port
	, int& external_port
	, portmap_protocol& protocol) const
{
	TORRENT_ASSERT(is_single_thread());
	TORRENT_ASSERT(index < int(m_mappings.size()) && index >= 0);
	if (index >= int(m_mappings.size()) || index < 0) return false;
	global_mapping_t const& m = m_mappings[index];
	if (m.protocol == portmap_protocol::none) return false;
	local_port = m.local_port;
	external_port = m.external_port;
	protocol = m.protocol;
	return true;
}

void upnp::resend_request(error_code const& ec)
{
	TORRENT_ASSERT(is_single_thread());
	COMPLETE_ASYNC("upnp::resend_request");
	if (ec) return;

	std::shared_ptr<upnp> me(self());

	if (m_closing) return;

	if (m_retry_count < 12
		&& (m_devices.empty() || m_retry_count < 4))
	{
		discover_device_impl();
		return;
	}

	if (m_devices.empty())
	{
		disable(errors::no_router);
		return;
	}

	for (auto const& dev : m_devices)
	{
		if (!dev.control_url.empty()
			|| dev.upnp_connection
			|| dev.disabled)
		{
			continue;
		}

		// we don't have a WANIP or WANPPP url for this device,
		// ask for it
		rootdevice& d = const_cast<rootdevice&>(dev);
		TORRENT_ASSERT(d.magic == 1337);
		TORRENT_TRY
		{
#ifndef TORRENT_DISABLE_LOGGING
			log("connecting to: %s", d.url.c_str());
#endif
			if (d.upnp_connection) d.upnp_connection->close();
			d.upnp_connection = std::make_shared<http_connection>(m_io_service
				, m_resolver
				, std::bind(&upnp::on_upnp_xml, self(), _1, _2
				, std::ref(d), _5));
			d.upnp_connection->get(d.url, seconds(30), 1);
		}
		TORRENT_CATCH (std::exception const& exc)
		{
			TORRENT_DECLARE_DUMMY(std::exception, exc);
			TORRENT_UNUSED(exc);
#ifndef TORRENT_DISABLE_LOGGING
			log("connection failed to: %s %s", d.url.c_str(), exc.what());
#endif
			d.disabled = true;
		}
	}
}

void upnp::on_reply(udp::endpoint const& from, char* buffer
	, std::size_t bytes_transferred)
{
	TORRENT_ASSERT(is_single_thread());
	std::shared_ptr<upnp> me(self());

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
	if (clock_type::now() - seconds(60) > m_last_if_update)
	{
		m_interfaces = enum_net_interfaces(m_io_service, ec);
#ifndef TORRENT_DISABLE_LOGGING
		if (ec && should_log())
		{
			log("when receiving response from: %s: %s"
				, print_endpoint(from).c_str(), convert_from_native(ec.message()).c_str());
		}
#endif
		m_last_if_update = aux::time_now();
	}

	if (!ec && !in_local_network(m_interfaces, from.address()))
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			char msg[400];
			int num_chars = std::snprintf(msg, sizeof(msg)
				, "ignoring response from: %s. IP is not on local network. "
				, print_endpoint(from).c_str());

			for (auto const& iface : m_interfaces)
			{
				num_chars += std::snprintf(msg + num_chars, sizeof(msg) - std::size_t(num_chars), "(%s,%s) "
					, print_address(iface.interface_address).c_str(), print_address(iface.netmask).c_str());
				if (num_chars >= int(sizeof(msg))) break;
			}
			log("%s", msg);
		}
#endif
		return;
	}

	bool non_router = false;
	if (m_ignore_non_routers)
	{
		std::vector<ip_route> const routes = enum_routes(m_io_service, ec);
		if (std::none_of(routes.begin(), routes.end()
			, [from] (ip_route const& rt) { return rt.gateway == from.address(); }))
		{
			// this upnp device is filtered because it's not in the
			// list of configured routers
			if (ec)
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					log("failed to enumerate routes when "
						"receiving response from: %s: %s"
						, print_endpoint(from).c_str(), convert_from_native(ec.message()).c_str());
				}
#endif
			}
			else
			{
#ifndef TORRENT_DISABLE_LOGGING
				if (should_log())
				{
					char msg[400];
					int num_chars = std::snprintf(msg, sizeof(msg), "SSDP response from: "
						"%s: IP is not a router. "
						, print_endpoint(from).c_str());
					for (auto const& route : routes)
					{
						num_chars += std::snprintf(msg + num_chars, sizeof(msg) - std::size_t(num_chars), "(%s,%s) "
							, print_address(route.gateway).c_str()
							, print_address(route.netmask).c_str());
						if (num_chars >= int(sizeof(msg))) break;
					}
					log("%s", msg);
				}
#endif
				non_router = true;
			}
		}
	}

	http_parser p;
	bool error = false;
	p.incoming(span<char const>(buffer, bytes_transferred), error);
	if (error)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("received malformed HTTP from: %s"
				, print_endpoint(from).c_str());
		}
#endif
		return;
	}

	if (p.status_code() != 200 && p.method() != "notify")
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			if (p.method().empty())
			{
				log("HTTP status %u from %s"
					, p.status_code(), print_endpoint(from).c_str());
			}
			else
			{
				log("HTTP method %s from %s"
					, p.method().c_str(), print_endpoint(from).c_str());
			}
		}
#endif
		return;
	}

	if (!p.header_finished())
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("incomplete HTTP packet from %s"
				, print_endpoint(from).c_str());
		}
#endif
		return;
	}

	std::string url = p.header("location");
	if (url.empty())
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("missing location header from %s"
				, print_endpoint(from).c_str());
		}
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
		std::tie(protocol, auth, d.hostname, d.port, d.path)
			= parse_url_components(d.url, ec);
		if (d.port == -1) d.port = protocol == "http" ? 80 : 443;

		if (ec)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				log("invalid URL %s from %s: %s"
					, d.url.c_str(), print_endpoint(from).c_str(), convert_from_native(ec.message()).c_str());
			}
#endif
			return;
		}

		// ignore the auth here. It will be re-parsed
		// by the http connection later

		if (protocol != "http")
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				log("unsupported protocol %s from %s"
					, protocol.c_str(), print_endpoint(from).c_str());
			}
#endif
			return;
		}

		if (d.port == 0)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				log("URL with port 0 from %s", print_endpoint(from).c_str());
			}
#endif
			return;
		}

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("found rootdevice: %s (%d)"
				, d.url.c_str(), int(m_devices.size()));
		}
#endif

		if (m_devices.size() >= 50)
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (should_log())
			{
				log("too many rootdevices: (%d). Ignoring %s"
					, int(m_devices.size()), d.url.c_str());
			}
#endif
			return;
		}
		d.non_router = non_router;

		TORRENT_ASSERT(d.mapping.empty());
		for (auto const& j : m_mappings)
		{
			mapping_t m;
			m.act = mapping_t::action::add;
			m.local_port = j.local_port;
			m.external_port = j.external_port;
			m.protocol = j.protocol;
			d.mapping.push_back(m);
		}
		std::tie(i, std::ignore) = m_devices.insert(d);
	}


	// iterate over the devices we know and connect and issue the mappings
	try_map_upnp();

	if (m_ignore_non_routers)
	{
		ADD_OUTSTANDING_ASYNC("upnp::map_timer");
		// check back in in a little bit to see if we have seen any
		// devices at one of our default routes. If not, we want to override
		// ignoring them and use them instead (better than not working).
		m_map_timer.expires_from_now(seconds(1), ec);
		m_map_timer.async_wait(std::bind(&upnp::map_timer, self(), _1));
	}
}

void upnp::map_timer(error_code const& ec)
{
	TORRENT_ASSERT(is_single_thread());
	COMPLETE_ASYNC("upnp::map_timer");
	if (ec) return;
	if (m_closing) return;

	try_map_upnp(true);
}

void upnp::try_map_upnp(bool timer)
{
	TORRENT_ASSERT(is_single_thread());
	if (m_devices.empty()) return;

	bool override_ignore_non_routers = false;
	if (m_ignore_non_routers && timer)
	{
		// if we don't ave any devices that match our default route, we
		// should try to map with the ones we did hear from anyway,
		// regardless of if they are not running at our gateway.
		override_ignore_non_routers = std::none_of(m_devices.begin()
			, m_devices.end(), [](rootdevice const& d) { return d.non_router == false; });
#ifndef TORRENT_DISABLE_LOGGING
		if (override_ignore_non_routers)
		{
			log("overriding ignore non-routers");
		}
#endif
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
#ifndef TORRENT_DISABLE_LOGGING
				log("connecting to: %s", d.url.c_str());
#endif

				if (d.upnp_connection) d.upnp_connection->close();
				d.upnp_connection = std::make_shared<http_connection>(m_io_service
					, m_resolver
					, std::bind(&upnp::on_upnp_xml, self(), _1, _2
					, std::ref(d), _5));
				d.upnp_connection->get(d.url, seconds(30), 1);
			}
			TORRENT_CATCH (std::exception const& exc)
			{
				TORRENT_DECLARE_DUMMY(std::exception, exc);
				TORRENT_UNUSED(exc);
#ifndef TORRENT_DISABLE_LOGGING
				log("connection failed to: %s %s", d.url.c_str(), exc.what());
#endif
				d.disabled = true;
			}
		}
	}
}

void upnp::post(upnp::rootdevice const& d, char const* soap
	, char const* soap_action)
{
	TORRENT_ASSERT(is_single_thread());
	TORRENT_ASSERT(d.magic == 1337);
	TORRENT_ASSERT(d.upnp_connection);

	char header[2048];
	std::snprintf(header, sizeof(header), "POST %s HTTP/1.1\r\n"
		"Host: %s:%u\r\n"
		"Content-Type: text/xml; charset=\"utf-8\"\r\n"
		"Content-Length: %d\r\n"
		"Soapaction: \"%s#%s\"\r\n\r\n"
		"%s"
		, d.path.c_str(), d.hostname.c_str(), d.port
		, int(strlen(soap)), d.service_namespace.c_str(), soap_action
		, soap);

	d.upnp_connection->m_sendbuffer = header;

#ifndef TORRENT_DISABLE_LOGGING
	log("sending: %s", header);
#endif
}

void upnp::create_port_mapping(http_connection& c, rootdevice& d, int const i)
{
	TORRENT_ASSERT(is_single_thread());

	TORRENT_ASSERT(d.magic == 1337);

	if (!d.upnp_connection)
	{
		TORRENT_ASSERT(d.disabled);
#ifndef TORRENT_DISABLE_LOGGING
		log("mapping %u aborted", i);
#endif
		return;
	}

	char const* soap_action = "AddPortMapping";

	error_code ec;
	std::string local_endpoint = print_address(c.socket().local_endpoint(ec).address());

	char soap[2048];
	std::snprintf(soap, sizeof(soap), "<?xml version=\"1.0\"?>\n"
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
		, soap_action, d.service_namespace.c_str(), d.mapping[i].external_port
		, (d.mapping[i].protocol == portmap_protocol::udp ? "UDP" : "TCP")
		, d.mapping[i].local_port
		, local_endpoint.c_str()
		, m_user_agent.c_str(), local_endpoint.c_str(), d.mapping[i].local_port
		, d.lease_duration, soap_action);

	post(d, soap, soap_action);
}

void upnp::next(rootdevice& d, int const i)
{
	TORRENT_ASSERT(is_single_thread());
	if (i < num_mappings() - 1)
	{
		update_map(d, i + 1);
	}
	else
	{
		auto const j = std::find_if(d.mapping.begin(), d.mapping.end()
			, [] (mapping_t const& m) { return m.act != mapping_t::action::none; });
		if (j == d.mapping.end()) return;

		update_map(d, int(j - d.mapping.begin()));
	}
}

void upnp::update_map(rootdevice& d, int const i)
{
	TORRENT_ASSERT(is_single_thread());
	TORRENT_ASSERT(d.magic == 1337);
	TORRENT_ASSERT(i < d.mapping.end_index());
	TORRENT_ASSERT(d.mapping.size() == m_mappings.size());

	if (d.upnp_connection) return;

	std::shared_ptr<upnp> me(self());

	mapping_t& m = d.mapping[i];

	if (m.act == mapping_t::action::none
		|| m.protocol == portmap_protocol::none)
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("mapping %u does not need updating, skipping", i);
#endif
		m.act = mapping_t::action::none;
		next(d, i);
		return;
	}

	TORRENT_ASSERT(!d.upnp_connection);
	TORRENT_ASSERT(!d.service_namespace.empty());

#ifndef TORRENT_DISABLE_LOGGING
	log("connecting to %s", d.hostname.c_str());
#endif
	if (m.act == mapping_t::action::add)
	{
		if (m.failcount > 5)
		{
			m.act = mapping_t::action::none;
			// giving up
			next(d, i);
			return;
		}

		if (d.upnp_connection) d.upnp_connection->close();
		d.upnp_connection = std::make_shared<http_connection>(m_io_service
			, m_resolver
			, std::bind(&upnp::on_upnp_map_response, self(), _1, _2
			, std::ref(d), i, _5), true, default_max_bottled_buffer_size
			, std::bind(&upnp::create_port_mapping, self(), _1, std::ref(d), i));

		d.upnp_connection->start(d.hostname, d.port
			, seconds(10), 1);
	}
	else if (m.act == mapping_t::action::del)
	{
		if (d.upnp_connection) d.upnp_connection->close();
		d.upnp_connection = std::make_shared<http_connection>(m_io_service
			, m_resolver
			, std::bind(&upnp::on_upnp_unmap_response, self(), _1, _2
			, std::ref(d), i, _5), true, default_max_bottled_buffer_size
			, std::bind(&upnp::delete_port_mapping, self(), std::ref(d), i));
		d.upnp_connection->start(d.hostname, d.port
			, seconds(10), 1);
	}

	m.act = mapping_t::action::none;
}

void upnp::delete_port_mapping(rootdevice& d, int const i)
{
	TORRENT_ASSERT(is_single_thread());

	TORRENT_ASSERT(d.magic == 1337);

	if (!d.upnp_connection)
	{
		TORRENT_ASSERT(d.disabled);
#ifndef TORRENT_DISABLE_LOGGING
		log("unmapping %u aborted", i);
#endif
		return;
	}

	char const* soap_action = "DeletePortMapping";

	char soap[2048];
	error_code ec;
	std::snprintf(soap, sizeof(soap), "<?xml version=\"1.0\"?>\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body><u:%s xmlns:u=\"%s\">"
		"<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>%u</NewExternalPort>"
		"<NewProtocol>%s</NewProtocol>"
		"</u:%s></s:Body></s:Envelope>"
		, soap_action, d.service_namespace.c_str()
		, d.mapping[i].external_port
		, (d.mapping[i].protocol == portmap_protocol::udp ? "UDP" : "TCP")
		, soap_action);

	post(d, soap, soap_action);
}

void find_control_url(int const type, string_view str, parse_state& state)
{
	if (type == xml_start_tag)
	{
		state.tag_stack.push_back(str);
	}
	else if (type == xml_end_tag)
	{
		if (!state.tag_stack.empty())
		{
			if (state.in_service && string_equal_no_case(state.tag_stack.back(), "service"))
				state.in_service = false;
			state.tag_stack.pop_back();
		}
	}
	else if (type == xml_string)
	{
		if (state.tag_stack.empty()) return;
		if (!state.in_service && state.top_tags("service", "servicetype"))
		{
			if (string_equal_no_case(str, "urn:schemas-upnp-org:service:WANIPConnection:1")
				|| string_equal_no_case(str, "urn:schemas-upnp-org:service:WANIPConnection:2")
				|| string_equal_no_case(str, "urn:schemas-upnp-org:service:WANPPPConnection:1"))
			{
				state.service_type.assign(str.begin(), str.end());
				state.in_service = true;
			}
		}
		else if (state.control_url.empty() && state.in_service
			&& state.top_tags("service", "controlurl") && str.size() > 0)
		{
			// default to the first (or only) control url in the router's listing
			state.control_url.assign(str.begin(), str.end());
		}
		else if (state.model.empty() && state.top_tags("device", "modelname"))
		{
			state.model.assign(str.begin(), str.end());
		}
		else if (string_equal_no_case(state.tag_stack.back(), "urlbase"))
		{
			state.url_base.assign(str.begin(), str.end());
		}
	}
}

void upnp::on_upnp_xml(error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d
	, http_connection& c)
{
	TORRENT_ASSERT(is_single_thread());
	std::shared_ptr<upnp> me(self());

	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection && d.upnp_connection.get() == &c)
	{
		d.upnp_connection->close();
		d.upnp_connection.reset();
	}

	if (e && e != boost::asio::error::eof)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("error while fetching control url from: %s: %s"
				, d.url.c_str(), convert_from_native(e.message()).c_str());
		}
#endif
		d.disabled = true;
		return;
	}

	if (!p.header_finished())
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("error while fetching control url from: %s: incomplete HTTP message"
			, d.url.c_str());
#endif
		d.disabled = true;
		return;
	}

	if (p.status_code() != 200)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("error while fetching control url from: %s: %s"
				, d.url.c_str(), convert_from_native(p.message()).c_str());
		}
#endif
		d.disabled = true;
		return;
	}

	parse_state s;
	auto body = p.get_body();
	xml_parse({body.data(), body.size()}, std::bind(&find_control_url, _1, _2, std::ref(s)));
	if (s.control_url.empty())
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("could not find a port mapping interface in response from: %s"
			, d.url.c_str());
#endif
		d.disabled = true;
		return;
	}
	d.service_namespace = s.service_type;

	TORRENT_ASSERT(!d.service_namespace.empty());

	if (!s.model.empty()) m_model = s.model;

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
		std::tie(protocol, auth, d.hostname, d.port, d.path)
			= parse_url_components(d.url, ec);
		if (d.port == -1) d.port = protocol == "http" ? 80 : 443;
		d.control_url = protocol + "://" + d.hostname + ":"
			+ to_string(d.port).data() + s.control_url;
	}

#ifndef TORRENT_DISABLE_LOGGING
	if (should_log())
	{
		log("found control URL: %s namespace %s "
			"urlbase: %s in response from %s"
			, d.control_url.c_str(), d.service_namespace.c_str()
			, s.url_base.c_str(), d.url.c_str());
	}
#endif

	std::tie(protocol, auth, d.hostname, d.port, d.path)
		= parse_url_components(d.control_url, ec);
	if (d.port == -1) d.port = protocol == "http" ? 80 : 443;

	if (ec)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("failed to parse URL '%s': %s"
				, d.control_url.c_str(), convert_from_native(ec.message()).c_str());
		}
#endif
		d.disabled = true;
		return;
	}

	d.upnp_connection = std::make_shared<http_connection>(m_io_service
		, m_resolver
		, std::bind(&upnp::on_upnp_get_ip_address_response, self(), _1, _2
		, std::ref(d), _5), true, default_max_bottled_buffer_size
		, std::bind(&upnp::get_ip_address, self(), std::ref(d)));
	d.upnp_connection->start(d.hostname, d.port
		, seconds(10), 1);
}

void upnp::get_ip_address(rootdevice& d)
{
	TORRENT_ASSERT(is_single_thread());

	TORRENT_ASSERT(d.magic == 1337);

	if (!d.upnp_connection)
	{
		TORRENT_ASSERT(d.disabled);
#ifndef TORRENT_DISABLE_LOGGING
		log("getting external IP address");
#endif
		return;
	}

	char const* soap_action = "GetExternalIPAddress";

	char soap[2048];
	std::snprintf(soap, sizeof(soap), "<?xml version=\"1.0\"?>\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body><u:%s xmlns:u=\"%s\">"
		"</u:%s></s:Body></s:Envelope>"
		, soap_action, d.service_namespace.c_str()
		, soap_action);

	post(d, soap, soap_action);
}

void upnp::disable(error_code const& ec)
{
	TORRENT_ASSERT(is_single_thread());
	m_disabled = true;

	// kill all mappings
	for (std::vector<global_mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol == portmap_protocol::none) continue;
		portmap_protocol const proto = i->protocol;
		i->protocol = portmap_protocol::none;
		m_callback.on_port_mapping(int(i - m_mappings.begin()), address(), 0, proto, ec
			, aux::portmap_transport::upnp);
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

void find_error_code(int const type, string_view string, error_code_parse_state& state)
{
	if (state.exit) return;
	if (type == xml_start_tag && string == "errorCode")
	{
		state.in_error_code = true;
	}
	else if (type == xml_string && state.in_error_code)
	{
		std::string error_code_str(string.begin(), string.end());
		state.error_code = std::atoi(error_code_str.c_str());
		state.exit = true;
	}
}

void find_ip_address(int const type, string_view string, ip_address_parse_state& state)
{
	find_error_code(type, string, state);
	if (state.exit) return;

	if (type == xml_start_tag && string == "NewExternalIPAddress")
	{
		state.in_ip_address = true;
	}
	else if (type == xml_string && state.in_ip_address)
	{
		state.ip_address.assign(string.begin(), string.end());
		state.exit = true;
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

struct upnp_error_category : boost::system::error_category
{
	const char* name() const BOOST_SYSTEM_NOEXCEPT override
	{
		return "UPnP error";
	}

	std::string message(int ev) const BOOST_SYSTEM_NOEXCEPT override
	{
		int num_errors = sizeof(error_codes) / sizeof(error_codes[0]);
		error_code_t* end = error_codes + num_errors;
		error_code_t tmp = {ev, nullptr};
		error_code_t* e = std::lower_bound(error_codes, end, tmp
			, [] (error_code_t const& lhs, error_code_t const& rhs)
			{ return lhs.code < rhs.code; });
		if (e != end && e->code == ev)
		{
			return e->msg;
		}
		char msg[500];
		std::snprintf(msg, sizeof(msg), "unknown UPnP error (%d)", ev);
		return msg;
	}

	boost::system::error_condition default_error_condition(
		int ev) const BOOST_SYSTEM_NOEXCEPT override
	{
		return boost::system::error_condition(ev, *this);
	}
};

boost::system::error_category& upnp_category()
{
	static upnp_error_category cat;
	return cat;
}

void upnp::on_upnp_get_ip_address_response(error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d
	, http_connection& c)
{
	TORRENT_ASSERT(is_single_thread());
	std::shared_ptr<upnp> me(self());

	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection && d.upnp_connection.get() == &c)
	{
		d.upnp_connection->close();
		d.upnp_connection.reset();
	}

	if (m_closing) return;

	if (e && e != boost::asio::error::eof)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("error while getting external IP address: %s"
				, convert_from_native(e.message()).c_str());
		}
#endif
		if (num_mappings() > 0) update_map(d, 0);
		return;
	}

	if (!p.header_finished())
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("error while getting external IP address: incomplete http message");
#endif
		if (num_mappings() > 0) update_map(d, 0);
		return;
	}

	if (p.status_code() != 200)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("error while getting external IP address: %s"
				, convert_from_native(p.message()).c_str());
		}
#endif
		if (num_mappings() > 0) update_map(d, 0);
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

	span<char const> body = p.get_body();
#ifndef TORRENT_DISABLE_LOGGING
	if (should_log())
	{
		log("get external IP address response: %s"
			, std::string(body.data(), body.size()).c_str());
	}
#endif

	ip_address_parse_state s;
	xml_parse({body.data(), body.size()}, std::bind(&find_ip_address, _1, _2, std::ref(s)));
#ifndef TORRENT_DISABLE_LOGGING
	if (s.error_code != -1)
	{
		log("error while getting external IP address, code: %u", s.error_code);
	}
#endif

	if (!s.ip_address.empty())
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("got router external IP address %s", s.ip_address.c_str());
#endif
		d.external_ip = address::from_string(s.ip_address.c_str(), ignore_error);
	}
	else
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("failed to find external IP address in response");
#endif
	}

	if (num_mappings() > 0) update_map(d, 0);
}

void upnp::on_upnp_map_response(error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d, int const mapping
	, http_connection& c)
{
	TORRENT_ASSERT(is_single_thread());
	std::shared_ptr<upnp> me(self());

	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection && d.upnp_connection.get() == &c)
	{
		d.upnp_connection->close();
		d.upnp_connection.reset();
	}

	if (e && e != boost::asio::error::eof)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("error while adding port map: %s"
				, convert_from_native(e.message()).c_str());
		}
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
#ifndef TORRENT_DISABLE_LOGGING
		log("error while adding port map: incomplete http message");
#endif
		next(d, mapping);
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
#ifndef TORRENT_DISABLE_LOGGING
		log("error while adding port map: invalid content-type, \"%s\". "
			"Expected text/xml or application/soap+xml", ct.c_str());
#endif
		next(d, mapping);
		return;
	}

	// We don't want to ignore responses with return codes other than 200
	// since those might contain valid UPnP error codes

	error_code_parse_state s;
	span<char const> body = p.get_body();
	xml_parse({body.data(), body.size()}, std::bind(&find_error_code, _1, _2, std::ref(s)));

	if (s.error_code != -1)
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("error while adding port map, code: %u"
			, s.error_code);
#endif
	}

	mapping_t& m = d.mapping[mapping];

	if (s.error_code == 725)
	{
		// only permanent leases supported
		d.lease_duration = 0;
		m.act = mapping_t::action::add;
		++m.failcount;
		update_map(d, mapping);
		return;
	}
	else if (s.error_code == 727)
	{
		return_error(mapping, s.error_code);
	}
	else if ((s.error_code == 718 || s.error_code == 501) && m.failcount < 4)
	{
		// some routers return 501 action failed, instead of 716
		// The external port conflicts with another mapping
		// pick a random port
		m.external_port = 40000 + int(random(10000));
		m.act = mapping_t::action::add;
		++m.failcount;
		update_map(d, mapping);
		return;
	}
	else if (s.error_code != -1)
	{
		return_error(mapping, s.error_code);
	}

#ifndef TORRENT_DISABLE_LOGGING
	if (should_log())
	{
		log("map response: %s"
			, std::string(body.data(), body.size()).c_str());
	}
#endif

	if (s.error_code == -1)
	{
		m_callback.on_port_mapping(mapping, d.external_ip, m.external_port, m.protocol, error_code()
			, aux::portmap_transport::upnp);
		if (d.lease_duration > 0)
		{
			m.expires = aux::time_now()
				+ seconds(int(d.lease_duration * 0.75f));
			time_point next_expire = m_refresh_timer.expires_at();
			if (next_expire < aux::time_now()
				|| next_expire > m.expires)
			{
				ADD_OUTSTANDING_ASYNC("upnp::on_expire");
				error_code ec;
				m_refresh_timer.expires_at(m.expires, ec);
				m_refresh_timer.async_wait(std::bind(&upnp::on_expire, self(), _1));
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

void upnp::return_error(int const mapping, int const code)
{
	TORRENT_ASSERT(is_single_thread());
	int num_errors = sizeof(error_codes) / sizeof(error_codes[0]);
	error_code_t* end = error_codes + num_errors;
	error_code_t tmp = {code, nullptr};
	error_code_t* e = std::lower_bound(error_codes, end, tmp
		, [] (error_code_t const& lhs, error_code_t const& rhs)
		{ return lhs.code < rhs.code; });

	std::string error_string = "UPnP mapping error ";
	error_string += to_string(code).data();
	if (e != end && e->code == code)
	{
		error_string += ": ";
		error_string += e->msg;
	}
	portmap_protocol const proto = m_mappings[mapping].protocol;
	m_callback.on_port_mapping(mapping, address(), 0, proto, error_code(code, upnp_category())
		, aux::portmap_transport::upnp);
}

void upnp::on_upnp_unmap_response(error_code const& e
	, libtorrent::http_parser const& p, rootdevice& d, int const mapping
	, http_connection& c)
{
	TORRENT_ASSERT(is_single_thread());
	std::shared_ptr<upnp> me(self());

	TORRENT_ASSERT(d.magic == 1337);
	if (d.upnp_connection && d.upnp_connection.get() == &c)
	{
		d.upnp_connection->close();
		d.upnp_connection.reset();
	}

	if (e && e != boost::asio::error::eof)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("error while deleting portmap: %s"
				, convert_from_native(e.message()).c_str());
		}
#endif
	}
	else if (!p.header_finished())
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("error while deleting portmap: incomplete http message");
#endif
	}
	else if (p.status_code() != 200)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("error while deleting portmap: %s"
				, convert_from_native(p.message()).c_str());
		}
#endif
	}
	else
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			span<char const> body = p.get_body();
			log("unmap response: %s"
				, std::string(body.data(), body.size()).c_str());
		}
#endif
	}

	error_code_parse_state s;
	if (p.header_finished())
	{
		span<char const> body = p.get_body();
		xml_parse({body.data(), body.size()}, std::bind(&find_error_code, _1, _2, std::ref(s)));
	}

	portmap_protocol const proto = m_mappings[mapping].protocol;

	m_callback.on_port_mapping(mapping, address(), 0, proto, p.status_code() != 200
		? error_code(p.status_code(), http_category())
		: error_code(s.error_code, upnp_category())
		, aux::portmap_transport::upnp);

	d.mapping[mapping].protocol = portmap_protocol::none;

	next(d, mapping);
}

void upnp::on_expire(error_code const& ec)
{
	TORRENT_ASSERT(is_single_thread());
	COMPLETE_ASYNC("upnp::on_expire");
	if (ec) return;

	time_point now = aux::time_now();
	time_point next_expire = max_time();

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
		ADD_OUTSTANDING_ASYNC("upnp::on_expire");
		error_code e;
		m_refresh_timer.expires_at(next_expire, e);
		m_refresh_timer.async_wait(std::bind(&upnp::on_expire, self(), _1));
	}
}

void upnp::close()
{
	TORRENT_ASSERT(is_single_thread());

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
			, end2(d.mapping.end()); j != end2; ++j)
		{
			if (j->protocol == portmap_protocol::none) continue;
			if (j->act == mapping_t::action::add)
			{
				j->act = mapping_t::action::none;
				continue;
			}
			j->act = mapping_t::action::del;
			m_mappings[int(j - d.mapping.begin())].protocol = portmap_protocol::none;
		}
		if (num_mappings() > 0) update_map(d, 0);
	}
}

}}
