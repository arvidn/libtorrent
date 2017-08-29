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

#include "libtorrent/natpmp.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"

namespace libtorrent {

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

void natpmp::start()
{
	TORRENT_ASSERT(is_single_thread());

	error_code ec;
	address gateway = get_default_gateway(m_socket.get_io_service(), ec);
	if (ec)
	{
#ifndef TORRENT_DISABLE_LOGGING
		if (should_log())
		{
			log("failed to find default route: %s"
				, convert_from_native(ec.message()).c_str());
		}
#endif
		disable(ec);
		return;
	}

	m_disabled = false;

	udp::endpoint nat_endpoint(gateway, 5351);
	if (nat_endpoint == m_nat_endpoint) return;
	m_nat_endpoint = nat_endpoint;

#ifndef TORRENT_DISABLE_LOGGING
	if (should_log())
	{
		log("found router at: %s"
			, print_address(m_nat_endpoint.address()).c_str());
	}
#endif

	m_socket.open(udp::v4(), ec);
	if (ec)
	{
		disable(ec);
		return;
	}
	m_socket.bind(udp::endpoint(address_v4::any(), 0), ec);
	if (ec)
	{
		disable(ec);
		return;
	}

	ADD_OUTSTANDING_ASYNC("natpmp::on_reply");
	m_socket.async_receive_from(boost::asio::buffer(&m_response_buffer[0]
		, sizeof(m_response_buffer))
		, m_remote, std::bind(&natpmp::on_reply, self(), _1, _2));
	send_get_ip_address_request();

	for (std::vector<mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol == portmap_protocol::none
			|| i->act != portmap_action::none)
			continue;
		i->act = portmap_action::add;
		update_mapping(int(i - m_mappings.begin()));
	}
}

void natpmp::send_get_ip_address_request()
{
	TORRENT_ASSERT(is_single_thread());
	using namespace libtorrent::detail;

	char buf[2];
	char* out = buf;
	write_uint8(0, out); // NAT-PMP version
	write_uint8(0, out); // public IP address request opcode
#ifndef TORRENT_DISABLE_LOGGING
	log("==> get public IP address");
#endif

	error_code ec;
	m_socket.send_to(boost::asio::buffer(buf, sizeof(buf)), m_nat_endpoint, 0, ec);
}

bool natpmp::get_mapping(int const index, int& local_port
	, int& external_port, portmap_protocol& protocol) const
{
	TORRENT_ASSERT(is_single_thread());

	TORRENT_ASSERT(index < int(m_mappings.size()) && index >= 0);
	if (index >= int(m_mappings.size()) || index < 0) return false;
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
			, total_seconds(m.expires - aux::time_now()));
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

	for (std::vector<mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol == portmap_protocol::none) continue;
		portmap_protocol const proto = i->protocol;
		i->protocol = portmap_protocol::none;
		int const index = int(i - m_mappings.begin());
		m_callback.on_port_mapping(index, address(), 0, proto, ec
			, portmap_transport::natpmp);
	}
	close_impl();
}

