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

#include <libtorrent/natpmp.hpp>
#include <libtorrent/io.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <asio/ip/host_name.hpp>

using boost::bind;
using namespace libtorrent;
using boost::posix_time::microsec_clock;

natpmp::natpmp(io_service& ios, portmap_callback_t const& cb)
	: m_callback(cb)
	, m_currently_mapping(-1)
	, m_retry_count(0)
	, m_socket(ios)
	, m_send_timer(ios)
	, m_refresh_timer(ios)
	, m_disabled(false)
{
	m_mappings[0].protocol = 2; // tcp
	m_mappings[1].protocol = 1; // udp

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log.open("natpmp.log", std::ios::in | std::ios::out | std::ios::trunc);
#endif
	
	udp::resolver r(ios);
	udp::resolver::iterator i = r.resolve(udp::resolver::query(asio::ip::host_name(), "0"));
	for (;i != udp::resolver_iterator(); ++i)
	{
		if (i->endpoint().address().is_v4()) break;
	}

	if (i == udp::resolver_iterator())
	{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << "local host name did not resolve to an IPv4 address. "
			"disabling NAT-PMP" << std::endl;
#endif
		m_disabled = true;
		return;
	}

	address_v4 local = i->endpoint().address().to_v4();
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << to_simple_string(microsec_clock::universal_time())
		<< " local ip: " << local.to_string() << std::endl;
#endif

	if ((local.to_ulong() & 0xff000000) != 0x0a000000
		&& (local.to_ulong() & 0xfff00000) != 0xac100000
		&& (local.to_ulong() & 0xffff0000) != 0xaca80000)
	{
		// the local address seems to be an external
		// internet address. Assume it is not behind a NAT
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << "not on a NAT. disabling NAT-PMP" << std::endl;
#endif
		m_disabled = true;
		return;
	}

	// assume the router is located on the local
	// network as x.x.x.1
	// TODO: find a better way to figure out the router IP
	m_nat_endpoint = udp::endpoint(
		address_v4((local.to_ulong() & 0xffffff00) | 1), 5351);

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << "assuming router is at: " << m_nat_endpoint.address().to_string() << std::endl;
#endif

	m_socket.open(udp::v4());
	m_socket.bind(udp::endpoint());
}

void natpmp::set_mappings(int tcp, int udp)
{
	if (m_disabled) return;
	update_mapping(0, tcp);
	update_mapping(1, udp);
}

void natpmp::update_mapping(int i, int port)
{
	natpmp::mapping& m = m_mappings[i];
	if (port <= 0) return;
	if (m.local_port != port)
		m.need_update = true;

	m.local_port = port;
	// prefer the same external port as the local port
	if (m.external_port == 0) m.external_port = port;

	if (m_currently_mapping == -1)
	{
		// the socket is not currently in use
		// send out a mapping request
		m_retry_count = 0;
		send_map_request(i);
		m_socket.async_receive_from(asio::buffer(&m_response_buffer, 16)
			, m_remote, bind(&natpmp::on_reply, this, _1, _2));
	}
}

void natpmp::send_map_request(int i) try
{
	using namespace libtorrent::detail;
	using boost::posix_time::milliseconds;

	assert(m_currently_mapping == -1
		|| m_currently_mapping == i);
	m_currently_mapping = i;
	mapping& m = m_mappings[i];
	char buf[12];
	char* out = buf;
	write_uint8(0, out); // NAT-PMP version
	write_uint8(m.protocol, out); // map "protocol"
	write_uint16(0, out); // reserved
	write_uint16(m.local_port, out); // private port
	write_uint16(m.external_port, out); // requested public port
	int ttl = m.external_port == 0 ? 0 : 3600;
	write_uint32(ttl, out); // port mapping lifetime

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << to_simple_string(microsec_clock::universal_time())
		<< " ==> port map request: " << (m.protocol == 1 ? "udp" : "tcp")
		<< " local: " << m.local_port << " external: " << m.external_port
		<< " ttl: " << ttl << std::endl;
#endif

	m_socket.send_to(asio::buffer(buf, 12), m_nat_endpoint);
	// linear back-off instead of exponential
	++m_retry_count;
	m_send_timer.expires_from_now(milliseconds(250 * m_retry_count));
	m_send_timer.async_wait(bind(&natpmp::resend_request, this, i, _1));
}
catch (std::exception& e)
{
	std::string err = e.what();
}

void natpmp::resend_request(int i, asio::error_code const& e)
{
	using boost::posix_time::hours;
	if (e) return;
	if (m_retry_count >= 9)
	{
		m_mappings[i].need_update = false;
		// try again in two hours
		m_mappings[i].expires
			= boost::posix_time::second_clock::universal_time() + hours(2);
		return;
	}
	send_map_request(i);
}

