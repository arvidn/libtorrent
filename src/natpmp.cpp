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

#include <boost/version.hpp>
#include <boost/bind.hpp>

#include "libtorrent/config.hpp"

#if defined TORRENT_WINDOWS || defined TORRENT_CYGWIN
// asio assumes that the windows error codes are defined already
#include <winsock2.h>
#endif

#if BOOST_VERSION < 103500
#include <asio/ip/host_name.hpp>
#else
#include <boost/asio/ip/host_name.hpp>
#endif

#include "libtorrent/natpmp.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/enum_net.hpp"

using namespace libtorrent;

natpmp::natpmp(io_service& ios, address const& listen_interface
	, portmap_callback_t const& cb, log_callback_t const& lcb)
	: m_callback(cb)
	, m_log_callback(lcb)
	, m_currently_mapping(-1)
	, m_retry_count(0)
	, m_socket(ios)
	, m_send_timer(ios)
	, m_refresh_timer(ios)
	, m_next_refresh(-1)
	, m_disabled(false)
	, m_abort(false)
{
	// unfortunately async operations rely on the storage
	// for this array not to be reallocated, by passing
	// around pointers to its elements. so reserve size for now
	m_mappings.reserve(10);
	rebind(listen_interface);
}

void natpmp::rebind(address const& listen_interface)
{
	mutex_t::scoped_lock l(m_mutex);

	error_code ec;
	address gateway = get_default_gateway(m_socket.get_io_service(), ec);
	if (ec)
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "failed to find default route: %s", ec.message().c_str());
		log(msg, l);
		disable(ec, l);
		return;
	}

	m_disabled = false;

	udp::endpoint nat_endpoint(gateway, 5351);
	if (nat_endpoint == m_nat_endpoint) return;
	m_nat_endpoint = nat_endpoint;

	char msg[200];
	snprintf(msg, sizeof(msg), "found router at: %s"
		, print_address(m_nat_endpoint.address()).c_str());
	log(msg, l);

	m_socket.open(udp::v4(), ec);
	if (ec)
	{
		disable(ec, l);
		return;
	}
	m_socket.bind(udp::endpoint(address_v4::any(), 0), ec);
	if (ec)
	{
		disable(ec, l);
		return;
	}

	m_socket.async_receive_from(asio::buffer(&m_response_buffer, 16)
		, m_remote, boost::bind(&natpmp::on_reply, self(), _1, _2));

	for (std::vector<mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol != none
			|| i->action != mapping_t::action_none)
			continue;
		i->action = mapping_t::action_add;
		update_mapping(i - m_mappings.begin(), l);
	}
}

bool natpmp::get_mapping(int index, int& local_port, int& external_port, int& protocol) const
{
	mutex_t::scoped_lock l(m_mutex);

	TORRENT_ASSERT(index < int(m_mappings.size()) && index >= 0);
	if (index >= int(m_mappings.size()) || index < 0) return false;
	mapping_t const& m = m_mappings[index];
	if (m.protocol == none) return false;
	local_port = m.local_port;
	external_port = m.external_port;
	protocol = m.protocol;
	return true;
}

void natpmp::log(char const* msg, mutex_t::scoped_lock& l)
{
	l.unlock();
	m_log_callback(msg);
	l.lock();
}

void natpmp::disable(error_code const& ec, mutex_t::scoped_lock& l)
{
	m_disabled = true;

	for (std::vector<mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol == none) continue;
		i->protocol = none;
		int index = i - m_mappings.begin();
		l.unlock();
		m_callback(index, 0, ec);
		l.lock();
	}
	close_impl(l);
}

void natpmp::delete_mapping(int index)
{
	mutex_t::scoped_lock l(m_mutex);

	TORRENT_ASSERT(index < int(m_mappings.size()) && index >= 0);
	if (index >= int(m_mappings.size()) || index < 0) return;
	mapping_t& m = m_mappings[index];

	if (m.protocol == none) return;
	if (!m.map_sent)
	{
		m.action = mapping_t::action_none;
		m.protocol = none;
		return;
	}

	m.action = mapping_t::action_delete;
	update_mapping(index, l);
}

