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
#include <boost/limits.hpp>
#include <boost/bind.hpp>

#ifdef TORRENT_USE_OPENSSL
#include <memory> // autp_ptr
#endif

#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/version.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/alloca.hpp"

#ifndef TORRENT_DISABLE_ENCRYPTION
#include "libtorrent/pe_crypto.hpp"
#include "libtorrent/hasher.hpp"
#endif

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
		, policy::peer* peerinfo
		, bool outgoing)
		: peer_connection(ses, tor, s, remote
			, peerinfo, outgoing)
		, m_state(read_protocol_identifier)
#ifndef TORRENT_DISABLE_EXTENSIONS
		, m_upload_only_id(0)
		, m_holepunch_id(0)
		, m_dont_have_id(0)
		, m_share_mode_id(0)
		, m_supports_extensions(false)
#endif
		, m_supports_dht_port(false)
		, m_supports_fast(false)
#ifndef TORRENT_DISABLE_ENCRYPTION
		, m_encrypted(false)
		, m_rc4_encrypted(false)
		, m_sync_bytes_read(0)
#endif
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		, m_sent_bitfield(false)
		, m_in_constructor(true)
		, m_sent_handshake(false)
#endif
	{
#ifdef TORRENT_VERBOSE_LOGGING
		peer_log("*** bt_peer_connection");
#endif

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		m_in_constructor = false;
#endif
		memset(m_reserved_bits, 0, sizeof(m_reserved_bits));
	}

	bt_peer_connection::bt_peer_connection(
		session_impl& ses
		, boost::shared_ptr<socket_type> s
		, tcp::endpoint const& remote
		, policy::peer* peerinfo)
		: peer_connection(ses, s, remote, peerinfo)
		, m_state(read_protocol_identifier)
#ifndef TORRENT_DISABLE_EXTENSIONS
		, m_upload_only_id(0)
		, m_holepunch_id(0)
		, m_dont_have_id(0)
		, m_share_mode_id(0)
		, m_supports_extensions(false)
#endif
		, m_supports_dht_port(false)
		, m_supports_fast(false)
#ifndef TORRENT_DISABLE_ENCRYPTION
		, m_encrypted(false)
		, m_rc4_encrypted(false)
		, m_sync_bytes_read(0)
#endif		
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
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
		m_quota[download_channel] = 2048;
		m_quota[upload_channel] = 2048;
#else
		m_quota[download_channel] = 80;
		m_quota[upload_channel] = 80;
#endif

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		m_in_constructor = false;
#endif
		memset(m_reserved_bits, 0, sizeof(m_reserved_bits));
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
		TORRENT_ASSERT(m_ses.is_network_thread());
	}

	void bt_peer_connection::on_connected()
	{
#ifndef TORRENT_DISABLE_ENCRYPTION
		
		pe_settings::enc_policy out_enc_policy = m_ses.get_pe_settings().out_enc_policy;

#ifdef TORRENT_USE_OPENSSL
		// never try an encrypted connection when already using SSL
		if (is_ssl(*get_socket()))
			out_enc_policy = pe_settings::disabled;
#endif
#ifdef TORRENT_VERBOSE_LOGGING
		char const* policy_name[] = {"forced", "enabled", "disabled"};
		peer_log("*** outgoing encryption policy: %s", policy_name[out_enc_policy]);
#endif

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
			write_dht_port(m_ses.m_external_udp_port);
#endif
	}

	void bt_peer_connection::write_dht_port(int listen_port)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);
		
#ifdef TORRENT_VERBOSE_LOGGING
		peer_log("==> DHT_PORT [ %d ]", listen_port);
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
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		m_sent_bitfield = true;
#endif
#ifdef TORRENT_VERBOSE_LOGGING
		peer_log("==> HAVE_ALL");
#endif
		char msg[] = {0,0,0,1, msg_have_all};
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_have_none()
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(m_sent_handshake && !m_sent_bitfield);
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		m_sent_bitfield = true;
#endif
#ifdef TORRENT_VERBOSE_LOGGING
		peer_log("==> HAVE_NONE");
#endif
		char msg[] = {0,0,0,1, msg_have_none};
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_reject_request(peer_request const& r)
	{
		INVARIANT_CHECK;

#ifdef TORRENT_STATS
		++m_ses.m_piece_rejects;
#endif

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

	void bt_peer_connection::write_suggest(int piece)
	{
		INVARIANT_CHECK;

		if (!m_supports_fast) return;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);
		TORRENT_ASSERT(associated_torrent().lock()->valid_metadata());

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		if (m_sent_suggested_pieces.empty())
			m_sent_suggested_pieces.resize(t->torrent_file().num_pieces(), false);

		if (m_sent_suggested_pieces[piece]) return;
		m_sent_suggested_pieces.set_bit(piece);

		char msg[] = {0,0,0,5, msg_suggest_piece, 0, 0, 0, 0};
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
		if (is_outgoing()) p.flags |= peer_info::local_connection;

#ifndef TORRENT_DISABLE_ENCRYPTION
		if (m_encrypted)
		{
			p.flags |= m_rc4_encrypted
				? peer_info::rc4_encrypted
				: peer_info::plaintext_encrypted;
		}
#endif

		if (!is_connecting() && in_handshake())
			p.flags |= peer_info::handshake;
		if (is_connecting() && !is_queued()) p.flags |= peer_info::connecting;
		if (is_queued()) p.flags |= peer_info::queued;

		p.client = m_client_version;
		p.connection_type = is_utp(*get_socket())
			? peer_info::bittorrent_utp
			: peer_info::standard_bittorrent;
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
		if (is_outgoing())
			peer_log("*** initiating encrypted handshake");
#endif

		m_dh_key_exchange.reset(new (std::nothrow) dh_key_exchange);
		if (!m_dh_key_exchange || !m_dh_key_exchange->good())
		{
			disconnect(errors::no_memory);
			return;
		}

		int pad_size = random() % 512;

#ifdef TORRENT_VERBOSE_LOGGING
		peer_log(" pad size: %d", pad_size);
#endif

		char msg[dh_key_len + 512];
		char* ptr = msg;
		int buf_size = dh_key_len + pad_size;

		memcpy(ptr, m_dh_key_exchange->get_local_key(), dh_key_len);
		ptr += dh_key_len;

		std::generate(ptr, ptr + pad_size, random);
		send_buffer(msg, buf_size);

#ifdef TORRENT_VERBOSE_LOGGING
		peer_log(" sent DH key");
#endif
	}

	void bt_peer_connection::write_pe3_sync()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_encrypted);
		TORRENT_ASSERT(!m_rc4_encrypted);
		TORRENT_ASSERT(is_outgoing());
		TORRENT_ASSERT(!m_sent_handshake);
		
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		
		hasher h;
		sha1_hash const& info_hash = t->torrent_file().info_hash();
		char const* const secret = m_dh_key_exchange->get_secret();

		int pad_size = random() % 512;

		// synchash,skeyhash,vc,crypto_provide,len(pad),pad,len(ia)
		char msg[20 + 20 + 8 + 4 + 2 + 512 + 2];
		char* ptr = msg;

		// sync hash (hash('req1',S))
		h.reset();
		h.update("req1",4);
		h.update(secret, dh_key_len);
		sha1_hash sync_hash = h.final();

		memcpy(ptr, &sync_hash[0], 20);
		ptr += 20;

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

		memcpy(ptr, &obfsc_hash[0], 20);
		ptr += 20;

		// Discard DH key exchange data, setup RC4 keys
		init_pe_rc4_handler(secret, info_hash);
		m_dh_key_exchange.reset(); // secret should be invalid at this point
	
		// write the verification constant and crypto field
		int encrypt_size = sizeof(msg) - 512 + pad_size - 40;

		int crypto_provide = 0;
		pe_settings::enc_level const& allowed_enc_level = m_ses.get_pe_settings().allowed_enc_level;

		if (allowed_enc_level == pe_settings::both) 
			crypto_provide = 0x03;
		else if (allowed_enc_level == pe_settings::rc4) 
			crypto_provide = 0x02;
		else if (allowed_enc_level == pe_settings::plaintext)
			crypto_provide = 0x01;

#ifdef TORRENT_VERBOSE_LOGGING
		char const* level[] = {"plaintext", "rc4", "plaintext rc4"};
		peer_log(" crypto provide : [ %s ]"
			, level[allowed_enc_level-1]);
#endif

		write_pe_vc_cryptofield(ptr, encrypt_size, crypto_provide, pad_size);
		m_enc_handler->encrypt(ptr, encrypt_size);
		send_buffer(msg, sizeof(msg) - 512 + pad_size);
	}

	void bt_peer_connection::write_pe4_sync(int crypto_select)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!is_outgoing());
		TORRENT_ASSERT(!m_encrypted);
		TORRENT_ASSERT(!m_rc4_encrypted);
		TORRENT_ASSERT(crypto_select == 0x02 || crypto_select == 0x01);
		TORRENT_ASSERT(!m_sent_handshake);

		int pad_size = random() % 512;

		const int buf_size = 8 + 4 + 2 + pad_size;
		char msg[512 + 8 + 4 + 2];
		write_pe_vc_cryptofield(msg, sizeof(msg), crypto_select, pad_size);

		m_enc_handler->encrypt(msg, buf_size);
		send_buffer(msg, buf_size);

		// encryption method has been negotiated
		if (crypto_select == 0x02) 
			m_rc4_encrypted = true;
		else // 0x01
			m_rc4_encrypted = false;

