/*

Copyright (c) 2003 - 2006, Arvid Norberg
Copyright (c) 2007, Arvid Norberg, Un Shyam
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
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/broadcast_socket.hpp"

#ifndef TORRENT_DISABLE_ENCRYPTION
#include "libtorrent/pe_crypto.hpp"
#include "libtorrent/hasher.hpp"
#endif

using boost::bind;
using boost::shared_ptr;
using libtorrent::aux::session_impl;

namespace libtorrent
{
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
		0, 0, 0,
		// FAST extension messages
		&bt_peer_connection::on_suggest_piece,
		&bt_peer_connection::on_have_all,
		&bt_peer_connection::on_have_none,
		&bt_peer_connection::on_reject_request,
		&bt_peer_connection::on_allowed_fast,
		0, 0,
		&bt_peer_connection::on_extended
	};


	bt_peer_connection::bt_peer_connection(
		session_impl& ses
		, boost::weak_ptr<torrent> tor
		, shared_ptr<socket_type> s
		, tcp::endpoint const& remote
		, policy::peer* peerinfo)
		: peer_connection(ses, tor, s, remote
			, peerinfo)
		, m_state(read_protocol_identifier)
#ifndef TORRENT_DISABLE_EXTENSIONS
		, m_supports_extensions(false)
#endif
		, m_supports_dht_port(false)
		, m_supports_fast(false)
#ifndef TORRENT_DISABLE_ENCRYPTION
		, m_encrypted(false)
		, m_rc4_encrypted(false)
		, m_sync_bytes_read(0)
		, m_enc_send_buffer(0, 0)
#endif
#ifdef TORRENT_DEBUG
		, m_sent_bitfield(false)
		, m_in_constructor(true)
		, m_sent_handshake(false)
#endif
	{
#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "*** bt_peer_connection\n";
#endif

#ifdef TORRENT_DEBUG
		m_in_constructor = false;
		m_encrypted_bytes = 0;
#endif
	}

	bt_peer_connection::bt_peer_connection(
		session_impl& ses
		, boost::shared_ptr<socket_type> s
		, tcp::endpoint const& remote
		, policy::peer* peerinfo)
		: peer_connection(ses, s, remote, peerinfo)
		, m_state(read_protocol_identifier)
#ifndef TORRENT_DISABLE_EXTENSIONS
		, m_supports_extensions(false)
#endif
		, m_supports_dht_port(false)
		, m_supports_fast(false)
#ifndef TORRENT_DISABLE_ENCRYPTION
		, m_encrypted(false)
		, m_rc4_encrypted(false)
		, m_sync_bytes_read(0)
		, m_enc_send_buffer(0, 0)
#endif		
#ifdef TORRENT_DEBUG
		, m_sent_bitfield(false)
		, m_in_constructor(true)
		, m_sent_handshake(false)
#endif
	{

		// we are not attached to any torrent yet.
		// we have to wait for the handshake to see
		// which torrent the connector want's to connect to


		// upload bandwidth will only be given to connections
		// that are part of a torrent. Since this is an incoming
		// connection, we have to give it some initial bandwidth
		// to send the handshake.
#ifndef TORRENT_DISABLE_ENCRYPTION
		m_bandwidth_limit[download_channel].assign(2048);
		m_bandwidth_limit[upload_channel].assign(2048);
#else
		m_bandwidth_limit[download_channel].assign(80);
		m_bandwidth_limit[upload_channel].assign(80);
#endif

#ifdef TORRENT_DEBUG
		m_in_constructor = false;
		m_encrypted_bytes = 0;
#endif
	}

	void bt_peer_connection::start()
	{
		peer_connection::start();
		
		// start in the state where we are trying to read the
		// handshake from the other side
		reset_recv_buffer(20);
		setup_receive();
	}

	bt_peer_connection::~bt_peer_connection()
	{
	}

	void bt_peer_connection::on_connected()
	{
#ifndef TORRENT_DISABLE_ENCRYPTION
		
		pe_settings::enc_policy const& out_enc_policy = m_ses.get_pe_settings().out_enc_policy;

		if (out_enc_policy == pe_settings::forced)
		{
			write_pe1_2_dhkey();
			if (is_disconnecting()) return;

			m_state = read_pe_dhkey;
			reset_recv_buffer(dh_key_len);
			setup_receive();
		}
		else if (out_enc_policy == pe_settings::enabled)
		{
			TORRENT_ASSERT(peer_info_struct());

			policy::peer* pi = peer_info_struct();
			if (pi->pe_support == true)
			{
				// toggle encryption support flag, toggled back to
				// true if encrypted portion of the handshake
				// completes correctly
				pi->pe_support = false;

				// if this fails, we need to reconnect
				// fast.
				fast_reconnect(true);

				write_pe1_2_dhkey();
				if (is_disconnecting()) return;
				m_state = read_pe_dhkey;
				reset_recv_buffer(dh_key_len);
				setup_receive();
			}
			else // pi->pe_support == false
			{
				// toggled back to false if standard handshake
				// completes correctly (without encryption)
				pi->pe_support = true;

				write_handshake();
				reset_recv_buffer(20);
				setup_receive();
			}
		}
		else if (out_enc_policy == pe_settings::disabled)
#endif
		{
			write_handshake();
			
			// start in the state where we are trying to read the
			// handshake from the other side
			reset_recv_buffer(20);
			setup_receive();
		}
	}
	
	void bt_peer_connection::on_metadata()
	{
		// connections that are still in the handshake
		// will send their bitfield when the handshake
		// is done
		if (m_state < read_packet_size) return;
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		write_bitfield();
#ifndef TORRENT_DISABLE_DHT
		if (m_supports_dht_port && m_ses.m_dht)
			write_dht_port(m_ses.get_dht_settings().service_port);
#endif
	}

	void bt_peer_connection::write_dht_port(int listen_port)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);
		
#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " ==> DHT_PORT [ " << listen_port << " ]\n";
#endif
		char msg[] = {0,0,0,3, msg_dht_port, 0, 0};
		char* ptr = msg + 5;
		detail::write_uint16(listen_port, ptr);
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_have_all()
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(m_sent_handshake && !m_sent_bitfield);
#ifdef TORRENT_DEBUG
		m_sent_bitfield = true;
#endif
#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " ==> HAVE_ALL\n";
#endif
		char msg[] = {0,0,0,1, msg_have_all};
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_have_none()
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(m_sent_handshake && !m_sent_bitfield);
#ifdef TORRENT_DEBUG
		m_sent_bitfield = true;
#endif
#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string()
			<< " ==> HAVE_NONE\n";
#endif
		char msg[] = {0,0,0,1, msg_have_none};
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_reject_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		if (!m_supports_fast) return;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);
		TORRENT_ASSERT(associated_torrent().lock()->valid_metadata());

		char msg[] = {0,0,0,13, msg_reject_request,0,0,0,0, 0,0,0,0, 0,0,0,0};
		char* ptr = msg + 5;
		detail::write_int32(r.piece, ptr); // index
		detail::write_int32(r.start, ptr); // begin
		detail::write_int32(r.length, ptr); // length
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_allow_fast(int piece)
	{
		INVARIANT_CHECK;

		if (!m_supports_fast) return;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);
		TORRENT_ASSERT(associated_torrent().lock()->valid_metadata());

		char msg[] = {0,0,0,5, msg_allowed_fast, 0, 0, 0, 0};
		char* ptr = msg + 5;
		detail::write_int32(piece, ptr);
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::get_specific_peer_info(peer_info& p) const
	{
		TORRENT_ASSERT(!associated_torrent().expired());

		if (is_interesting()) p.flags |= peer_info::interesting;
		if (is_choked()) p.flags |= peer_info::choked;
		if (is_peer_interested()) p.flags |= peer_info::remote_interested;
		if (has_peer_choked()) p.flags |= peer_info::remote_choked;
		if (support_extensions()) p.flags |= peer_info::supports_extensions;
		if (is_local()) p.flags |= peer_info::local_connection;

#ifndef TORRENT_DISABLE_ENCRYPTION
		if (m_encrypted)
		{
			m_rc4_encrypted ? 
				p.flags |= peer_info::rc4_encrypted :
				p.flags |= peer_info::plaintext_encrypted;
		}
#endif

		if (!is_connecting() && in_handshake())
			p.flags |= peer_info::handshake;
		if (is_connecting() && !is_queued()) p.flags |= peer_info::connecting;
		if (is_queued()) p.flags |= peer_info::queued;

		p.client = m_client_version;
		p.connection_type = peer_info::standard_bittorrent;

	}
	
	bool bt_peer_connection::in_handshake() const
	{
		return m_state < read_packet_size;
	}

#ifndef TORRENT_DISABLE_ENCRYPTION

	void bt_peer_connection::write_pe1_2_dhkey()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_encrypted);
		TORRENT_ASSERT(!m_rc4_encrypted);
		TORRENT_ASSERT(!m_dh_key_exchange.get());
		TORRENT_ASSERT(!m_sent_handshake);

#ifdef TORRENT_VERBOSE_LOGGING
		if (is_local())
			(*m_logger) << " initiating encrypted handshake\n";
#endif

		m_dh_key_exchange.reset(new (std::nothrow) dh_key_exchange);
		if (!m_dh_key_exchange || !m_dh_key_exchange->good())
		{
			disconnect("out of memory");
			return;
		}

		int pad_size = std::rand() % 512;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << " pad size: " << pad_size << "\n";
#endif

		buffer::interval send_buf = allocate_send_buffer(dh_key_len + pad_size);
		if (send_buf.begin == 0)
		{
			disconnect("out of memory");
			return;
		}

		std::copy(m_dh_key_exchange->get_local_key(),
			m_dh_key_exchange->get_local_key() + dh_key_len,
			send_buf.begin);

		std::generate(send_buf.begin + dh_key_len, send_buf.end, std::rand);
#ifdef TORRENT_DEBUG
		m_encrypted_bytes += send_buf.left();
		TORRENT_ASSERT(m_encrypted_bytes <= send_buffer_size());
#endif
		setup_send();

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << " sent DH key\n";
#endif
	}

	void bt_peer_connection::write_pe3_sync()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_encrypted);
		TORRENT_ASSERT(!m_rc4_encrypted);
		TORRENT_ASSERT(is_local());
		TORRENT_ASSERT(!m_sent_handshake);
		
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		
		hasher h;
		sha1_hash const& info_hash = t->torrent_file().info_hash();
		char const* const secret = m_dh_key_exchange->get_secret();

		int pad_size = rand() % 512;

		TORRENT_ASSERT(send_buffer_size() == m_encrypted_bytes);

		// synchash,skeyhash,vc,crypto_provide,len(pad),pad,len(ia)
		buffer::interval send_buf = 
			allocate_send_buffer(20 + 20 + 8 + 4 + 2 + pad_size + 2);
		if (send_buf.begin == 0) return; // out of memory

		// sync hash (hash('req1',S))
		h.reset();
		h.update("req1",4);
		h.update(secret, dh_key_len);
		sha1_hash sync_hash = h.final();

		std::copy(sync_hash.begin(), sync_hash.end(), send_buf.begin);
		send_buf.begin += 20;

		// stream key obfuscated hash [ hash('req2',SKEY) xor hash('req3',S) ]
		h.reset();
		h.update("req2",4);
		h.update((const char*)info_hash.begin(), 20);
		sha1_hash streamkey_hash = h.final();

		h.reset();
		h.update("req3",4);
		h.update(secret, dh_key_len);
		sha1_hash obfsc_hash = h.final();
		obfsc_hash ^= streamkey_hash;

		std::copy(obfsc_hash.begin(), obfsc_hash.end(), send_buf.begin);
		send_buf.begin += 20;

		// Discard DH key exchange data, setup RC4 keys
		init_pe_RC4_handler(secret, info_hash);
		m_dh_key_exchange.reset(); // secret should be invalid at this point
	
		// write the verification constant and crypto field
		TORRENT_ASSERT(send_buf.left() == 8 + 4 + 2 + pad_size + 2);
		int encrypt_size = send_buf.left();

		int crypto_provide = 0;
		pe_settings::enc_level const& allowed_enc_level = m_ses.get_pe_settings().allowed_enc_level;

		if (allowed_enc_level == pe_settings::both) 
			crypto_provide = 0x03;
		else if (allowed_enc_level == pe_settings::rc4) 
			crypto_provide = 0x02;
		else if (allowed_enc_level == pe_settings::plaintext)
			crypto_provide = 0x01;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << " crypto provide : [ ";
		if (allowed_enc_level == pe_settings::both)
			(*m_logger) << "plaintext rc4 ]\n";
		else if (allowed_enc_level == pe_settings::rc4)
			(*m_logger) << "rc4 ]\n";
		else if (allowed_enc_level == pe_settings::plaintext)
			(*m_logger) << "plaintext ]\n";
#endif

		write_pe_vc_cryptofield(send_buf, crypto_provide, pad_size);
		m_RC4_handler->encrypt(send_buf.end - encrypt_size, encrypt_size);
#ifdef TORRENT_DEBUG
		const int packet_size = 20 + 20 + 8 + 4 + 2 + pad_size + 2;
		TORRENT_ASSERT(send_buffer_size() - packet_size == m_encrypted_bytes);
		m_encrypted_bytes += packet_size;
		TORRENT_ASSERT(m_encrypted_bytes == send_buffer_size());
#endif

		TORRENT_ASSERT(send_buf.begin == send_buf.end);
		setup_send();
	}

	void bt_peer_connection::write_pe4_sync(int crypto_select)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!is_local());
		TORRENT_ASSERT(!m_encrypted);
		TORRENT_ASSERT(!m_rc4_encrypted);
		TORRENT_ASSERT(crypto_select == 0x02 || crypto_select == 0x01);
		TORRENT_ASSERT(!m_sent_handshake);

		int pad_size =rand() % 512;

		TORRENT_ASSERT(send_buffer_size() == m_encrypted_bytes);

		const int buf_size = 8 + 4 + 2 + pad_size;
		buffer::interval send_buf = allocate_send_buffer(buf_size);
		if (send_buf.begin == 0) return; // out of memory
		write_pe_vc_cryptofield(send_buf, crypto_select, pad_size);

		m_RC4_handler->encrypt(send_buf.end - buf_size, buf_size);
		TORRENT_ASSERT(send_buffer_size() - buf_size == m_encrypted_bytes);
#ifdef TORRENT_DEBUG
		m_encrypted_bytes += buf_size;
		TORRENT_ASSERT(m_encrypted_bytes <= send_buffer_size());
#endif
		setup_send();

		// encryption method has been negotiated
		if (crypto_select == 0x02) 
			m_rc4_encrypted = true;
		else // 0x01
			m_rc4_encrypted = false;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << " crypto select : [ ";
		if (crypto_select == 0x01)
			(*m_logger) << "plaintext ]\n";
		else
			(*m_logger) << "rc4 ]\n";
#endif
	}

 	void bt_peer_connection::write_pe_vc_cryptofield(buffer::interval& write_buf
		, int crypto_field, int pad_size)
 	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(crypto_field <= 0x03 && crypto_field > 0);
		// vc,crypto_field,len(pad),pad, (len(ia))
		TORRENT_ASSERT( (write_buf.left() == 8+4+2+pad_size+2 && is_local()) ||
				(write_buf.left() == 8+4+2+pad_size   && !is_local()) );
		TORRENT_ASSERT(!m_sent_handshake);

		// encrypt(vc, crypto_provide/select, len(Pad), len(IA))
		// len(pad) is zero for now, len(IA) only for outgoing connections
		
		// vc
		std::fill(write_buf.begin, write_buf.begin + 8, 0);
		write_buf.begin += 8;

		detail::write_uint32(crypto_field, write_buf.begin);
		detail::write_uint16(pad_size, write_buf.begin); // len (pad)

		// fill pad with zeroes
		std::generate(write_buf.begin, write_buf.begin + pad_size, &std::rand);
		write_buf.begin += pad_size;

		// append len(ia) if we are initiating
		if (is_local())
			detail::write_uint16(handshake_len, write_buf.begin); // len(IA)
		
		TORRENT_ASSERT(write_buf.begin == write_buf.end);
 	}

	void bt_peer_connection::init_pe_RC4_handler(char const* secret, sha1_hash const& stream_key)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(secret);
		
		hasher h;
		static const char keyA[] = "keyA";
		static const char keyB[] = "keyB";

		// encryption rc4 longkeys
		// outgoing connection : hash ('keyA',S,SKEY)
		// incoming connection : hash ('keyB',S,SKEY)
		
		is_local() ? h.update(keyA, 4) : h.update(keyB, 4);
		h.update(secret, dh_key_len);
		h.update((char const*)stream_key.begin(), 20);
		const sha1_hash local_key = h.final();

		h.reset();

		// decryption rc4 longkeys
		// outgoing connection : hash ('keyB',S,SKEY)
		// incoming connection : hash ('keyA',S,SKEY)
		
		is_local() ? h.update(keyB, 4) : h.update(keyA, 4);
		h.update(secret, dh_key_len);
		h.update((char const*)stream_key.begin(), 20);
		const sha1_hash remote_key = h.final();
		
		TORRENT_ASSERT(!m_RC4_handler.get());
		m_RC4_handler.reset(new (std::nothrow) RC4_handler(local_key, remote_key));
		if (!m_RC4_handler)
		{
			disconnect("no memory");
			return;
		}

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << " computed RC4 keys\n";
#endif
	}

	void bt_peer_connection::send_buffer(char const* buf, int size, int flags)
	{
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(size > 0);
		
		encrypt_pending_buffer();

#ifndef TORRENT_DISABLE_ENCRYPTION
		if (m_encrypted && m_rc4_encrypted)
		{
			TORRENT_ASSERT(send_buffer_size() == m_encrypted_bytes);
			m_RC4_handler->encrypt(const_cast<char*>(buf), size);
#ifdef TORRENT_DEBUG
			m_encrypted_bytes += size;
#endif
		}
#endif
		
		peer_connection::send_buffer(buf, size, flags);
	}

	buffer::interval bt_peer_connection::allocate_send_buffer(int size)
	{
#ifndef TORRENT_DISABLE_ENCRYPTION
		encrypt_pending_buffer();
		if (m_encrypted && m_rc4_encrypted)
		{
			TORRENT_ASSERT(m_enc_send_buffer.left() == 0);
			m_enc_send_buffer = peer_connection::allocate_send_buffer(size);
			return m_enc_send_buffer;
		}
		else
#endif
		{
			buffer::interval i = peer_connection::allocate_send_buffer(size);
			return i;
		}
	}
	
#ifndef TORRENT_DISABLE_ENCRYPTION
	void bt_peer_connection::encrypt_pending_buffer()
	{
 		if (m_encrypted && m_rc4_encrypted && m_enc_send_buffer.left())
		{
			TORRENT_ASSERT(m_enc_send_buffer.begin);
			TORRENT_ASSERT(m_enc_send_buffer.end);
			TORRENT_ASSERT(m_RC4_handler);
			TORRENT_ASSERT(send_buffer_size() - m_enc_send_buffer.left() == m_encrypted_bytes);
#ifdef TORRENT_DEBUG
			m_encrypted_bytes += m_enc_send_buffer.left();
			TORRENT_ASSERT(m_encrypted_bytes <= send_buffer_size());
#endif
			
 			m_RC4_handler->encrypt(m_enc_send_buffer.begin, m_enc_send_buffer.left());
			m_enc_send_buffer.end = m_enc_send_buffer.begin;
		}
	}
#endif

	void bt_peer_connection::setup_send()
	{
#ifndef TORRENT_DISABLE_ENCRYPTION
		encrypt_pending_buffer();
		TORRENT_ASSERT(!m_encrypted || !m_rc4_encrypted || m_encrypted_bytes == send_buffer_size());
#endif
		peer_connection::setup_send();
	}

	int bt_peer_connection::get_syncoffset(char const* src, int src_size,
		char const* target, int target_size) const
	{
		TORRENT_ASSERT(target_size >= src_size);
		TORRENT_ASSERT(src_size > 0);
		TORRENT_ASSERT(src);
		TORRENT_ASSERT(target);

		int traverse_limit = target_size - src_size;

		// TODO: this could be optimized using knuth morris pratt
		for (int i = 0; i < traverse_limit; ++i)
		{
			char const* target_ptr = target + i;
			if (std::equal(src, src+src_size, target_ptr))
				return i;
		}

//	    // Partial sync
// 		for (int i = 0; i < target_size; ++i)
// 		{
// 			// first is iterator in src[] at which mismatch occurs
// 			// second is iterator in target[] at which mismatch occurs
// 			std::pair<const char*, const char*> ret;
// 			int src_sync_size;
//  			if (i > traverse_limit) // partial sync test
//  			{
//  				ret = std::mismatch(src, src + src_size - (i - traverse_limit), &target[i]);
//  				src_sync_size = ret.first - src;
//  				if (src_sync_size == (src_size - (i - traverse_limit)))
//  					return i;
//  			}
//  			else // complete sync test
// 			{
// 				ret = std::mismatch(src, src + src_size, &target[i]);
// 				src_sync_size = ret.first - src;
// 				if (src_sync_size == src_size)
// 					return i;
// 			}
// 		}

        // no complete sync
		return -1;
	}
#endif // #ifndef TORRENT_DISABLE_ENCRYPTION
	
	void bt_peer_connection::write_handshake()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_sent_handshake);
#ifdef TORRENT_DEBUG
		m_sent_handshake = true;
#endif

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		// add handshake to the send buffer
		const char version_string[] = "BitTorrent protocol";
		const int string_len = sizeof(version_string)-1;

		buffer::interval i = allocate_send_buffer(1 + string_len + 8 + 20 + 20);
		if (i.begin == 0) return; // out of memory
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
		std::fill(i.begin, i.begin + 8, 0);

#ifndef TORRENT_DISABLE_DHT
		// indicate that we support the DHT messages
		*(i.begin + 7) |= 0x01;
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		// we support extensions
		*(i.begin + 5) |= 0x10;
#endif

		// we support FAST extension
		*(i.begin + 7) |= 0x04;

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
		TORRENT_ASSERT(i.begin == i.end);

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " ==> HANDSHAKE\n";
#endif
		setup_send();
	}

	boost::optional<piece_block_progress> bt_peer_connection::downloading_piece_progress() const
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		buffer::const_interval recv_buffer = receive_buffer();
		// are we currently receiving a 'piece' message?
		if (m_state != read_packet
			|| recv_buffer.left() < 9
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
		p.bytes_downloaded = recv_buffer.left() - 9;
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
		(*m_logger) << time_now_string() << " <== KEEPALIVE\n";
#endif
		incoming_keepalive();
	}

	// -----------------------------
	// ----------- CHOKE -----------
	// -----------------------------

	void bt_peer_connection::on_choke(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received > 0);
		if (packet_size() != 1)
		{
			disconnect("'choke' message size != 1", 2);
			return;
		}
		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		incoming_choke();
		if (is_disconnecting()) return;
		if (!m_supports_fast)
		{
			boost::shared_ptr<torrent> t = associated_torrent().lock();
			TORRENT_ASSERT(t);
			while (!download_queue().empty())
			{
				piece_block const& b = download_queue().front().block;
				peer_request r;
				r.piece = b.piece_index;
				r.start = b.block_index * t->block_size();
				r.length = t->block_size();
				incoming_reject_request(r);
			}
		}
	}

	// -----------------------------
	// ---------- UNCHOKE ----------
	// -----------------------------

	void bt_peer_connection::on_unchoke(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received > 0);
		if (packet_size() != 1)
		{
			disconnect("'unchoke' message size != 1", 2);
			return;
		}
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

		TORRENT_ASSERT(received > 0);
		if (packet_size() != 1)
		{
			disconnect("'interested' message size != 1", 2);
			return;
		}
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

		TORRENT_ASSERT(received > 0);
		if (packet_size() != 1)
		{
			disconnect("'not interested' message size != 1", 2);
			return;
		}
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

		TORRENT_ASSERT(received > 0);
		if (packet_size() != 5)
		{
			disconnect("'have' message size != 5", 2);
			return;
		}
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

		TORRENT_ASSERT(received > 0);

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		// if we don't have the metedata, we cannot
		// verify the bitfield size
		if (t->valid_metadata()
			&& packet_size() - 1 != (t->torrent_file().num_pieces() + 7) / 8)
		{
			std::stringstream msg;
			msg << "got bitfield with invalid size: " << (packet_size() - 1)
				<< " bytes. expected: " << ((t->torrent_file().num_pieces() + 7) / 8)
				<< " bytes";
			disconnect(msg.str().c_str(), 2);
			return;
		}

		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();

		bitfield bits;
		bits.borrow_bytes((char*)recv_buffer.begin + 1
			, t->valid_metadata()?get_bitfield().size():(packet_size()-1)*8);
		
		incoming_bitfield(bits);
	}

	// -----------------------------
	// ---------- REQUEST ----------
	// -----------------------------

	void bt_peer_connection::on_request(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received > 0);
		if (packet_size() != 13)
		{
			disconnect("'request' message size != 13", 2);
			return;
		}
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

		TORRENT_ASSERT(received > 0);
		
		buffer::const_interval recv_buffer = receive_buffer();
		int recv_pos = recv_buffer.end - recv_buffer.begin;

		if (recv_pos == 1)
		{
			TORRENT_ASSERT(!has_disk_receive_buffer());
			if (!allocate_disk_receive_buffer(packet_size() - 9))
				return;
		}
		TORRENT_ASSERT(has_disk_receive_buffer() || packet_size() == 9);

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
			TORRENT_ASSERT(recv_pos - received < 9);
			TORRENT_ASSERT(recv_pos > 9);
			TORRENT_ASSERT(9 - (recv_pos - received) <= 9);
			m_statistics.received_bytes(
				recv_pos - 9
				, 9 - (recv_pos - received));
		}

		incoming_piece_fragment();
		if (is_disconnecting()) return;
		if (!packet_finished()) return;

		const char* ptr = recv_buffer.begin + 1;
		peer_request p;
		p.piece = detail::read_int32(ptr);
		p.start = detail::read_int32(ptr);
		p.length = packet_size() - 9;

		disk_buffer_holder holder(m_ses, release_disk_receive_buffer());
		incoming_piece(p, holder);
	}

	// -----------------------------
	// ---------- CANCEL -----------
	// -----------------------------

	void bt_peer_connection::on_cancel(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received > 0);
		if (packet_size() != 13)
		{
			disconnect("'cancel' message size != 13", 2);
			return;
		}
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

		TORRENT_ASSERT(received > 0);
		if (packet_size() != 3)
		{
			disconnect("'dht_port' message size != 3", 2);
			return;
		}
		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();

		const char* ptr = recv_buffer.begin + 1;
		int listen_port = detail::read_uint16(ptr);
		
		incoming_dht_port(listen_port);
	}

	void bt_peer_connection::on_suggest_piece(int received)
	{
		INVARIANT_CHECK;

		if (!m_supports_fast)
		{
			disconnect("got 'suggest_piece' without FAST excension support", 2);
			return;
		}

		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();

		const char* ptr = recv_buffer.begin + 1;
		int piece = detail::read_uint32(ptr);
		incoming_suggest(piece);
	}

	void bt_peer_connection::on_have_all(int received)
	{
		INVARIANT_CHECK;

		if (!m_supports_fast)
		{
			disconnect("got 'have_all' without FAST extension support", 2);
			return;
		}
		m_statistics.received_bytes(0, received);
		incoming_have_all();
	}

	void bt_peer_connection::on_have_none(int received)
	{
		INVARIANT_CHECK;

		if (!m_supports_fast)
		{
			disconnect("got 'have_none' without FAST extension support", 2);
			return;
		}
		m_statistics.received_bytes(0, received);
		incoming_have_none();
	}

	void bt_peer_connection::on_reject_request(int received)
	{
		INVARIANT_CHECK;

		if (!m_supports_fast)
		{
			disconnect("got 'reject_request' without FAST extension support", 2);
			return;
		}

		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();

		peer_request r;
		const char* ptr = recv_buffer.begin + 1;
		r.piece = detail::read_int32(ptr);
		r.start = detail::read_int32(ptr);
		r.length = detail::read_int32(ptr);
		
		incoming_reject_request(r);
	}

	void bt_peer_connection::on_allowed_fast(int received)
	{
		INVARIANT_CHECK;

		if (!m_supports_fast)
		{
			disconnect("got 'allowed_fast' without FAST extension support", 2);
			return;
		}

		m_statistics.received_bytes(0, received);
		if (!packet_finished()) return;
		buffer::const_interval recv_buffer = receive_buffer();
		const char* ptr = recv_buffer.begin + 1;
		int index = detail::read_int32(ptr);
		
		incoming_allowed_fast(index);
	}

	// -----------------------------
	// --------- EXTENDED ----------
	// -----------------------------

	void bt_peer_connection::on_extended(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received > 0);
		m_statistics.received_bytes(0, received);
		if (packet_size() < 2)
		{
			disconnect("'extended' message smaller than 2 bytes", 2);
			return;
		}

		if (associated_torrent().expired())
		{
			disconnect("'extended' message sent before proper handshake", 2);
			return;
		}

		buffer::const_interval recv_buffer = receive_buffer();
		if (recv_buffer.left() < 2) return;

		TORRENT_ASSERT(*recv_buffer.begin == msg_extended);
		++recv_buffer.begin;

		int extended_id = detail::read_uint8(recv_buffer.begin);

		if (extended_id == 0)
		{
			on_extended_handshake();
			return;
		}

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_extended(packet_size() - 2, extended_id
				, recv_buffer))
				return;
		}
#endif

		std::stringstream msg;
		msg << "unknown extended message id: " << extended_id;
		disconnect(msg.str().c_str(), 2);
		return;
	}

	void bt_peer_connection::on_extended_handshake()
	{
		if (!packet_finished()) return;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		buffer::const_interval recv_buffer = receive_buffer();

		lazy_entry root;
		lazy_bdecode(recv_buffer.begin + 2, recv_buffer.end, root);
		if (root.type() != lazy_entry::dict_t)
		{
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << "invalid extended handshake\n";
#endif
			return;
		}

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << "<== EXTENDED HANDSHAKE: \n" << root;
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin();
			!m_extensions.empty() && i != m_extensions.end();)
		{
			// a false return value means that the extension
			// isn't supported by the other end. So, it is removed.
			if (!(*i)->on_extension_handshake(root))
				i = m_extensions.erase(i);
			else
				++i;
		}
		if (is_disconnecting()) return;
#endif

		// there is supposed to be a remote listen port
		int listen_port = root.dict_find_int_value("p");
		if (listen_port > 0 && peer_info_struct() != 0)
		{
			t->get_policy().update_peer_port(listen_port
				, peer_info_struct(), peer_info::incoming);
			if (is_disconnecting()) return;
		}
		// there should be a version too
		// but where do we put that info?
		
		std::string client_info = root.dict_find_string_value("v");
		if (!client_info.empty()) m_client_version = client_info;

		int reqq = root.dict_find_int_value("reqq");
		if (reqq > 0) m_max_out_request_queue = reqq;

		if (root.dict_find_int_value("upload_only"))
			set_upload_only(true);

		std::string myip = root.dict_find_string_value("yourip");
		if (!myip.empty())
		{
			// TODO: don't trust this blindly
			if (myip.size() == address_v4::bytes_type::static_size)
			{
				address_v4::bytes_type bytes;
				std::copy(myip.begin(), myip.end(), bytes.begin());
				m_ses.set_external_address(address_v4(bytes));
			}
			else if (myip.size() == address_v6::bytes_type::static_size)
			{
				address_v6::bytes_type bytes;
				std::copy(myip.begin(), myip.end(), bytes.begin());
				address_v6 ipv6_address(bytes);
				if (ipv6_address.is_v4_mapped())
					m_ses.set_external_address(ipv6_address.to_v4());
				else
					m_ses.set_external_address(ipv6_address);
			}
		}

		// if we're finished and this peer is uploading only
		// disconnect it
		if (t->is_finished() && upload_only())
			disconnect("upload to upload connection, closing");
	}

	bool bt_peer_connection::dispatch_message(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received > 0);

		// this means the connection has been closed already
		if (associated_torrent().expired()) return false;

		buffer::const_interval recv_buffer = receive_buffer();

		TORRENT_ASSERT(recv_buffer.left() >= 1);
		int packet_type = recv_buffer[0];
		if (packet_type < 0
			|| packet_type >= num_supported_messages
			|| m_message_handler[packet_type] == 0)
		{
#ifndef TORRENT_DISABLE_EXTENSIONS
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				if ((*i)->on_unknown_message(packet_size(), packet_type
					, buffer::const_interval(recv_buffer.begin+1
					, recv_buffer.end)))
					return packet_finished();
			}
#endif

			std::stringstream msg;
			msg << "unknown message id: " << packet_type << " size: " << packet_size();
			disconnect(msg.str().c_str(), 2);
			return packet_finished();
		}

		TORRENT_ASSERT(m_message_handler[packet_type] != 0);

		// call the correct handler for this packet type
		(this->*m_message_handler[packet_type])(received);

		return packet_finished();
	}

	void bt_peer_connection::write_keepalive()
	{
		INVARIANT_CHECK;

		// Don't require the bitfield to have been sent at this point
		// the case where m_sent_bitfield may not be true is if the
		// torrent doesn't have any metadata, and a peer is timimg out.
		// then the keep-alive message will be sent before the bitfield
		// this is a violation to the original protocol, but necessary
		// for the metadata extension.
		TORRENT_ASSERT(m_sent_handshake);

		char msg[] = {0,0,0,0};
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_cancel(peer_request const& r)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);
		TORRENT_ASSERT(associated_torrent().lock()->valid_metadata());

		char msg[17] = {0,0,0,13, msg_cancel};
		char* ptr = msg + 5;
		detail::write_int32(r.piece, ptr); // index
		detail::write_int32(r.start, ptr); // begin
		detail::write_int32(r.length, ptr); // length
		send_buffer(msg, sizeof(msg));

		if (!m_supports_fast)
			incoming_reject_request(r);
	}

	void bt_peer_connection::write_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);
		TORRENT_ASSERT(associated_torrent().lock()->valid_metadata());

		char msg[17] = {0,0,0,13, msg_request};
		char* ptr = msg + 5;

		detail::write_int32(r.piece, ptr); // index
		detail::write_int32(r.start, ptr); // begin
		detail::write_int32(r.length, ptr); // length
		send_buffer(msg, sizeof(msg), message_type_request);
	}

	void bt_peer_connection::write_bitfield()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(m_sent_handshake && !m_sent_bitfield);
		TORRENT_ASSERT(t->valid_metadata());

		// in this case, have_all or have_none should be sent instead
		TORRENT_ASSERT(!m_supports_fast || !t->is_seed() || t->num_have() != 0);

		if (m_supports_fast && t->is_seed())
		{
			write_have_all();
			send_allowed_set();
			return;
		}
		else if (m_supports_fast && t->num_have() == 0)
		{
			write_have_none();
			send_allowed_set();
			return;
		}
		else if (t->num_have() == 0)
		{
			// don't send a bitfield if we don't have any pieces
#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string() << " *** NOT SENDING BITFIELD\n";
#endif
#ifdef TORRENT_DEBUG
			m_sent_bitfield = true;
#endif
			return;
		}
	
		int num_pieces = t->torrent_file().num_pieces();
		int lazy_pieces[50];
		int num_lazy_pieces = 0;
		int lazy_piece = 0;

		if (t->is_seed() && m_ses.settings().lazy_bitfields
#ifndef TORRENT_DISABLE_ENCRYPTION
			&& !m_encrypted
#endif
			)
		{
			num_lazy_pieces = (std::min)(50, num_pieces / 10);
			if (num_lazy_pieces < 1) num_lazy_pieces = 1;
			for (int i = 0; i < num_pieces; ++i)
			{
				if (rand() % (num_pieces - i) >= num_lazy_pieces - lazy_piece) continue;
				lazy_pieces[lazy_piece++] = i;
			}
			TORRENT_ASSERT(lazy_piece == num_lazy_pieces);
			lazy_piece = 0;
		}

		const int packet_size = (num_pieces + 7) / 8 + 5;
	
		buffer::interval i = allocate_send_buffer(packet_size);	
		if (i.begin == 0) return; // out of memory

		detail::write_int32(packet_size - 4, i.begin);
		detail::write_uint8(msg_bitfield, i.begin);

		if (t->is_seed())
		{
			memset(i.begin, 0xff, packet_size - 6);

			// Clear trailing bits
			unsigned char *p = ((unsigned char *)i.begin) + packet_size - 6;
			*p = (0xff << ((8 - (num_pieces & 7)) & 7)) & 0xff;
		}
		else
		{
			memset(i.begin, 0, packet_size - 5);
			piece_picker const& p = t->picker();
			int mask = 0x80;
			unsigned char* byte = (unsigned char*)i.begin;
			for (int i = 0; i < num_pieces; ++i)
			{
				if (p.have_piece(i)) *byte |= mask;
				mask >>= 1;
				if (mask == 0)
				{
					mask = 0x80;
					++byte;
				}
			}
		}
		for (int c = 0; c < num_lazy_pieces; ++c)
			i.begin[lazy_pieces[c] / 8] &= ~(0x80 >> (lazy_pieces[c] & 7));
		TORRENT_ASSERT(i.end - i.begin == (num_pieces + 7) / 8);

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " ==> BITFIELD ";

		std::stringstream bitfield_string;
		for (int k = 0; k < num_pieces; ++k)
		{
			if (i.begin[k / 8] & (0x80 >> (k % 8))) bitfield_string << "1";
			else bitfield_string << "0";
		}
		bitfield_string << "\n";
		(*m_logger) << bitfield_string.str();
#endif
#ifdef TORRENT_DEBUG
		m_sent_bitfield = true;
#endif

		setup_send();

		if (num_lazy_pieces > 0)
		{
			for (int i = 0; i < num_lazy_pieces; ++i)
			{
				write_have(lazy_pieces[i]);
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << time_now_string()
					<< " ==> HAVE    [ piece: " << lazy_pieces[i] << "]\n";
#endif
			}
		}

		if (m_supports_fast)
			send_allowed_set();
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void bt_peer_connection::write_extensions()
	{
		INVARIANT_CHECK;

#ifdef TORRENT_VERBOSE_LOGGING
		(*m_logger) << time_now_string() << " ==> EXTENSIONS\n";
#endif
		TORRENT_ASSERT(m_supports_extensions);
		TORRENT_ASSERT(m_sent_handshake);

		entry handshake(entry::dictionary_t);
		entry extension_list(entry::dictionary_t);

		handshake["m"] = extension_list;

		// only send the port in case we bade the connection
		// on incoming connections the other end already knows
		// our listen port
		if (is_local()) handshake["p"] = m_ses.listen_port();
		handshake["v"] = m_ses.settings().user_agent;
		std::string remote_address;
		std::back_insert_iterator<std::string> out(remote_address);
		detail::write_address(remote().address(), out);
		handshake["yourip"] = remote_address;
		handshake["reqq"] = m_ses.settings().max_allowed_in_request_queue;
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		if (t->is_finished() && (!m_ses.settings().lazy_bitfields
#ifndef TORRENT_DISABLE_ENCRYPTION
			|| m_encrypted
#endif
			))
			handshake["upload_only"] = 1;

		tcp::endpoint ep = m_ses.get_ipv6_interface();
		if (!is_any(ep.address()))
		{
			std::string ipv6_address;
			std::back_insert_iterator<std::string> out(ipv6_address);
			detail::write_address(ep.address(), out);
			handshake["ipv6"] = ipv6_address;
		}

		// loop backwards, to make the first extension be the last
		// to fill in the handshake (i.e. give the first extensions priority)
		for (extension_list_t::reverse_iterator i = m_extensions.rbegin()
			, end(m_extensions.rend()); i != end; ++i)
		{
			(*i)->add_handshake(handshake);
		}

		std::vector<char> msg;
		bencode(std::back_inserter(msg), handshake);

		// make room for message
		buffer::interval i = allocate_send_buffer(6 + msg.size());
		if (i.begin == 0) return; // out of memory
		
		// write the length of the message
		detail::write_int32((int)msg.size() + 2, i.begin);
		detail::write_uint8(msg_extended, i.begin);
		// signal handshake message
		detail::write_uint8(0, i.begin);

		std::copy(msg.begin(), msg.end(), i.begin);
		i.begin += msg.size();
		TORRENT_ASSERT(i.begin == i.end);

#ifdef TORRENT_VERBOSE_LOGGING
		std::stringstream ext;
		handshake.print(ext);
		(*m_logger) << "==> EXTENDED HANDSHAKE: \n" << ext.str();
#endif

		setup_send();
	}
#endif

	void bt_peer_connection::write_choke()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);

		if (is_choked()) return;
		char msg[] = {0,0,0,1,msg_choke};
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_unchoke()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);

		char msg[] = {0,0,0,1,msg_unchoke};
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_interested()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);

		char msg[] = {0,0,0,1,msg_interested};
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_not_interested()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);

		char msg[] = {0,0,0,1,msg_not_interested};
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_have(int index)
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(associated_torrent().lock()->valid_metadata());
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < associated_torrent().lock()->torrent_file().num_pieces());
		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);

		char msg[] = {0,0,0,5,msg_have,0,0,0,0};
		char* ptr = msg + 5;
		detail::write_int32(index, ptr);
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_piece(peer_request const& r, disk_buffer_holder& buffer)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		char msg[4 + 1 + 4 + 4];
		char* ptr = msg;
		TORRENT_ASSERT(r.length <= 16 * 1024);
		detail::write_int32(r.length + 1 + 4 + 4, ptr);
		detail::write_uint8(msg_piece, ptr);
		detail::write_int32(r.piece, ptr);
		detail::write_int32(r.start, ptr);
		send_buffer(msg, sizeof(msg));

		append_send_buffer(buffer.get(), r.length
			, boost::bind(&session_impl::free_disk_buffer
			, boost::ref(m_ses), _1));
		buffer.release();

		m_payloads.push_back(range(send_buffer_size() - r.length, r.length));
		setup_send();
	}

	namespace
	{
		struct match_peer_id
		{
			match_peer_id(peer_id const& id, peer_connection const* pc)
				: m_id(id), m_pc(pc)
			{ TORRENT_ASSERT(pc); }

			bool operator()(std::pair<const address, policy::peer> const& p) const
			{
				return p.second.connection != m_pc
					&& p.second.connection
					&& p.second.connection->pid() == m_id
					&& !p.second.connection->pid().is_all_zeros()
					&& p.second.addr == m_pc->remote().address();
			}

			peer_id const& m_id;
			peer_connection const* m_pc;
		};
	}

	// --------------------------
	// RECEIVE DATA
	// --------------------------

	void bt_peer_connection::on_receive(error_code const& error
		, std::size_t bytes_transferred)
	{
		INVARIANT_CHECK;

		if (error) return;
		boost::shared_ptr<torrent> t = associated_torrent().lock();

		if (in_handshake()) 
			m_statistics.received_bytes(0, bytes_transferred);
	
#ifndef TORRENT_DISABLE_ENCRYPTION
		TORRENT_ASSERT(in_handshake() || !m_rc4_encrypted || m_encrypted);
		if (m_rc4_encrypted && m_encrypted)
		{
			std::pair<buffer::interval, buffer::interval> wr_buf = wr_recv_buffers(bytes_transferred);
			m_RC4_handler->decrypt(wr_buf.first.begin, wr_buf.first.left());
			if (wr_buf.second.left()) m_RC4_handler->decrypt(wr_buf.second.begin, wr_buf.second.left());
		}
#endif

		buffer::const_interval recv_buffer = receive_buffer();

#ifndef TORRENT_DISABLE_ENCRYPTION
		// m_state is set to read_pe_dhkey in initial state
		// (read_protocol_identifier) for incoming, or in constructor
		// for outgoing
		if (m_state == read_pe_dhkey)
		{
			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
			TORRENT_ASSERT(packet_size() == dh_key_len);
			TORRENT_ASSERT(recv_buffer == receive_buffer());

			if (!packet_finished()) return;
			
			// write our dh public key. m_dh_key_exchange is
			// initialized in write_pe1_2_dhkey()
			if (!is_local()) write_pe1_2_dhkey();
			if (is_disconnecting()) return;
			
			// read dh key, generate shared secret
			if (m_dh_key_exchange->compute_secret(recv_buffer.begin) == -1)
			{
				disconnect("out of memory");
				return;
			}

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " received DH key\n";
#endif
						
			// PadA/B can be a max of 512 bytes, and 20 bytes more for
			// the sync hash (if incoming), or 8 bytes more for the
			// encrypted verification constant (if outgoing). Instead
			// of requesting the maximum possible, request the maximum
			// possible to ensure we do not overshoot the standard
			// handshake.

			if (is_local())
			{
				m_state = read_pe_syncvc;
				write_pe3_sync();

				// initial payload is the standard handshake, this is
				// always rc4 if sent here. m_rc4_encrypted is flagged
				// again according to peer selection.
				m_rc4_encrypted = true;
				m_encrypted = true;
				write_handshake();
				m_rc4_encrypted = false;
				m_encrypted = false;

				// vc,crypto_select,len(pad),pad, encrypt(handshake)
				// 8+4+2+0+handshake_len
				reset_recv_buffer(8+4+2+0+handshake_len);
			}
			else
			{
				// already written dh key
				m_state = read_pe_synchash;
				// synchash,skeyhash,vc,crypto_provide,len(pad),pad,encrypt(handshake)
				reset_recv_buffer(20+20+8+4+2+0+handshake_len);
			}
			TORRENT_ASSERT(!packet_finished());
			return;
		}

		// cannot fall through into
		if (m_state == read_pe_synchash)
		{
			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
			TORRENT_ASSERT(!is_local());
			TORRENT_ASSERT(recv_buffer == receive_buffer());
		   
 			if (recv_buffer.left() < 20)
			{
				if (packet_finished())
					disconnect("sync hash not found", 2);
				return;
			}

			if (!m_sync_hash.get())
			{
				TORRENT_ASSERT(m_sync_bytes_read == 0);
				hasher h;

				// compute synchash (hash('req1',S))
				h.update("req1", 4);
				h.update(m_dh_key_exchange->get_secret(), dh_key_len);

				m_sync_hash.reset(new (std::nothrow) sha1_hash(h.final()));
				if (!m_sync_hash)
				{
					disconnect("no memory");
					return;
				}
			}

			int syncoffset = get_syncoffset((char*)m_sync_hash->begin(), 20
				, recv_buffer.begin, recv_buffer.left());

			// No sync 
			if (syncoffset == -1)
			{
				std::size_t bytes_processed = recv_buffer.left() - 20;
				m_sync_bytes_read += bytes_processed;
				if (m_sync_bytes_read >= 512)
				{
					disconnect("sync hash not found within 532 bytes", 2);
					return;
				}

				cut_receive_buffer(bytes_processed, (std::min)(packet_size()
					, (512+20) - m_sync_bytes_read));

				TORRENT_ASSERT(!packet_finished());
				return;
			}
			// found complete sync
			else
			{
				std::size_t bytes_processed = syncoffset + 20;
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << " sync point (hash) found at offset " 
					<< m_sync_bytes_read + bytes_processed - 20 << "\n";
#endif
				m_state = read_pe_skey_vc;
				// skey,vc - 28 bytes
				m_sync_hash.reset();
				cut_receive_buffer(bytes_processed, 28);
			}
		}

		if (m_state == read_pe_skey_vc)
		{
			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
			TORRENT_ASSERT(!is_local());
			TORRENT_ASSERT(packet_size() == 28);

			if (!packet_finished()) return;

			recv_buffer = receive_buffer();

			aux::session_impl::torrent_map::const_iterator i;

			for (i = m_ses.m_torrents.begin(); i != m_ses.m_torrents.end(); ++i)
			{
				torrent const& ti = *i->second;
				sha1_hash const& skey_hash = ti.obfuscated_hash();
				sha1_hash obfs_hash = m_dh_key_exchange->get_hash_xor_mask();
				obfs_hash ^= skey_hash;

				if (std::equal(recv_buffer.begin, recv_buffer.begin + 20,
					(char*)&obfs_hash[0]))
				{
					if (!t)
					{
						attach_to_torrent(ti.info_hash());
						if (is_disconnecting()) return;

						t = associated_torrent().lock();
						TORRENT_ASSERT(t);
					}

					init_pe_RC4_handler(m_dh_key_exchange->get_secret(), ti.info_hash());
#ifdef TORRENT_VERBOSE_LOGGING
					(*m_logger) << " stream key found, torrent located.\n";
#endif
					break;
				}
			}

			if (!m_RC4_handler.get())
			{
				disconnect("invalid streamkey identifier (info hash) in encrypted handshake", 2);
				return;
			}

			// verify constant
			buffer::interval wr_recv_buf = wr_recv_buffer();
			m_RC4_handler->decrypt(wr_recv_buf.begin + 20, 8);
			wr_recv_buf.begin += 28;

			const char sh_vc[] = {0,0,0,0, 0,0,0,0};
			if (!std::equal(sh_vc, sh_vc+8, recv_buffer.begin + 20))
			{
				disconnect("unable to verify constant", 2);
				return;
			}

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " verification constant found\n";
#endif
			m_state = read_pe_cryptofield;
			reset_recv_buffer(4 + 2);
		}

		// cannot fall through into
		if (m_state == read_pe_syncvc)
		{
			TORRENT_ASSERT(is_local());
			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
 			TORRENT_ASSERT(recv_buffer == receive_buffer());
			
			if (recv_buffer.left() < 8)
			{
				if (packet_finished())
					disconnect("sync verification constant not found", 2);
				return;
			}

			// generate the verification constant
			if (!m_sync_vc.get()) 
			{
				TORRENT_ASSERT(m_sync_bytes_read == 0);

				m_sync_vc.reset(new (std::nothrow) char[8]);
				if (!m_sync_vc)
				{
					disconnect("no memory");
					return;
				}
				std::fill(m_sync_vc.get(), m_sync_vc.get() + 8, 0);
				m_RC4_handler->decrypt(m_sync_vc.get(), 8);
			}

			TORRENT_ASSERT(m_sync_vc.get());
			int syncoffset = get_syncoffset(m_sync_vc.get(), 8
				, recv_buffer.begin, recv_buffer.left());

			// No sync 
			if (syncoffset == -1)
			{
				std::size_t bytes_processed = recv_buffer.left() - 8;
				m_sync_bytes_read += bytes_processed;
				if (m_sync_bytes_read >= 512)
				{
					disconnect("sync verification constant not found within 520 bytes", 2);
					return;
				}

				cut_receive_buffer(bytes_processed, (std::min)(packet_size(), (512+8) - m_sync_bytes_read));

				TORRENT_ASSERT(!packet_finished());
				return;
			}
			// found complete sync
			else
			{
				std::size_t bytes_processed = syncoffset + 8;
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << " sync point (verification constant) found at offset " 
							<< m_sync_bytes_read + bytes_processed - 8 << "\n";
#endif
				cut_receive_buffer (bytes_processed, 4 + 2);

				// delete verification constant
				m_sync_vc.reset();
				m_state = read_pe_cryptofield;
				// fall through
			}
		}

		if (m_state == read_pe_cryptofield) // local/remote
		{
			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
			TORRENT_ASSERT(packet_size() == 4+2);
			
			if (!packet_finished()) return;

			buffer::interval wr_buf = wr_recv_buffer();
			m_RC4_handler->decrypt(wr_buf.begin, packet_size());

			recv_buffer = receive_buffer();
			
			int crypto_field = detail::read_int32(recv_buffer.begin);

#ifdef TORRENT_VERBOSE_LOGGING
			if (!is_local())
				(*m_logger) << " crypto provide : [ ";
			else
				(*m_logger) << " crypto select : [ ";

			if (crypto_field & 0x01)
				(*m_logger) << "plaintext ";
			if (crypto_field & 0x02)
				(*m_logger) << "rc4 ";
			(*m_logger) << "]\n";
#endif

			if (!is_local())
			{
				int crypto_select = 0;
				// select a crypto method
				switch (m_ses.get_pe_settings().allowed_enc_level)
				{
				case pe_settings::plaintext:
					if (!(crypto_field & 0x01))
					{
						disconnect("plaintext not provided", 1);
						return;
					}
					crypto_select = 0x01;
					break;
				case pe_settings::rc4:
					if (!(crypto_field & 0x02))
					{
						disconnect("rc4 not provided", 1);
						return;
					}
					crypto_select = 0x02;
					break;
				case pe_settings::both:
					if (m_ses.get_pe_settings().prefer_rc4)
					{
						if (crypto_field & 0x02) 
							crypto_select = 0x02;
						else if (crypto_field & 0x01)
							crypto_select = 0x01;
					}
					else
					{
						if (crypto_field & 0x01)
							crypto_select = 0x01;
						else if (crypto_field & 0x02)
							crypto_select = 0x02;
					}
					if (!crypto_select)
					{
						disconnect("rc4/plaintext not provided", 1);
						return;
					}
					break;
				} // switch
				
				// write the pe4 step
				write_pe4_sync(crypto_select);
			}
			else // is_local()
			{
				// check if crypto select is valid
				pe_settings::enc_level const& allowed_enc_level = m_ses.get_pe_settings().allowed_enc_level;

				if (crypto_field == 0x02)
				{
					if (allowed_enc_level == pe_settings::plaintext)
					{
						disconnect("rc4 selected by peer when not provided", 2);
						return;
					}
					m_rc4_encrypted = true;
				}
				else if (crypto_field == 0x01)
				{
					if (allowed_enc_level == pe_settings::rc4)
					{
						disconnect("plaintext selected by peer when not provided", 2);
						return;
					}
					m_rc4_encrypted = false;
				}
				else
				{
					disconnect("unsupported crypto method selected by peer", 2);
					return;
				}
			}

			int len_pad = detail::read_int16(recv_buffer.begin);
			if (len_pad < 0 || len_pad > 512)
			{
				disconnect("invalid pad length", 2);
				return;
			}
			
			m_state = read_pe_pad;
			if (!is_local())
				reset_recv_buffer(len_pad + 2); // len(IA) at the end of pad
			else
			{
				if (len_pad == 0)
				{
					m_encrypted = true;
					m_state = init_bt_handshake;
				}
				else
					reset_recv_buffer(len_pad);
			}
		}

		if (m_state == read_pe_pad)
		{
			TORRENT_ASSERT(!m_encrypted);
			if (!packet_finished()) return;

			int pad_size = is_local() ? packet_size() : packet_size() - 2;

			buffer::interval wr_buf = wr_recv_buffer();
			m_RC4_handler->decrypt(wr_buf.begin, packet_size());

			recv_buffer = receive_buffer();
				
			if (!is_local())
			{
				recv_buffer.begin += pad_size;
				int len_ia = detail::read_int16(recv_buffer.begin);
				
				if (len_ia < 0) 
				{
					disconnect("invalid len_ia in handshake", 2);
					return;
				}

#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << " len(IA) : " << len_ia << "\n";
#endif
				if (len_ia == 0)
				{
					// everything after this is Encrypt2
					m_encrypted = true;
					m_state = init_bt_handshake;
				}
				else
				{
					m_state = read_pe_ia;
					reset_recv_buffer(len_ia);
				}
			}
			else // is_local()
			{
				// everything that arrives after this is Encrypt2
				m_encrypted = true;
				m_state = init_bt_handshake;
			}
		}

		if (m_state == read_pe_ia)
		{
			TORRENT_ASSERT(!is_local());
			TORRENT_ASSERT(!m_encrypted);

			if (!packet_finished()) return;

			// ia is always rc4, so decrypt it
			buffer::interval wr_buf = wr_recv_buffer();
			m_RC4_handler->decrypt(wr_buf.begin, packet_size());

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " decrypted ia : " << packet_size() << " bytes\n";
#endif

			if (!m_rc4_encrypted)
			{
				m_RC4_handler.reset();
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << " destroyed rc4 keys\n";
#endif
			}

			// everything that arrives after this is Encrypt2
			m_encrypted = true;

			m_state = read_protocol_identifier;
			cut_receive_buffer(0, 20);
		}

		if (m_state == init_bt_handshake)
		{
			TORRENT_ASSERT(m_encrypted);

			// decrypt remaining received bytes
			if (m_rc4_encrypted)
			{
				buffer::interval wr_buf = wr_recv_buffer();
				wr_buf.begin += packet_size();
				m_RC4_handler->decrypt(wr_buf.begin, wr_buf.left());
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << " decrypted remaining " << wr_buf.left() << " bytes\n";
#endif
			}
			else // !m_rc4_encrypted
			{
				m_RC4_handler.reset();
#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << " destroyed rc4 keys\n";
#endif
			}

			// payload stream, start with 20 handshake bytes
			m_state = read_protocol_identifier;
			reset_recv_buffer(20);

			// encrypted portion of handshake completed, toggle
			// peer_info pe_support flag back to true
			if (is_local() &&
				m_ses.get_pe_settings().out_enc_policy == pe_settings::enabled)
			{
				policy::peer* pi = peer_info_struct();
				TORRENT_ASSERT(pi);
				
				pi->pe_support = true;
			}
		}

#endif // #ifndef TORRENT_DISABLE_ENCRYPTION
		
		if (m_state == read_protocol_identifier)
		{
			TORRENT_ASSERT(packet_size() == 20);

			if (!packet_finished()) return;
			recv_buffer = receive_buffer();

			int packet_size = recv_buffer[0];
			const char protocol_string[] = "BitTorrent protocol";

			if (packet_size != 19 ||
				!std::equal(recv_buffer.begin + 1, recv_buffer.begin + 19, protocol_string))
			{
#ifndef TORRENT_DISABLE_ENCRYPTION
				if (!is_local() && m_ses.get_pe_settings().in_enc_policy == pe_settings::disabled)
				{
					disconnect("encrypted incoming connections disabled");
					return;
				}

				// Don't attempt to perform an encrypted handshake
				// within an encrypted connection
				if (!m_encrypted && !is_local())
				{
#ifdef TORRENT_VERBOSE_LOGGING
 					(*m_logger) << " attempting encrypted connection\n";
#endif
 					m_state = read_pe_dhkey;
					cut_receive_buffer(0, dh_key_len);
					TORRENT_ASSERT(!packet_finished());
 					return;
				}
				
				TORRENT_ASSERT((!is_local() && m_encrypted) || is_local());
#endif // #ifndef TORRENT_DISABLE_ENCRYPTION
				disconnect("incorrect protocol identifier", 2);
				return;
			}

#ifndef TORRENT_DISABLE_ENCRYPTION
			TORRENT_ASSERT(m_state != read_pe_dhkey);

			if (!is_local() && 
				(m_ses.get_pe_settings().in_enc_policy == pe_settings::forced) &&
				!m_encrypted) 
			{
				disconnect("non encrypted incoming connections disabled");
				return;
			}
#endif

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << " BitTorrent protocol\n";
#endif

			m_state = read_info_hash;
			reset_recv_buffer(28);
		}

		// fall through
		if (m_state == read_info_hash)
		{
			TORRENT_ASSERT(packet_size() == 28);

			if (!packet_finished()) return;
			recv_buffer = receive_buffer();


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
			if (recv_buffer[7] & 0x04)
				(*m_logger) << "supports FAST extensions\n";
			if (recv_buffer[5] & 0x10)
				(*m_logger) << "supports extensions protocol\n";
#endif

#ifndef DISABLE_EXTENSIONS
			std::memcpy(m_reserved_bits, recv_buffer.begin, 20);
			if ((recv_buffer[5] & 0x10))
				m_supports_extensions = true;
#endif
			if (recv_buffer[7] & 0x01)
				m_supports_dht_port = true;

			if (recv_buffer[7] & 0x04)
				m_supports_fast = true;

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
				if (is_disconnecting()) return;
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
					disconnect("invalid info-hash in handshake", 2);
					return;
				}

#ifdef TORRENT_VERBOSE_LOGGING
				(*m_logger) << " info_hash received\n";
#endif
			}

			t = associated_torrent().lock();
			TORRENT_ASSERT(t);
			
			// if this is a local connection, we have already
			// sent the handshake
			if (!is_local()) write_handshake();
//			if (t->valid_metadata())
//				write_bitfield();

			if (is_disconnecting()) return;

			TORRENT_ASSERT(t->get_policy().has_connection(this));

			m_state = read_peer_id;
 			reset_recv_buffer(20);
		}

		// fall through
		if (m_state == read_peer_id)
		{
  			if (!t)
			{
				TORRENT_ASSERT(!packet_finished()); // TODO
				return;
			}
			TORRENT_ASSERT(packet_size() == 20);
			
 			if (!packet_finished()) return;
			recv_buffer = receive_buffer();

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
 
			if (t->settings().allow_multiple_connections_per_ip)
			{
				// now, let's see if this connection should be closed
				policy& p = t->get_policy();
				policy::iterator i = std::find_if(p.begin_peer(), p.end_peer()
					, match_peer_id(pid, this));
				if (i != p.end_peer())
				{
					TORRENT_ASSERT(i->second.connection->pid() == pid);
					// we found another connection with the same peer-id
					// which connection should be closed in order to be
					// sure that the other end closes the same connection?
					// the peer with greatest peer-id is the one allowed to
					// initiate connections. So, if our peer-id is greater than
					// the others, we should close the incoming connection,
					// if not, we should close the outgoing one.
					if (pid < m_ses.get_peer_id() && is_local())
					{
						i->second.connection->disconnect("duplicate peer-id, connection closed");
					}
					else
					{
						disconnect("duplicate peer-id, connection closed");
						return;
					}
				}
			}

			// disconnect if the peer has the same peer-id as ourself
			// since it most likely is ourself then
			if (pid == m_ses.get_peer_id())
			{
				disconnect("closing connection to ourself", 1);
				return;
			}
 
			m_client_version = identify_client(pid);
			boost::optional<fingerprint> f = client_fingerprint(pid);
			if (f && std::equal(f->name, f->name + 2, "BC"))
			{
				// if this is a bitcomet client, lower the request queue size limit
				if (m_max_out_request_queue > 50) m_max_out_request_queue = 50;
			}

#ifndef TORRENT_DISABLE_EXTENSIONS
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end;)
			{
				if (!(*i)->on_handshake(m_reserved_bits))
				{
					i = m_extensions.erase(i);
				}
				else
				{
					++i;
				}
			}
			if (is_disconnecting()) return;

			if (m_supports_extensions) write_extensions();
#endif

#ifdef TORRENT_VERBOSE_LOGGING
			(*m_logger) << time_now_string() << " <== HANDSHAKE\n";
#endif
			// consider this a successful connection, reset the failcount
			if (peer_info_struct()) peer_info_struct()->failcount = 0;
			
#ifndef TORRENT_DISABLE_ENCRYPTION
			// Toggle pe_support back to false if this is a
			// standard successful connection
			if (is_local() && !m_encrypted &&
				m_ses.get_pe_settings().out_enc_policy == pe_settings::enabled)
			{
				policy::peer* pi = peer_info_struct();
				TORRENT_ASSERT(pi);

				pi->pe_support = false;
			}
#endif

			m_state = read_packet_size;
			reset_recv_buffer(5);
			if (t->ready_for_connections())
			{
				write_bitfield();
#ifndef TORRENT_DISABLE_DHT
				if (m_supports_dht_port && m_ses.m_dht)
					write_dht_port(m_ses.get_dht_settings().service_port);
#endif
			}

			TORRENT_ASSERT(!packet_finished());
			return;
		}

		// cannot fall through into
		if (m_state == read_packet_size)
		{
			// Make sure this is not fallen though into
			TORRENT_ASSERT(recv_buffer == receive_buffer());
			TORRENT_ASSERT(packet_size() == 5);

			if (!t) return;
			m_statistics.received_bytes(0, bytes_transferred);

			if (recv_buffer.left() < 4) return;

			const char* ptr = recv_buffer.begin;
			int packet_size = detail::read_int32(ptr);

			// don't accept packets larger than 1 MB
			if (packet_size > 1024*1024 || packet_size < 0)
			{
				// packet too large
				std::stringstream msg;
				msg << "packet > 1 MB (" << (unsigned int)packet_size << " bytes)";
				disconnect(msg.str().c_str(), 2);
				return;
			}
					
			if (packet_size == 0)
			{
				incoming_keepalive();
				if (is_disconnecting()) return;
				// keepalive message
				m_state = read_packet_size;
				cut_receive_buffer(4, 5);
				return;
			}
			else
			{
				if (recv_buffer.left() < 5) return;

				m_state = read_packet;
				cut_receive_buffer(4, packet_size);
				bytes_transferred = 1;
				recv_buffer = receive_buffer();
				TORRENT_ASSERT(recv_buffer.left() == 1);
			}
		}

		if (m_state == read_packet)
		{
			TORRENT_ASSERT(recv_buffer == receive_buffer());
			if (!t) return;
			if (dispatch_message(bytes_transferred))
			{
				m_state = read_packet_size;
				reset_recv_buffer(5);
			}
			TORRENT_ASSERT(!packet_finished());
			return;
		}
		
		TORRENT_ASSERT(!packet_finished());
	}	

	// --------------------------
	// SEND DATA
	// --------------------------

	void bt_peer_connection::on_sent(error_code const& error
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

#if defined TORRENT_DEBUG && !defined TORRENT_DISABLE_ENCRYPTION
		if (m_encrypted_bytes > 0)
		{
			if (m_rc4_encrypted)
			{
				m_encrypted_bytes -= bytes_transferred;
				TORRENT_ASSERT(m_encrypted_bytes >= 0);
			}
			else
			{
				m_encrypted_bytes -= (std::min)(int(bytes_transferred), m_encrypted_bytes);
			}
			TORRENT_ASSERT(m_encrypted_bytes >= 0);
		}
#endif

		TORRENT_ASSERT(amount_payload <= (int)bytes_transferred);
		m_statistics.sent_bytes(amount_payload, bytes_transferred - amount_payload);
	}

#ifdef TORRENT_DEBUG
	void bt_peer_connection::check_invariant() const
	{
#ifndef TORRENT_DISABLE_ENCRYPTION
		TORRENT_ASSERT( (bool(m_state != read_pe_dhkey) || m_dh_key_exchange.get())
				|| !is_local());

		TORRENT_ASSERT(!m_rc4_encrypted || m_RC4_handler.get());
#endif
		if (is_seed() && m_initialized) TORRENT_ASSERT(upload_only());

		if (!in_handshake())
		{
			TORRENT_ASSERT(m_sent_handshake);
		}

		if (!m_payloads.empty())
		{
			for (std::deque<range>::const_iterator i = m_payloads.begin();
				i != m_payloads.end() - 1; ++i)
			{
				TORRENT_ASSERT(i->start + i->length <= (i+1)->start);
			}
		}
	}
#endif

}