void natpmp::delete_mapping(int const index)
{
	TORRENT_ASSERT(is_single_thread());

	TORRENT_ASSERT(index < int(m_mappings.size()) && index >= 0);
	if (index >= int(m_mappings.size()) || index < 0) return;
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

int natpmp::add_mapping(portmap_protocol const p, int const external_port
	, tcp::endpoint const local_ep)
{
	TORRENT_ASSERT(is_single_thread());

	if (m_disabled) return -1;

	auto i = std::find_if(m_mappings.begin()
		, m_mappings.end(), [] (mapping_t const& m) { return m.protocol == portmap_protocol::none; });
	if (i == m_mappings.end())
	{
		m_mappings.push_back(mapping_t());
		i = m_mappings.end() - 1;
	}
	i->protocol = p;
	i->external_port = external_port;
	i->local_port = local_ep.port();
	i->act = portmap_action::add;

	int const mapping_index = int(i - m_mappings.begin());
#ifndef TORRENT_DISABLE_LOGGING
	mapping_log("add",*i);
#endif

	update_mapping(mapping_index);
	return mapping_index;
}

void natpmp::try_next_mapping(int const i)
{
	TORRENT_ASSERT(is_single_thread());
	if (i < int(m_mappings.size()) - 1)
	{
		update_mapping(i + 1);
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

	update_mapping(int(m - m_mappings.begin()));
}

void natpmp::update_mapping(int const i)
{
	TORRENT_ASSERT(is_single_thread());
	if (i == int(m_mappings.size()))
	{
		if (m_abort)
		{
			error_code ec;
			m_send_timer.cancel(ec);
			m_socket.close(ec);
		}
		return;
	}

	natpmp::mapping_t const& m = m_mappings[i];

#ifndef TORRENT_DISABLE_LOGGING
	mapping_log("update", m);
#endif

	if (m.act == portmap_action::none
		|| m.protocol == portmap_protocol::none)
	{
		try_next_mapping(i);
		return;
	}

	if (m_currently_mapping == -1)
	{
		// the socket is not currently in use
		// send out a mapping request
		m_retry_count = 0;
		send_map_request(i);
	}
}

void natpmp::send_map_request(int const i)
{
	TORRENT_ASSERT(is_single_thread());
	using namespace libtorrent::detail;

	TORRENT_ASSERT(m_currently_mapping == -1
		|| m_currently_mapping == i);
	m_currently_mapping = i;
	mapping_t& m = m_mappings[i];
	TORRENT_ASSERT(m.act != portmap_action::none);
	char buf[12];
	char* out = buf;
	write_uint8(0, out); // NAT-PMP version
	write_uint8(m.protocol == portmap_protocol::udp ? 1 : 2, out); // map "protocol"
	write_uint16(0, out); // reserved
	write_uint16(m.local_port, out); // private port
	write_uint16(m.external_port, out); // requested public port
	int ttl = m.act == portmap_action::add ? 3600 : 0;
	write_uint32(ttl, out); // port mapping lifetime

#ifndef TORRENT_DISABLE_LOGGING
	if (should_log())
	{
		log("==> port map [ mapping: %d action: %s"
			" proto: %s local: %u external: %u ttl: %u ]"
			, i, to_string(m.act)
			, to_string(m.protocol)
			, m.local_port, m.external_port, ttl);
	}
#endif

	error_code ec;
	m_socket.send_to(boost::asio::buffer(buf, sizeof(buf)), m_nat_endpoint, 0, ec);
	m.map_sent = true;
	m.outstanding_request = true;
	if (m_abort)
	{
		// when we're shutting down, ignore the
		// responses and just remove all mappings
		// immediately
		m_currently_mapping = -1;
		m.act = portmap_action::none;
		try_next_mapping(i);
	}
	else
	{
		ADD_OUTSTANDING_ASYNC("natpmp::resend_request");
		// linear back-off instead of exponential
		++m_retry_count;
		m_send_timer.expires_from_now(milliseconds(250 * m_retry_count), ec);
		m_send_timer.async_wait(std::bind(&natpmp::resend_request, self(), i, _1));
	}
}

void natpmp::resend_request(int const i, error_code const& e)
{
	TORRENT_ASSERT(is_single_thread());
	COMPLETE_ASYNC("natpmp::resend_request");
	if (e) return;
	if (m_currently_mapping != i) return;

	// if we're shutting down, don't retry, just move on
	// to the next mapping
	if (m_retry_count >= 9 || m_abort)
	{
		m_currently_mapping = -1;
		m_mappings[i].act = portmap_action::none;
		// try again in two hours
		m_mappings[i].expires = aux::time_now() + hours(2);
		try_next_mapping(i);
		return;
	}
	send_map_request(i);
}

void natpmp::on_reply(error_code const& e
	, std::size_t bytes_transferred)
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

	ADD_OUTSTANDING_ASYNC("natpmp::on_reply");
	// make a copy of the response packet buffer
	// to avoid overwriting it in the next receive call
	char msg_buf[sizeof(m_response_buffer)];
	memcpy(msg_buf, m_response_buffer, bytes_transferred);

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

	if (bytes_transferred < 12)
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("received packet of invalid size: %d", int(bytes_transferred));
#endif
		return;
	}

	char* in = msg_buf;
	int version = read_uint8(in);
	int cmd = read_uint8(in);
	int result = read_uint16(in);
	int time = aux::numeric_cast<int>(read_uint32(in));
	TORRENT_UNUSED(version);
	TORRENT_UNUSED(time);

	if (cmd == 128)
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

	if (bytes_transferred != 16)
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("received packet of invalid size: %d", int(bytes_transferred));
#endif
		return;
	}

	int const private_port = read_uint16(in);
	int const public_port = read_uint16(in);
	int const lifetime = aux::numeric_cast<int>(read_uint32(in));

	portmap_protocol const protocol = (cmd - 128 == 1)
		? portmap_protocol::udp
		: portmap_protocol::tcp;