#ifdef TORRENT_VERBOSE_LOGGING
		peer_log(" crypto select : [ %s ]"
			, (crypto_select == 0x01) ? "plaintext" : "rc4");
#endif
	}

 	void bt_peer_connection::write_pe_vc_cryptofield(char* write_buf, int len
		, int crypto_field, int pad_size)
 	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(crypto_field <= 0x03 && crypto_field > 0);
		// vc,crypto_field,len(pad),pad, (len(ia))
		TORRENT_ASSERT((len >= 8+4+2+pad_size+2 && is_outgoing())
			|| (len >= 8+4+2+pad_size && !is_outgoing()));
		TORRENT_ASSERT(!m_sent_handshake);

		// encrypt(vc, crypto_provide/select, len(Pad), len(IA))
		// len(pad) is zero for now, len(IA) only for outgoing connections
		
		// vc
		memset(write_buf, 0, 8);
		write_buf += 8;

		detail::write_uint32(crypto_field, write_buf);
		detail::write_uint16(pad_size, write_buf); // len (pad)

		// fill pad with zeroes
		std::generate(write_buf, write_buf + pad_size, &random);
		write_buf += pad_size;

		// append len(ia) if we are initiating
		if (is_outgoing())
			detail::write_uint16(handshake_len, write_buf); // len(IA)
 	}

	void bt_peer_connection::init_pe_rc4_handler(char const* secret, sha1_hash const& stream_key)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(secret);
		
		hasher h;
		static const char keyA[] = "keyA";
		static const char keyB[] = "keyB";

		// encryption rc4 longkeys
		// outgoing connection : hash ('keyA',S,SKEY)
		// incoming connection : hash ('keyB',S,SKEY)
		
		if (is_outgoing()) h.update(keyA, 4); else h.update(keyB, 4);
		h.update(secret, dh_key_len);
		h.update((char const*)stream_key.begin(), 20);
		const sha1_hash local_key = h.final();

		h.reset();

		// decryption rc4 longkeys
		// outgoing connection : hash ('keyB',S,SKEY)
		// incoming connection : hash ('keyA',S,SKEY)
		
		if (is_outgoing()) h.update(keyB, 4); else h.update(keyA, 4);
		h.update(secret, dh_key_len);
		h.update((char const*)stream_key.begin(), 20);
		const sha1_hash remote_key = h.final();
		
		TORRENT_ASSERT(!m_enc_handler.get());
		m_enc_handler.reset(new (std::nothrow) rc4_handler);
		m_enc_handler->set_incoming_key(&remote_key[0], 20);
		m_enc_handler->set_outgoing_key(&local_key[0], 20);

		if (!m_enc_handler)
		{
			disconnect(errors::no_memory);
			return;
		}

#ifdef TORRENT_VERBOSE_LOGGING
		peer_log(" computed RC4 keys");
