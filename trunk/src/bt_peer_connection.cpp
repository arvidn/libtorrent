/*

Copyright (c) 2003, Arvid Norberg
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

#include <vector>
#include <iostream>
#include <iomanip>
#include <limits>
#include <boost/bind.hpp>

#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/aux_/session_impl.hpp"

using namespace boost::posix_time;
using boost::bind;
using boost::shared_ptr;
using libtorrent::aux::session_impl;

namespace libtorrent
{

	// the names of the extensions to look for in
	// the extensions-message
	const char* bt_peer_connection::extension_names[] =
	{ "", "LT_chat", "LT_metadata", "LT_peer_exchange" };

	const bt_peer_connection::message_handler
	bt_peer_connection::m_message_handler[] =
	{
		&bt_peer_connection::on_choke,
		&bt_peer_connection::on_unchoke,
		&bt_peer_connection::on_interested,
		&bt_peer_connection::on_not_interested,
		&bt_peer_connection::on_have,
		&bt_peer_connection::on_bitfield,
		&bt_peer_connection::on_request,
		&bt_peer_connection::on_piece,
		&bt_peer_connection::on_cancel,
		&bt_peer_connection::on_dht_port,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		&bt_peer_connection::on_extended
	};


	bt_peer_connection::bt_peer_connection(
		session_impl& ses
		, boost::weak_ptr<torrent> tor
		, shared_ptr<stream_socket> s
		, tcp::endpoint const& remote)
		: peer_connection(ses, tor, s, remote)
		, m_state(read_protocol_length)
		, m_supports_extensions(false)
		, m_supports_dht_port(false)
		, m_no_metadata(
			boost::gregorian::date(1970, boost::date_time::Jan, 1)
			, boost::posix_time::seconds(0))
		, m_metadata_request(
			boost::gregorian::date(1970, boost::date_time::Jan, 1)
			, boost::posix_time::seconds(0))
		, m_waiting_metadata_request(false)
		, m_metadata_progress(0)
#ifndef NDEBUG
		, m_in_constructor(true)
#endif
	{
#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "*** bt_peer_connection\n";
#endif

		// initialize the extension list to zero, since
		// we don't know which extensions the other
		// end supports yet
		std::fill(m_extension_messages, m_extension_messages + num_supported_extensions, 0);

		write_handshake();

		// start in the state where we are trying to read the
		// handshake from the other side
		reset_recv_buffer(1);

		// assume the other end has no pieces
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);
		
		if (t->ready_for_connections())
			write_bitfield(t->pieces());

		setup_send();
		setup_receive();
#ifndef NDEBUG
		m_in_constructor = false;
#endif
	}

	bt_peer_connection::bt_peer_connection(
		session_impl& ses
		, boost::shared_ptr<stream_socket> s)
		: peer_connection(ses, s)
		, m_state(read_protocol_length)
		, m_supports_extensions(false)
		, m_supports_dht_port(false)
		, m_no_metadata(
			boost::gregorian::date(1970, boost::date_time::Jan, 1)
			, boost::posix_time::seconds(0))
		, m_metadata_request(
			boost::gregorian::date(1970, boost::date_time::Jan, 1)
			, boost::posix_time::seconds(0))
		, m_waiting_metadata_request(false)
		, m_metadata_progress(0)
#ifndef NDEBUG
		, m_in_constructor(true)
#endif
	{
		// initialize the extension list to zero, since
		// we don't know which extensions the other
		// end supports yet
		std::fill(m_extension_messages, m_extension_messages + num_supported_extensions, 0);

		// we are not attached to any torrent yet.
		// we have to wait for the handshake to see
		// which torrent the connector want's to connect to

		// start in the state where we are trying to read the
		// handshake from the other side
		reset_recv_buffer(1);
		setup_receive();
#ifndef NDEBUG
		m_in_constructor = false;
#endif
	}

	bt_peer_connection::~bt_peer_connection()
	{
	}

	void bt_peer_connection::write_dht_port(int listen_port)
	{
		INVARIANT_CHECK;

		buffer::interval packet = allocate_send_buffer(7);
		detail::write_uint32(3, packet.begin);
		detail::write_uint8(msg_dht_port, packet.begin);
		detail::write_uint16(listen_port, packet.begin);
		assert(packet.begin == packet.end);
		setup_send();
	}

	void bt_peer_connection::get_peer_info(peer_info& p) const
	{
		assert(!associated_torrent().expired());

		p.down_speed = statistics().download_rate();
		p.up_speed = statistics().upload_rate();
		p.payload_down_speed = statistics().download_payload_rate();
		p.payload_up_speed = statistics().upload_payload_rate();
		p.pid = pid();
		p.ip = remote();

		p.total_download = statistics().total_payload_download();
		p.total_upload = statistics().total_payload_upload();

		if (m_ul_bandwidth_quota.given == std::numeric_limits<int>::max())
			p.upload_limit = -1;
		else
			p.upload_limit = m_ul_bandwidth_quota.given;

		if (m_dl_bandwidth_quota.given == std::numeric_limits<int>::max())
			p.download_limit = -1;
		else
			p.download_limit = m_dl_bandwidth_quota.given;

		p.load_balancing = total_free_upload();

		p.download_queue_length = (int)download_queue().size();
		p.upload_queue_length = (int)upload_queue().size();

		if (boost::optional<piece_block_progress> ret = downloading_piece_progress())
		{
			p.downloading_piece_index = ret->piece_index;
			p.downloading_block_index = ret->block_index;
			p.downloading_progress = ret->bytes_downloaded;
			p.downloading_total = ret->full_block_bytes;
		}
		else
		{
			p.downloading_piece_index = -1;
			p.downloading_block_index = -1;
			p.downloading_progress = 0;
			p.downloading_total = 0;
		}

		p.flags = 0;
		if (is_interesting()) p.flags |= peer_info::interesting;
		if (is_choked()) p.flags |= peer_info::choked;
		if (is_peer_interested()) p.flags |= peer_info::remote_interested;
		if (has_peer_choked()) p.flags |= peer_info::remote_choked;
		if (support_extensions()) p.flags |= peer_info::supports_extensions;
		if (is_local()) p.flags |= peer_info::local_connection;
		if (!is_connecting() && m_state < read_packet_size)
			p.flags |= peer_info::handshake;
		if (is_connecting() && !is_queued()) p.flags |= peer_info::connecting;
		if (is_queued()) p.flags |= peer_info::queued;
		
		p.pieces = get_bitfield();
		p.seed = is_seed();
		
		p.client = m_client_version;
		p.connection_type = peer_info::standard_bittorrent;
	}

	void bt_peer_connection::write_handshake()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);

		// add handshake to the send buffer
		const char version_string[] = "BitTorrent protocol";
		const int string_len = sizeof(version_string)-1;

		buffer::interval i = allocate_send_buffer(1 + string_len + 8 + 20 + 20);
		// length of version string
		*i.begin = string_len;
		++i.begin;

		// version string itself
		std::copy(
			version_string
			, version_string + string_len
			, i.begin);
		i.begin += string_len;

		// 8 zeroes
		std::fill(
			i.begin
			, i.begin + 8
			, 0);

#ifndef TORRENT_DISABLE_DHT
		// indicate that we support the DHT messages
		*(i.begin + 7) = 0x01;
#endif
		
		// we support extensions
		*(i.begin + 5) = 0x10;

		i.begin += 8;

		// info hash
		sha1_hash const& ih = t->torrent_file().info_hash();
		std::copy(ih.begin(), ih.end(), i.begin);
		i.begin += 20;

		// peer id
		std::copy(
			m_ses.get_peer_id().begin()
			, m_ses.get_peer_id().end()
			, i.begin);
		i.begin += 20;
		assert(i.begin == i.end);

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> HANDSHAKE\n";
#endif
		setup_send();
	}

	boost::optional<piece_block_progress> bt_peer_connection::downloading_piece_progress() const
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);

		buffer::const_interval recv_buffer = receive_buffer();
		// are we currently receiving a 'piece' message?
		if (m_state != read_packet
			|| (recv_buffer.end - recv_buffer.begin) < 9
			|| recv_buffer[0] != msg_piece)
			return boost::optional<piece_block_progress>();

		const char* ptr = recv_buffer.begin + 1;
		peer_request r;
		r.piece = detail::read_int32(ptr);
		r.start = detail::read_int32(ptr);
		r.length = packet_size() - 9;

		// is any of the piece message header data invalid?
		if (!verify_piece(r))
			return boost::optional<piece_block_progress>();

		piece_block_progress p;

		p.piece_index = r.piece;
		p.block_index = r.start / t->block_size();
		p.bytes_downloaded = recv_buffer.end - recv_buffer.begin - 9;
		p.full_block_bytes = r.length;

		return boost::optional<piece_block_progress>(p);
	}


	// message handlers

	// -----------------------------
	// --------- KEEPALIVE ---------
	// -----------------------------

	void bt_peer_connection::on_keepalive()
	{
		INVARIANT_CHECK;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " <== KEEPALIVE\n";
#endif
		incoming_keepalive();
	}

	// -----------------------------
	// ----------- CHOKE -----------
	// -----------------------------

	void bt_peer_connection::on_choke(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (packet_size() != 1)
			throw protocol_error("'choke' message size != 1");
		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		incoming_choke();
	}

	// -----------------------------
	// ---------- UNCHOKE ----------
	// -----------------------------

	void bt_peer_connection::on_unchoke(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (packet_size() != 1)
			throw protocol_error("'unchoke' message size != 1");
		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		incoming_unchoke();
	}

	// -----------------------------
	// -------- INTERESTED ---------
	// -----------------------------

	void bt_peer_connection::on_interested(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (packet_size() != 1)
			throw protocol_error("'interested' message size != 1");
		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		incoming_interested();
	}

	// -----------------------------
	// ------ NOT INTERESTED -------
	// -----------------------------

	void bt_peer_connection::on_not_interested(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (packet_size() != 1)
			throw protocol_error("'not interested' message size != 1");
		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		incoming_not_interested();
	}

	// -----------------------------
	// ----------- HAVE ------------
	// -----------------------------

	void bt_peer_connection::on_have(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (packet_size() != 5)
			throw protocol_error("'have' message size != 5");
		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();

		const char* ptr = recv_buffer.begin + 1;
		int index = detail::read_int32(ptr);

		incoming_have(index);
	}

	// -----------------------------
	// --------- BITFIELD ----------
	// -----------------------------

	void bt_peer_connection::on_bitfield(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);

		// if we don't have the metedata, we cannot
		// verify the bitfield size
		if (t->valid_metadata()
			&& packet_size() - 1 != ((int)get_bitfield().size() + 7) / 8)
			throw protocol_error("bitfield with invalid size");

		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();

		std::vector<bool> bitfield;
		
		if (!t->valid_metadata())
			bitfield.resize((packet_size() - 1) * 8);
		else
			bitfield.resize(get_bitfield().size());

		// if we don't have metadata yet
		// just remember the bitmask
		// don't update the piecepicker
		// (since it doesn't exist yet)
		for (int i = 0; i < (int)bitfield.size(); ++i)
			bitfield[i] = (recv_buffer[1 + (i>>3)] & (1 << (7 - (i&7)))) != 0;
		incoming_bitfield(bitfield);
	}

	// -----------------------------
	// ---------- REQUEST ----------
	// -----------------------------

	void bt_peer_connection::on_request(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (packet_size() != 13)
			throw protocol_error("'request' message size != 13");
		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();

		peer_request r;
		const char* ptr = recv_buffer.begin + 1;
		r.piece = detail::read_int32(ptr);
		r.start = detail::read_int32(ptr);
		r.length = detail::read_int32(ptr);
		
		incoming_request(r);
	}

	// -----------------------------
	// ----------- PIECE -----------
	// -----------------------------

	void bt_peer_connection::on_piece(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		
		buffer::const_interval recv_buffer = receive_buffer();
		int recv_pos = recv_buffer.end - recv_buffer.begin;

		// classify the received data as protocol chatter
		// or data payload for the statistics
		if (recv_pos <= 9)
			// only received protocol data
			m_statistics.received_bytes(0, received);
		else if (recv_pos - received >= 9)
			// only received payload data
			m_statistics.received_bytes(received, 0);
		else
		{
			// received a bit of both
			assert(recv_pos - received < 9);
			assert(recv_pos > 9);
			assert(9 - (recv_pos - received) <= 9);
			m_statistics.received_bytes(
				recv_pos - 9
				, 9 - (recv_pos - received));
		}

		incoming_piece_fragment();
		if (!packet_finished()) return;

		const char* ptr = recv_buffer.begin + 1;
		peer_request p;
		p.piece = detail::read_int32(ptr);
		p.start = detail::read_int32(ptr);
		p.length = packet_size() - 9;

		incoming_piece(p, recv_buffer.begin + 9);
	}

	// -----------------------------
	// ---------- CANCEL -----------
	// -----------------------------

	void bt_peer_connection::on_cancel(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (packet_size() != 13)
			throw protocol_error("'cancel' message size != 13");
		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();

		peer_request r;
		const char* ptr = recv_buffer.begin + 1;
		r.piece = detail::read_int32(ptr);
		r.start = detail::read_int32(ptr);
		r.length = detail::read_int32(ptr);

		incoming_cancel(r);
	}

	// -----------------------------
	// --------- DHT PORT ----------
	// -----------------------------

	void bt_peer_connection::on_dht_port(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		if (packet_size() != 3)
			throw protocol_error("'dht_port' message size != 3");
		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();

		const char* ptr = recv_buffer.begin + 1;
		int listen_port = detail::read_uint16(ptr);
		
		incoming_dht_port(listen_port);
	}

	// -----------------------------
	// --------- EXTENDED ----------
	// -----------------------------

	void bt_peer_connection::on_extended(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);
		m_statistics.received_bytes(0, received);
		if (packet_size() < 2)
			throw protocol_error("'extended' message smaller than 2 bytes");

		if (associated_torrent().expired())
			throw protocol_error("'extended' message sent before proper handshake");

		buffer::const_interval recv_buffer = receive_buffer();
		if (recv_buffer.end - recv_buffer.begin < 2) return;

		assert(*recv_buffer.begin == msg_extended);
		++recv_buffer.begin;

		int extended_id = detail::read_uint8(recv_buffer.begin);

		if (extended_id > 0 && extended_id < num_supported_extensions
			&& !m_ses.extension_enabled(extended_id))
			throw protocol_error("'extended' message using disabled extension");

		switch (extended_id)
		{
		case extended_handshake:
			on_extended_handshake(); break;
		case extended_chat_message:
			on_chat(); break;
		case extended_metadata_message:
			on_metadata(); break;
		case extended_peer_exchange_message:
			on_peer_exchange(); break;
		default:
			throw protocol_error("unknown extended message id: "
				+ boost::lexical_cast<std::string>(extended_id));
		};
	}

	void bt_peer_connection::write_chat_message(const std::string& msg)
	{
		INVARIANT_CHECK;

		assert(msg.length() <= 1 * 1024);
		if (!supports_extension(extended_chat_message)) return;

		entry e(entry::dictionary_t);
		e["msg"] = msg;

		std::vector<char> message;
		bencode(std::back_inserter(message), e);

		buffer::interval i = allocate_send_buffer(message.size() + 6);

		detail::write_uint32(1 + 1 + (int)message.size(), i.begin);
		detail::write_uint8(msg_extended, i.begin);
		detail::write_uint8(m_extension_messages[extended_chat_message], i.begin);

		std::copy(message.begin(), message.end(), i.begin);
		i.begin += message.size();
		assert(i.begin == i.end);
		setup_send();
	}


	void bt_peer_connection::on_extended_handshake() try
	{
		if (!packet_finished()) return;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);

		buffer::const_interval recv_buffer = receive_buffer();
	
		entry root = bdecode(recv_buffer.begin + 2, recv_buffer.end);

#ifdef TORRENT_VERBOSE_LOGGING
		std::stringstream ext;
		root.print(ext);
		(*m_logger) << "<== EXTENDED HANDSHAKE: \n" << ext.str();
#endif

		if (entry* msgs = root.find_key("m"))
		{
			if (msgs->type() == entry::dictionary_t)
			{
				// this must be the initial handshake message
				// lets see if any of our extensions are supported
				// if not, we will signal no extensions support to the upper layer
				for (int i = 1; i < num_supported_extensions; ++i)
				{
					if (entry* f = msgs->find_key(extension_names[i]))
					{
						m_extension_messages[i] = (int)f->integer();
					}
					else
					{
						m_extension_messages[i] = 0;
					}
				}
			}
		}

		// there is supposed to be a remote listen port
		if (entry* listen_port = root.find_key("p"))
		{
			if (listen_port->type() == entry::int_t)
			{
				tcp::endpoint adr(remote().address()
					, (unsigned short)listen_port->integer());
				t->get_policy().peer_from_tracker(adr, pid());
			}
		}
		// there should be a version too
		// but where do we put that info?
		
		if (entry* client_info = root.find_key("v"))
		{
			if (client_info->type() == entry::string_t)
				m_client_version = client_info->string();
		}

		if (entry* reqq = root.find_key("reqq"))
		{
			if (reqq->type() == entry::int_t)
				m_max_out_request_queue = reqq->integer();
			if (m_max_out_request_queue < 1)
				m_max_out_request_queue = 1;
		}
	}
	catch (std::exception& exc)
	{
#ifdef TORRENT_VERBOSE_LOGGIGN
		(*m_logger) << "invalid extended handshake: " << exc.what() << "\n";
#endif
	}

	// -----------------------------
	// ----------- CHAT ------------
	// -----------------------------

	void bt_peer_connection::on_chat()
	{
		if (packet_size() > 2 * 1024)
			throw protocol_error("CHAT message larger than 2 kB");

		if (!packet_finished()) return;

		try
		{
			boost::shared_ptr<torrent> t = associated_torrent().lock();
			assert(t);
		
			buffer::const_interval recv_buffer = receive_buffer();
			entry d = bdecode(recv_buffer.begin + 2, recv_buffer.end);
			const std::string& str = d["msg"].string();

			if (t->alerts().should_post(alert::critical))
			{
				t->alerts().post_alert(
					chat_message_alert(
						t->get_handle()
						, remote(), str));
			}

		}
		catch (invalid_encoding&)
		{
			// TODO: post an alert about the invalid chat message
			return;
//			throw protocol_error("invalid bencoding in CHAT message");
		}
		catch (type_error&)
		{
			// TODO: post an alert about the invalid chat message
			return;
//			throw protocol_error("invalid types in bencoded CHAT message");
		}
		return;
	}

	// -----------------------------
	// --------- METADATA ----------
	// -----------------------------

	void bt_peer_connection::on_metadata()
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);

		if (packet_size() > 500 * 1024)
			throw protocol_error("metadata message larger than 500 kB");

		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();
		recv_buffer.begin += 2;
		int type = detail::read_uint8(recv_buffer.begin);

		switch (type)
		{
		case 0: // request
			{
				int start = detail::read_uint8(recv_buffer.begin);
				int size = detail::read_uint8(recv_buffer.begin) + 1;

				if (packet_size() != 5)
				{
					// invalid metadata request
					throw protocol_error("invalid metadata request");
				}

				write_metadata(std::make_pair(start, size));
			}
			break;
		case 1: // data
			{
				if (recv_buffer.end - recv_buffer.begin < 8) return;
				int total_size = detail::read_int32(recv_buffer.begin);
				int offset = detail::read_int32(recv_buffer.begin);
				int data_size = packet_size() - 2 - 9;

				if (total_size > 500 * 1024)
					throw protocol_error("metadata size larger than 500 kB");
				if (total_size <= 0)
					throw protocol_error("invalid metadata size");
				if (offset > total_size || offset < 0)
					throw protocol_error("invalid metadata offset");
				if (offset + data_size > total_size)
					throw protocol_error("invalid metadata message");

				t->metadata_progress(total_size
					, recv_buffer.left() - m_metadata_progress);
				m_metadata_progress = recv_buffer.left();
				if (!packet_finished()) return;

#ifdef TORRENT_VERBOSE_LOGGING
				using namespace boost::posix_time;
				(*m_logger) << to_simple_string(second_clock::universal_time())
					<< " <== METADATA [ tot: " << total_size << " offset: "
					<< offset << " size: " << data_size << " ]\n";
#endif

				m_waiting_metadata_request = false;
				t->received_metadata(recv_buffer.begin, data_size
					, offset, total_size);
				m_metadata_progress = 0;
			}
			break;
		case 2: // have no data
			if (!packet_finished()) return;

			m_no_metadata = second_clock::universal_time();
			if (m_waiting_metadata_request)
				t->cancel_metadata_request(m_last_metadata_request);
			m_waiting_metadata_request = false;
			break;
		default:
			throw protocol_error("unknown metadata extension message: "
				+ boost::lexical_cast<std::string>(type));
		}
		
	}

	// -----------------------------
	// ------ PEER EXCHANGE --------
	// -----------------------------

	void bt_peer_connection::on_peer_exchange()
	{
		
	}

	bool bt_peer_connection::has_metadata() const
	{
		using namespace boost::posix_time;
		return second_clock::universal_time() - m_no_metadata > minutes(5);
	}

	bool bt_peer_connection::dispatch_message(int received)
	{
		INVARIANT_CHECK;

		assert(received > 0);

		// this means the connection has been closed already
		if (associated_torrent().expired()) return false;

		buffer::const_interval recv_buffer = receive_buffer();

		int packet_type = recv_buffer[0];
		if (packet_type < 0
			|| packet_type >= num_supported_messages
			|| m_message_handler[packet_type] == 0)
		{
			throw protocol_error("unknown message id: "
				+ boost::lexical_cast<std::string>(packet_type)
				+ " size: " + boost::lexical_cast<std::string>(packet_size()));
		}

		assert(m_message_handler[packet_type] != 0);

		// call the correct handler for this packet type
		(this->*m_message_handler[packet_type])(received);

		if (!packet_finished()) return false;

		return true;
	}

	void bt_peer_connection::write_keepalive()
	{
		INVARIANT_CHECK;

		char buf[] = {0,0,0,0};
		send_buffer(buf, buf + sizeof(buf));
	}

	void bt_peer_connection::write_cancel(peer_request const& r)
	{
		INVARIANT_CHECK;

		assert(associated_torrent().lock()->valid_metadata());

		char buf[] = {0,0,0,13, msg_cancel};

		buffer::interval i = allocate_send_buffer(17);

		std::copy(buf, buf + 5, i.begin);
		i.begin += 5;

		// index
		detail::write_int32(r.piece, i.begin);
		// begin
		detail::write_int32(r.start, i.begin);
		// length
		detail::write_int32(r.length, i.begin);
		assert(i.begin == i.end);

		setup_send();
	}

	void bt_peer_connection::write_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		assert(associated_torrent().lock()->valid_metadata());

		char buf[] = {0,0,0,13, msg_request};

		buffer::interval i = allocate_send_buffer(17);

		std::copy(buf, buf + 5, i.begin);
		i.begin += 5;

		// index
		detail::write_int32(r.piece, i.begin);
		// begin
		detail::write_int32(r.start, i.begin);
		// length
		detail::write_int32(r.length, i.begin);
		assert(i.begin == i.end);

		setup_send();
	}

	void bt_peer_connection::write_metadata(std::pair<int, int> req)
	{
		assert(req.first >= 0);
		assert(req.second > 0);
		assert(req.second <= 256);
		assert(req.first + req.second <= 256);
		assert(!associated_torrent().expired());
		INVARIANT_CHECK;

		// abort if the peer doesn't support the metadata extension
		if (!supports_extension(extended_metadata_message)) return;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);

		if (t->valid_metadata())
		{
			std::pair<int, int> offset
				= req_to_offset(req, (int)t->metadata().size());

			buffer::interval i = allocate_send_buffer(15 + offset.second);

			// yes, we have metadata, send it
			detail::write_uint32(11 + offset.second, i.begin);
			detail::write_uint8(msg_extended, i.begin);
			detail::write_uint8(m_extension_messages[extended_metadata_message]
				, i.begin);
			// means 'data packet'
			detail::write_uint8(1, i.begin);
			detail::write_uint32((int)t->metadata().size(), i.begin);
			detail::write_uint32(offset.first, i.begin);
			std::vector<char> const& metadata = t->metadata();
			std::copy(metadata.begin() + offset.first
				, metadata.begin() + offset.first + offset.second, i.begin);
			i.begin += offset.second;
			assert(i.begin == i.end);
		}
		else
		{
			buffer::interval i = allocate_send_buffer(4 + 3);
			// we don't have the metadata, reply with
			// don't have-message
			detail::write_uint32(1 + 2, i.begin);
			detail::write_uint8(msg_extended, i.begin);
			detail::write_uint8(m_extension_messages[extended_metadata_message]
				, i.begin);
			// means 'have no data'
			detail::write_uint8(2, i.begin);
			assert(i.begin == i.end);
		}
		setup_send();
	}

	void bt_peer_connection::write_metadata_request(std::pair<int, int> req)
	{
		assert(req.first >= 0);
		assert(req.second > 0);
		assert(req.first + req.second <= 256);
		assert(!associated_torrent().expired());
		assert(!associated_torrent().lock()->valid_metadata());
		INVARIANT_CHECK;

		int start = req.first;
		int size = req.second;

		// abort if the peer doesn't support the metadata extension
		if (!supports_extension(extended_metadata_message)) return;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> METADATA_REQUEST [ start: " << req.first
			<< " size: " << req.second << " ]\n";
#endif

		buffer::interval i = allocate_send_buffer(9);

		detail::write_uint32(1 + 1 + 3, i.begin);
		detail::write_uint8(msg_extended, i.begin);
		detail::write_uint8(m_extension_messages[extended_metadata_message]
			, i.begin);
		// means 'request data'
		detail::write_uint8(0, i.begin);
		detail::write_uint8(start, i.begin);
		detail::write_uint8(size - 1, i.begin);
		assert(i.begin == i.end);
		setup_send();
	}

	void bt_peer_connection::write_bitfield(std::vector<bool> const& bitfield)
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);

		if (t->num_pieces() == 0) return;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> BITFIELD ";

		for (int i = 0; i < (int)get_bitfield().size(); ++i)
		{
			if (bitfield[i]) (*m_logger) << "1";
			else (*m_logger) << "0";
		}
		(*m_logger) << "\n";
#endif
		const int packet_size = ((int)bitfield.size() + 7) / 8 + 5;
	
		buffer::interval i = allocate_send_buffer(packet_size);	

		detail::write_int32(packet_size - 4, i.begin);
		detail::write_uint8(msg_bitfield, i.begin);

		std::fill(i.begin, i.end, 0);
		for (int c = 0; c < (int)bitfield.size(); ++c)
		{
			if (bitfield[c])
				i.begin[c >> 3] |= 1 << (7 - (c & 7));
		}
		assert(i.end - i.begin == ((int)bitfield.size() + 7) / 8);
		setup_send();
	}

	void bt_peer_connection::write_extensions()
	{
		INVARIANT_CHECK;

#ifdef TORRENT_VERBOSE_LOGGING
		using namespace boost::posix_time;
		(*m_logger) << to_simple_string(second_clock::universal_time())
			<< " ==> EXTENSIONS\n";
#endif
		assert(m_supports_extensions);

		entry handshake(entry::dictionary_t);
		entry extension_list(entry::dictionary_t);

		for (int i = 1; i < num_supported_extensions; ++i)
		{
			// if this specific extension is disabled
			// just don't add it to the supported set
			if (!m_ses.extension_enabled(i)) continue;
			extension_list[extension_names[i]] = i;
		}

		handshake["m"] = extension_list;
		handshake["p"] = m_ses.listen_port();
		handshake["v"] = m_ses.settings().user_agent;
		std::string remote_address;
		std::back_insert_iterator<std::string> out(remote_address);
		detail::write_address(remote().address(), out);
		handshake["ip"] = remote_address;
		handshake["reqq"] = m_ses.settings().max_allowed_in_request_queue;

		std::vector<char> msg;
		bencode(std::back_inserter(msg), handshake);

		// make room for message
		buffer::interval i = allocate_send_buffer(6 + msg.size());
		
		// write the length of the message
		detail::write_int32((int)msg.size() + 2, i.begin);
		detail::write_uint8(msg_extended, i.begin);
		// signal handshake message
		detail::write_uint8(extended_handshake, i.begin);

		std::copy(msg.begin(), msg.end(), i.begin);
		i.begin += msg.size();
		assert(i.begin == i.end);

#ifdef TORRENT_VERBOSE_LOGGING
		std::stringstream ext;
		handshake.print(ext);
		(*m_logger) << "==> EXTENDED HANDSHAKE: \n" << ext.str();
#endif

		setup_send();
	}

	void bt_peer_connection::write_choke()
	{
		INVARIANT_CHECK;

		if (is_choked()) return;
		char msg[] = {0,0,0,1,msg_choke};
		send_buffer(msg, msg + sizeof(msg));
	}

	void bt_peer_connection::write_unchoke()
	{
		INVARIANT_CHECK;

		char msg[] = {0,0,0,1,msg_unchoke};
		send_buffer(msg, msg + sizeof(msg));
	}

	void bt_peer_connection::write_interested()
	{
		INVARIANT_CHECK;

		char msg[] = {0,0,0,1,msg_interested};
		send_buffer(msg, msg + sizeof(msg));
	}

	void bt_peer_connection::write_not_interested()
	{
		INVARIANT_CHECK;

		char msg[] = {0,0,0,1,msg_not_interested};
		send_buffer(msg, msg + sizeof(msg));
	}

	void bt_peer_connection::write_have(int index)
	{
		assert(associated_torrent().lock()->valid_metadata());
		assert(index >= 0);
		assert(index < associated_torrent().lock()->torrent_file().num_pieces());
		INVARIANT_CHECK;

		const int packet_size = 9;
		char msg[packet_size] = {0,0,0,5,msg_have};
		char* ptr = msg + 5;
		detail::write_int32(index, ptr);
		send_buffer(msg, msg + packet_size);
	}

	void bt_peer_connection::write_piece(peer_request const& r)
	{
		INVARIANT_CHECK;

		const int packet_size = 4 + 5 + 4 + r.length;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		assert(t);

		buffer::interval i = allocate_send_buffer(packet_size);
		
		detail::write_int32(packet_size-4, i.begin);
		detail::write_uint8(msg_piece, i.begin);
		detail::write_int32(r.piece, i.begin);
		detail::write_int32(r.start, i.begin);

		t->filesystem().read(
			i.begin, r.piece, r.start, r.length);

		assert(i.begin + r.length == i.end);

		m_payloads.push_back(range(send_buffer_size() - r.length, r.length));
		setup_send();
	}

	// --------------------------
	// RECEIVE DATA
	// --------------------------

	// throws exception when the client should be disconnected
	void bt_peer_connection::on_receive(const asio::error& error
		, std::size_t bytes_transferred)
	{
		INVARIANT_CHECK;

		if (error) return;
	
		buffer::const_interval recv_buffer = receive_buffer();
	
		boost::shared_ptr<torrent> t = associated_torrent().lock();
	
		switch(m_state)
		{
		case read_protocol_length:
		{
			m_statistics.received_bytes(0, bytes_transferred);
			if (!packet_finished()) break;

			int packet_size = recv_buffer[0];

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " protocol length: " << packet_size << "\n";
#endif
			if (packet_size > 100 || packet_size <= 0)
			{
				std::stringstream s;
				s << "incorrect protocol length ("
					<< packet_size
					<< ") should be 19.";
				throw std::runtime_error(s.str());
			}
			m_state = read_protocol_string;
			reset_recv_buffer(packet_size);
		}
		break;

		case read_protocol_string:
		{
			m_statistics.received_bytes(0, bytes_transferred);
			if (!packet_finished()) break;

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " protocol: '" << std::string(recv_buffer.begin
				, recv_buffer.end) << "'\n";
#endif
			const char protocol_string[] = "BitTorrent protocol";
			if (!std::equal(recv_buffer.begin, recv_buffer.end
				, protocol_string))
			{
				const char cmd[] = "version";
				if (recv_buffer.end - recv_buffer.begin == 7 && std::equal(
					recv_buffer.begin, recv_buffer.end, cmd))
				{
#ifdef TORRENT_VERBOSE_LOGGING
					(*m_logger) << "sending libtorrent version\n";
#endif
					asio::write(*get_socket(), asio::buffer("libtorrent version " LIBTORRENT_VERSION "\n", 27));
					throw std::runtime_error("closing");
				}
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << "incorrect protocol name\n";
#endif
				std::stringstream s;
				s << "got invalid protocol name: '"
					<< std::string(recv_buffer.begin, recv_buffer.end)
					<< "'";
				throw std::runtime_error(s.str());
			}

			m_state = read_info_hash;
			reset_recv_buffer(28);
		}
		break;

		case read_info_hash:
		{
			m_statistics.received_bytes(0, bytes_transferred);
			if (!packet_finished()) break;

// MassaRoddel
#ifdef TORRENT_VERBOSE_LOGGING	
			for (int i=0; i < 8; ++i)
			{
				for (int j=0; j < 8; ++j)
				{
					if (recv_buffer[i] & (0x80 >> j)) (*m_logger) << "1";
					else (*m_logger) << "0";
				}
			}
			(*m_logger) << "\n";
			if (recv_buffer[7] & 0x01)
				(*m_logger) << "supports DHT port message\n";
			if (recv_buffer[7] & 0x02)
				(*m_logger) << "supports XBT peer exchange message\n";
			if (recv_buffer[5] & 0x10)
				(*m_logger) << "supports LT/uT extensions\n";
#endif

			if ((recv_buffer[5] & 0x10) && m_ses.extensions_enabled())
				m_supports_extensions = true;
			if (recv_buffer[7] & 0x01)
				m_supports_dht_port = true;

			// ok, now we have got enough of the handshake. Is this connection
			// attached to a torrent?
			if (!t)
			{
				// now, we have to see if there's a torrent with the
				// info_hash we got from the peer
				sha1_hash info_hash;
				std::copy(recv_buffer.begin + 8, recv_buffer.begin + 28
					, (char*)info_hash.begin());

				attach_to_torrent(info_hash);
				t = associated_torrent().lock();
				assert(t);

				assert(t->get_policy().has_connection(this));

				// yes, we found the torrent
				// reply with our handshake
				write_handshake();
				write_bitfield(t->pieces());
			}
			else
			{
				// verify info hash
				if (!std::equal(recv_buffer.begin + 8, recv_buffer.begin + 28
					, (const char*)t->torrent_file().info_hash().begin()))
				{
#ifdef TORRENT_VERBOSE_LOGGING
					(*m_logger) << " received invalid info_hash\n";
#endif
					throw std::runtime_error("invalid info-hash in handshake");
				}
			}

#ifndef TORRENT_DISABLE_DHT
			if (m_supports_dht_port && m_ses.m_dht)
				write_dht_port(m_ses.kad_settings().service_port);
#endif

			m_state = read_peer_id;
			reset_recv_buffer(20);
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " info_hash received\n";
#endif
		}
		break;

		case read_peer_id:
		{
			if (!t) return;
			m_statistics.received_bytes(0, bytes_transferred);
			if (!packet_finished()) break;
			assert(packet_size() == 20);

#ifdef TORRENT_VERBOSE_LOGGING
			{
				peer_id tmp;
				std::copy(recv_buffer.begin, recv_buffer.begin + 20, (char*)tmp.begin());
				std::stringstream s;
				s << "received peer_id: " << tmp << " client: " << identify_client(tmp) << "\n";
				s << "as ascii: ";
				for (peer_id::iterator i = tmp.begin(); i != tmp.end(); ++i)
				{
					if (std::isprint(*i)) s << *i;
					else s << ".";
				}
				s << "\n";
				(*m_logger) << s.str();
			}
#endif
			peer_id pid;
			std::copy(recv_buffer.begin, recv_buffer.begin + 20, (char*)pid.begin());
			set_pid(pid);
			
			m_client_version = identify_client(pid);
			boost::optional<fingerprint> f = client_fingerprint(pid);
			if (f && std::equal(f->name, f->name + 2, "BC"))
			{
				// if this is a bitcomet client, lower the request queue size limit
				if (m_max_out_request_queue > 50) m_max_out_request_queue = 50;
			}

			// disconnect if the peer has the same peer-id as ourself
			// since it most likely is ourself then
			if (pid == m_ses.get_peer_id())
				throw std::runtime_error("closing connection to ourself");

			if (m_supports_extensions) write_extensions();
/*
			if (!m_active)
			{
				m_attached_to_torrent = true;
				assert(m_torrent->get_policy().has_connection(this));
			}
*/
			m_state = read_packet_size;
			reset_recv_buffer(4);
		}
		break;

		case read_packet_size:
		{
			if (!t) return;
			m_statistics.received_bytes(0, bytes_transferred);
			if (!packet_finished()) break;

			const char* ptr = recv_buffer.begin;
			int packet_size = detail::read_int32(ptr);

			// don't accept packets larger than 1 MB
			if (packet_size > 1024*1024 || packet_size < 0)
			{
				// packet too large
				throw std::runtime_error("packet > 1 MB ("
					+ boost::lexical_cast<std::string>(
					(unsigned int)packet_size) + " bytes)");
			}
					
			if (packet_size == 0)
			{
				incoming_keepalive();
				// keepalive message
				m_state = read_packet_size;
				reset_recv_buffer(4);
			}
			else
			{
				m_state = read_packet;
				reset_recv_buffer(packet_size);
			}
		}
		break;

		case read_packet:
		{
			if (!t) return;
			if (dispatch_message(bytes_transferred))
			{
				m_state = read_packet_size;
				reset_recv_buffer(4);
			}
		}
		break;

		}
	}

	// --------------------------
	// SEND DATA
	// --------------------------

	// throws exception when the client should be disconnected
	void bt_peer_connection::on_sent(asio::error const& error
		, std::size_t bytes_transferred)
	{
		INVARIANT_CHECK;

		if (error) return;

		// manage the payload markers
		int amount_payload = 0;
		if (!m_payloads.empty())
		{
			for (std::deque<range>::iterator i = m_payloads.begin();
				i != m_payloads.end(); ++i)
			{
				i->start -= bytes_transferred;
				if (i->start < 0)
				{
					if (i->start + i->length <= 0)
					{
						amount_payload += i->length;
					}
					else
					{
						amount_payload += -i->start;
						i->length -= -i->start;
						i->start = 0;
					}
				}
			}
		}

		// TODO: move the erasing into the loop above
		// remove all payload ranges that has been sent
		m_payloads.erase(
			std::remove_if(m_payloads.begin(), m_payloads.end(), range_below_zero)
			, m_payloads.end());

		assert(amount_payload <= (int)bytes_transferred);
		m_statistics.sent_bytes(amount_payload, bytes_transferred - amount_payload);
	}


	void bt_peer_connection::on_tick()
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		if (!t) return;

		// if we don't have any metadata, and this peer
		// supports the request metadata extension
		// and we aren't currently waiting for a request
		// reply. Then, send a request for some metadata.
		if (!t->valid_metadata()
			&& supports_extension(extended_metadata_message)
			&& !m_waiting_metadata_request
			&& has_metadata())
		{
			m_last_metadata_request = t->metadata_request();
			write_metadata_request(m_last_metadata_request);
			m_waiting_metadata_request = true;
			m_metadata_request = second_clock::universal_time();
		}
	}
	
#ifndef NDEBUG
	void bt_peer_connection::check_invariant() const
	{
		if (!m_in_constructor)
			peer_connection::check_invariant();

		if (!m_payloads.empty())
		{
			for (std::deque<range>::const_iterator i = m_payloads.begin();
				i != m_payloads.end() - 1; ++i)
			{
				assert(i->start + i->length <= (i+1)->start);
			}
		}
	}
#endif

}