#ifndef TORRENT_DISABLE_LOGGING
	char msg[200];
	int num_chars = std::snprintf(msg, sizeof(msg), "<== port map ["
		" protocol: %s local: %u external: %u ttl: %u ]"
		, (cmd - 128 == 1 ? "udp" : "tcp")
		, private_port, public_port, lifetime);

	if (version != 0)
	{
		std::snprintf(msg + num_chars, sizeof(msg) - aux::numeric_cast<std::size_t>(num_chars), "unexpected version: %u"
			, version);
		log("%s", msg);
	}
#endif

	mapping_t* m = nullptr;
	int index = -1;
	for (auto i = m_mappings.begin(), end(m_mappings.end()); i != end; ++i)
	{
		if (private_port != i->local_port) continue;
		if (protocol != i->protocol) continue;
		if (!i->map_sent) continue;
		if (!i->outstanding_request) continue;
		m = &*i;
		index = int(i - m_mappings.begin());
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
		m->expires = aux::time_now() + seconds(int(lifetime * 0.7f));
		m->external_port = public_port;
	}

	if (result != 0)
	{
		// TODO: 3 it would be nice to have a separate NAT-PMP error category
		static errors::error_code_enum const errors[] =
		{
			errors::unsupported_protocol_version,
			errors::natpmp_not_authorized,
			errors::network_failure,
			errors::no_resources,
			errors::unsupported_opcode
		};
		errors::error_code_enum ev = errors::no_error;
		if (result >= 1 && result <= 5) ev = errors[result - 1];

		m->expires = aux::time_now() + hours(2);
		portmap_protocol const proto = m->protocol;
		m_callback.on_port_mapping(index, address(), 0, proto
			, ev, portmap_transport::natpmp);
	}
	else if (m->act == portmap_action::add)
	{
		portmap_protocol const proto = m->protocol;
		m_callback.on_port_mapping(index, m_external_ip, m->external_port, proto
			, errors::no_error, portmap_transport::natpmp);
	}

	if (m_abort) return;

	m_currently_mapping = -1;
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
	int min_index = -1;
	for (std::vector<mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol == portmap_protocol::none
			|| i->act != portmap_action::none) continue;
		int const index = int(i - m_mappings.begin());
		if (i->expires < now)
		{
#ifndef TORRENT_DISABLE_LOGGING
			log("mapping %u expired", index);
#endif
			i->act = portmap_action::add;
			if (m_next_refresh == index) m_next_refresh = -1;
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

	if (min_index >= 0)
	{
#ifndef TORRENT_DISABLE_LOGGING
		log("next expiration [ idx: %d ttl: %" PRId64 " ]"
			, min_index, total_seconds(min_expire - aux::time_now()));
#endif
		error_code ec;
		if (m_next_refresh >= 0) m_refresh_timer.cancel(ec);

		ADD_OUTSTANDING_ASYNC("natpmp::mapping_expired");
		m_refresh_timer.expires_from_now(min_expire - now, ec);
		m_refresh_timer.async_wait(std::bind(&natpmp::mapping_expired, self(), _1, min_index));
		m_next_refresh = min_index;
	}
}

void natpmp::mapping_expired(error_code const& e, int const i)
{
	TORRENT_ASSERT(is_single_thread());
	COMPLETE_ASYNC("natpmp::mapping_expired");
	if (e) return;
#ifndef TORRENT_DISABLE_LOGGING
	log("mapping %u expired", i);
#endif
	m_mappings[i].act = portmap_action::add;
	if (m_next_refresh == i) m_next_refresh = -1;
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
	m_currently_mapping = -1;
	update_mapping(0);
}

} // namespace libtorrent