#endif
	}

	void bt_peer_connection::append_const_send_buffer(char const* buffer, int size)
	{
#ifndef TORRENT_DISABLE_ENCRYPTION
		if (m_encrypted && m_rc4_encrypted)
		{
			// if we're encrypting this buffer, we need to make a copy
			// since we'll mutate it
			char* buf = (char*)malloc(size);
			memcpy(buf, buffer, size);
			bt_peer_connection::append_send_buffer(buf, size, boost::bind(&::free, _1));
		}
		else
#endif
		{
			peer_connection::append_const_send_buffer(buffer, size);
		}
	}

	void encrypt(char* buf, int len, void* userdata)
	{
		rc4_handler* rc4 = (rc4_handler*)userdata;
		rc4->encrypt(buf, len);
	}

	void bt_peer_connection::send_buffer(char const* buf, int size, int flags
			, void (*f)(char*, int, void*), void* ud)
	{
		TORRENT_ASSERT(f == 0);
		TORRENT_ASSERT(ud == 0);
		TORRENT_ASSERT(buf);
		TORRENT_ASSERT(size > 0);
		
		void* userdata = 0;
		void (*fun)(char*, int, void*) = 0;
#ifndef TORRENT_DISABLE_ENCRYPTION
		if (m_encrypted && m_rc4_encrypted)
		{
			fun = encrypt;
			userdata = m_enc_handler.get();
		}
#endif
		
		peer_connection::send_buffer(buf, size, flags, fun, userdata);
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
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		m_sent_handshake = true;
#endif

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		// add handshake to the send buffer
		const char version_string[] = "BitTorrent protocol";
		const int string_len = sizeof(version_string)-1;

		char handshake[1 + string_len + 8 + 20 + 20];
		char* ptr = handshake;
		// length of version string
		detail::write_uint8(string_len, ptr);
		// protocol identifier
		memcpy(ptr, version_string, string_len);
		ptr += string_len;
		// 8 zeroes
		memset(ptr, 0, 8);

#ifndef TORRENT_DISABLE_DHT
		// indicate that we support the DHT messages
		*(ptr + 7) |= 0x01;
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		// we support extensions
		*(ptr + 5) |= 0x10;
#endif

		// we support merkle torrents
		*(ptr + 5) |= 0x08;

		// we support FAST extension
		*(ptr + 7) |= 0x04;

#ifdef TORRENT_VERBOSE_LOGGING	
		std::string bitmask;
		for (int k = 0; k < 8; ++k)
		{
			for (int j = 0; j < 8; ++j)
			{
				if (ptr[k] & (0x80 >> j)) bitmask += '1';
				else bitmask += '0';
			}
		}
		peer_log(">>> EXTENSION_BITS [ %s ]", bitmask.c_str());
#endif
		ptr += 8;

		// info hash
		sha1_hash const& ih = t->torrent_file().info_hash();
		memcpy(ptr, &ih[0], 20);
		ptr += 20;

		// peer id
		if (m_ses.m_settings.anonymous_mode)
		{
			// in anonymous mode, every peer connection
			// has a unique peer-id
			for (int i = 0; i < 20; ++i)
				*ptr++ = rand();
		}
		else
		{
			memcpy(ptr, &m_ses.get_peer_id()[0], 20);
//			ptr += 20;
		}

#ifdef TORRENT_VERBOSE_LOGGING
		peer_log("==> HANDSHAKE [ ih: %s ]", to_hex(ih.to_string()).c_str());
#endif
		send_buffer(handshake, sizeof(handshake));
	}

	boost::optional<piece_block_progress> bt_peer_connection::downloading_piece_progress() const
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		buffer::const_interval recv_buffer = receive_buffer();
		// are we currently receiving a 'piece' message?
		if (m_state != read_packet
			|| recv_buffer.left() <= 9
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
		peer_log("<== KEEPALIVE");
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
		m_statistics.received_bytes(0, received);
		if (packet_size() != 1)
		{
			disconnect(errors::invalid_choke, 2);
			return;
		}
		if (!packet_finished()) return;

		incoming_choke();
		if (is_disconnecting()) return;
		if (!m_supports_fast)
		{
			// we just got choked, and the peer that choked use
			// doesn't support fast extensions, so we have to
			// assume that the choke message implies that all
			// of our requests are rejected. Go through them and
			// pretend that we received reject request messages
			boost::shared_ptr<torrent> t = associated_torrent().lock();
			TORRENT_ASSERT(t);
			while (!download_queue().empty())
			{
				piece_block const& b = download_queue().front().block;
				peer_request r;
				r.piece = b.piece_index;
				r.start = b.block_index * t->block_size();
				r.length = t->block_size();
				// if it's the last piece, make sure to
				// set the length of the request to not
				// exceed the end of the torrent. This is
				// necessary in order to maintain a correct
				// m_outsanding_bytes
				if (r.piece == t->torrent_file().num_pieces() - 1)
				{
					r.length = (std::min)(t->torrent_file().piece_size(
						r.piece) - r.start, r.length);
				}
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
		m_statistics.received_bytes(0, received);
		if (packet_size() != 1)
		{
			disconnect(errors::invalid_unchoke, 2);
			return;
		}
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
		m_statistics.received_bytes(0, received);
		if (packet_size() != 1)
		{
			disconnect(errors::invalid_interested, 2);
			return;
		}
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
		m_statistics.received_bytes(0, received);
		if (packet_size() != 1)
		{
			disconnect(errors::invalid_not_interested, 2);
			return;
		}
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
		m_statistics.received_bytes(0, received);
		if (packet_size() != 5)
		{
			disconnect(errors::invalid_have, 2);
			return;
		}
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

		m_statistics.received_bytes(0, received);
		// if we don't have the metedata, we cannot
		// verify the bitfield size
		if (t->valid_metadata()
			&& packet_size() - 1 != (t->torrent_file().num_pieces() + 7) / 8)
		{
			disconnect(errors::invalid_bitfield_size, 2);
			return;
		}

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
		m_statistics.received_bytes(0, received);
		if (packet_size() != 13)
		{
			disconnect(errors::invalid_request, 2);
			return;
		}
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
		int recv_pos = receive_pos(); // recv_buffer.end - recv_buffer.begin;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		bool merkle = (unsigned char)recv_buffer.begin[0] == 250;
		if (merkle)
		{
			if (recv_pos == 1)
			{
				set_soft_packet_size(13);
				m_statistics.received_bytes(0, received);
				return;
			}
			if (recv_pos < 13)
			{
				m_statistics.received_bytes(0, received);
				return;
			}
			if (recv_pos == 13)
			{
				const char* ptr = recv_buffer.begin + 9;
				int list_size = detail::read_int32(ptr);
				// now we know how long the bencoded hash list is
				// and we can allocate the disk buffer and receive
				// into it

				if (list_size > packet_size() - 13)
				{
					disconnect(errors::invalid_hash_list, 2);
					return;
				}

				if (packet_size() - 13 - list_size > t->block_size())
				{
					disconnect(errors::packet_too_large, 2);
					return;
				}

				TORRENT_ASSERT(!has_disk_receive_buffer());
				if (!allocate_disk_receive_buffer(packet_size() - 13 - list_size))
				{
					m_statistics.received_bytes(0, received);
					return;
				}
			}
		}
		else
		{
			if (recv_pos == 1)
			{
				TORRENT_ASSERT(!has_disk_receive_buffer());

				if (packet_size() - 9 > t->block_size())
				{
					disconnect(errors::packet_too_large, 2);
					return;
				}

				if (!allocate_disk_receive_buffer(packet_size() - 9))
				{
					m_statistics.received_bytes(0, received);
					return;
				}
			}
		}
		TORRENT_ASSERT(has_disk_receive_buffer() || packet_size() == 9);
		// classify the received data as protocol chatter
		// or data payload for the statistics
		int piece_bytes = 0;

		int header_size = merkle?13:9;

		peer_request p;
		int list_size = 0;

		if (recv_pos >= header_size)
		{
			const char* ptr = recv_buffer.begin + 1;
			p.piece = detail::read_int32(ptr);
			p.start = detail::read_int32(ptr);

			if (merkle)
			{
				list_size = detail::read_int32(ptr);
				p.length = packet_size() - list_size - header_size;
				header_size += list_size;
			}
			else
			{
				p.length = packet_size() - header_size;
			}
		}

		if (recv_pos <= header_size)
		{
			// only received protocol data
			m_statistics.received_bytes(0, received);
		}
		else if (recv_pos - received >= header_size)
		{
			// only received payload data
			m_statistics.received_bytes(received, 0);
			piece_bytes = received;
		}
		else
		{
			// received a bit of both
			TORRENT_ASSERT(recv_pos - received < header_size);
			TORRENT_ASSERT(recv_pos > header_size);
			TORRENT_ASSERT(header_size - (recv_pos - received) <= header_size);
			m_statistics.received_bytes(
				recv_pos - header_size
				, header_size - (recv_pos - received));
			piece_bytes = recv_pos - header_size;
		}

		if (recv_pos < header_size) return;

#ifdef TORRENT_VERBOSE_LOGGING
//			peer_log("<== PIECE_FRAGMENT p: %d start: %d length: %d"
//				, p.piece, p.start, p.length);
#endif

		if (recv_pos - received < header_size && recv_pos >= header_size)
		{
			// call this once, the first time the entire header
			// has been received
			start_receive_piece(p);
			if (is_disconnecting()) return;
		}

		TORRENT_ASSERT(has_disk_receive_buffer() || packet_size() == header_size);

		incoming_piece_fragment(piece_bytes);
		if (!packet_finished()) return;

		if (merkle && list_size > 0)
		{
#ifdef TORRENT_VERBOSE_LOGGING
			peer_log("<== HASHPIECE [ piece: %d list: %d ]", p.piece, list_size);
#endif
			lazy_entry hash_list;
			error_code ec;
			if (lazy_bdecode(recv_buffer.begin + 13, recv_buffer.begin+ 13 + list_size
				, hash_list, ec) != 0)
			{
				disconnect(errors::invalid_hash_piece, 2);
				return;
			}

			// the list has this format:
			// [ [node-index, hash], [node-index, hash], ... ]
			if (hash_list.type() != lazy_entry::list_t)
			{
				disconnect(errors::invalid_hash_list, 2);
				return;
			}

			std::map<int, sha1_hash> nodes;
			for (int i = 0; i < hash_list.list_size(); ++i)
			{
				lazy_entry const* e = hash_list.list_at(i);
				if (e->type() != lazy_entry::list_t
					|| e->list_size() != 2
					|| e->list_at(0)->type() != lazy_entry::int_t
					|| e->list_at(1)->type() != lazy_entry::string_t
					|| e->list_at(1)->string_length() != 20) continue;

				nodes.insert(std::make_pair(int(e->list_int_value_at(0))
					, sha1_hash(e->list_at(1)->string_ptr())));
			}
			if (!nodes.empty() && !t->add_merkle_nodes(nodes, p.piece))
			{
				disconnect(errors::invalid_hash_piece, 2);
				return;
			}
		}

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
		m_statistics.received_bytes(0, received);
		if (packet_size() != 13)
		{
			disconnect(errors::invalid_cancel, 2);
			return;
		}
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
		m_statistics.received_bytes(0, received);
		if (packet_size() != 3)
		{
			disconnect(errors::invalid_dht_port, 2);
			return;
		}
		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();

		const char* ptr = recv_buffer.begin + 1;
		int listen_port = detail::read_uint16(ptr);
		
		incoming_dht_port(listen_port);

		if (!m_supports_dht_port)
		{
			m_supports_dht_port = true;
#ifndef TORRENT_DISABLE_DHT
			if (m_supports_dht_port && m_ses.m_dht)
				write_dht_port(m_ses.m_external_udp_port);
#endif
		}
	}

	void bt_peer_connection::on_suggest_piece(int received)
	{
		INVARIANT_CHECK;

		m_statistics.received_bytes(0, received);
		if (!m_supports_fast)
		{
			disconnect(errors::invalid_suggest, 2);
			return;
		}

		if (!packet_finished()) return;

		buffer::const_interval recv_buffer = receive_buffer();

		const char* ptr = recv_buffer.begin + 1;
		int piece = detail::read_uint32(ptr);
		incoming_suggest(piece);
	}

	void bt_peer_connection::on_have_all(int received)
	{
		INVARIANT_CHECK;

		m_statistics.received_bytes(0, received);
		if (!m_supports_fast)
		{
			disconnect(errors::invalid_have_all, 2);
			return;
		}
		incoming_have_all();
	}

	void bt_peer_connection::on_have_none(int received)
	{
		INVARIANT_CHECK;

		m_statistics.received_bytes(0, received);
		if (!m_supports_fast)
		{
			disconnect(errors::invalid_have_none, 2);
			return;
		}
		incoming_have_none();
	}

	void bt_peer_connection::on_reject_request(int received)
	{
		INVARIANT_CHECK;

		m_statistics.received_bytes(0, received);
		if (!m_supports_fast)
		{
			disconnect(errors::invalid_reject, 2);
			return;
		}

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

		m_statistics.received_bytes(0, received);
		if (!m_supports_fast)
		{
			disconnect(errors::invalid_allow_fast, 2);
			return;
		}

		if (!packet_finished()) return;
		buffer::const_interval recv_buffer = receive_buffer();
		const char* ptr = recv_buffer.begin + 1;
		int index = detail::read_int32(ptr);
		
		incoming_allowed_fast(index);
	}

	// -----------------------------
	// -------- RENDEZVOUS ---------
	// -----------------------------

#ifndef TORRENT_DISABLE_EXTENSIONS
	void bt_peer_connection::on_holepunch()
	{
		INVARIANT_CHECK;

		if (!packet_finished()) return;

		// we can't accept holepunch messages from peers
		// that don't support the holepunch extension
		// because we wouldn't be able to respond
		if (m_holepunch_id == 0) return;

		buffer::const_interval recv_buffer = receive_buffer();
		TORRENT_ASSERT(*recv_buffer.begin == msg_extended);
		++recv_buffer.begin;
		TORRENT_ASSERT(*recv_buffer.begin == holepunch_msg);
		++recv_buffer.begin;

		const char* ptr = recv_buffer.begin;

		// ignore invalid messages
		if (recv_buffer.left() < 2) return;

		int msg_type = detail::read_uint8(ptr);
		int addr_type = detail::read_uint8(ptr);

		tcp::endpoint ep;

		if (addr_type == 0)
		{
			if (recv_buffer.left() < 2 + 4 + 2) return;
			// IPv4 address
			ep = detail::read_v4_endpoint<tcp::endpoint>(ptr);
		}
#if TORRENT_USE_IPV6
		else if (addr_type == 1)
		{
			// IPv6 address
			if (recv_buffer.left() < 2 + 18 + 2) return;
			ep = detail::read_v6_endpoint<tcp::endpoint>(ptr);
		}
#endif
		else
		{
#if defined TORRENT_VERBOSE_LOGGING
			error_code ec;
			static const char* hp_msg_name[] = {"rendezvous", "connect", "failed"};
			peer_log("<== HOLEPUNCH [ msg: %s from %s to: unknown address type ]"
				, (msg_type >= 0 && msg_type < 3 ? hp_msg_name[msg_type] : "unknown message type")
				, print_address(remote().address()).c_str());
#endif

			return; // unknown address type
		}

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		if (!t) return;

		switch (msg_type)
		{
			case hp_rendezvous: // rendezvous
			{
#if defined TORRENT_VERBOSE_LOGGING
				peer_log("<== HOLEPUNCH [ msg: rendezvous to: %s ]"
					, print_address(ep.address()).c_str());
#endif
				// this peer is asking us to introduce it to
				// the peer at 'ep'. We need to find which of
				// our connections points to that endpoint
				bt_peer_connection* p = t->find_peer(ep);
				if (p == 0)
				{
					// we're not connected to this peer
					write_holepunch_msg(hp_failed, ep, hp_not_connected);
					break;
				}
				if (!p->supports_holepunch())
				{
					write_holepunch_msg(hp_failed, ep, hp_no_support);
					break;
				}
				if (p == this)
				{
					write_holepunch_msg(hp_failed, ep, hp_no_self);
					break;
				}

				write_holepunch_msg(hp_connect, ep, 0);
				p->write_holepunch_msg(hp_connect, remote(), 0);
			} break;
			case hp_connect:
			{
				// add or find the peer with this endpoint
				policy::peer* p = t->get_policy().add_peer(ep, peer_id(0), peer_info::pex, 0);
				if (p == 0 || p->connection)
				{
#if defined TORRENT_VERBOSE_LOGGING
					peer_log("<== HOLEPUNCH [ msg:connect to: %s error: failed to add peer ]"
						, print_address(ep.address()).c_str());
#endif
					// we either couldn't add this peer, or it's
					// already connected. Just ignore the connect message
					break;
				}
				if (p->banned)
				{
#if defined TORRENT_VERBOSE_LOGGING
					peer_log("<== HOLEPUNCH [ msg:connect to: %s error: peer banned ]"
						, print_address(ep.address()).c_str());
#endif
					// this peer is banned, don't connect to it
					break;
				
				}
				// to make sure we use the uTP protocol
				p->supports_utp = true;
				// #error make sure we make this a connection candidate
				// in case it has too many failures for instance
				t->connect_to_peer(p, true);
				// mark this connection to be in holepunch mode
				// so that it will retry faster and stick to uTP while it's
				// retrying
				if (p->connection)
					p->connection->set_holepunch_mode();
#if defined TORRENT_VERBOSE_LOGGING
					peer_log("<== HOLEPUNCH [ msg:connect to: %s ]"
						, print_address(ep.address()).c_str());
#endif
			} break;
			case hp_failed:
			{
				boost::uint32_t error = detail::read_uint32(ptr);
#if defined TORRENT_VERBOSE_LOGGING
				error_code ec;
				char const* err_msg[] = {"no such peer", "not connected", "no support", "no self"};
				peer_log("<== HOLEPUNCH [ msg:failed error: %d msg: %s ]", error
					, ((error > 0 && error < 5)?err_msg[error-1]:"unknown message id"));
#endif
				// #error deal with holepunch errors
				(void)error;
			} break;
#if defined TORRENT_VERBOSE_LOGGING
			default:
			{
				error_code ec;
				peer_log("<== HOLEPUNCH [ msg: unknown message type (%d) to: %s ]"
					, msg_type, print_address(ep.address()).c_str());
			}
#endif
		}
	}

	void bt_peer_connection::write_holepunch_msg(int type, tcp::endpoint const& ep, int error)
	{
		char buf[35];
		char* ptr = buf + 6;
		detail::write_uint8(type, ptr);
		if (ep.address().is_v4()) detail::write_uint8(0, ptr);
		else detail::write_uint8(1, ptr);
		detail::write_endpoint(ep, ptr);

#if defined TORRENT_VERBOSE_LOGGING
		error_code ec;
		static const char* hp_msg_name[] = {"rendezvous", "connect", "failed"};
		static const char* hp_error_string[] = {"", "no such peer", "not connected", "no support", "no self"};
		peer_log("==> HOLEPUNCH [ msg: %s to: %s error: %s ]"
			, (type >= 0 && type < 3 ? hp_msg_name[type] : "unknown message type")
			, print_address(ep.address()).c_str()
			, hp_error_string[error]);
#endif
		if (type == hp_failed)
		{
			detail::write_uint32(error, ptr);
		}

		// write the packet length and type
		char* hdr = buf;
		detail::write_uint32(ptr - buf - 4, hdr);
		detail::write_uint8(msg_extended, hdr);
		detail::write_uint8(m_holepunch_id, hdr);

		TORRENT_ASSERT(ptr <= buf + sizeof(buf));

		send_buffer(buf, ptr - buf);
	}
#endif // TORRENT_DISABLE_EXTENSIONS

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
			disconnect(errors::invalid_extended, 2);
			return;
		}

		if (associated_torrent().expired())
		{
			disconnect(errors::invalid_extended, 2);
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
			disconnect_if_redundant();
			return;
		}

		if (extended_id == upload_only_msg)
		{
			if (!packet_finished()) return;
			if (packet_size() != 3)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("<== UPLOAD_ONLY [ ERROR: unexpected packet size: %d ]", packet_size());
#endif
				return;
			}
			bool ul = detail::read_uint8(recv_buffer.begin) != 0;
#ifdef TORRENT_VERBOSE_LOGGING
			peer_log("<== UPLOAD_ONLY [ %s ]", (ul?"true":"false"));
#endif
			set_upload_only(ul);
			return;
		}

		if (extended_id == share_mode_msg)
		{
			if (!packet_finished()) return;
			if (packet_size() != 3)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("<== SHARE_MODE [ ERROR: unexpected packet size: %d ]", packet_size());
#endif
				return;
			}
			bool sm = detail::read_uint8(recv_buffer.begin) != 0;
