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
#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#if defined TORRENT_OS2
#include <pthread.h>
#endif

#include <boost/asio/ip/host_name.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.
#include <cstdarg>
#include <functional>
#include <cstring> // for memcpy

#include "libtorrent/natpmp.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/broadcast_socket.hpp" // for is_local
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

namespace libtorrent {

struct pcp_error_category final : boost::system::error_category
{
	const char* name() const BOOST_SYSTEM_NOEXCEPT override
	{ return "pcp error"; }
	std::string message(int ev) const override
	{
		static char const* msgs[] =
		{
			"success",
			"unsupported version",
			"not authorized",
			"malformed request",
			"unsupported opcode",
			"unsupported option",
			"malformed option",
			"network failure",
			"no resources",
			"unsupported protocol",
			"user exceeded quota",
			"cannot provide external",
			"address mismatch",
			"excessive remote peers",
		};
		if (ev < 0 || ev >= int(sizeof(msgs)/sizeof(msgs[0])))
			return "Unknown error";
		return msgs[ev];
	}
	boost::system::error_condition default_error_condition(
		int ev) const BOOST_SYSTEM_NOEXCEPT override
	{ return boost::system::error_condition(ev, *this); }
};

boost::system::error_category& pcp_category()
{
	static pcp_error_category pcp_category;
	return pcp_category;
}

namespace errors
{
	// hidden
	boost::system::error_code make_error_code(pcp_errors e)
	{
		return boost::system::error_code(e, pcp_category());
	}
}

error_code natpmp::from_result_code(int const version, int result)
{
	if (version == version_natpmp)
	{
		// a few nat-pmp result codes map to different codes
		// in pcp
		switch (result)
		{
		case 3:result = 7; break;
		case 4:result = 8; break;
		case 5:result = 4; break;
		}
	}
	return errors::pcp_errors(result);
}

char const* natpmp::version_to_string(protocol_version version)
{
	return version == version_natpmp ? "NAT-PMP" : "PCP";
}

using namespace aux;
using namespace std::placeholders;

natpmp::natpmp(io_service& ios
	, aux::portmap_callback& cb)
	: m_callback(cb)
	, m_socket(ios)
	, m_send_timer(ios)
	, m_refresh_timer(ios)
{
	// unfortunately async operations rely on the storage
	// for this array not to be reallocated, by passing
	// around pointers to its elements. so reserve size for now
	m_mappings.reserve(10);
}

void natpmp::start(ip_interface const& ip)
{
	TORRENT_ASSERT(is_single_thread());

	// assume servers support PCP and fall back to NAT-PMP
	// if necessary
	m_version = version_pcp;

	address const& local_address = ip.interface_address;

	error_code ec;
	auto const routes = enum_routes(get_io_service(m_socket), ec);
	if (ec)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("failed to enumerate routes: %s"
				, convert_from_native(ec.message()).c_str());
		}
#endif
		disable(ec);
	}

	auto const route = get_gateway(ip, routes);

	if (!route)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("failed to find default route for \"%s\" %s: %s"
				, ip.name, local_address.to_string().c_str()
				, convert_from_native(ec.message()).c_str());
		}
#endif
		disable(ec);
		return;
	}

	m_disabled = false;

	udp::endpoint const nat_endpoint(*route, 5351);
	if (nat_endpoint == m_nat_endpoint) return;
	m_nat_endpoint = nat_endpoint;

#ifndef TORRENT_DISABLE_LOGGING
	if (should_log())
	{
		log("found gateway at: %s"
			, print_address(m_nat_endpoint.address()).c_str());
	}
#endif

	m_socket.open(local_address.is_v4() ? udp::v4() : udp::v6(), ec);
	if (ec)
	{
		disable(ec);
		return;
	}
	m_socket.bind({local_address, 0}, ec);
	if (ec)
	{
		disable(ec);
		return;
	}

	ADD_OUTSTANDING_ASYNC("natpmp::on_reply");
	m_socket.async_receive_from(boost::asio::buffer(&m_response_buffer[0]
		, sizeof(m_response_buffer))
		, m_remote, std::bind(&natpmp::on_reply, self(), _1, _2));
	if (m_version == version_natpmp)
		send_get_ip_address_request();

	for (auto i = m_mappings.begin(), end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol == portmap_protocol::none
			|| i->act != portmap_action::none)
			continue;
		i->act = portmap_action::add;
		update_mapping(port_mapping_t(int(i - m_mappings.begin())));
	}
}

