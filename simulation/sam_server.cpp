/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "sam_server.hpp"

#include "libtorrent/hasher.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/aux_/escape_string.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <utility>

#if TORRENT_USE_I2P

using namespace lt::literals;
using boost::system::error_code;

namespace sim {

namespace {

std::string base64encode_i2p(lt::span<char const> s)
{
	std::string ret = lt::base64encode(std::string(s.begin(), s.end()));
	std::replace(ret.begin(), ret.end(), '+', '-');
	std::replace(ret.begin(), ret.end(), '/', '~');
	return ret;
}

std::pair<std::string_view, std::string_view> split(std::string_view s, char const delim)
{
	auto const pos = s.find(delim);
	if (pos == std::string_view::npos)
		return {s, {}};
	return {s.substr(0, pos), s.substr(pos + 1)};
}

std::string_view find_arg_value(std::string_view args, std::string_view const key)
{
	while (!args.empty())
	{
		std::string_view token;
		std::tie(token, args) = split(args, ' ');
		auto const [name, value] = split(token, '=');
		if (name == key)
			return value;
	}
	return {};
}

int g_next_connection_id = 0;

template <typename Queue>
std::shared_ptr<sam_connection> pop_open(Queue& q)
{
	while (!q.empty())
	{
		auto c = q.front().lock();
		q.pop_front();
		if (c && c->is_open())
			return c;
	}
	return {};
}
}

sam_session* sam_state::find_session(std::string_view const id)
{
	auto const it = sessions.find(id);
	return it == sessions.end() ? nullptr : &it->second;
}

sam_session* sam_state::find_destination(std::string_view const b64)
{
	for (auto& s : sessions)
		if (s.second.destination == b64)
			return &s.second;
	return nullptr;
}

sam_session* sam_state::find_b32(std::string_view const name)
{
	for (auto& s : sessions)
		if (s.second.b32_name == name)
			return &s.second;
	return nullptr;
}

sam_server::sam_server(asio::io_context& ios, unsigned short const listen_port)
	: m_ios(ios)
	, m_listen_socket(ios)
	, m_state(std::make_shared<sam_state>())
	, m_conn(std::make_shared<sam_connection>(ios, m_state))
{
	auto const local_ip = ios.get_ips().front();
	m_listen_socket.open(local_ip.is_v4() ? asio::ip::tcp::v4() : asio::ip::tcp::v6());
	m_listen_socket.bind(asio::ip::tcp::endpoint(local_ip, listen_port));
	m_listen_socket.listen();
	m_listen_socket.async_accept(
		m_conn->socket(), m_ep, [this](error_code const& ec) { on_accept(ec); });
}

void sam_server::on_accept(error_code const& ec)
{
	if (ec == asio::error::operation_aborted)
		return;
	if (ec)
	{
		std::printf("sam_server::on_accept: (%d) %s\n", ec.value(), ec.message().c_str());
		return;
	}

	m_conn->start();
	m_conn = std::make_shared<sam_connection>(m_ios, m_state);
	m_listen_socket.async_accept(
		m_conn->socket(), m_ep, [this](error_code const& e) { on_accept(e); });
}

void sam_server::stop() { m_listen_socket.close(); }

std::optional<lt::sha256_hash> sam_server::destination(asio::ip::address const& client) const
{
	for (auto const& s : m_state->sessions)
		if (s.second.client == client)
			return s.second.dest_hash;
	return std::nullopt;
}

sam_connection::sam_connection(asio::io_context& ios, std::shared_ptr<sam_state> state)
	: m_socket(ios)
	, m_state(std::move(state))
	, m_id(g_next_connection_id++)
{}

void sam_connection::log(char const* fmt, ...) const
{
	std::printf("sam_server[%d]: ", m_id);
	va_list v;
	va_start(v, fmt);
	std::vprintf(fmt, v);
	va_end(v);
	std::printf("\n");
}

void sam_connection::start()
{
	error_code ec;
	auto const ep = m_socket.remote_endpoint(ec);
	log("accepted connection from %s", ep.address().to_string().c_str());
	read();
}

void sam_connection::read()
{
	m_socket.async_read_some(asio::buffer(m_recv_buffer),
		[self = shared_from_this()](
			error_code const& ec, std::size_t const n) { self->on_read(ec, n); });
}

void sam_connection::on_read(error_code const& ec, std::size_t const bytes_transferred)
{
	if (ec)
	{
		close(ec.message().c_str());
		return;
	}

	std::string_view const received(m_recv_buffer.data(), bytes_transferred);
	if (m_conn_state == state_t::relay)
	{
		m_peer->send(std::string(received));
		read();
		return;
	}
	if (m_conn_state == state_t::draining)
	{
		read();
		return;
	}
	if (m_conn_state == state_t::waiting)
	{
		// held until pair_with() forwards it to the peer
		m_line_buffer.append(received);
		read();
		return;
	}

	m_line_buffer.append(received);
	for (;;)
	{
		auto const pos = m_line_buffer.find('\n');
		if (pos == std::string::npos)
			break;
		std::string const line = m_line_buffer.substr(0, pos);
		m_line_buffer.erase(0, pos + 1);
		handle_line(line);
		if (m_conn_state == state_t::closed)
			return;
		// whatever follows STREAM CONNECT/ACCEPT is stream payload, not commands
		if (m_conn_state != state_t::command)
			break;
	}
	read();
}

void sam_connection::handle_line(std::string_view const line)
{
	log("<== %.*s", int(std::min(line.size(), std::size_t(120))), line.data());
	auto const [cmd1, rest] = split(line, ' ');
	auto const [cmd2, args] = split(rest, ' ');

	if (m_conn_state == state_t::hello)
	{
		if (cmd1 != "HELLO"_sv || cmd2 != "VERSION"_sv)
		{
			close("expected HELLO VERSION");
			return;
		}
		m_conn_state = state_t::command;
		send("HELLO REPLY RESULT=OK VERSION=3.1\n");
	}
	else if (cmd1 == "SESSION"_sv && cmd2 == "CREATE"_sv)
		session_create(find_arg_value(args, "ID"));
	else if (cmd1 == "NAMING"_sv && cmd2 == "LOOKUP"_sv)
		naming_lookup(find_arg_value(args, "NAME"));
	else if (cmd1 == "STREAM"_sv && cmd2 == "CONNECT"_sv)
		stream_connect(find_arg_value(args, "ID"), find_arg_value(args, "DESTINATION"));
	else if (cmd1 == "STREAM"_sv && cmd2 == "ACCEPT"_sv)
		stream_accept(find_arg_value(args, "ID"));
	else
		close("unknown command");
}

void sam_connection::session_create(std::string_view const id)
{
	if (id.empty() || m_state->find_session(id))
	{
		send("SESSION STATUS RESULT=DUPLICATED_ID\n");
		return;
	}

	// a 256 byte public key and a 128 byte signing key, followed by a KEY
	// certificate for Ed25519 signatures (type 7) and ElGamal crypto (type 0),
	// since libtorrent rejects destinations that don't look authentic
	std::array<char, 384 + 7> raw;
	lt::aux::random_bytes(lt::span<char>(raw).first(384));
	std::array<char, 7> const key_cert{5, 0, 4, 0, 7, 0, 0};
	std::copy(key_cert.begin(), key_cert.end(), raw.begin() + 384);

	sam_session& s = m_state->sessions[std::string(id)];
	s.destination = base64encode_i2p(raw);
	s.dest_hash = lt::hasher256(raw).final();
	s.b32_name = lt::base32encode_i2p(s.dest_hash) + ".b32.i2p";
	s.client = m_socket.remote_endpoint().address();
	m_session_id = id;

	send("SESSION STATUS RESULT=OK DESTINATION=" + s.destination + "\n");
}

void sam_connection::naming_lookup(std::string_view const name)
{
	sam_session const* s =
		(name == "ME"_sv) ? m_state->find_session(m_session_id) : m_state->find_b32(name);

	if (s == nullptr)
	{
		send("NAMING REPLY RESULT=KEY_NOT_FOUND NAME=" + std::string(name) + "\n");
		return;
	}
	send("NAMING REPLY RESULT=OK NAME=" + std::string(name) + " VALUE=" + s->destination + "\n");
}

void sam_connection::stream_connect(std::string_view const id, std::string_view const dest)
{
	sam_session const* src = m_state->find_session(id);
	if (src == nullptr)
	{
		send("STREAM STATUS RESULT=INVALID_ID\n");
		return;
	}
	sam_session* target = m_state->find_destination(dest);
	if (target == nullptr)
	{
		send("STREAM STATUS RESULT=CANT_REACH_PEER\n");
		return;
	}

	m_conn_state = state_t::waiting;
	m_src_dest = src->destination;
	if (auto acceptor = pop_open(target->acceptors))
		pair_with(std::move(acceptor));
	else
	{
		log("no acceptor waiting, queuing connector (%d queued)",
			int(target->connectors.size()) + 1);
		target->connectors.emplace_back(shared_from_this());
	}
}

void sam_connection::stream_accept(std::string_view const id)
{
	sam_session* s = m_state->find_session(id);
	if (s == nullptr)
	{
		send("STREAM STATUS RESULT=INVALID_ID\n");
		return;
	}

	m_conn_state = state_t::waiting;
	send("STREAM STATUS RESULT=OK\n");

	if (auto connector = pop_open(s->connectors))
		connector->pair_with(shared_from_this());
	else
	{
		log("no connector waiting, queuing acceptor (%d queued)", int(s->acceptors.size()) + 1);
		s->acceptors.emplace_back(shared_from_this());
	}

	if (!s->accepted)
	{
		s->accepted = true;
		if (m_state->on_session)
			m_state->on_session(s->client);
	}
}

void sam_connection::pair_with(std::shared_ptr<sam_connection> acceptor)
{
	log("relaying to [%d]", acceptor->m_id);
	acceptor->send(m_src_dest + "\n");
	send("STREAM STATUS RESULT=OK\n");
	m_peer = acceptor;
	acceptor->m_peer = shared_from_this();
	m_conn_state = state_t::relay;
	acceptor->m_conn_state = state_t::relay;
	forward_residual();
	acceptor->forward_residual();
}

void sam_connection::forward_residual()
{
	if (m_line_buffer.empty())
		return;
	log("forwarding %d buffered bytes to [%d]", int(m_line_buffer.size()), m_peer->m_id);
	m_peer->send(std::exchange(m_line_buffer, {}));
}

void sam_connection::send(std::string buf)
{
	if (m_conn_state != state_t::relay && m_conn_state != state_t::draining)
		log("==> %.*s", int(std::min(buf.size() - 1, std::size_t(120))), buf.data());
	m_send_queue.push_back(std::move(buf));
	if (!m_writing)
		do_write();
}

void sam_connection::do_write()
{
	if (m_send_queue.empty() || m_conn_state == state_t::closed)
	{
		m_writing = false;
		if (m_conn_state == state_t::draining)
			close("drained");
		return;
	}
	m_writing = true;
	asio::async_write(m_socket,
		asio::buffer(m_send_queue.front()),
		[self = shared_from_this()](error_code const& ec, std::size_t) {
			self->m_send_queue.pop_front();
			if (ec)
			{
				self->close(ec.message().c_str());
				return;
			}
			self->do_write();
		});
}

void sam_connection::close(char const* const reason)
{
	if (m_conn_state == state_t::closed)
		return;
	log("closing: %s", reason);
	m_conn_state = state_t::closed;

	error_code ignore;
	m_socket.close(ignore);

	if (auto const it = m_state->sessions.find(m_session_id); it != m_state->sessions.end())
	{
		auto connectors = std::move(it->second.connectors);
		auto acceptors = std::move(it->second.acceptors);
		log("removing session %s", m_session_id.c_str());
		m_state->sessions.erase(it);

		while (auto c = pop_open(connectors))
		{
			c->send("STREAM STATUS RESULT=CANT_REACH_PEER\n");
			c->close_after_send("target session closed");
		}
		while (auto a = pop_open(acceptors))
			a->close("session closed");
	}

	if (auto peer = std::move(m_peer))
		peer->close_after_send("relay peer closed");
}

void sam_connection::close_after_send(char const* const reason)
{
	if (m_conn_state == state_t::closed)
		return;
	log("closing after send: %s", reason);
	m_conn_state = state_t::draining;
	m_peer.reset();
	if (!m_writing)
		close("nothing to drain");
}
}

#endif // TORRENT_USE_I2P