#ifdef TORRENT_VERBOSE_LOGGING
			peer_log("<== SHARE_MODE [ %s ]", (sm?"true":"false"));
#endif
			set_share_mode(sm);
			return;
		}

		if (extended_id == holepunch_msg)
		{
			if (!packet_finished()) return;
#ifdef TORRENT_VERBOSE_LOGGING
			peer_log("<== HOLEPUNCH");
#endif
			on_holepunch();
			return;
		}

		if (extended_id == dont_have_msg)
		{
			if (!packet_finished()) return;
			if (packet_size() != 6)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("<== DONT_HAVE [ ERROR: unexpected packet size: %d ]", packet_size());
#endif
				return;
			}
			int piece = detail::read_uint32(recv_buffer.begin) != 0;
			incoming_dont_have(piece);
			return;
		}

#ifdef TORRENT_VERBOSE_LOGGING
		if (packet_finished())
			peer_log("<== EXTENSION MESSAGE [ msg: %d size: %d ]"
				, extended_id, packet_size());
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_extended(packet_size() - 2, extended_id
				, recv_buffer))
				return;
		}
#endif

		disconnect(errors::invalid_message, 2);
		return;
	}

	void bt_peer_connection::on_extended_handshake()
	{
		if (!packet_finished()) return;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		buffer::const_interval recv_buffer = receive_buffer();

		lazy_entry root;
		error_code ec;
		int pos;
		int ret = lazy_bdecode(recv_buffer.begin + 2, recv_buffer.end, root, ec, &pos);
		if (ret != 0 || ec || root.type() != lazy_entry::dict_t)
		{
#ifdef TORRENT_VERBOSE_LOGGING
			peer_log("*** invalid extended handshake: %s pos: %d"
				, ec.message().c_str(), pos);
#endif
			return;
		}

#ifdef TORRENT_VERBOSE_LOGGING
		peer_log("<== EXTENDED HANDSHAKE: %s", print_entry(root).c_str());
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

		// upload_only
		if (lazy_entry const* m = root.dict_find_dict("m"))
		{
			m_upload_only_id = boost::uint8_t(m->dict_find_int_value("upload_only", 0));
			m_holepunch_id = boost::uint8_t(m->dict_find_int_value("ut_holepunch", 0));
			m_dont_have_id = boost::uint8_t(m->dict_find_int_value("lt_donthave", 0));
		}
#endif

		// there is supposed to be a remote listen port
		int listen_port = int(root.dict_find_int_value("p"));
		if (listen_port > 0 && peer_info_struct() != 0)
		{
			t->get_policy().update_peer_port(listen_port
				, peer_info_struct(), peer_info::incoming);
			received_listen_port();
			if (is_disconnecting()) return;
		}

		// there should be a version too
		// but where do we put that info?

		int last_seen_complete = boost::uint8_t(root.dict_find_int_value("complete_ago", -1));
		if (last_seen_complete >= 0) set_last_seen_complete(last_seen_complete);
		
		std::string client_info = root.dict_find_string_value("v");
		if (!client_info.empty()) m_client_version = client_info;

		int reqq = int(root.dict_find_int_value("reqq"));
		if (reqq > 0) m_max_out_request_queue = reqq;

		if (root.dict_find_int_value("upload_only", 0))
			set_upload_only(true);

		if (root.dict_find_int_value("share_mode", 0))
			set_share_mode(true);

		std::string myip = root.dict_find_string_value("yourip");
		if (!myip.empty())
		{
			// TODO: don't trust this blindly
			if (myip.size() == address_v4::bytes_type().size())
			{
				address_v4::bytes_type bytes;
				std::copy(myip.begin(), myip.end(), bytes.begin());
				m_ses.set_external_address(address_v4(bytes)
					, aux::session_impl::source_peer, remote().address());
			}
#if TORRENT_USE_IPV6
			else if (myip.size() == address_v6::bytes_type().size())
			{
				address_v6::bytes_type bytes;
				std::copy(myip.begin(), myip.end(), bytes.begin());
				address_v6 ipv6_address(bytes);
				if (ipv6_address.is_v4_mapped())
					m_ses.set_external_address(ipv6_address.to_v4()
						, aux::session_impl::source_peer, remote().address());
				else
					m_ses.set_external_address(ipv6_address
						, aux::session_impl::source_peer, remote().address());
			}
#endif
		}

		// if we're finished and this peer is uploading only
		// disconnect it
		if (t->is_finished() && upload_only()
			&& t->settings().close_redundant_connections
			&& !t->share_mode())
			disconnect(errors::upload_upload_connection);
	}

	bool bt_peer_connection::dispatch_message(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received > 0);

		// this means the connection has been closed already
		if (associated_torrent().expired())
		{
			m_statistics.received_bytes(0, received);
			return false;
		}

		buffer::const_interval recv_buffer = receive_buffer();

		TORRENT_ASSERT(recv_buffer.left() >= 1);
		int packet_type = (unsigned char)recv_buffer[0];
		if (packet_type == 250) packet_type = msg_piece;
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

			m_statistics.received_bytes(0, received);
			// What's going on here?!
			// break in debug builds to allow investigation