void natpmp::send_get_ip_address_request()
{
	TORRENT_ASSERT(is_single_thread());
	using namespace libtorrent::detail;

	// this opcode only exists in NAT-PMP
	// PCP routers report the external IP in the response to a MAP operation
	TORRENT_ASSERT(m_version == version_natpmp);
	if (m_version != version_natpmp)
		return;

	char buf[2];
	char* out = buf;
	write_uint8(version_natpmp, out);
	write_uint8(0, out); // public IP address request opcode
#ifndef TORRENT_DISABLE_LOGGING
	log("==> get public IP address");
#endif

	error_code ec;
	m_socket.send_to(boost::asio::buffer(buf, sizeof(buf)), m_nat_endpoint, 0, ec);
}

bool natpmp::get_mapping(port_mapping_t const index, int& local_port
	, int& external_port, portmap_protocol& protocol) const
{
	TORRENT_ASSERT(is_single_thread());

	TORRENT_ASSERT(index < m_mappings.end_index() && index >= port_mapping_t{});
	if (index >= m_mappings.end_index() || index < port_mapping_t{}) return false;
	mapping_t const& m = m_mappings[index];
	if (m.protocol == portmap_protocol::none) return false;
	local_port = m.local_port;
	external_port = m.external_port;
	protocol = m.protocol;
	return true;
}

#ifndef TORRENT_DISABLE_LOGGING
bool natpmp::should_log() const
{
	return m_callback.should_log_portmap(portmap_transport::natpmp);
}

void natpmp::mapping_log(char const* op, mapping_t const& m) const
{
	if (should_log())
	{
		log("%s-mapping: proto: %s port: %d local-port: %d action: %s ttl: %" PRId64
			, op
			, m.protocol == portmap_protocol::none
			? "none" : to_string(m.protocol)
			, m.external_port
			, m.local_port
			, to_string(m.act)
			, (m.expires.time_since_epoch() != seconds(0))
				? total_seconds(m.expires - aux::time_now())
				: std::int64_t(0));
	}
}

TORRENT_FORMAT(2, 3)
void natpmp::log(char const* fmt, ...) const
{
	TORRENT_ASSERT(is_single_thread());
	if (!should_log()) return;
	char msg[200];
	va_list v;
	va_start(v, fmt);
	std::vsnprintf(msg, sizeof(msg), fmt, v);
	va_end(v);
	m_callback.log_portmap(portmap_transport::natpmp, msg);
}
#endif

void natpmp::disable(error_code const& ec)
{
	TORRENT_ASSERT(is_single_thread());
	m_disabled = true;

	for (auto i = m_mappings.begin(), end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol == portmap_protocol::none) continue;
		portmap_protocol const proto = i->protocol;
		i->protocol = portmap_protocol::none;
		port_mapping_t const index(static_cast<int>(i - m_mappings.begin()));
		m_callback.on_port_mapping(index, address(), 0, proto, ec
			, portmap_transport::natpmp);
	}
	close_impl();
}

void natpmp::delete_mapping(port_mapping_t const index)
{
	TORRENT_ASSERT(is_single_thread());

	TORRENT_ASSERT(index < m_mappings.end_index() && index >= port_mapping_t{});
	if (index >= m_mappings.end_index() || index < port_mapping_t{}) return;
	mapping_t& m = m_mappings[index];

	if (m.protocol == portmap_protocol::none) return;
	if (!m.map_sent)
	{
		m.act = portmap_action::none;
		m.protocol = portmap_protocol::none;
		return;
	}

	m.act = portmap_action::del;
	update_mapping(index);
}