void natpmp::on_reply(asio::error_code const& e
	, std::size_t bytes_transferred)
{
	using namespace libtorrent::detail;
	using boost::posix_time::seconds;
	if (e) return;

	try
	{

		if (m_remote != m_nat_endpoint)
		{
			m_socket.async_receive_from(asio::buffer(&m_response_buffer, 16)
				, m_remote, bind(&natpmp::on_reply, this, _1, _2));
			return;
		}
		
		m_send_timer.cancel();

		assert(m_currently_mapping >= 0);
		int i = m_currently_mapping;
		mapping& m = m_mappings[i];

		char* in = m_response_buffer;
		int version = read_uint8(in);
		int cmd = read_uint8(in);
		int result = read_uint16(in);
		int time = read_uint32(in);
		int private_port = read_uint16(in);
		int public_port = read_uint16(in);
		int lifetime = read_uint32(in);

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		m_log << to_simple_string(microsec_clock::universal_time())
			<< " <== port map response: " << (cmd - 128 == 1 ? "udp" : "tcp")
			<< " local: " << private_port << " external: " << public_port
			<< " ttl: " << lifetime << std::endl;
#endif

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		if (version != 0)
		{
			m_log << "*** unexpected version: " << version << std::endl;
		}
#endif

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		if (private_port != m.local_port)
		{
			m_log << "*** unexpected local port: " << private_port << std::endl;
		}
#endif

#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
		if (cmd != 128 + m.protocol)
		{
			m_log << "*** unexpected protocol: " << (cmd - 128) << std::endl;
		}
#endif

		if (public_port == 0 || lifetime == 0)
		{
			// this means the mapping was
			// successfully closed
			m.local_port = 0;
		}
		else
		{
			m.expires = boost::posix_time::second_clock::universal_time()
				+ seconds(int(lifetime * 0.7f));
			m.external_port = public_port;
		}
		
		if (result != 0)
		{
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
			m_log << "*** ERROR: " << result << std::endl;
#endif
			std::stringstream errmsg;
			errmsg << "NAT router reports error (" << result << ") ";
			switch (result)
			{
				case 1: errmsg << "Unsupported protocol version"; break;
				case 2: errmsg << "Not authorized to create port map (enable NAT-PMP on your router)"; break;
				case 3: errmsg << "Network failure"; break;
				case 4: errmsg << "Out of resources"; break;
				case 5: errmsg << "Unsupported opcpde"; break;
			}
			throw std::runtime_error(errmsg.str());
		}

		int tcp_port = 0;
		int udp_port = 0;
		if (m.protocol == 1) udp_port = m.external_port;
		else tcp_port = public_port;
		m_callback(tcp_port, udp_port, "");
	}
	catch (std::exception& e)
	{
		using boost::posix_time::hours;
		// try again in two hours
		m_mappings[m_currently_mapping].expires
			= boost::posix_time::second_clock::universal_time() + hours(2);
		m_callback(0, 0, e.what());
	}
	int i = m_currently_mapping;
	m_currently_mapping = -1;
	m_mappings[i].need_update = false;
	update_expiration_timer();
	try_next_mapping(i);
}

void natpmp::update_expiration_timer()
{
	using boost::posix_time::seconds;
	boost::posix_time::ptime now = boost::posix_time::second_clock::universal_time();
	boost::posix_time::ptime min_expire = now + seconds(3600);
	int min_index = -1;
	for (int i = 0; i < 2; ++i)
		if (m_mappings[i].expires < min_expire
			&& m_mappings[i].local_port != 0)
		{
			min_expire = m_mappings[i].expires;
			min_index = i;
		}

	if (min_index >= 0)
	{
		m_refresh_timer.expires_from_now(min_expire - now);
		m_refresh_timer.async_wait(bind(&natpmp::mapping_expired, this, _1, min_index));
	}
}

void natpmp::mapping_expired(asio::error_code const& e, int i)
{
	if (e) return;
#if defined(TORRENT_LOGGING) || defined(TORRENT_VERBOSE_LOGGING)
	m_log << "*** mapping " << i << " expired, updating" << std::endl;
#endif
	refresh_mapping(i);
}

void natpmp::refresh_mapping(int i)
{
	m_mappings[i].need_update = true;
	if (m_currently_mapping == -1)
	{
		// the socket is not currently in use
		// send out a mapping request
		m_retry_count = 0;
		send_map_request(i);
		m_socket.async_receive_from(asio::buffer(&m_response_buffer, 16)
			, m_remote, bind(&natpmp::on_reply, this, _1, _2));
	}
}

void natpmp::try_next_mapping(int i)
{
	++i;
	if (i >= 2) i = 0;
	if (m_mappings[i].need_update)
		refresh_mapping(i);
}

void natpmp::close()
{
	if (m_disabled) return;
	for (int i = 0; i < 2; ++i)
	{
		if (m_mappings[i].local_port == 0)
			continue;
		m_mappings[i].external_port = 0;
		refresh_mapping(i);
	}
}