//			TORRENT_ASSERT(false);
			disconnect(errors::invalid_message);
			return packet_finished();
		}

		TORRENT_ASSERT(m_message_handler[packet_type] != 0);

#ifdef TORRENT_DEBUG
		size_type cur_payload_dl = m_statistics.last_payload_downloaded();
		size_type cur_protocol_dl = m_statistics.last_protocol_downloaded();
#endif
		// call the correct handler for this packet type
		(this->*m_message_handler[packet_type])(received);
#ifdef TORRENT_DEBUG
		TORRENT_ASSERT(m_statistics.last_payload_downloaded() - cur_payload_dl >= 0);
		TORRENT_ASSERT(m_statistics.last_protocol_downloaded() - cur_protocol_dl >= 0);
		size_type stats_diff = m_statistics.last_payload_downloaded() - cur_payload_dl +
			m_statistics.last_protocol_downloaded() - cur_protocol_dl;
		TORRENT_ASSERT(stats_diff == received);
#endif

		return packet_finished();
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void bt_peer_connection::write_upload_only()
	{
		INVARIANT_CHECK;
		
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		if (m_upload_only_id == 0) return;
		if (t->share_mode()) return;

		// if we send upload-only, the other end is very likely to disconnect
		// us, at least if it's a seed. If we don't want to close redundant
		// connections, don't sent upload-only
		if (!m_ses.settings().close_redundant_connections) return;

		char msg[7] = {0, 0, 0, 3, msg_extended};
		char* ptr = msg + 5;
		detail::write_uint8(m_upload_only_id, ptr);
		// if we're super seeding, we don't want to make peers
		// think that we only have a single piece and is upload
		// only, since they might disconnect immediately when
		// they have downloaded a single piece, although we'll
		// make another piece available
		detail::write_uint8(t->is_upload_only() && !t->super_seeding(), ptr);
		send_buffer(msg, sizeof(msg));
	}

	void bt_peer_connection::write_share_mode()
	{
		INVARIANT_CHECK;
		
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		if (m_share_mode_id == 0) return;

		char msg[7] = {0, 0, 0, 3, msg_extended};
		char* ptr = msg + 5;
		detail::write_uint8(m_share_mode_id, ptr);
		detail::write_uint8(t->share_mode(), ptr);
		send_buffer(msg, sizeof(msg));
	}
#endif

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

		if (t->super_seeding())
		{
			if (m_supports_fast) write_have_none();

			// if we are super seeding, pretend to not have any piece
			// and don't send a bitfield
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			m_sent_bitfield = true;
#endif

			// bootstrap superseeding by sending one have message
			superseed_piece(t->get_piece_to_super_seed(
				get_bitfield()));
			return;
		}
		else if (m_supports_fast && t->is_seed())
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
			peer_log(" *** NOT SENDING BITFIELD");
#endif
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
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
				if (int(random() % (num_pieces - i)) >= num_lazy_pieces - lazy_piece) continue;
				lazy_pieces[lazy_piece++] = i;
			}
			TORRENT_ASSERT(lazy_piece == num_lazy_pieces);
			lazy_piece = 0;
		}

		const int packet_size = (num_pieces + 7) / 8 + 5;
	
		char* msg = TORRENT_ALLOCA(char, packet_size);
		if (msg == 0) return; // out of memory
		unsigned char* ptr = (unsigned char*)msg;

		detail::write_int32(packet_size - 4, ptr);
		detail::write_uint8(msg_bitfield, ptr);

		if (t->is_seed())
		{
			memset(ptr, 0xff, packet_size - 6);

			// Clear trailing bits
			unsigned char *p = ((unsigned char *)msg) + packet_size - 1;
			*p = (0xff << ((8 - (num_pieces & 7)) & 7)) & 0xff;
		}
		else
		{
			memset(ptr, 0, packet_size - 5);
			piece_picker const& p = t->picker();
			int mask = 0x80;
			for (int i = 0; i < num_pieces; ++i)
			{
				if (p.have_piece(i)) *ptr |= mask;
				mask >>= 1;
				if (mask == 0)
				{
					mask = 0x80;
					++ptr;
				}
			}
		}
		for (int c = 0; c < num_lazy_pieces; ++c)
			msg[5 + lazy_pieces[c] / 8] &= ~(0x80 >> (lazy_pieces[c] & 7));

#ifdef TORRENT_VERBOSE_LOGGING

		std::string bitfield_string;
		bitfield_string.resize(num_pieces);
		for (int k = 0; k < num_pieces; ++k)
		{
			if (msg[5 + k / 8] & (0x80 >> (k % 8))) bitfield_string[k] = '1';
			else bitfield_string[k] = '0';
		}
		peer_log("==> BITFIELD [ %s ]", bitfield_string.c_str());
#endif
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		m_sent_bitfield = true;
#endif

		send_buffer(msg, packet_size);

		if (num_lazy_pieces > 0)
		{
			for (int i = 0; i < num_lazy_pieces; ++i)
			{
#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("==> HAVE    [ piece: %d ]", lazy_pieces[i]);
#endif
				write_have(lazy_pieces[i]);
			}
			// TODO: if we're finished, send upload_only message
		}

		if (m_supports_fast)
			send_allowed_set();
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void bt_peer_connection::write_extensions()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_supports_extensions);
		TORRENT_ASSERT(m_sent_handshake);

		entry handshake;
		entry::dictionary_type& m = handshake["m"].dict();

		// only send the port in case we bade the connection
		// on incoming connections the other end already knows
		// our listen port
		if (!m_ses.m_settings.anonymous_mode)
		{
			if (is_outgoing()) handshake["p"] = m_ses.listen_port();
			handshake["v"] = m_ses.settings().user_agent;
		}

		std::string remote_address;
		std::back_insert_iterator<std::string> out(remote_address);
		detail::write_address(remote().address(), out);
		handshake["yourip"] = remote_address;
		handshake["reqq"] = m_ses.settings().max_allowed_in_request_queue;
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		m["upload_only"] = upload_only_msg;
		m["ut_holepunch"] = holepunch_msg;
		m["share_mode"] = share_mode_msg;
		m["lt_donthave"] = dont_have_msg;

		int complete_ago = -1;
		if (t->last_seen_complete() > 0) complete_ago = t->time_since_complete();
		handshake["complete_ago"] = complete_ago;

		// if we're using lazy bitfields or if we're super seeding, don't say
		// we're upload only, since it might make peers disconnect
		// don't tell anyone we're upload only when in share mode
		// we want to stay connected to seeds
		// if we're super seeding, we don't want to make peers
		// think that we only have a single piece and is upload
		// only, since they might disconnect immediately when
		// they have downloaded a single piece, although we'll
		// make another piece available
		if (t->is_upload_only()
			&& !t->share_mode()
			&& !t->super_seeding()
			&& (!m_ses.settings().lazy_bitfields
#ifndef TORRENT_DISABLE_ENCRYPTION
			|| m_encrypted
#endif
			))
			handshake["upload_only"] = 1;

		if (t->share_mode())
			handshake["share_mode"] = 1;

		if (!m_ses.m_settings.anonymous_mode)
		{
			tcp::endpoint ep = m_ses.get_ipv6_interface();
			if (!is_any(ep.address()))
			{
				std::string ipv6_address;
				std::back_insert_iterator<std::string> out(ipv6_address);
				detail::write_address(ep.address(), out);
				handshake["ipv6"] = ipv6_address;
			}
		}

		// loop backwards, to make the first extension be the last
		// to fill in the handshake (i.e. give the first extensions priority)
		for (extension_list_t::reverse_iterator i = m_extensions.rbegin()
			, end(m_extensions.rend()); i != end; ++i)
		{
			(*i)->add_handshake(handshake);
		}