port_mapping_t natpmp::add_mapping(portmap_protocol const p, int const external_port
	, tcp::endpoint const local_ep)
{
	TORRENT_ASSERT(is_single_thread());

	if (m_disabled) return port_mapping_t{-1};

	auto i = std::find_if(m_mappings.begin()
		, m_mappings.end(), [] (mapping_t const& m) { return m.protocol == portmap_protocol::none; });
	if (i == m_mappings.end())
	{
		m_mappings.push_back(mapping_t());
		i = m_mappings.end() - 1;
	}
	aux::random_bytes(i->nonce);
	i->protocol = p;
	i->external_port = external_port;
	i->local_port = local_ep.port();
	i->act = portmap_action::add;

	port_mapping_t const mapping_index(static_cast<int>(i - m_mappings.begin()));
#ifndef TORRENT_DISABLE_LOGGING
	mapping_log("add",*i);
#endif

	update_mapping(mapping_index);
	return port_mapping_t{mapping_index};
}

void natpmp::try_next_mapping(port_mapping_t const i)
{
	TORRENT_ASSERT(is_single_thread());
	if (i < prev(m_mappings.end_index()))
	{
		update_mapping(next(i));
		return;
	}

	auto const m = std::find_if(
		m_mappings.begin(), m_mappings.end()
		, [] (mapping_t const& ma) { return ma.act != portmap_action::none
			&& ma.protocol != portmap_protocol::none; });

	if (m == m_mappings.end())
	{
		if (m_abort)
		{
			error_code ec;
			m_send_timer.cancel(ec);
			m_socket.close(ec);
		}
		return;
	}

	update_mapping(port_mapping_t(static_cast<int>(m - m_mappings.begin())));
}

void natpmp::update_mapping(port_mapping_t const i)
{
	TORRENT_ASSERT(is_single_thread());
	if (i == m_mappings.end_index())
	{
		if (m_abort)
		{
			error_code ec;
			m_send_timer.cancel(ec);
			m_socket.close(ec);
		}
		return;
	}

	mapping_t const& m = m_mappings[i];

#ifndef TORRENT_DISABLE_LOGGING
	mapping_log("update", m);
#endif

	if (m.act == portmap_action::none
		|| m.protocol == portmap_protocol::none)
	{
		try_next_mapping(i);
		return;
	}

	if (m_currently_mapping == port_mapping_t{-1})
	{
		// the socket is not currently in use
		// send out a mapping request
		m_retry_count = 0;
		send_map_request(i);
	}
}