int natpmp::add_mapping(protocol_type p, int external_port, int local_port)
{
	mutex_t::scoped_lock l(m_mutex);

	if (m_disabled) return -1;

	std::vector<mapping_t>::iterator i = std::find_if(m_mappings.begin()
		, m_mappings.end(), boost::bind(&mapping_t::protocol, _1) == int(none));
	if (i == m_mappings.end())
	{
		m_mappings.push_back(mapping_t());
		i = m_mappings.end() - 1;
	}
	i->protocol = p;
	i->external_port = external_port;
	i->local_port = local_port;
	i->action = mapping_t::action_add;

	int mapping_index = i - m_mappings.begin();

	update_mapping(mapping_index, l);
	return mapping_index;
}

void natpmp::try_next_mapping(int i, mutex_t::scoped_lock& l)
{
/*
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	ptime now = time_now();
	for (std::vector<mapping_t>::iterator m = m_mappings.begin()
		, end(m_mappings.end()); m != end; ++m)
	{
		m_log << "     " << (m - m_mappings.begin()) << " [ "
			"proto: " << (m->protocol == none ? "none" : m->protocol == tcp ? "tcp" : "udp")
			<< " port: " << m->external_port
			<< " local-port: " << m->local_port
			<< " action: " << (m->action == mapping_t::action_none ? "none" : m->action == mapping_t::action_add ? "add" : "delete")
			<< " ttl: " << total_seconds(m->expires - now)
			<< " ]" << std::endl;
	}
#endif
*/
	if (i < int(m_mappings.size()) - 1)
	{
		update_mapping(i + 1, l);
		return;
	}

	std::vector<mapping_t>::iterator m = std::find_if(
		m_mappings.begin(), m_mappings.end()
		, boost::bind(&mapping_t::action, _1) != int(mapping_t::action_none));

	if (m == m_mappings.end())
	{
		if (m_abort)
		{
			error_code ec;
			m_send_timer.cancel(ec);
			m_socket.close(ec);
		}
//#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
//		m_log << "     done" << (m_abort?" shutting down":"") << std::endl;
//#endif
		return;
	}

//#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
//	m_log << "     updating " << (m - m_mappings.begin()) << std::endl;
//#endif

	update_mapping(m - m_mappings.begin(), l);
}

void natpmp::update_mapping(int i, mutex_t::scoped_lock& l)
{
	if (i == int(m_mappings.size()))
	{
		if (m_abort)
		{
			error_code ec;
			m_send_timer.cancel(ec);
			m_socket.close(ec);
		}
//#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
//		m_log << "     done" << (m_abort?" shutting down":"") << std::endl;
//#endif
		return;
	}

	natpmp::mapping_t& m = m_mappings[i];
	if (m.action == mapping_t::action_none
		|| m.protocol == none)
	{
		try_next_mapping(i, l);
		return;
	}

	if (m_currently_mapping == -1)
	{
		// the socket is not currently in use
		// send out a mapping request
		m_retry_count = 0;
		send_map_request(i, l);
	}
}

void natpmp::send_map_request(int i, mutex_t::scoped_lock& l)
{
	using namespace libtorrent::detail;

	TORRENT_ASSERT(m_currently_mapping == -1
		|| m_currently_mapping == i);
	m_currently_mapping = i;
	mapping_t& m = m_mappings[i];
	TORRENT_ASSERT(m.action != mapping_t::action_none);
	char buf[12];
	char* out = buf;
	write_uint8(0, out); // NAT-PMP version
	write_uint8(m.protocol, out); // map "protocol"
	write_uint16(0, out); // reserved
	write_uint16(m.local_port, out); // private port
	write_uint16(m.external_port, out); // requested public port
	int ttl = m.action == mapping_t::action_add ? 3600 : 0;
	write_uint32(ttl, out); // port mapping lifetime

	char msg[200];
	snprintf(msg, sizeof(msg), "==> port map [ action: %s"
		" proto: %s local: %u external: %u ttl: %u ]"
		, m.action == mapping_t::action_add ? "add" : "delete"
		, m.protocol == udp ? "udp" : "tcp"
		, m.local_port, m.external_port, ttl);
	log(msg, l);

	error_code ec;
	m_socket.send_to(asio::buffer(buf, 12), m_nat_endpoint, 0, ec);
	m.map_sent = true;
	m.outstanding_request = true;
	if (m_abort)
	{
		// when we're shutting down, ignore the
		// responses and just remove all mappings
		// immediately
		m_currently_mapping = -1;
		m.action = mapping_t::action_none;
		try_next_mapping(i, l);
	}
	else
	{
		// linear back-off instead of exponential
		++m_retry_count;
		m_send_timer.expires_from_now(milliseconds(250 * m_retry_count), ec);
		m_send_timer.async_wait(boost::bind(&natpmp::resend_request, self(), i, _1));
	}
}