#ifndef NDEBUG
		// make sure there are not conflicting extensions
		std::set<int> ext;
		for (entry::dictionary_type::const_iterator i = m.begin()
			, end(m.end()); i != end; ++i)
		{
			if (i->second.type() != entry::int_t) continue;
			int val = int(i->second.integer());
			TORRENT_ASSERT(ext.find(val) == ext.end());
			ext.insert(val);
		}
#endif

		std::vector<char> dict_msg;
		bencode(std::back_inserter(dict_msg), handshake);

		char msg[6];
		char* ptr = msg;
		
		// write the length of the message
		detail::write_int32((int)dict_msg.size() + 2, ptr);
		detail::write_uint8(msg_extended, ptr);
		// signal handshake message
		detail::write_uint8(0, ptr);
		send_buffer(msg, sizeof(msg));
		send_buffer(&dict_msg[0], dict_msg.size());

#if defined TORRENT_VERBOSE_LOGGING && TORRENT_USE_IOSTREAM
		std::stringstream handshake_str;
		handshake.print(handshake_str);
		peer_log("==> EXTENDED HANDSHAKE: %s", handshake_str.str().c_str());
#endif
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

		bool merkle = t->torrent_file().is_merkle_torrent() && r.start == 0;
	// the hash piece looks like this:
	// uint8_t  msg
	// uint32_t piece index
	// uint32_t start
	// uint32_t list len
	// var      bencoded list
	// var      piece data
		char msg[4 + 1 + 4 + 4 + 4];
		char* ptr = msg;
		TORRENT_ASSERT(r.length <= 16 * 1024);
		detail::write_int32(r.length + 1 + 4 + 4, ptr);
		if (merkle)
			detail::write_uint8(250, ptr);
		else
			detail::write_uint8(msg_piece, ptr);
		detail::write_int32(r.piece, ptr);
		detail::write_int32(r.start, ptr);

		// if this is a merkle torrent and the start offset
		// is 0, we need to include the merkle node hashes
		if (merkle)
		{
			std::vector<char>	piece_list_buf;
			entry piece_list;
			entry::list_type& l = piece_list.list();
			std::map<int, sha1_hash> merkle_node_list = t->torrent_file().build_merkle_list(r.piece);
			for (std::map<int, sha1_hash>::iterator i = merkle_node_list.begin()
				, end(merkle_node_list.end()); i != end; ++i)
			{
				l.push_back(entry(entry::list_t));
				l.back().list().push_back(i->first);
				l.back().list().push_back(i->second.to_string());
			}
			bencode(std::back_inserter(piece_list_buf), piece_list);
			detail::write_int32(piece_list_buf.size(), ptr);

			char* ptr = msg;
			detail::write_int32(r.length + 1 + 4 + 4 + 4 + piece_list_buf.size(), ptr);

			send_buffer(msg, 17);
			send_buffer(&piece_list_buf[0], piece_list_buf.size());
		}
		else
		{
			send_buffer(msg, 13);
		}

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

			bool operator()(policy::peer const* p) const
			{
				return p->connection != m_pc
					&& p->connection
					&& p->connection->pid() == m_id
					&& !p->connection->pid().is_all_zeros()
					&& p->address() == m_pc->remote().address();
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

		if (error)
		{
			m_statistics.received_bytes(0, bytes_transferred);
			return;
		}

		boost::shared_ptr<torrent> t = associated_torrent().lock();
	
#ifndef TORRENT_DISABLE_ENCRYPTION
		TORRENT_ASSERT(in_handshake() || !m_rc4_encrypted || m_encrypted);
		if (m_rc4_encrypted && m_encrypted)
		{
			std::pair<buffer::interval, buffer::interval> wr_buf = wr_recv_buffers(bytes_transferred);
			m_enc_handler->decrypt(wr_buf.first.begin, wr_buf.first.left());
			if (wr_buf.second.left()) m_enc_handler->decrypt(wr_buf.second.begin, wr_buf.second.left());
		}
#endif

		buffer::const_interval recv_buffer = receive_buffer();

#ifndef TORRENT_DISABLE_ENCRYPTION
		// m_state is set to read_pe_dhkey in initial state
		// (read_protocol_identifier) for incoming, or in constructor
		// for outgoing
		if (m_state == read_pe_dhkey)
		{
			m_statistics.received_bytes(0, bytes_transferred);

			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
			TORRENT_ASSERT(packet_size() == dh_key_len);
			TORRENT_ASSERT(recv_buffer == receive_buffer());

			if (!packet_finished()) return;
			
			// write our dh public key. m_dh_key_exchange is
			// initialized in write_pe1_2_dhkey()
			if (!is_outgoing()) write_pe1_2_dhkey();
			if (is_disconnecting()) return;
			
			// read dh key, generate shared secret
			if (m_dh_key_exchange->compute_secret(recv_buffer.begin) == -1)
			{
				disconnect(errors::no_memory);
				return;
			}

#ifdef TORRENT_VERBOSE_LOGGING
			peer_log("*** received DH key");
#endif
						
			// PadA/B can be a max of 512 bytes, and 20 bytes more for
			// the sync hash (if incoming), or 8 bytes more for the
			// encrypted verification constant (if outgoing). Instead
			// of requesting the maximum possible, request the maximum
			// possible to ensure we do not overshoot the standard
			// handshake.

			if (is_outgoing())
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
			TORRENT_ASSERT(!is_outgoing());
			TORRENT_ASSERT(recv_buffer == receive_buffer());
		   
 			if (recv_buffer.left() < 20)
			{
				m_statistics.received_bytes(0, bytes_transferred);

				if (packet_finished())
					disconnect(errors::sync_hash_not_found, 1);
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
					m_statistics.received_bytes(0, bytes_transferred);
					disconnect(errors::no_memory);
					return;
				}
			}

			int syncoffset = get_syncoffset((char*)m_sync_hash->begin(), 20
				, recv_buffer.begin, recv_buffer.left());

			// No sync 
			if (syncoffset == -1)
			{
				m_statistics.received_bytes(0, bytes_transferred);

				std::size_t bytes_processed = recv_buffer.left() - 20;
				m_sync_bytes_read += bytes_processed;
				if (m_sync_bytes_read >= 512)
				{
					disconnect(errors::sync_hash_not_found, 1);
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
				peer_log("*** sync point (hash) found at offset %d"
					, m_sync_bytes_read + bytes_processed - 20);
#endif
				m_state = read_pe_skey_vc;
				// skey,vc - 28 bytes
				m_sync_hash.reset();
				int transferred_used = bytes_processed - recv_buffer.left() + bytes_transferred;
				TORRENT_ASSERT(transferred_used <= int(bytes_transferred));
				m_statistics.received_bytes(0, transferred_used);
				bytes_transferred -= transferred_used;
				cut_receive_buffer(bytes_processed, 28);
			}
		}

		if (m_state == read_pe_skey_vc)
		{
			m_statistics.received_bytes(0, bytes_transferred);
			bytes_transferred = 0;

			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
			TORRENT_ASSERT(!is_outgoing());
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
						attach_to_torrent(ti.info_hash(), false);
						if (is_disconnecting()) return;

						t = associated_torrent().lock();
						TORRENT_ASSERT(t);
					}

					init_pe_rc4_handler(m_dh_key_exchange->get_secret(), ti.info_hash());
#ifdef TORRENT_VERBOSE_LOGGING
					peer_log("*** stream key found, torrent located");
#endif
					break;
				}
			}

			if (!m_enc_handler.get())
			{
				disconnect(errors::invalid_info_hash, 1);
				return;
			}

			// verify constant
			buffer::interval wr_recv_buf = wr_recv_buffer();
			m_enc_handler->decrypt(wr_recv_buf.begin + 20, 8);
			wr_recv_buf.begin += 28;

			const char sh_vc[] = {0,0,0,0, 0,0,0,0};
			if (!std::equal(sh_vc, sh_vc+8, recv_buffer.begin + 20))
			{
				disconnect(errors::invalid_encryption_constant, 2);
				return;
			}