void natpmp::send_map_request(port_mapping_t const i)
{
	TORRENT_ASSERT(is_single_thread());
	using namespace libtorrent::detail;

	TORRENT_ASSERT(m_currently_mapping == port_mapping_t{-1}
		|| m_currently_mapping == i);
	m_currently_mapping = i;
	mapping_t& m = m_mappings[i];
	TORRENT_ASSERT(m.act != portmap_action::none);
	char buf[60];
	char* out = buf;
	int ttl = m.act == portmap_action::add ? 3600 : 0;
	if (m_version == version_natpmp)
	{
		write_uint8(m_version, out);
		write_uint8(m.protocol == portmap_protocol::udp ? 1 : 2, out); // map "protocol"
		write_uint16(0, out); // reserved
		write_uint16(m.local_port, out); // private port
		write_uint16(m.external_port, out); // requested public port
		write_uint32(ttl, out); // port mapping lifetime
	}
	else if (m_version == version_pcp)
	{
		// PCP requires the use of IPv6 addresses even for IPv4 messages
		write_uint8(m_version, out);
		write_uint8(opcode_map, out);
		write_uint16(0, out); // reserved
		write_uint32(ttl, out);
		address const local_addr = m_socket.local_endpoint().address();
		auto const local_bytes = local_addr.is_v4()
			? address_v6::v4_mapped(local_addr.to_v4()).to_bytes()
			: local_addr.to_v6().to_bytes();
		out = std::copy(local_bytes.begin(), local_bytes.end(), out);
		out = std::copy(m.nonce.begin(), m.nonce.end(), out);
		// translate portmap_protocol to an IANA protocol number
		int const protocol =
			(m.protocol == portmap_protocol::tcp) ? 6
			: (m.protocol == portmap_protocol::udp) ? 17
			: 0;
		write_int8(protocol, out);
		write_uint8(0, out); // reserved
		write_uint16(0, out); // reserved
		write_uint16(m.local_port, out);
		write_uint16(m.external_port, out);
		address_v6 external_addr;
		if (!m.external_address.is_unspecified())
		{
			external_addr = m.external_address.is_v4()
				? address_v6::v4_mapped(m.external_address.to_v4())
				: m.external_address.to_v6();
		}
		else if (is_local(local_addr))
		{
			external_addr = local_addr.is_v4()
				? address_v6::v4_mapped(address_v4())
				: address_v6();
		}
		else if (local_addr.is_v4())
		{
			external_addr = address_v6::v4_mapped(local_addr.to_v4());
		}
		else
		{
			external_addr = local_addr.to_v6();
		}
		write_address(external_addr, out);
	}
	else
	{
		TORRENT_ASSERT_FAIL();
	}

#ifndef TORRENT_DISABLE_LOGGING
	if (should_log())
	{
		log("==> port map [ mapping: %d action: %s"
			" transport: %s proto: %s local: %u external: %u ttl: %u ]"
			, static_cast<int>(i), to_string(m.act)
			, version_to_string(m_version)
			, to_string(m.protocol)
			, m.local_port, m.external_port, ttl);
	}
#endif

	error_code ec;
	m_socket.send_to(boost::asio::buffer(buf, std::size_t(out - buf)), m_nat_endpoint, 0, ec);
	m.map_sent = true;
	m.outstanding_request = true;
	if (m_abort)
	{
		// when we're shutting down, ignore the
		// responses and just remove all mappings
		// immediately
		m_currently_mapping = port_mapping_t{-1};
		m.act = portmap_action::none;
		try_next_mapping(i);
	}
	else
	{
		ADD_OUTSTANDING_ASYNC("natpmp::resend_request");
		// linear back-off instead of exponential
		++m_retry_count;
		m_send_timer.expires_from_now(milliseconds(250 * m_retry_count), ec);
		m_send_timer.async_wait(std::bind(&natpmp::on_resend_request, self(), i, _1));
	}
}

void natpmp::on_resend_request(port_mapping_t const i, error_code const& e)
{
	TORRENT_ASSERT(is_single_thread());
	COMPLETE_ASYNC("natpmp::resend_request");
	if (e) return;
	resend_request(i);
}

void natpmp::resend_request(port_mapping_t const i)
{
	if (m_currently_mapping != i) return;

	// if we're shutting down, don't retry, just move on
	// to the next mapping
	if (m_retry_count >= 9 || m_abort)
	{
		m_currently_mapping = port_mapping_t{-1};
		m_mappings[i].act = portmap_action::none;
		// try again in two hours
		m_mappings[i].expires = aux::time_now() + hours(2);
		try_next_mapping(i);
		return;
	}
	send_map_request(i);
}

void natpmp::on_reply(error_code const& e
	, std::size_t const bytes_transferred)
{
	TORRENT_ASSERT(is_single_thread());

	COMPLETE_ASYNC("natpmp::on_reply");

	using namespace libtorrent::detail;
	if (e)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("error on receiving reply: %s"
				, convert_from_native(e.message()).c_str());
		}
#endif
		return;
	}

	if (m_abort) return;

	ADD_OUTSTANDING_ASYNC("natpmp::on_reply");
	// make a copy of the response packet buffer
	// to avoid overwriting it in the next receive call
	std::array<char, sizeof(m_response_buffer)> msg_buf;
	std::memcpy(msg_buf.data(), m_response_buffer, bytes_transferred);

	m_socket.async_receive_from(boost::asio::buffer(&m_response_buffer[0]
		, sizeof(m_response_buffer))
		, m_remote, std::bind(&natpmp::on_reply, self(), _1, _2));

	if (m_remote != m_nat_endpoint)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("received packet from wrong IP: %s"
				, print_endpoint(m_remote).c_str());
		}