void natpmp::resend_request(int i, error_code const& e)
{
	if (e) return;
	mutex_t::scoped_lock l(m_mutex);
	if (m_currently_mapping != i) return;

	// if we're shutting down, don't retry, just move on
	// to the next mapping
	if (m_retry_count >= 9 || m_abort)
	{
		m_currently_mapping = -1;
		m_mappings[i].action = mapping_t::action_none;
		// try again in two hours
		m_mappings[i].expires = time_now() + hours(2);
		try_next_mapping(i, l);
		return;
	}
	send_map_request(i, l);
}

void natpmp::on_reply(error_code const& e
	, std::size_t bytes_transferred)
{
	mutex_t::scoped_lock l(m_mutex);

	using namespace libtorrent::detail;
	if (e)
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "error on receiving reply: %s", e.message().c_str());
		log(msg, l);
		return;
	}

	m_socket.async_receive_from(asio::buffer(&m_response_buffer, 16)
		, m_remote, boost::bind(&natpmp::on_reply, self(), _1, _2));

	// simulate packet loss
/*
	if ((rand() % 2) == 0)
	{
		log(" simulating drop", l);
		return;
	}
*/
	if (m_remote != m_nat_endpoint)
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "received packet from wrong IP: %s"
			, print_endpoint(m_remote).c_str());
		log(msg, l);
		return;
	}

	error_code ec;
	m_send_timer.cancel(ec);

	char* in = m_response_buffer;
	int version = read_uint8(in);
	int cmd = read_uint8(in);
	int result = read_uint16(in);
	int time = read_uint32(in);
	int private_port = read_uint16(in);
	int public_port = read_uint16(in);
	int lifetime = read_uint32(in);

	(void)time; // to remove warning

	int protocol = (cmd - 128 == 1)?udp:tcp;

	char msg[200];
	int num_chars = snprintf(msg, sizeof(msg), "<== port map ["
		" protocol: %s local: %u external: %u ttl: %u ]"
		, (cmd - 128 == 1 ? "udp" : "tcp")
		, private_port, public_port, lifetime);

	if (version != 0)
	{
		snprintf(msg + num_chars, sizeof(msg) - num_chars, "unexpected version: %u"
			, version);
		log(msg, l);
	}

	mapping_t* m = 0;
	int index = -1;
	for (std::vector<mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
		if (private_port != i->local_port) continue;
		if (protocol != i->protocol) continue;
		if (!i->map_sent) continue;
		if (!i->outstanding_request) continue;
		m = &*i;
		index = i - m_mappings.begin();
		break;
	}

	if (m == 0)
	{
		snprintf(msg + num_chars, sizeof(msg) - num_chars, " not found in map table");
		log(msg, l);
		return;
	}
	m->outstanding_request = false;

	log(msg, l);

	if (public_port == 0 || lifetime == 0)
	{
		// this means the mapping was
		// successfully closed
		m->protocol = none;
	}
	else
	{
		m->expires = time_now() + seconds(int(lifetime * 0.7f));
		m->external_port = public_port;
	}

	if (result != 0)
	{
		int errors[] =
		{
			errors::unsupported_protocol_version,
			errors::natpmp_not_authorized,
			errors::network_failure,
			errors::no_resources,
			errors::unsupported_opcode,
		};
		int ev = errors::no_error;
		if (result >= 1 && result <= 5) ev = errors[result - 1];

		m->expires = time_now() + hours(2);
		l.unlock();
		m_callback(index, 0, error_code(ev, get_libtorrent_category()));
		l.lock();
	}
	else if (m->action == mapping_t::action_add)
	{
		l.unlock();
		m_callback(index, m->external_port, error_code(errors::no_error, get_libtorrent_category()));
		l.lock();
	}

	if (m_abort) return;

	m_currently_mapping = -1;
	m->action = mapping_t::action_none;
	m_send_timer.cancel(ec);
	update_expiration_timer(l);
	try_next_mapping(index, l);
}