#ifdef TORRENT_VERBOSE_LOGGING
			peer_log("*** verification constant found");
#endif
			m_state = read_pe_cryptofield;
			reset_recv_buffer(4 + 2);
		}

		// cannot fall through into
		if (m_state == read_pe_syncvc)
		{
			TORRENT_ASSERT(is_outgoing());
			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
 			TORRENT_ASSERT(recv_buffer == receive_buffer());
			
			if (recv_buffer.left() < 8)
			{
				m_statistics.received_bytes(0, bytes_transferred);
				if (packet_finished())
					disconnect(errors::invalid_encryption_constant, 2);
				return;
			}

			// generate the verification constant
			if (!m_sync_vc.get()) 
			{
				TORRENT_ASSERT(m_sync_bytes_read == 0);

				m_sync_vc.reset(new (std::nothrow) char[8]);
				if (!m_sync_vc)
				{
					disconnect(errors::no_memory);
					return;
				}
				std::fill(m_sync_vc.get(), m_sync_vc.get() + 8, 0);
				m_enc_handler->decrypt(m_sync_vc.get(), 8);
			}

			TORRENT_ASSERT(m_sync_vc.get());
			int syncoffset = get_syncoffset(m_sync_vc.get(), 8
				, recv_buffer.begin, recv_buffer.left());

			// No sync 
			if (syncoffset == -1)
			{
				std::size_t bytes_processed = recv_buffer.left() - 8;
				m_sync_bytes_read += bytes_processed;
				m_statistics.received_bytes(0, bytes_transferred);

				if (m_sync_bytes_read >= 512)
				{
					disconnect(errors::invalid_encryption_constant, 2);
					return;
				}

				cut_receive_buffer(bytes_processed, (std::min)(packet_size()
					, (512+8) - m_sync_bytes_read));

				TORRENT_ASSERT(!packet_finished());
			}
			// found complete sync
			else
			{
				std::size_t bytes_processed = syncoffset + 8;
#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("*** sync point (verification constant) found at offset %d"
					, m_sync_bytes_read + bytes_processed - 8);
#endif
				int transferred_used = bytes_processed - recv_buffer.left() + bytes_transferred;
				TORRENT_ASSERT(transferred_used <= int(bytes_transferred));
				m_statistics.received_bytes(0, transferred_used);
				bytes_transferred -= transferred_used;

				cut_receive_buffer(bytes_processed, 4 + 2);

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
			m_statistics.received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			
			if (!packet_finished()) return;

			buffer::interval wr_buf = wr_recv_buffer();
			m_enc_handler->decrypt(wr_buf.begin, packet_size());

			recv_buffer = receive_buffer();
			
			int crypto_field = detail::read_int32(recv_buffer.begin);

#ifdef TORRENT_VERBOSE_LOGGING
			peer_log("*** crypto %s : [%s%s ]"
				, is_outgoing() ? "select" : "provide"
				, (crypto_field & 1) ? " plaintext" : ""
				, (crypto_field & 2) ? " rc4" : "");
#endif

			if (!is_outgoing())
			{
				// select a crypto method
				int allowed_encryption = m_ses.get_pe_settings().allowed_enc_level;
				int crypto_select = crypto_field & allowed_encryption;
	
				// when prefer_rc4 is set, keep the most significant bit
				// otherwise keep the least significant one
				if (m_ses.get_pe_settings().prefer_rc4)
				{
					int mask = INT_MAX;
					while (crypto_select & (mask << 1))
					{
						mask <<= 1;
						crypto_select = crypto_select & mask;
					}
				}
				else
				{
					int mask = INT_MAX;
					while (crypto_select & (mask >> 1))
					{
						mask >>= 1;
						crypto_select = crypto_select & mask;
					}
				}

				if (crypto_select == 0)
				{
						disconnect(errors::unsupported_encryption_mode, 1);
						return;
				}

				// write the pe4 step
				write_pe4_sync(crypto_select);
			}
			else // is_outgoing()
			{
				// check if crypto select is valid
				int allowed_encryption = m_ses.get_pe_settings().allowed_enc_level;

				crypto_field &= allowed_encryption; 
				if (crypto_field == 0)
				{
					// we don't allow any of the offered encryption levels
					disconnect(errors::unsupported_encryption_mode_selected, 2);
					return;
				}

				if (crypto_field == pe_settings::plaintext)
					m_rc4_encrypted = false;
				else if (crypto_field == pe_settings::rc4)
					m_rc4_encrypted = true;
			}

			int len_pad = detail::read_int16(recv_buffer.begin);
			if (len_pad < 0 || len_pad > 512)
			{
				disconnect(errors::invalid_pad_size, 2);
				return;
			}
			
			m_state = read_pe_pad;
			if (!is_outgoing())
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
			m_statistics.received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			if (!packet_finished()) return;

			int pad_size = is_outgoing() ? packet_size() : packet_size() - 2;

			buffer::interval wr_buf = wr_recv_buffer();
			m_enc_handler->decrypt(wr_buf.begin, packet_size());

			recv_buffer = receive_buffer();
				
			if (!is_outgoing())
			{
				recv_buffer.begin += pad_size;
				int len_ia = detail::read_int16(recv_buffer.begin);
				
				if (len_ia < 0) 
				{
					disconnect(errors::invalid_encrypt_handshake, 2);
					return;
				}

#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("*** len(IA) : %d", len_ia);
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
			else // is_outgoing()
			{
				// everything that arrives after this is Encrypt2
				m_encrypted = true;
				m_state = init_bt_handshake;
			}
		}

		if (m_state == read_pe_ia)
		{
			m_statistics.received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			TORRENT_ASSERT(!is_outgoing());
			TORRENT_ASSERT(!m_encrypted);

			if (!packet_finished()) return;

			// ia is always rc4, so decrypt it
			buffer::interval wr_buf = wr_recv_buffer();
			m_enc_handler->decrypt(wr_buf.begin, packet_size());

#ifdef TORRENT_VERBOSE_LOGGING
			peer_log("*** decrypted ia : %d bytes", packet_size());
#endif

			if (!m_rc4_encrypted)
			{
				m_enc_handler.reset();
#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("*** destroyed rc4 keys");
#endif
			}

			// everything that arrives after this is encrypted
			m_encrypted = true;

			m_state = read_protocol_identifier;
			cut_receive_buffer(0, 20);
		}

		if (m_state == init_bt_handshake)
		{
			m_statistics.received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			TORRENT_ASSERT(m_encrypted);

			// decrypt remaining received bytes
			if (m_rc4_encrypted)
			{
				buffer::interval wr_buf = wr_recv_buffer();
				wr_buf.begin += packet_size();
				m_enc_handler->decrypt(wr_buf.begin, wr_buf.left());
#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("*** decrypted remaining %d bytes", wr_buf.left());
#endif
			}
			else // !m_rc4_encrypted
			{
				m_enc_handler.reset();
#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("*** destroyed encryption handler");
#endif
			}

			// payload stream, start with 20 handshake bytes
			m_state = read_protocol_identifier;
			reset_recv_buffer(20);

			// encrypted portion of handshake completed, toggle
			// peer_info pe_support flag back to true
			if (is_outgoing() &&
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
			m_statistics.received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			TORRENT_ASSERT(packet_size() == 20);

			if (!packet_finished()) return;
			recv_buffer = receive_buffer();

			int packet_size = recv_buffer[0];
			const char protocol_string[] = "\x13" "BitTorrent protocol";

			if (packet_size != 19 ||
				memcmp(recv_buffer.begin, protocol_string, 20) != 0)
			{
#ifndef TORRENT_DISABLE_ENCRYPTION
#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("*** unrecognized protocol header");
#endif

#ifdef TORRENT_USE_OPENSSL
				if (is_ssl(*get_socket()))
				{
#ifdef TORRENT_VERBOSE_LOGGING
					peer_log("*** SSL peers are not allowed to use any other encryption");
#endif
					disconnect(errors::invalid_info_hash, 1);
					return;
				}
#endif // TORRENT_USE_OPENSSL

				if (!is_outgoing()
					&& m_ses.get_pe_settings().in_enc_policy == pe_settings::disabled)
				{
					disconnect(errors::no_incoming_encrypted);
					return;
				}

				// Don't attempt to perform an encrypted handshake
				// within an encrypted connection. For local connections,
				// we're expected to already have passed the encrypted
				// handshake by this point
				if (m_encrypted || is_outgoing())
				{
					disconnect(errors::invalid_info_hash, 1);
					return;
				}

#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("*** attempting encrypted connection");
#endif
				m_state = read_pe_dhkey;
				cut_receive_buffer(0, dh_key_len);
				TORRENT_ASSERT(!packet_finished());
				return;
#else
				disconnect(errors::invalid_info_hash, 1);
				return;
#endif // TORRENT_DISABLE_ENCRYPTION
			}
			else
			{
#ifndef TORRENT_DISABLE_ENCRYPTION
				TORRENT_ASSERT(m_state != read_pe_dhkey);

				if (!is_outgoing()
					&& m_ses.get_pe_settings().in_enc_policy == pe_settings::forced
					&& !m_encrypted
					&& !is_ssl(*get_socket()))
				{
					disconnect(errors::no_incoming_regular);
					return;
				}
#endif

#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("<== BitTorrent protocol");
#endif
			}

			m_state = read_info_hash;
			reset_recv_buffer(28);
		}

		// fall through
		if (m_state == read_info_hash)
		{
			m_statistics.received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			TORRENT_ASSERT(packet_size() == 28);

			if (!packet_finished()) return;
			recv_buffer = receive_buffer();


#ifdef TORRENT_VERBOSE_LOGGING	
			std::string extensions;
			extensions.resize(8 * 8);
			for (int i=0; i < 8; ++i)
			{
				for (int j=0; j < 8; ++j)
				{
					if (recv_buffer[i] & (0x80 >> j)) extensions[i*8+j] = '1';
					else extensions[i*8+j] = '0';
				}
			}
			peer_log("<== EXTENSIONS [ %s ext: %s%s%s]"
				, extensions.c_str()
				, (recv_buffer[7] & 0x01) ? "DHT " : ""
				, (recv_buffer[7] & 0x04) ? "FAST " : ""
				, (recv_buffer[5] & 0x10) ? "extension " : "");
#endif

#ifndef TORRENT_DISABLE_EXTENSIONS
			std::memcpy(m_reserved_bits, recv_buffer.begin, 8);
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

#ifndef TORRENT_DISABLE_ENCRYPTION
				bool allow_encrypted = m_encrypted && m_rc4_encrypted;
#else
				bool allow_encrypted = true;
#endif

				attach_to_torrent(info_hash, allow_encrypted);
				if (is_disconnecting()) return;
			}
			else
			{
				// verify info hash
				if (!std::equal(recv_buffer.begin + 8, recv_buffer.begin + 28
					, (const char*)t->torrent_file().info_hash().begin()))
				{
#ifdef TORRENT_VERBOSE_LOGGING
					peer_log("*** received invalid info_hash");
#endif
					disconnect(errors::invalid_info_hash, 1);
					return;
				}

#ifdef TORRENT_VERBOSE_LOGGING
				peer_log("<<< info_hash received");
#endif
			}

			t = associated_torrent().lock();
			TORRENT_ASSERT(t);
			
			// if this is a local connection, we have already
			// sent the handshake
			if (!is_outgoing()) write_handshake();
//			if (t->valid_metadata())
//				write_bitfield();
			TORRENT_ASSERT(m_sent_handshake);

			if (is_disconnecting()) return;

			TORRENT_ASSERT(t->get_policy().has_connection(this));

			m_state = read_peer_id;
 			reset_recv_buffer(20);
		}

		// fall through
		if (m_state == read_peer_id)
		{
			TORRENT_ASSERT(m_sent_handshake);
			m_statistics.received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
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
				char hex_pid[41];
				to_hex(recv_buffer.begin, 20, hex_pid);
				hex_pid[40] = 0;
				char ascii_pid[21];
				ascii_pid[20] = 0;
				for (int i = 0; i != 20; ++i)
				{
					if (is_print(recv_buffer.begin[i])) ascii_pid[i] = recv_buffer.begin[i];
					else ascii_pid[i] = '.';
				}
				peer_log("<<< received peer_id: %s client: %s\nas ascii: %s\n"
					, hex_pid, identify_client(peer_id(recv_buffer.begin)).c_str(), ascii_pid);
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
					TORRENT_ASSERT((*i)->connection->pid() == pid);
					// we found another connection with the same peer-id
					// which connection should be closed in order to be
					// sure that the other end closes the same connection?
					// the peer with greatest peer-id is the one allowed to
					// initiate connections. So, if our peer-id is greater than
					// the others, we should close the incoming connection,
					// if not, we should close the outgoing one.
					if (pid < m_ses.get_peer_id() && is_outgoing())
					{
						(*i)->connection->disconnect(errors::duplicate_peer_id);
					}
					else
					{
						disconnect(errors::duplicate_peer_id);
						return;
					}
				}
			}

			// disconnect if the peer has the same peer-id as ourself
			// since it most likely is ourself then
			if (pid == m_ses.get_peer_id())
			{
				if (peer_info_struct()) t->get_policy().ban_peer(peer_info_struct());
				disconnect(errors::self_connection, 1);
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
			peer_log("<== HANDSHAKE");
#endif
			// consider this a successful connection, reset the failcount
			if (peer_info_struct()) t->get_policy().set_failcount(peer_info_struct(), 0);
			
#ifndef TORRENT_DISABLE_ENCRYPTION
			// Toggle pe_support back to false if this is a
			// standard successful connection
			if (is_outgoing() && !m_encrypted &&
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
					write_dht_port(m_ses.m_external_udp_port);
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

			if (recv_buffer.left() < 4)
			{
				m_statistics.received_bytes(0, bytes_transferred);
				return;
			}
			int transferred_used = 4 - recv_buffer.left() + bytes_transferred;
			TORRENT_ASSERT(transferred_used <= int(bytes_transferred));
			m_statistics.received_bytes(0, transferred_used);
			bytes_transferred -= transferred_used;

			const char* ptr = recv_buffer.begin;
			int packet_size = detail::read_int32(ptr);

			// don't accept packets larger than 1 MB
			if (packet_size > 1024*1024 || packet_size < 0)
			{
				m_statistics.received_bytes(0, bytes_transferred);
				// packet too large
				disconnect(errors::packet_too_large, 2);
				return;
			}
					
			if (packet_size == 0)
			{
				m_statistics.received_bytes(0, bytes_transferred);
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
				TORRENT_ASSERT(bytes_transferred == 1);
				recv_buffer = receive_buffer();
				TORRENT_ASSERT(recv_buffer.left() == 1);
			}
		}

		if (m_state == read_packet)
		{
			TORRENT_ASSERT(recv_buffer == receive_buffer());
			if (!t)
			{
				m_statistics.received_bytes(0, bytes_transferred);
				disconnect(errors::torrent_removed, 1);
				return;
			}
#ifdef TORRENT_DEBUG
			size_type cur_payload_dl = m_statistics.last_payload_downloaded();
			size_type cur_protocol_dl = m_statistics.last_protocol_downloaded();
#endif
			if (dispatch_message(bytes_transferred))
			{
				m_state = read_packet_size;
				reset_recv_buffer(5);
			}
#ifdef TORRENT_DEBUG
			TORRENT_ASSERT(m_statistics.last_payload_downloaded() - cur_payload_dl >= 0);
			TORRENT_ASSERT(m_statistics.last_protocol_downloaded() - cur_protocol_dl >= 0);
			size_type stats_diff = m_statistics.last_payload_downloaded() - cur_payload_dl +
				m_statistics.last_protocol_downloaded() - cur_protocol_dl;
			TORRENT_ASSERT(stats_diff == size_type(bytes_transferred));
#endif
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

		if (error)
		{
			m_statistics.sent_bytes(0, bytes_transferred);
			return;
		}

		// manage the payload markers
		int amount_payload = 0;
		if (!m_payloads.empty())
		{
			for (std::vector<range>::iterator i = m_payloads.begin();
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

		TORRENT_ASSERT(amount_payload <= (int)bytes_transferred);
		m_statistics.sent_bytes(amount_payload, bytes_transferred - amount_payload);
		
		if (amount_payload > 0)
		{
			boost::shared_ptr<torrent> t = associated_torrent().lock();
			TORRENT_ASSERT(t);
			if (t) t->update_last_upload();
		}
	}

#ifdef TORRENT_DEBUG
	void bt_peer_connection::check_invariant() const
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();

#ifndef TORRENT_DISABLE_ENCRYPTION
		TORRENT_ASSERT( (bool(m_state != read_pe_dhkey) || m_dh_key_exchange.get())
				|| !is_outgoing());

		TORRENT_ASSERT(!m_rc4_encrypted || m_enc_handler.get());
#endif
		if (!in_handshake())
		{
			TORRENT_ASSERT(m_sent_handshake);
		}

		if (!m_payloads.empty())
		{
			for (std::vector<range>::const_iterator i = m_payloads.begin();
				i != m_payloads.end() - 1; ++i)
			{
				TORRENT_ASSERT(i->start + i->length <= (i+1)->start);
			}
		}
	}
#endif

}