#endif
		return;
	}

	error_code ec;
	m_send_timer.cancel(ec);

	if (bytes_transferred < 4)
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("received packet of invalid size: %d", int(bytes_transferred));
#endif
		return;
	}

	char* in = msg_buf.data();
	int const version = read_uint8(in);

	if (version != version_natpmp && version != version_pcp)
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("unexpected version: %u", version);
#endif
		return;
	}

	int cmd = read_uint8(in);
	if (version == version_pcp)
	{
		cmd &= 0x7f;
	}
	int result;
	if (version == version_pcp)
	{
		++in; // reserved
		result = read_uint8(in);
	}
	else
	{
		result = read_uint16(in);
	}

	if (result == errors::pcp_unsupp_version)
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("unsupported version");
#endif
		if (m_version == version_pcp && !is_v6(m_socket.local_endpoint()))
		{
			m_version = version_natpmp;
			resend_request(m_currently_mapping);
			send_get_ip_address_request();
		}
		return;
	}

	if ((version == version_natpmp && bytes_transferred < 12)
		|| (version == version_pcp && bytes_transferred < 24))
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("received packet of invalid size: %d", int(bytes_transferred));
#endif
		return;
	}

	int lifetime = 0;
	if (version == version_pcp)
	{
		lifetime = aux::numeric_cast<int>(read_uint32(in));
	}
	int const time = aux::numeric_cast<int>(read_uint32(in));
	if (version == version_pcp) in += 12; // reserved
	TORRENT_UNUSED(time);

	if (version == version_natpmp && cmd == 128)
	{
		// public IP request response
		m_external_ip = read_v4_address(in);

#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("<== public IP address [ %s ]", print_address(m_external_ip).c_str());
		}
#endif
		return;

	}

	if ((version == version_natpmp && bytes_transferred != 16)
		|| (version == version_pcp && bytes_transferred != 60))
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("received packet of invalid size: %d", int(bytes_transferred));
#endif
		return;
	}

	std::array<char, 12> nonce;
	portmap_protocol protocol = portmap_protocol::none;
	if (version == version_pcp)
	{
		std::memcpy(nonce.data(), in, nonce.size());
		in += nonce.size();
		int p = read_uint8(in);
		protocol = p == 6 ? portmap_protocol::tcp
			: portmap_protocol::udp;
		in += 3; // reserved
	}
	int const private_port = read_uint16(in);
	int const public_port = read_uint16(in);
	if (version == version_natpmp)
		lifetime = aux::numeric_cast<int>(read_uint32(in));
	address external_addr;
	if (version == version_pcp)
	{
		external_addr = read_v6_address(in);
		if (external_addr.to_v6().is_v4_mapped())
			external_addr = external_addr.to_v6().to_v4();
	}

	if (version == version_natpmp)
	{
		protocol = (cmd - 128 == 1)
			? portmap_protocol::udp
			: portmap_protocol::tcp;
	}

#ifndef TORRENT_DISABLE_LOGGING
	char msg[200];
	int const num_chars = std::snprintf(msg, sizeof(msg), "<== port map ["
		" transport: %s protocol: %s local: %d external: %d ttl: %d ]"
		, version_to_string(protocol_version(version))
		, (protocol == portmap_protocol::udp ? "udp" : "tcp")
		, private_port, public_port, lifetime);
#endif

	mapping_t* m = nullptr;
	port_mapping_t index{-1};
	for (auto i = m_mappings.begin(), end(m_mappings.end()); i != end; ++i)
	{
		if (private_port != i->local_port) continue;
		if (protocol != i->protocol) continue;
		if (!i->map_sent) continue;
		if (!i->outstanding_request) continue;
		if (version == version_pcp && nonce != i->nonce) continue;
		m = &*i;
		index = port_mapping_t(static_cast<int>(i - m_mappings.begin()));
		break;
	}

	if (m == nullptr)
	{
#ifndef TORRENT_DISABLE_LOGGING
		snprintf(msg + num_chars, sizeof(msg) - aux::numeric_cast<std::size_t>(num_chars), " not found in map table");
		log("%s", msg);
#endif
		return;
	}
	m->outstanding_request = false;