void natpmp::update_expiration_timer(boost::mutex::scoped_lock& l)
{
	if (m_abort) return;

	ptime now = time_now() + milliseconds(100);
/*
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << time_now_string() << "update_expiration_timer " << std::endl;
	for (std::vector<mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
		m_log << "     " << (i - m_mappings.begin()) << " [ "
			"proto: " << (i->protocol == none ? "none" : i->protocol == tcp ? "tcp" : "udp")
			<< " port: " << i->external_port
			<< " local-port: " << i->local_port
			<< " action: " << (i->action == mapping_t::action_none ? "none" : i->action == mapping_t::action_add ? "add" : "delete")
			<< " ttl: " << total_seconds(i->expires - now)
			<< " ]" << std::endl;
	}
#endif
*/
	ptime min_expire = now + seconds(3600);
	int min_index = -1;
	for (std::vector<mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
		if (i->protocol == none
			|| i->action != mapping_t::action_none) continue;
		int index = i - m_mappings.begin();
		if (i->expires < now)
		{
			char msg[200];
			snprintf(msg, sizeof(msg), "mapping %u expired", index);
			log(msg, l);
			i->action = mapping_t::action_add;
			if (m_next_refresh == index) m_next_refresh = -1;
			update_mapping(index, l);
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
/*
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << time_now_string() << " next expiration ["
			" i: " << min_index
			<< " ttl: " << total_seconds(min_expire - time_now())
			<< " ]" << std::endl;
#endif
*/
		error_code ec;
		if (m_next_refresh >= 0) m_refresh_timer.cancel(ec);
		m_refresh_timer.expires_from_now(min_expire - now, ec);
		m_refresh_timer.async_wait(boost::bind(&natpmp::mapping_expired, self(), _1, min_index));
		m_next_refresh = min_index;
	}
}

void natpmp::mapping_expired(error_code const& e, int i)
{
	if (e) return;
	mutex_t::scoped_lock l(m_mutex);
	char msg[200];
	snprintf(msg, sizeof(msg), "mapping %u expired", i);
	log(msg, l);
	m_mappings[i].action = mapping_t::action_add;
	if (m_next_refresh == i) m_next_refresh = -1;
	update_mapping(i, l);
}

void natpmp::close()
{
	mutex_t::scoped_lock l(m_mutex);
	close_impl(l);
}

void natpmp::close_impl(mutex_t::scoped_lock& l)
{
	m_abort = true;
	log("closing", l);
/*
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << time_now_string() << " close" << std::endl;
#endif
*/
	if (m_disabled) return;
	ptime now = time_now();
	for (std::vector<mapping_t>::iterator i = m_mappings.begin()
		, end(m_mappings.end()); i != end; ++i)
	{
/*
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << "     " << (i - m_mappings.begin()) << " [ "
			"proto: " << (i->protocol == none ? "none" : i->protocol == tcp ? "tcp" : "udp")
			<< " port: " << i->external_port
			<< " local-port: " << i->local_port
			<< " action: " << (i->action == mapping_t::action_none ? "none" : i->action == mapping_t::action_add ? "add" : "delete")
			<< " ttl: " << total_seconds(i->expires - now)
			<< " ]" << std::endl;
#endif
*/
		if (i->protocol == none) continue;
		i->action = mapping_t::action_delete;
	}
	error_code ec;
	m_refresh_timer.cancel(ec);
	m_currently_mapping = -1;
	update_mapping(0, l);
}