#ifndef TORRENT_DISABLE_LOGGING
	log("%s", msg);
#endif

	if (public_port == 0 || lifetime == 0)
	{
		// this means the mapping was
		// successfully closed
		m->protocol = portmap_protocol::none;
	}
	else
	{
		m->expires = aux::time_now() + seconds(lifetime * 3 / 4);
		m->external_port = public_port;
		if (!external_addr.is_unspecified())
			m->external_address = external_addr;
	}

	if (result != 0)
	{
		m->expires = aux::time_now() + hours(2);
		portmap_protocol const proto = m->protocol;
		m_callback.on_port_mapping(port_mapping_t{index}, address(), 0, proto
			, from_result_code(version, result), portmap_transport::natpmp);
	}
	else if (m->act == portmap_action::add)
	{
		portmap_protocol const proto = m->protocol;
		address const ext_ip = version == version_pcp ? m->external_address : m_external_ip;
		m_callback.on_port_mapping(port_mapping_t{index}, ext_ip, m->external_port, proto
			, errors::pcp_success, portmap_transport::natpmp);
	}

	m_currently_mapping = port_mapping_t{-1};
	m->act = portmap_action::none;
	m_send_timer.cancel(ec);
	update_expiration_timer();
	try_next_mapping(index);
}

void natpmp::update_expiration_timer()
{
	TORRENT_ASSERT(is_single_thread());
	if (m_abort) return;

	time_point const now = aux::time_now() + milliseconds(100);
	time_point min_expire = now + seconds(3600);
	port_mapping_t min_index{-1};
	for (auto i = m_mappings.begin(), end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol == portmap_protocol::none
			|| i->act != portmap_action::none) continue;
		port_mapping_t const index(static_cast<int>(i - m_mappings.begin()));
		if (i->expires < now)
		{
#ifndef TORRENT_DISABLE_LOGGING
			log("mapping %u expired", static_cast<int>(index));
#endif
			i->act = portmap_action::add;
			if (m_next_refresh == index) m_next_refresh = port_mapping_t{-1};
			update_mapping(index);
		}
		else if (i->expires < min_expire)
		{
			min_expire = i->expires;
			min_index = index;
		}
	}

	// this is already the mapping we're waiting for
	if (m_next_refresh == min_index) return;

	if (min_index >= port_mapping_t{})
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("next expiration [ idx: %d ttl: %" PRId64 " ]"
			, static_cast<int>(min_index), total_seconds(min_expire - aux::time_now()));
#endif
		error_code ec;
		if (m_next_refresh >= port_mapping_t{}) m_refresh_timer.cancel(ec);

		ADD_OUTSTANDING_ASYNC("natpmp::mapping_expired");
		m_refresh_timer.expires_from_now(min_expire - now, ec);
		m_refresh_timer.async_wait(std::bind(&natpmp::mapping_expired, self(), _1, min_index));
		m_next_refresh = min_index;
	}
}

void natpmp::mapping_expired(error_code const& e, port_mapping_t const i)
{
	TORRENT_ASSERT(is_single_thread());
	COMPLETE_ASYNC("natpmp::mapping_expired");
	if (e) return;
#ifndef TORRENT_DISABLE_LOGGING
	log("mapping %u expired", static_cast<int>(i));
#endif
	m_mappings[i].act = portmap_action::add;
	if (m_next_refresh == i) m_next_refresh = port_mapping_t{-1};
	update_mapping(i);
}

void natpmp::close()
{
	TORRENT_ASSERT(is_single_thread());
	close_impl();
}

void natpmp::close_impl()
{
	TORRENT_ASSERT(is_single_thread());
	m_abort = true;
#ifndef TORRENT_DISABLE_LOGGING
	log("closing");
#endif
	if (m_disabled) return;
	for (auto& m : m_mappings)
	{
		if (m.protocol == portmap_protocol::none) continue;
		m.act = portmap_action::del;
	}
	error_code ec;
	m_refresh_timer.cancel(ec);
	m_currently_mapping = port_mapping_t{-1};
	update_mapping(port_mapping_t{});
}

} // namespace libtorrent
