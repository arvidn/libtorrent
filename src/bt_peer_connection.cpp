/*

Copyright (c) 2003-2014, Arvid Norberg
Copyright (c) 2007-2014, Arvid Norberg, Un Shyam
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
#include "libtorrent/aux_/session_interface.hpp"
//#include "libtorrent/aux_/escape_string.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/random.hpp"
#include "libtorrent/alloca.hpp"
#include "libtorrent/socket_type.hpp"
#include "libtorrent/performance_counters.hpp" // for counters

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
#include "libtorrent/pe_crypto.hpp"
#include "libtorrent/hasher.hpp"
#endif

using boost::shared_ptr;

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
#ifndef TORRENT_DISABLE_EXTENSIONS
		0, 0,
		&bt_peer_connection::on_extended
#endif
	};


	bt_peer_connection::bt_peer_connection(peer_connection_args const& pack
		, peer_id const& pid)
		: peer_connection(pack)
		, m_state(read_protocol_identifier)
		, m_supports_extensions(false)
		, m_supports_dht_port(false)
		, m_supports_fast(false)
		, m_sent_bitfield(false)
		, m_sent_handshake(false)
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		, m_encrypted(false)
		, m_rc4_encrypted(false)
		, m_recv_buffer(peer_connection::m_recv_buffer)
#endif
		, m_our_peer_id(pid)
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		, m_sync_bytes_read(0)
#endif
#ifndef TORRENT_DISABLE_EXTENSIONS
		, m_upload_only_id(0)
		, m_holepunch_id(0)
#endif
#ifndef TORRENT_DISABLE_EXTENSIONS
		, m_dont_have_id(0)
		, m_share_mode_id(0)
#endif
#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		, m_in_constructor(true)
#endif
	{
#ifdef TORRENT_LOGGING
		peer_log("*** bt_peer_connection");
#endif

#if TORRENT_USE_ASSERTS
		m_in_constructor = false;
#endif
#ifndef TORRENT_DISABLE_EXTENSIONS
		memset(m_reserved_bits, 0, sizeof(m_reserved_bits));
#endif
	}

	void bt_peer_connection::start()
	{
		peer_connection::start();
		
		// start in the state where we are trying to read the
		// handshake from the other side
		m_recv_buffer.reset(20);
		setup_receive();
	}

	bt_peer_connection::~bt_peer_connection()
	{
	}

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
	void bt_peer_connection::switch_send_crypto(boost::shared_ptr<crypto_plugin> crypto)
	{
		if (m_enc_handler.switch_send_crypto(crypto, send_buffer_size() - get_send_barrier()))
			set_send_barrier(send_buffer_size());
	}

	void bt_peer_connection::switch_recv_crypto(boost::shared_ptr<crypto_plugin> crypto)
	{
		m_enc_handler.switch_recv_crypto(crypto, m_recv_buffer);
	}
#endif

	void bt_peer_connection::on_connected()
	{
		if (is_disconnecting()) return;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		if (t->graceful_pause())
		{
#ifdef TORRENT_LOGGING
			peer_log("*** ON_CONNECTED [ graceful-paused ]");
#endif
			disconnect(error_code(errors::torrent_paused), op_bittorrent);
			return;
		}

		// make sure are much as possible of the response ends up in the same
		// packet, or at least back-to-back packets
		cork c_(*this);

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		
		boost::uint8_t out_enc_policy = m_settings.get_int(settings_pack::out_enc_policy);

#ifdef TORRENT_USE_OPENSSL
		// never try an encrypted connection when already using SSL
		if (is_ssl(*get_socket()))
			out_enc_policy = settings_pack::pe_disabled;
#endif
#ifdef TORRENT_LOGGING
		char const* policy_name[] = {"forced", "enabled", "disabled"};
		peer_log("*** outgoing encryption policy: %s", policy_name[out_enc_policy]);
#endif

		if (out_enc_policy == settings_pack::pe_forced)
		{
			write_pe1_2_dhkey();
			if (is_disconnecting()) return;

			m_state = read_pe_dhkey;
			m_recv_buffer.reset(dh_key_len);
			setup_receive();
		}
		else if (out_enc_policy == settings_pack::pe_enabled)
		{
			TORRENT_ASSERT(peer_info_struct());

			torrent_peer* pi = peer_info_struct();
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
				m_recv_buffer.reset(dh_key_len);
				setup_receive();
			}
			else // pi->pe_support == false
			{
				// toggled back to false if standard handshake
				// completes correctly (without encryption)
				pi->pe_support = true;

				write_handshake();
				m_recv_buffer.reset(20);
				setup_receive();
			}
		}
		else if (out_enc_policy == settings_pack::pe_disabled)
#endif
		{
			write_handshake();
			
			// start in the state where we are trying to read the
			// handshake from the other side
			m_recv_buffer.reset(20);
			setup_receive();
		}
	}
	
	void bt_peer_connection::on_metadata()
	{
#ifdef TORRENT_LOGGING
		peer_log("*** ON_METADATA");
#endif

		disconnect_if_redundant();
		if (m_disconnecting) return;

		// connections that are still in the handshake
		// will send their bitfield when the handshake
		// is done
#ifndef TORRENT_DISABLE_EXTENSIONS
		write_upload_only();
#endif

		if (!m_sent_handshake) return;
		if (m_sent_bitfield) return;
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		write_bitfield();
		TORRENT_ASSERT(m_sent_bitfield);
#ifndef TORRENT_DISABLE_DHT
		if (m_supports_dht_port && m_ses.has_dht())
			write_dht_port(m_ses.external_udp_port());
#endif
	}

	void bt_peer_connection::write_dht_port(int listen_port)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);
		
#ifdef TORRENT_LOGGING
		peer_log("==> DHT_PORT [ %d ]", listen_port);
#endif
		char msg[] = {0,0,0,3, msg_dht_port, 0, 0};
		char* ptr = msg + 5;
		detail::write_uint16(listen_port, ptr);
		send_buffer(msg, sizeof(msg));

		stats_counters().inc_stats_counter(counters::num_outgoing_dht_port);
	}

	void bt_peer_connection::write_have_all()
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(m_sent_handshake);
		m_sent_bitfield = true;
#ifdef TORRENT_LOGGING
		peer_log("==> HAVE_ALL");
#endif
		char msg[] = {0,0,0,1, msg_have_all};
		send_buffer(msg, sizeof(msg));

		stats_counters().inc_stats_counter(counters::num_outgoing_have_all);
	}

	void bt_peer_connection::write_have_none()
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(m_sent_handshake);
		m_sent_bitfield = true;
#ifdef TORRENT_LOGGING
		peer_log("==> HAVE_NONE");
#endif
		char msg[] = {0,0,0,1, msg_have_none};
		send_buffer(msg, sizeof(msg));

		stats_counters().inc_stats_counter(counters::num_outgoing_have_none);
	}

	void bt_peer_connection::write_reject_request(peer_request const& r)
	{
		INVARIANT_CHECK;

		stats_counters().inc_stats_counter(counters::piece_rejects);

		if (!m_supports_fast) return;

#ifdef TORRENT_LOGGING
		peer_log("==> REJECT_PIECE [ piece: %d | s: %d | l: %d ]"
			, r.piece, r.start, r.length);
#endif
		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);
		TORRENT_ASSERT(associated_torrent().lock()->valid_metadata());

		char msg[] = {0,0,0,13, msg_reject_request,0,0,0,0, 0,0,0,0, 0,0,0,0};
		char* ptr = msg + 5;
		detail::write_int32(r.piece, ptr); // index
		detail::write_int32(r.start, ptr); // begin
		detail::write_int32(r.length, ptr); // length
		send_buffer(msg, sizeof(msg));

		stats_counters().inc_stats_counter(counters::num_outgoing_reject);
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

		stats_counters().inc_stats_counter(counters::num_outgoing_allowed_fast);
	}

	void bt_peer_connection::write_suggest(int piece)
	{
		INVARIANT_CHECK;

		if (!m_supports_fast) return;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(t->valid_metadata());

#ifdef TORRENT_LOGGING
		peer_log("==> SUGGEST [ piece: %d num_peers: %d ]", piece
			, t->has_picker() ? t->picker().get_availability(piece) : -1);
#endif

		char msg[] = {0,0,0,5, msg_suggest_piece, 0, 0, 0, 0};
		char* ptr = msg + 5;
		detail::write_int32(piece, ptr);
		send_buffer(msg, sizeof(msg));

		stats_counters().inc_stats_counter(counters::num_outgoing_suggest);
	}

	char random_byte()
	{ return random() & 0xff; }

	void bt_peer_connection::get_specific_peer_info(peer_info& p) const
	{
		TORRENT_ASSERT(!associated_torrent().expired());

		if (is_interesting()) p.flags |= peer_info::interesting;
		if (is_choked()) p.flags |= peer_info::choked;
		if (is_peer_interested()) p.flags |= peer_info::remote_interested;
		if (has_peer_choked()) p.flags |= peer_info::remote_choked;
		if (support_extensions()) p.flags |= peer_info::supports_extensions;
		if (is_outgoing()) p.flags |= peer_info::local_connection;
#if TORRENT_USE_I2P
		if (is_i2p(*get_socket())) p.flags |= peer_info::i2p_socket;
#endif
		if (is_utp(*get_socket())) p.flags |= peer_info::utp_socket;
		if (is_ssl(*get_socket())) p.flags |= peer_info::ssl_socket;

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		if (m_encrypted)
		{
			p.flags |= m_rc4_encrypted
				? peer_info::rc4_encrypted
				: peer_info::plaintext_encrypted;
		}
#endif

		if (!is_connecting() && in_handshake())
			p.flags |= peer_info::handshake;
		if (is_connecting()) p.flags |= peer_info::connecting;

		p.client = m_client_version;
		p.connection_type = peer_info::standard_bittorrent;
	}
	
	bool bt_peer_connection::in_handshake() const
	{
		return m_state < read_packet_size;
	}

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)

	void bt_peer_connection::write_pe1_2_dhkey()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_encrypted);
		TORRENT_ASSERT(!m_rc4_encrypted);
		TORRENT_ASSERT(!m_dh_key_exchange.get());
		TORRENT_ASSERT(!m_sent_handshake);

#ifdef TORRENT_LOGGING
		if (is_outgoing())
			peer_log("*** initiating encrypted handshake");
#endif

		m_dh_key_exchange.reset(new (std::nothrow) dh_key_exchange);
		if (!m_dh_key_exchange || !m_dh_key_exchange->good())
		{
			disconnect(errors::no_memory, op_encryption);
			return;
		}

		int pad_size = random() % 512;

#ifdef TORRENT_LOGGING
		peer_log(" pad size: %d", pad_size);
#endif

		char msg[dh_key_len + 512];
		char* ptr = msg;
		int buf_size = dh_key_len + pad_size;

		memcpy(ptr, m_dh_key_exchange->get_local_key(), dh_key_len);
		ptr += dh_key_len;

		std::generate(ptr, ptr + pad_size, random_byte);
		send_buffer(msg, buf_size);

#ifdef TORRENT_LOGGING
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

		boost::uint8_t crypto_provide = m_settings.get_int(settings_pack::allowed_enc_level);

		// this is an invalid setting, but let's just make the best of the situation
		if ((crypto_provide & settings_pack::pe_both) == 0)
			crypto_provide = settings_pack::pe_both;

#ifdef TORRENT_LOGGING
		char const* level[] = {"plaintext", "rc4", "plaintext rc4"};
		peer_log(" crypto provide : [ %s ]"
			, level[crypto_provide-1]);
#endif

		write_pe_vc_cryptofield(ptr, encrypt_size, crypto_provide, pad_size);
		std::vector<asio::mutable_buffer> vec;
		vec.push_back(asio::mutable_buffer(ptr, encrypt_size));
		m_rc4->encrypt(vec);
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

		std::vector<asio::mutable_buffer> vec;
		vec.push_back(asio::mutable_buffer(msg, buf_size));
		m_rc4->encrypt(vec);
		send_buffer(msg, buf_size);

		// encryption method has been negotiated
		if (crypto_select == 0x02)
			m_rc4_encrypted = true;
		else // 0x01
			m_rc4_encrypted = false;

#ifdef TORRENT_LOGGING
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
		std::generate(write_buf, write_buf + pad_size, random_byte);
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
		
		TORRENT_ASSERT(!m_rc4.get());
		m_rc4 = boost::make_shared<rc4_handler>();

		if (!m_rc4)
		{
			disconnect(errors::no_memory, op_encryption);
			return;
		}

		m_rc4->set_incoming_key(&remote_key[0], 20);
		m_rc4->set_outgoing_key(&local_key[0], 20);

#ifdef TORRENT_LOGGING
		peer_log(" computed RC4 keys");
#endif
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

	void bt_peer_connection::rc4_decrypt(char* pos, int len)
	{
		std::vector<asio::mutable_buffer> vec;
		vec.push_back(asio::mutable_buffer(pos, len));
		int consume = 0;
		int produce = len;
		int packet_size = 0;
		m_rc4->decrypt(vec, consume, produce, packet_size);
	}
#endif // #if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)

	void regular_c_free(char* buf, void* /* userdata */
		, block_cache_reference /* ref */)
	{
		::free(buf);
	}

	void bt_peer_connection::append_const_send_buffer(char const* buffer, int size
		, chained_buffer::free_buffer_fun destructor, void* userdata
		, block_cache_reference ref)
	{
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		if (!m_enc_handler.is_send_plaintext())
		{
			// if we're encrypting this buffer, we need to make a copy
			// since we'll mutate it
			char* buf = (char*)malloc(size);
			memcpy(buf, buffer, size);
			append_send_buffer(buf, size, &regular_c_free, NULL);
			destructor((char*)buffer, userdata, ref);
		}
		else
#endif
		{
			peer_connection::append_const_send_buffer(buffer, size, destructor
				, userdata, ref);
		}
	}

	void bt_peer_connection::write_handshake(bool plain_handshake)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(!m_sent_handshake);
		m_sent_handshake = true;

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

		if (m_settings.get_bool(settings_pack::support_merkle_torrents))
		{
			// we support merkle torrents
			*(ptr + 5) |= 0x08;
		}

		// we support FAST extension
		*(ptr + 7) |= 0x04;

#ifdef TORRENT_LOGGING	
		std::string bitmask;
		for (int k = 0; k < 8; ++k)
		{
			for (int j = 0; j < 8; ++j)
			{
				if (ptr[k] & (0x80 >> j)) bitmask += '1';
				else bitmask += '0';
			}
		}
		peer_log("==> EXTENSIONS [ %s ]", bitmask.c_str());
#endif
		ptr += 8;

		// info hash
		sha1_hash const& ih = t->torrent_file().info_hash();
		memcpy(ptr, &ih[0], 20);
		ptr += 20;

		// peer id
		if (m_settings.get_bool(settings_pack::anonymous_mode))
		{
			// in anonymous mode, every peer connection
			// has a unique peer-id
			for (int i = 0; i < 20; ++i)
				m_our_peer_id[i] = random() & 0xff;
		}

		memcpy(ptr, &m_our_peer_id[0], 20);
		ptr += 20;

#ifdef TORRENT_LOGGING
		{
			char hex_pid[41];
			to_hex((char const*)&m_our_peer_id[0], 20, hex_pid);
			hex_pid[40] = 0;
			peer_log(">>> sent peer_id: %s client: %s"
				, hex_pid, identify_client(m_our_peer_id).c_str());
		}
		peer_log("==> HANDSHAKE [ ih: %s ]", to_hex(ih.to_string()).c_str());
#endif
		send_buffer(handshake, sizeof(handshake));

		// for encrypted peers, just send a plain handshake. We
		// don't know at this point if the rest should be
		// obfuscated or not, we have to wait for the other end's
		// response first.
		if (plain_handshake) return;

		// we don't know how many pieces there are until we
		// have the metadata
		if (t->ready_for_connections())
		{
			write_bitfield();
#ifndef TORRENT_DISABLE_DHT
			if (m_supports_dht_port && m_ses.has_dht())
				write_dht_port(m_ses.external_udp_port());
#endif

			// if we don't have any pieces, don't do any preemptive
			// unchoking at all.
			if (t->num_have() > 0)
			{
				// if the peer is ignoring unchoke slots, or if we have enough
				// unused slots, unchoke this peer right away, to save a round-trip
				// in case it's interested.
				maybe_unchoke_this_peer();
			}
		}
	}

	boost::optional<piece_block_progress> bt_peer_connection::downloading_piece_progress() const
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		buffer::const_interval recv_buffer = m_recv_buffer.get();
		// are we currently receiving a 'piece' message?
		if (m_state != read_packet
			|| recv_buffer.left() <= 9
			|| recv_buffer[0] != msg_piece)
			return boost::optional<piece_block_progress>();

		const char* ptr = recv_buffer.begin + 1;
		peer_request r;
		r.piece = detail::read_int32(ptr);
		r.start = detail::read_int32(ptr);
		r.length = m_recv_buffer.packet_size() - 9;

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

#ifdef TORRENT_LOGGING
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

		TORRENT_ASSERT(received >= 0);
		received_bytes(0, received);
		if (m_recv_buffer.packet_size() != 1)
		{
			disconnect(errors::invalid_choke, op_bittorrent, 2);
			return;
		}
		if (!m_recv_buffer.packet_finished()) return;

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

		TORRENT_ASSERT(received >= 0);
		received_bytes(0, received);
		if (m_recv_buffer.packet_size() != 1)
		{
			disconnect(errors::invalid_unchoke, op_bittorrent, 2);
			return;
		}
		if (!m_recv_buffer.packet_finished()) return;

		incoming_unchoke();
	}

	// -----------------------------
	// -------- INTERESTED ---------
	// -----------------------------

	void bt_peer_connection::on_interested(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received >= 0);
		received_bytes(0, received);
		if (m_recv_buffer.packet_size() != 1)
		{
			disconnect(errors::invalid_interested, op_bittorrent, 2);
			return;
		}
		if (!m_recv_buffer.packet_finished()) return;

		incoming_interested();
	}

	// -----------------------------
	// ------ NOT INTERESTED -------
	// -----------------------------

	void bt_peer_connection::on_not_interested(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received >= 0);
		received_bytes(0, received);
		if (m_recv_buffer.packet_size() != 1)
		{
			disconnect(errors::invalid_not_interested, op_bittorrent, 2);
			return;
		}
		if (!m_recv_buffer.packet_finished()) return;

		incoming_not_interested();
	}

	// -----------------------------
	// ----------- HAVE ------------
	// -----------------------------

	void bt_peer_connection::on_have(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received >= 0);
		received_bytes(0, received);
		if (m_recv_buffer.packet_size() != 5)
		{
			disconnect(errors::invalid_have, op_bittorrent, 2);
			return;
		}
		if (!m_recv_buffer.packet_finished()) return;

		buffer::const_interval recv_buffer = m_recv_buffer.get();

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

		TORRENT_ASSERT(received >= 0);

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		received_bytes(0, received);
		// if we don't have the metedata, we cannot
		// verify the bitfield size
		if (t->valid_metadata()
			&& m_recv_buffer.packet_size() - 1 != (t->torrent_file().num_pieces() + 7) / 8)
		{
			disconnect(errors::invalid_bitfield_size, op_bittorrent, 2);
			return;
		}

		if (!m_recv_buffer.packet_finished()) return;

		buffer::const_interval recv_buffer = m_recv_buffer.get();

		bitfield bits;
		bits.assign((char*)recv_buffer.begin + 1
			, t->valid_metadata()?get_bitfield().size():(m_recv_buffer.packet_size()-1)*8);
		
		incoming_bitfield(bits);
	}

	// -----------------------------
	// ---------- REQUEST ----------
	// -----------------------------

	void bt_peer_connection::on_request(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received >= 0);
		received_bytes(0, received);
		if (m_recv_buffer.packet_size() != 13)
		{
			disconnect(errors::invalid_request, op_bittorrent, 2);
			return;
		}
		if (!m_recv_buffer.packet_finished()) return;

		buffer::const_interval recv_buffer = m_recv_buffer.get();

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

		TORRENT_ASSERT(received >= 0);
		
		buffer::const_interval recv_buffer = m_recv_buffer.get();
		int recv_pos = m_recv_buffer.pos(); // recv_buffer.end - recv_buffer.begin;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		bool merkle = (unsigned char)recv_buffer.begin[0] == 250;
		if (merkle)
		{
			if (recv_pos == 1)
			{
				m_recv_buffer.set_soft_packet_size(13);
				received_bytes(0, received);
				return;
			}
			if (recv_pos < 13)
			{
				received_bytes(0, received);
				return;
			}
			if (recv_pos == 13)
			{
				const char* ptr = recv_buffer.begin + 9;
				int list_size = detail::read_int32(ptr);
				// now we know how long the bencoded hash list is
				// and we can allocate the disk buffer and receive
				// into it

				if (list_size > m_recv_buffer.packet_size() - 13)
				{
					disconnect(errors::invalid_hash_list, op_bittorrent, 2);
					return;
				}

				if (m_recv_buffer.packet_size() - 13 - list_size > t->block_size())
				{
					disconnect(errors::packet_too_large, op_bittorrent, 2);
					return;
				}

				m_recv_buffer.assert_no_disk_buffer();
				if (!m_settings.get_bool(settings_pack::contiguous_recv_buffer) &&
					m_recv_buffer.can_recv_contiguous(m_recv_buffer.packet_size() - 13 - list_size))
				{
					if (!allocate_disk_receive_buffer(m_recv_buffer.packet_size() - 13 - list_size))
					{
						received_bytes(0, received);
						return;
					}
				}
			}
		}
		else
		{
			if (recv_pos == 1)
			{
				m_recv_buffer.assert_no_disk_buffer();

				if (m_recv_buffer.packet_size() - 9 > t->block_size())
				{
					disconnect(errors::packet_too_large, op_bittorrent, 2);
					return;
				}

				if (!m_settings.get_bool(settings_pack::contiguous_recv_buffer) &&
					m_recv_buffer.can_recv_contiguous(m_recv_buffer.packet_size() - 9))
				{
					if (!allocate_disk_receive_buffer(m_recv_buffer.packet_size() - 9))
					{
						received_bytes(0, received);
						return;
					}
				}
			}
		}
		TORRENT_ASSERT(m_settings.get_bool(settings_pack::contiguous_recv_buffer) || m_recv_buffer.has_disk_buffer() || m_recv_buffer.packet_size() == 9);
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
				p.length = m_recv_buffer.packet_size() - list_size - header_size;
				header_size += list_size;
			}
			else
			{
				p.length = m_recv_buffer.packet_size() - header_size;
			}
		}

		if (recv_pos <= header_size)
		{
			// only received protocol data
			received_bytes(0, received);
		}
		else if (recv_pos - received >= header_size)
		{
			// only received payload data
			received_bytes(received, 0);
			piece_bytes = received;
		}
		else
		{
			// received a bit of both
			TORRENT_ASSERT(recv_pos - received < header_size);
			TORRENT_ASSERT(recv_pos > header_size);
			TORRENT_ASSERT(header_size - (recv_pos - received) <= header_size);
			received_bytes(
				recv_pos - header_size
				, header_size - (recv_pos - received));
			piece_bytes = recv_pos - header_size;
		}

		if (recv_pos < header_size) return;

#ifdef TORRENT_LOGGING
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

		TORRENT_ASSERT(m_settings.get_bool(settings_pack::contiguous_recv_buffer) || m_recv_buffer.has_disk_buffer() || m_recv_buffer.packet_size() == header_size);

		incoming_piece_fragment(piece_bytes);
		if (!m_recv_buffer.packet_finished()) return;

		if (merkle && list_size > 0)
		{
#ifdef TORRENT_LOGGING
			peer_log("<== HASHPIECE [ piece: %d list: %d ]", p.piece, list_size);
#endif
			bdecode_node hash_list;
			error_code ec;
			if (bdecode(recv_buffer.begin + 13, recv_buffer.begin+ 13 + list_size
				, hash_list, ec) != 0)
			{
				disconnect(errors::invalid_hash_piece, op_bittorrent, 2);
				return;
			}

			// the list has this format:
			// [ [node-index, hash], [node-index, hash], ... ]
			if (hash_list.type() != bdecode_node::list_t)
			{
				disconnect(errors::invalid_hash_list, op_bittorrent, 2);
				return;
			}

			std::map<int, sha1_hash> nodes;
			for (int i = 0; i < hash_list.list_size(); ++i)
			{
				bdecode_node e = hash_list.list_at(i);
				if (e.type() != bdecode_node::list_t
					|| e.list_size() != 2
					|| e.list_at(0).type() != bdecode_node::int_t
					|| e.list_at(1).type() != bdecode_node::string_t
					|| e.list_at(1).string_length() != 20) continue;

				nodes.insert(std::make_pair(int(e.list_int_value_at(0))
					, sha1_hash(e.list_at(1).string_ptr())));
			}
			if (!nodes.empty() && !t->add_merkle_nodes(nodes, p.piece))
			{
				disconnect(errors::invalid_hash_piece, op_bittorrent, 2);
				return;
			}
		}

		char* disk_buffer = m_recv_buffer.release_disk_buffer();
		if (disk_buffer)
		{
			disk_buffer_holder holder(m_allocator, disk_buffer);
			incoming_piece(p, holder);
		}
		else
		{
			incoming_piece(p, recv_buffer.begin + header_size);
		}
	}

	// -----------------------------
	// ---------- CANCEL -----------
	// -----------------------------

	void bt_peer_connection::on_cancel(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received >= 0);
		received_bytes(0, received);
		if (m_recv_buffer.packet_size() != 13)
		{
			disconnect(errors::invalid_cancel, op_bittorrent, 2);
			return;
		}
		if (!m_recv_buffer.packet_finished()) return;

		buffer::const_interval recv_buffer = m_recv_buffer.get();

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

		TORRENT_ASSERT(received >= 0);
		received_bytes(0, received);
		if (m_recv_buffer.packet_size() != 3)
		{
			disconnect(errors::invalid_dht_port, op_bittorrent, 2);
			return;
		}
		if (!m_recv_buffer.packet_finished()) return;

		buffer::const_interval recv_buffer = m_recv_buffer.get();

		const char* ptr = recv_buffer.begin + 1;
		int listen_port = detail::read_uint16(ptr);
		
		incoming_dht_port(listen_port);

		if (!m_supports_dht_port)
		{
			m_supports_dht_port = true;
#ifndef TORRENT_DISABLE_DHT
			if (m_supports_dht_port && m_ses.has_dht())
				write_dht_port(m_ses.external_udp_port());
#endif
		}
	}

	void bt_peer_connection::on_suggest_piece(int received)
	{
		INVARIANT_CHECK;

		received_bytes(0, received);
		if (!m_supports_fast)
		{
			disconnect(errors::invalid_suggest, op_bittorrent, 2);
			return;
		}

		if (!m_recv_buffer.packet_finished()) return;

		buffer::const_interval recv_buffer = m_recv_buffer.get();

		const char* ptr = recv_buffer.begin + 1;
		int piece = detail::read_uint32(ptr);
		incoming_suggest(piece);
	}

	void bt_peer_connection::on_have_all(int received)
	{
		INVARIANT_CHECK;

		received_bytes(0, received);
		if (!m_supports_fast)
		{
			disconnect(errors::invalid_have_all, op_bittorrent, 2);
			return;
		}
		incoming_have_all();
	}

	void bt_peer_connection::on_have_none(int received)
	{
		INVARIANT_CHECK;

		received_bytes(0, received);
		if (!m_supports_fast)
		{
			disconnect(errors::invalid_have_none, op_bittorrent, 2);
			return;
		}
		incoming_have_none();
	}

	void bt_peer_connection::on_reject_request(int received)
	{
		INVARIANT_CHECK;

		received_bytes(0, received);
		if (!m_supports_fast)
		{
			disconnect(errors::invalid_reject, op_bittorrent, 2);
			return;
		}

		if (!m_recv_buffer.packet_finished()) return;

		buffer::const_interval recv_buffer = m_recv_buffer.get();

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

		received_bytes(0, received);
		if (!m_supports_fast)
		{
			disconnect(errors::invalid_allow_fast, op_bittorrent, 2);
			return;
		}

		if (!m_recv_buffer.packet_finished()) return;
		buffer::const_interval recv_buffer = m_recv_buffer.get();
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

		if (!m_recv_buffer.packet_finished()) return;

		// we can't accept holepunch messages from peers
		// that don't support the holepunch extension
		// because we wouldn't be able to respond
		if (m_holepunch_id == 0) return;

		buffer::const_interval recv_buffer = m_recv_buffer.get();
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
#if defined TORRENT_LOGGING
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
#if defined TORRENT_LOGGING
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
				torrent_peer* p = t->add_peer(ep, peer_info::pex);
				if (p == 0 || p->connection)
				{
#if defined TORRENT_LOGGING
					peer_log("<== HOLEPUNCH [ msg:connect to: %s error: failed to add peer ]"
						, print_address(ep.address()).c_str());
#endif
					// we either couldn't add this peer, or it's
					// already connected. Just ignore the connect message
					break;
				}
				if (p->banned)
				{
#if defined TORRENT_LOGGING
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
				t->update_want_peers();
				if (p->connection)
					p->connection->set_holepunch_mode();
#if defined TORRENT_LOGGING
					peer_log("<== HOLEPUNCH [ msg:connect to: %s ]"
						, print_address(ep.address()).c_str());
#endif
			} break;
			case hp_failed:
			{
				boost::uint32_t error = detail::read_uint32(ptr);
#if defined TORRENT_LOGGING
				error_code ec;
				char const* err_msg[] = {"no such peer", "not connected", "no support", "no self"};
				peer_log("<== HOLEPUNCH [ msg:failed error: %d msg: %s ]", error
					, ((error > 0 && error < 5)?err_msg[error-1]:"unknown message id"));
#endif
				// #error deal with holepunch errors
				(void)error;
			} break;
#if defined TORRENT_LOGGING
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

#if defined TORRENT_LOGGING
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

		stats_counters().inc_stats_counter(counters::num_outgoing_extended);
	}
#endif // TORRENT_DISABLE_EXTENSIONS

	// -----------------------------
	// --------- EXTENDED ----------
	// -----------------------------

#ifndef TORRENT_DISABLE_EXTENSIONS
	void bt_peer_connection::on_extended(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received >= 0);
		received_bytes(0, received);
		if (m_recv_buffer.packet_size() < 2)
		{
			disconnect(errors::invalid_extended, op_bittorrent, 2);
			return;
		}

		if (associated_torrent().expired())
		{
			disconnect(errors::invalid_extended, op_bittorrent, 2);
			return;
		}

		buffer::const_interval recv_buffer = m_recv_buffer.get();
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
			if (!m_recv_buffer.packet_finished()) return;
			if (m_recv_buffer.packet_size() != 3)
			{
#ifdef TORRENT_LOGGING
				peer_log("<== UPLOAD_ONLY [ ERROR: unexpected packet size: %d ]", m_recv_buffer.packet_size());
#endif
				return;
			}
			bool ul = detail::read_uint8(recv_buffer.begin) != 0;
#ifdef TORRENT_LOGGING
			peer_log("<== UPLOAD_ONLY [ %s ]", (ul?"true":"false"));
#endif
			set_upload_only(ul);
			return;
		}

		if (extended_id == share_mode_msg)
		{
			if (!m_recv_buffer.packet_finished()) return;
			if (m_recv_buffer.packet_size() != 3)
			{
#ifdef TORRENT_LOGGING
				peer_log("<== SHARE_MODE [ ERROR: unexpected packet size: %d ]", m_recv_buffer.packet_size());
#endif
				return;
			}
			bool sm = detail::read_uint8(recv_buffer.begin) != 0;
#ifdef TORRENT_LOGGING
			peer_log("<== SHARE_MODE [ %s ]", (sm?"true":"false"));
#endif
			set_share_mode(sm);
			return;
		}

		if (extended_id == holepunch_msg)
		{
			if (!m_recv_buffer.packet_finished()) return;
#ifdef TORRENT_LOGGING
			peer_log("<== HOLEPUNCH");
#endif
			on_holepunch();
			return;
		}

		if (extended_id == dont_have_msg)
		{
			if (!m_recv_buffer.packet_finished()) return;
			if (m_recv_buffer.packet_size() != 6)
			{
#ifdef TORRENT_LOGGING
				peer_log("<== DONT_HAVE [ ERROR: unexpected packet size: %d ]", m_recv_buffer.packet_size());
#endif
				return;
			}
			int piece = detail::read_uint32(recv_buffer.begin);
			incoming_dont_have(piece);
			return;
		}

#ifdef TORRENT_LOGGING
		if (m_recv_buffer.packet_finished())
			peer_log("<== EXTENSION MESSAGE [ msg: %d size: %d ]"
				, extended_id, m_recv_buffer.packet_size());
#endif

		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			if ((*i)->on_extended(m_recv_buffer.packet_size() - 2, extended_id
				, recv_buffer))
				return;
		}

		disconnect(errors::invalid_message, op_bittorrent, 2);
		return;
	}

	void bt_peer_connection::on_extended_handshake()
	{
		if (!m_recv_buffer.packet_finished()) return;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		buffer::const_interval recv_buffer = m_recv_buffer.get();

		bdecode_node root;
		error_code ec;
		int pos;
		int ret = bdecode(recv_buffer.begin + 2, recv_buffer.end, root, ec, &pos);
		if (ret != 0 || ec || root.type() != bdecode_node::dict_t)
		{
#ifdef TORRENT_LOGGING
			peer_log("*** invalid extended handshake: %s pos: %d"
				, ec.message().c_str(), pos);
#endif
			return;
		}

#ifdef TORRENT_LOGGING
		peer_log("<== EXTENDED HANDSHAKE: %s", print_entry(root).c_str());
#endif

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
		if (bdecode_node m = root.dict_find_dict("m"))
		{
			m_upload_only_id = boost::uint8_t(m.dict_find_int_value("upload_only", 0));
			m_holepunch_id = boost::uint8_t(m.dict_find_int_value("ut_holepunch", 0));
			m_dont_have_id = boost::uint8_t(m.dict_find_int_value("lt_donthave", 0));
		}

		// there is supposed to be a remote listen port
		int listen_port = int(root.dict_find_int_value("p"));
		if (listen_port > 0 && peer_info_struct() != 0)
		{
			t->update_peer_port(listen_port, peer_info_struct(), peer_info::incoming);
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
		if (reqq > 0) max_out_request_queue(reqq);

		if (root.dict_find_int_value("upload_only", 0))
			set_upload_only(true);

		if (m_settings.get_bool(settings_pack::support_share_mode)
			&& root.dict_find_int_value("share_mode", 0))
			set_share_mode(true);

		std::string myip = root.dict_find_string_value("yourip");
		if (!myip.empty())
		{
			if (myip.size() == address_v4::bytes_type().size())
			{
				address_v4::bytes_type bytes;
				std::copy(myip.begin(), myip.end(), bytes.begin());
				m_ses.set_external_address(address_v4(bytes)
					, aux::session_interface::source_peer, remote().address());
			}
#if TORRENT_USE_IPV6
			else if (myip.size() == address_v6::bytes_type().size())
			{
				address_v6::bytes_type bytes;
				std::copy(myip.begin(), myip.end(), bytes.begin());
				address_v6 ipv6_address(bytes);
				if (ipv6_address.is_v4_mapped())
					m_ses.set_external_address(ipv6_address.to_v4()
						, aux::session_interface::source_peer, remote().address());
				else
					m_ses.set_external_address(ipv6_address
						, aux::session_interface::source_peer, remote().address());
			}
#endif
		}

		// if we're finished and this peer is uploading only
		// disconnect it
		if (t->is_finished() && upload_only()
			&& m_settings.get_bool(settings_pack::close_redundant_connections)
			&& !t->share_mode())
			disconnect(errors::upload_upload_connection, op_bittorrent);

		stats_counters().inc_stats_counter(counters::num_incoming_ext_handshake);
	}
#endif // TORRENT_DISABLE_EXTENSIONS

	bool bt_peer_connection::dispatch_message(int received)
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(received >= 0);

		// this means the connection has been closed already
		if (associated_torrent().expired())
		{
			received_bytes(0, received);
			return false;
		}

		buffer::const_interval recv_buffer = m_recv_buffer.get();

		TORRENT_ASSERT(recv_buffer.left() >= 1);
		int packet_type = (unsigned char)recv_buffer[0];

		if (m_settings.get_bool(settings_pack::support_merkle_torrents)
			&& packet_type == 250) packet_type = msg_piece;

		if (packet_type < 0
			|| packet_type >= num_supported_messages
			|| m_message_handler[packet_type] == 0)
		{
#ifndef TORRENT_DISABLE_EXTENSIONS
			for (extension_list_t::iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				if ((*i)->on_unknown_message(m_recv_buffer.packet_size(), packet_type
					, buffer::const_interval(recv_buffer.begin+1
					, recv_buffer.end)))
					return m_recv_buffer.packet_finished();
			}
#endif

			received_bytes(0, received);
			// What's going on here?!
			// break in debug builds to allow investigation
//			TORRENT_ASSERT(false);
			disconnect(errors::invalid_message, op_bittorrent);
			return m_recv_buffer.packet_finished();
		}

		TORRENT_ASSERT(m_message_handler[packet_type] != 0);

#ifdef TORRENT_DEBUG
		boost::int64_t cur_payload_dl = statistics().last_payload_downloaded();
		boost::int64_t cur_protocol_dl = statistics().last_protocol_downloaded();
#endif

		// call the correct handler for this packet type
		(this->*m_message_handler[packet_type])(received);
#ifdef TORRENT_DEBUG
		TORRENT_ASSERT(statistics().last_payload_downloaded() - cur_payload_dl >= 0);
		TORRENT_ASSERT(statistics().last_protocol_downloaded() - cur_protocol_dl >= 0);
		boost::int64_t stats_diff = statistics().last_payload_downloaded() - cur_payload_dl +
			statistics().last_protocol_downloaded() - cur_protocol_dl;
		TORRENT_ASSERT(stats_diff == received);
#endif

		bool finished = m_recv_buffer.packet_finished();

		if (finished)
		{
			// count this packet in the session stats counters
			int counter = counters::num_incoming_extended;
			if (packet_type <= msg_dht_port)
				counter = counters::num_incoming_choke + packet_type;
			else if (packet_type <= msg_allowed_fast)
				counter = counters::num_incoming_suggest + packet_type;
			else if (packet_type <= msg_extended)
				counter = counters::num_incoming_extended;
			else
				TORRENT_ASSERT(false);

			stats_counters().inc_stats_counter(counter);
		}

		return finished;
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
		if (!m_settings.get_bool(settings_pack::close_redundant_connections)) return;

#ifdef TORRENT_LOGGING
		peer_log("==> UPLOAD_ONLY [ %d ]"
			, int(t->is_upload_only() && !t->super_seeding()));
#endif

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

		stats_counters().inc_stats_counter(counters::num_outgoing_extended);
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

		stats_counters().inc_stats_counter(counters::num_outgoing_extended);
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

		stats_counters().inc_stats_counter(counters::num_outgoing_cancel);

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

		stats_counters().inc_stats_counter(counters::num_outgoing_request);
	}

	void bt_peer_connection::write_bitfield()
	{
		INVARIANT_CHECK;

		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);
		TORRENT_ASSERT(m_sent_handshake);
		TORRENT_ASSERT(t->valid_metadata());

		if (t->super_seeding())
		{
#ifdef TORRENT_LOGGING
			peer_log(" *** NOT SENDING BITFIELD, super seeding");
#endif
			if (m_supports_fast) write_have_none();

			// if we are super seeding, pretend to not have any piece
			// and don't send a bitfield
			m_sent_bitfield = true;

			// bootstrap superseeding by sending two have message
			int piece = t->get_piece_to_super_seed(get_bitfield());
			if (piece >= 0) superseed_piece(-1, piece);
			piece = t->get_piece_to_super_seed(get_bitfield());
			if (piece >= 0) superseed_piece(-1, piece);
			return;
		}
		else if (m_supports_fast && t->is_seed() && !m_settings.get_bool(settings_pack::lazy_bitfields))
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
#ifdef TORRENT_LOGGING
			peer_log(" *** NOT SENDING BITFIELD");
#endif
			m_sent_bitfield = true;
			return;
		}
	
		int num_pieces = t->torrent_file().num_pieces();

		int lazy_pieces[50];
		int num_lazy_pieces = 0;
		int lazy_piece = 0;

		if (t->is_seed() && m_settings.get_bool(settings_pack::lazy_bitfields)
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
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
		}

		const int packet_size = (num_pieces + 7) / 8 + 5;
	
		char* msg = TORRENT_ALLOCA(char, packet_size);
		if (msg == 0) return; // out of memory
		unsigned char* ptr = (unsigned char*)msg;

		detail::write_int32(packet_size - 4, ptr);
		detail::write_uint8(msg_bitfield, ptr);

		if (t->is_seed())
		{
			memset(ptr, 0xff, packet_size - 5);

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

		// add predictive pieces to the bitfield as well, since we won't
		// announce them again
		for (std::vector<int>::const_iterator i = t->predictive_pieces().begin()
			, end(t->predictive_pieces().end()); i != end; ++i)
			msg[5 + *i / 8] |= (0x80 >> (*i & 7));

#ifdef TORRENT_LOGGING

		std::string bitfield_string;
		bitfield_string.resize(num_pieces);
		for (int k = 0; k < num_pieces; ++k)
		{
			if (msg[5 + k / 8] & (0x80 >> (k % 8))) bitfield_string[k] = '1';
			else bitfield_string[k] = '0';
		}
		peer_log("==> BITFIELD [ %s ]", bitfield_string.c_str());
#endif
		m_sent_bitfield = true;

		send_buffer(msg, packet_size);

		stats_counters().inc_stats_counter(counters::num_outgoing_bitfield);

		if (num_lazy_pieces > 0)
		{
			for (int i = 0; i < num_lazy_pieces; ++i)
			{
#ifdef TORRENT_LOGGING
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

		// if we're using a proxy, our listen port won't be useful
		// anyway.
		if (!m_settings.get_bool(settings_pack::force_proxy) && is_outgoing())
			handshake["p"] = m_ses.listen_port();

		// only send the port in case we bade the connection
		// on incoming connections the other end already knows
		// our listen port
		if (!m_settings.get_bool(settings_pack::anonymous_mode))
		{
			handshake["v"] = m_settings.get_str(settings_pack::handshake_client_version).empty()
				? m_settings.get_str(settings_pack::user_agent)
				: m_settings.get_str(settings_pack::handshake_client_version);
		}

		std::string remote_address;
		std::back_insert_iterator<std::string> out(remote_address);
		detail::write_address(remote().address(), out);
		handshake["yourip"] = remote_address;
		handshake["reqq"] = m_settings.get_int(settings_pack::max_allowed_in_request_queue);
		boost::shared_ptr<torrent> t = associated_torrent().lock();
		TORRENT_ASSERT(t);

		m["upload_only"] = upload_only_msg;
		m["ut_holepunch"] = holepunch_msg;
		if (m_settings.get_bool(settings_pack::support_share_mode))
			m["share_mode"] = share_mode_msg;
		m["lt_donthave"] = dont_have_msg;

		int complete_ago = -1;
		if (t->last_seen_complete() > 0) complete_ago = t->time_since_complete();
		handshake["complete_ago"] = complete_ago;

		// if we're using lazy bitfields or if we're super seeding, don't say
		// we're upload only, since it might make peers disconnect. don't tell
		// anyone we're upload only when in share mode, we want to stay connected
		// to seeds. if we're super seeding, we don't want to make peers think
		// that we only have a single piece and is upload only, since they might
		// disconnect immediately when they have downloaded a single piece,
		// although we'll make another piece available. If we don't have
		// metadata, we also need to suppress saying we're upload-only. If we do,
		// we may be disconnected before we receive the metadata.
		if (t->is_upload_only()
			&& !t->share_mode()
			&& t->valid_metadata()
			&& !t->super_seeding()
			&& (!m_settings.get_bool(settings_pack::lazy_bitfields)
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
			|| m_encrypted
#endif
			))
		{
			handshake["upload_only"] = 1;
		}

		if (m_settings.get_bool(settings_pack::support_share_mode)
			&& t->share_mode())
			handshake["share_mode"] = 1;

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

		stats_counters().inc_stats_counter(counters::num_outgoing_ext_handshake);

#if defined TORRENT_LOGGING
		peer_log("==> EXTENDED HANDSHAKE: %s", handshake.to_string().c_str());
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

		stats_counters().inc_stats_counter(counters::num_outgoing_choke);
	}

	void bt_peer_connection::write_unchoke()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);

		char msg[] = {0,0,0,1,msg_unchoke};
		send_buffer(msg, sizeof(msg));

		stats_counters().inc_stats_counter(counters::num_outgoing_unchoke);

#ifndef TORRENT_DISABLE_EXTENSIONS
		for (extension_list_t::iterator i = m_extensions.begin()
			, end(m_extensions.end()); i != end; ++i)
		{
			(*i)->sent_unchoke();
		}
#endif
	}

	void bt_peer_connection::write_interested()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);

		char msg[] = {0,0,0,1,msg_interested};
		send_buffer(msg, sizeof(msg));

		stats_counters().inc_stats_counter(counters::num_outgoing_interested);
	}

	void bt_peer_connection::write_not_interested()
	{
		INVARIANT_CHECK;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);

		char msg[] = {0,0,0,1,msg_not_interested};
		send_buffer(msg, sizeof(msg));

		stats_counters().inc_stats_counter(counters::num_outgoing_not_interested);
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

		stats_counters().inc_stats_counter(counters::num_outgoing_have);
	}

	void bt_peer_connection::write_dont_have(int index)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		INVARIANT_CHECK;
		TORRENT_ASSERT(associated_torrent().lock()->valid_metadata());
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < associated_torrent().lock()->torrent_file().num_pieces());

		if (in_handshake()) return;

		TORRENT_ASSERT(m_sent_handshake && m_sent_bitfield);

		if (!m_supports_extensions || m_dont_have_id == 0) return;

		char msg[] = {0,0,0,6,msg_extended,char(m_dont_have_id),0,0,0,0};
		char* ptr = msg + 6;
		detail::write_int32(index, ptr);
		send_buffer(msg, sizeof(msg));

		stats_counters().inc_stats_counter(counters::num_outgoing_extended);
#endif
	}

	void buffer_reclaim_block(char* /* buffer */, void* userdata
		, block_cache_reference ref)
	{
		buffer_allocator_interface* buf = (buffer_allocator_interface*)userdata;
		buf->reclaim_block(ref);
	}

	void buffer_free_disk_buf(char* buffer, void* userdata
		, block_cache_reference /* ref */)
	{
		buffer_allocator_interface* buf = (buffer_allocator_interface*)userdata;
		buf->free_disk_buffer(buffer);
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
		if (m_settings.get_bool(settings_pack::support_merkle_torrents) && merkle)
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

		if (buffer.ref().storage == 0)
		{
			append_send_buffer(buffer.get(), r.length
				, &buffer_free_disk_buf, &m_allocator);
		}
		else
		{
			append_const_send_buffer(buffer.get(), r.length
				, &buffer_reclaim_block, &m_allocator, buffer.ref());
		}
		buffer.release();

		m_payloads.push_back(range(send_buffer_size() - r.length, r.length));
		setup_send();

		stats_counters().inc_stats_counter(counters::num_outgoing_piece);
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
			received_bytes(0, bytes_transferred);
			return;
		}

		// make sure are much as possible of the response ends up in the same
		// packet, or at least back-to-back packets
		cork c_(*this);

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		if (!m_enc_handler.is_recv_plaintext())
		{
			int consumed = m_enc_handler.decrypt(m_recv_buffer, bytes_transferred);
	#ifdef TORRENT_LOGGING
			if (consumed + bytes_transferred > 0)
				peer_log("<== decrypted block [ s = %d ]", consumed + bytes_transferred);
	#endif
			if (bytes_transferred == SIZE_MAX)
			{
				disconnect(errors::parse_failed, op_encryption);
				return;
			}
			received_bytes(0, consumed);

			int sub_transferred = 0;
			while (bytes_transferred > 0 &&
				((sub_transferred = m_recv_buffer.advance_pos(bytes_transferred)) > 0))
			{
	#if TORRENT_USE_ASSERTS
				boost::int64_t cur_payload_dl = m_statistics.last_payload_downloaded();
				boost::int64_t cur_protocol_dl = m_statistics.last_protocol_downloaded();
	#endif
				on_receive_impl(sub_transferred);
				bytes_transferred -= sub_transferred;
				TORRENT_ASSERT(sub_transferred > 0);

	#if TORRENT_USE_ASSERTS
				TORRENT_ASSERT(m_statistics.last_payload_downloaded() - cur_payload_dl >= 0);
				TORRENT_ASSERT(m_statistics.last_protocol_downloaded() - cur_protocol_dl >= 0);
				boost::int64_t stats_diff = m_statistics.last_payload_downloaded() - cur_payload_dl +
					m_statistics.last_protocol_downloaded() - cur_protocol_dl;
				TORRENT_ASSERT(stats_diff == int(sub_transferred));
	#endif

				if (m_disconnecting) return;
			}
		}
		else
#endif
			on_receive_impl(bytes_transferred);
	}

	void bt_peer_connection::on_receive_impl(std::size_t bytes_transferred)
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();

		buffer::const_interval recv_buffer = m_recv_buffer.get();

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		// m_state is set to read_pe_dhkey in initial state
		// (read_protocol_identifier) for incoming, or in constructor
		// for outgoing
		if (m_state == read_pe_dhkey)
		{
			received_bytes(0, bytes_transferred);

			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
			TORRENT_ASSERT(m_recv_buffer.packet_size() == dh_key_len);
			TORRENT_ASSERT(recv_buffer == m_recv_buffer.get());

			if (!m_recv_buffer.packet_finished()) return;
			
			// write our dh public key. m_dh_key_exchange is
			// initialized in write_pe1_2_dhkey()
			if (!is_outgoing()) write_pe1_2_dhkey();
			if (is_disconnecting()) return;
			
			// read dh key, generate shared secret
			if (m_dh_key_exchange->compute_secret(recv_buffer.begin) != 0)
			{
				disconnect(errors::no_memory, op_encryption);
				return;
			}

#ifdef TORRENT_LOGGING
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
				switch_send_crypto(m_rc4);
				write_handshake(true);
				switch_send_crypto(boost::shared_ptr<crypto_plugin>());

				// vc,crypto_select,len(pad),pad, encrypt(handshake)
				// 8+4+2+0+handshake_len
				m_recv_buffer.reset(8+4+2+0+handshake_len);
			}
			else
			{
				// already written dh key
				m_state = read_pe_synchash;
				// synchash,skeyhash,vc,crypto_provide,len(pad),pad,encrypt(handshake)
				m_recv_buffer.reset(20+20+8+4+2+0+handshake_len);
			}
			TORRENT_ASSERT(!m_recv_buffer.packet_finished());
			return;
		}

		// cannot fall through into
		if (m_state == read_pe_synchash)
		{
			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
			TORRENT_ASSERT(!is_outgoing());
			TORRENT_ASSERT(recv_buffer == m_recv_buffer.get());

			if (recv_buffer.left() < 20)
			{
				received_bytes(0, bytes_transferred);

				if (m_recv_buffer.packet_finished())
					disconnect(errors::sync_hash_not_found, op_bittorrent, 1);
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
					received_bytes(0, bytes_transferred);
					disconnect(errors::no_memory, op_encryption);
					return;
				}
			}

			int syncoffset = get_syncoffset((char*)m_sync_hash->begin(), 20
				, recv_buffer.begin, recv_buffer.left());

			// No sync 
			if (syncoffset == -1)
			{
				received_bytes(0, bytes_transferred);

				std::size_t bytes_processed = recv_buffer.left() - 20;
				m_sync_bytes_read += bytes_processed;
				if (m_sync_bytes_read >= 512)
				{
					disconnect(errors::sync_hash_not_found, op_encryption, 1);
					return;
				}

				m_recv_buffer.cut(bytes_processed, (std::min)(m_recv_buffer.packet_size()
					, (512+20) - m_sync_bytes_read));

				TORRENT_ASSERT(!m_recv_buffer.packet_finished());
				return;
			}
			// found complete sync
			else
			{
				std::size_t bytes_processed = syncoffset + 20;
#ifdef TORRENT_LOGGING
				peer_log("*** sync point (hash) found at offset %d"
					, m_sync_bytes_read + bytes_processed - 20);
#endif
				m_state = read_pe_skey_vc;
				// skey,vc - 28 bytes
				m_sync_hash.reset();
				int transferred_used = bytes_processed - recv_buffer.left() + bytes_transferred;
				TORRENT_ASSERT(transferred_used <= int(bytes_transferred));
				received_bytes(0, transferred_used);
				bytes_transferred -= transferred_used;
				m_recv_buffer.cut(bytes_processed, 28);
			}
		}

		if (m_state == read_pe_skey_vc)
		{
			received_bytes(0, bytes_transferred);
			bytes_transferred = 0;

			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
			TORRENT_ASSERT(!is_outgoing());
			TORRENT_ASSERT(m_recv_buffer.packet_size() == 28);

			if (!m_recv_buffer.packet_finished()) return;
			if (is_disconnecting()) return;
			TORRENT_ASSERT(!is_disconnecting());

			recv_buffer = m_recv_buffer.get();

			TORRENT_ASSERT(!is_disconnecting());

			sha1_hash ih(recv_buffer.begin);
			torrent const* ti = m_ses.find_encrypted_torrent(ih, m_dh_key_exchange->get_hash_xor_mask());

			if (ti)
			{
				if (!t)
				{
					attach_to_torrent(ti->info_hash(), false);
					if (is_disconnecting()) return;
					TORRENT_ASSERT(!is_disconnecting());

					t = associated_torrent().lock();
					TORRENT_ASSERT(t);
				}
			
				init_pe_rc4_handler(m_dh_key_exchange->get_secret(), ti->info_hash());
#ifdef TORRENT_LOGGING
				peer_log("*** stream key found, torrent located");
#endif
			}

			if (!m_rc4.get())
			{
				disconnect(errors::invalid_info_hash, op_bittorrent, 1);
				return;
			}

			// verify constant
			buffer::interval wr_recv_buf = m_recv_buffer.mutable_buffer();
			rc4_decrypt(wr_recv_buf.begin + 20, 8);
			wr_recv_buf.begin += 28;

			const char sh_vc[] = {0,0,0,0, 0,0,0,0};
			if (!std::equal(sh_vc, sh_vc+8, recv_buffer.begin + 20))
			{
				disconnect(errors::invalid_encryption_constant, op_encryption, 2);
				return;
			}

#ifdef TORRENT_LOGGING
			peer_log("*** verification constant found");
#endif
			m_state = read_pe_cryptofield;
			m_recv_buffer.reset(4 + 2);
		}

		// cannot fall through into
		if (m_state == read_pe_syncvc)
		{
			TORRENT_ASSERT(is_outgoing());
			TORRENT_ASSERT(!m_encrypted);
			TORRENT_ASSERT(!m_rc4_encrypted);
			TORRENT_ASSERT(recv_buffer == m_recv_buffer.get());
			
			if (recv_buffer.left() < 8)
			{
				received_bytes(0, bytes_transferred);
				if (m_recv_buffer.packet_finished())
					disconnect(errors::invalid_encryption_constant, op_encryption, 2);
				return;
			}

			// generate the verification constant
			if (!m_sync_vc.get()) 
			{
				TORRENT_ASSERT(m_sync_bytes_read == 0);

				m_sync_vc.reset(new (std::nothrow) char[8]);
				if (!m_sync_vc)
				{
					disconnect(errors::no_memory, op_encryption);
					return;
				}
				std::fill(m_sync_vc.get(), m_sync_vc.get() + 8, 0);
				rc4_decrypt(m_sync_vc.get(), 8);
			}

			TORRENT_ASSERT(m_sync_vc.get());
			int syncoffset = get_syncoffset(m_sync_vc.get(), 8
				, recv_buffer.begin, recv_buffer.left());

			// No sync 
			if (syncoffset == -1)
			{
				std::size_t bytes_processed = recv_buffer.left() - 8;
				m_sync_bytes_read += bytes_processed;
				received_bytes(0, bytes_transferred);

				if (m_sync_bytes_read >= 512)
				{
					disconnect(errors::invalid_encryption_constant, op_encryption, 2);
					return;
				}

				m_recv_buffer.cut(bytes_processed, (std::min)(m_recv_buffer.packet_size()
					, (512+8) - m_sync_bytes_read));

				TORRENT_ASSERT(!m_recv_buffer.packet_finished());
			}
			// found complete sync
			else
			{
				std::size_t bytes_processed = syncoffset + 8;
#ifdef TORRENT_LOGGING
				peer_log("*** sync point (verification constant) found at offset %d"
					, m_sync_bytes_read + bytes_processed - 8);
#endif
				int transferred_used = bytes_processed - recv_buffer.left() + bytes_transferred;
				TORRENT_ASSERT(transferred_used <= int(bytes_transferred));
				received_bytes(0, transferred_used);
				bytes_transferred -= transferred_used;

				m_recv_buffer.cut(bytes_processed, 4 + 2);

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
			TORRENT_ASSERT(m_recv_buffer.packet_size() == 4+2);
			received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			
			if (!m_recv_buffer.packet_finished()) return;

			buffer::interval wr_buf = m_recv_buffer.mutable_buffer();
			rc4_decrypt(wr_buf.begin, m_recv_buffer.packet_size());

			recv_buffer = m_recv_buffer.get();
			
			boost::uint32_t crypto_field = detail::read_uint32(recv_buffer.begin);

#ifdef TORRENT_LOGGING
			peer_log("*** crypto %s : [%s%s ]"
				, is_outgoing() ? "select" : "provide"
				, (crypto_field & 1) ? " plaintext" : ""
				, (crypto_field & 2) ? " rc4" : "");
#endif

			if (!is_outgoing())
			{
				// select a crypto method
				int allowed_encryption = m_settings.get_int(settings_pack::allowed_enc_level);
				boost::uint32_t crypto_select = crypto_field & allowed_encryption;
	
				// when prefer_rc4 is set, keep the most significant bit
				// otherwise keep the least significant one
				if (m_settings.get_bool(settings_pack::prefer_rc4))
				{
					boost::uint32_t mask = (std::numeric_limits<boost::uint32_t>::max)();
					while (crypto_select & (mask << 1))
					{
						mask <<= 1;
						crypto_select = crypto_select & mask;
					}
				}
				else
				{
					boost::uint32_t mask = (std::numeric_limits<boost::uint32_t>::max)();
					while (crypto_select & (mask >> 1))
					{
						mask >>= 1;
						crypto_select = crypto_select & mask;
					}
				}

				if (crypto_select == 0)
				{
					disconnect(errors::unsupported_encryption_mode, op_encryption, 1);
					return;
				}

				// write the pe4 step
				write_pe4_sync(crypto_select);
			}
			else // is_outgoing()
			{
				// check if crypto select is valid
				int allowed_encryption = m_settings.get_int(settings_pack::allowed_enc_level);

				crypto_field &= allowed_encryption; 
				if (crypto_field == 0)
				{
					// we don't allow any of the offered encryption levels
					disconnect(errors::unsupported_encryption_mode_selected, op_encryption, 2);
					return;
				}

				if (crypto_field == settings_pack::pe_plaintext)
					m_rc4_encrypted = false;
				else if (crypto_field == settings_pack::pe_rc4)
					m_rc4_encrypted = true;
			}

			int len_pad = detail::read_int16(recv_buffer.begin);
			if (len_pad < 0 || len_pad > 512)
			{
				disconnect(errors::invalid_pad_size, op_encryption, 2);
				return;
			}
			
			m_state = read_pe_pad;
			if (!is_outgoing())
				m_recv_buffer.reset(len_pad + 2); // len(IA) at the end of pad
			else
			{
				if (len_pad == 0)
				{
					m_encrypted = true;
					if (m_rc4_encrypted)
					{
						switch_send_crypto(m_rc4);
						switch_recv_crypto(m_rc4);
					}
					m_state = init_bt_handshake;
				}
				else
					m_recv_buffer.reset(len_pad);
			}
		}

		if (m_state == read_pe_pad)
		{
			TORRENT_ASSERT(!m_encrypted);
			received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			if (!m_recv_buffer.packet_finished()) return;

			int pad_size = is_outgoing() ? m_recv_buffer.packet_size() : m_recv_buffer.packet_size() - 2;

			buffer::interval wr_buf = m_recv_buffer.mutable_buffer();
			rc4_decrypt(wr_buf.begin, m_recv_buffer.packet_size());

			recv_buffer = m_recv_buffer.get();

			if (!is_outgoing())
			{
				recv_buffer.begin += pad_size;
				int len_ia = detail::read_int16(recv_buffer.begin);
				
				if (len_ia < 0) 
				{
					disconnect(errors::invalid_encrypt_handshake, op_encryption, 2);
					return;
				}

#ifdef TORRENT_LOGGING
				peer_log("*** len(IA) : %d", len_ia);
#endif
				if (len_ia == 0)
				{
					// everything after this is Encrypt2
					m_encrypted = true;
					if (m_rc4_encrypted)
					{
						switch_send_crypto(m_rc4);
						switch_recv_crypto(m_rc4);
					}
					m_state = init_bt_handshake;
				}
				else
				{
					m_state = read_pe_ia;
					m_recv_buffer.reset(len_ia);
				}
			}
			else // is_outgoing()
			{
				// everything that arrives after this is Encrypt2
				m_encrypted = true;
				if (m_rc4_encrypted)
				{
					switch_send_crypto(m_rc4);
					switch_recv_crypto(m_rc4);
				}
				m_state = init_bt_handshake;
			}
		}

		if (m_state == read_pe_ia)
		{
			received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			TORRENT_ASSERT(!is_outgoing());
			TORRENT_ASSERT(!m_encrypted);

			if (!m_recv_buffer.packet_finished()) return;

			// ia is always rc4, so decrypt it
			buffer::interval wr_buf = m_recv_buffer.mutable_buffer();
			rc4_decrypt(wr_buf.begin, m_recv_buffer.packet_size());

#ifdef TORRENT_LOGGING
			peer_log("*** decrypted ia : %d bytes", m_recv_buffer.packet_size());
#endif

			// everything that arrives after this is encrypted
			m_encrypted = true;
			if (m_rc4_encrypted)
			{
				switch_send_crypto(m_rc4);
				switch_recv_crypto(m_rc4);
			}
			m_rc4.reset();

			m_state = read_protocol_identifier;
			m_recv_buffer.cut(0, 20);
		}

		if (m_state == init_bt_handshake)
		{
			received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			TORRENT_ASSERT(m_encrypted);

			if (is_outgoing() && t->ready_for_connections())
			{
				write_bitfield();
#ifndef TORRENT_DISABLE_DHT
				if (m_supports_dht_port && m_ses.has_dht())
					write_dht_port(m_ses.external_udp_port());
#endif

				// if we don't have any pieces, don't do any preemptive
				// unchoking at all.
				if (t->num_have() > 0)
				{
					// if the peer is ignoring unchoke slots, or if we have enough
					// unused slots, unchoke this peer right away, to save a round-trip
					// in case it's interested.
					maybe_unchoke_this_peer();
				}
			}

			// decrypt remaining received bytes
			if (m_rc4_encrypted)
			{
				buffer::interval wr_buf = m_recv_buffer.mutable_buffer();
				wr_buf.begin += m_recv_buffer.packet_size();
				rc4_decrypt(wr_buf.begin, wr_buf.left());

#ifdef TORRENT_LOGGING
				peer_log("*** decrypted remaining %d bytes", wr_buf.left());
#endif
			}
			m_rc4.reset();

			// payload stream, start with 20 handshake bytes
			m_state = read_protocol_identifier;
			m_recv_buffer.reset(20);

			// encrypted portion of handshake completed, toggle
			// peer_info pe_support flag back to true
			if (is_outgoing() &&
				m_settings.get_int(settings_pack::out_enc_policy)
					== settings_pack::pe_enabled)
			{
				torrent_peer* pi = peer_info_struct();
				TORRENT_ASSERT(pi);
				
				pi->pe_support = true;
			}
		}

#endif // #if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		
		if (m_state == read_protocol_identifier)
		{
			received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			TORRENT_ASSERT(m_recv_buffer.packet_size() == 20);

			if (!m_recv_buffer.packet_finished()) return;
			recv_buffer = m_recv_buffer.get();

			int packet_size = recv_buffer[0];
			const char protocol_string[] = "\x13" "BitTorrent protocol";

			if (packet_size != 19 ||
				memcmp(recv_buffer.begin, protocol_string, 20) != 0)
			{
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
#ifdef TORRENT_LOGGING
				peer_log("*** unrecognized protocol header");
#endif

#ifdef TORRENT_USE_OPENSSL
				if (is_ssl(*get_socket()))
				{
#ifdef TORRENT_LOGGING
					peer_log("*** SSL peers are not allowed to use any other encryption");
#endif
					disconnect(errors::invalid_info_hash, op_bittorrent, 1);
					return;
				}
#endif // TORRENT_USE_OPENSSL

				if (!is_outgoing()
					&& m_settings.get_int(settings_pack::in_enc_policy)
						== settings_pack::pe_disabled)
				{
					disconnect(errors::no_incoming_encrypted, op_bittorrent);
					return;
				}

				// Don't attempt to perform an encrypted handshake
				// within an encrypted connection. For local connections,
				// we're expected to already have passed the encrypted
				// handshake by this point
				if (m_encrypted || is_outgoing())
				{
					disconnect(errors::invalid_info_hash, op_bittorrent, 1);
					return;
				}

#ifdef TORRENT_LOGGING
				peer_log("*** attempting encrypted connection");
#endif
				m_state = read_pe_dhkey;
				m_recv_buffer.cut(0, dh_key_len);
				TORRENT_ASSERT(!m_recv_buffer.packet_finished());
				return;
#else
				disconnect(errors::invalid_info_hash, op_bittorrent, 1);
				return;
#endif // TORRENT_DISABLE_ENCRYPTION
			}
			else
			{
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
				TORRENT_ASSERT(m_state != read_pe_dhkey);

				if (!is_outgoing()
					&& m_settings.get_int(settings_pack::in_enc_policy)
						== settings_pack::pe_forced
					&& !m_encrypted
					&& !is_ssl(*get_socket()))
				{
					disconnect(errors::no_incoming_regular, op_bittorrent);
					return;
				}
#endif

#ifdef TORRENT_LOGGING
				peer_log("<== BitTorrent protocol");
#endif
			}

			m_state = read_info_hash;
			m_recv_buffer.reset(28);
		}

		// fall through
		if (m_state == read_info_hash)
		{
			received_bytes(0, bytes_transferred);
			bytes_transferred = 0;
			TORRENT_ASSERT(m_recv_buffer.packet_size() == 28);

			if (!m_recv_buffer.packet_finished()) return;
			recv_buffer = m_recv_buffer.get();


#ifdef TORRENT_LOGGING	
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

			t = associated_torrent().lock();

			// ok, now we have got enough of the handshake. Is this connection
			// attached to a torrent?
			if (!t)
			{
				// now, we have to see if there's a torrent with the
				// info_hash we got from the peer
				sha1_hash info_hash;
				std::copy(recv_buffer.begin + 8, recv_buffer.begin + 28
					, (char*)info_hash.begin());

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
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
#ifdef TORRENT_LOGGING
					peer_log("*** received invalid info_hash");
#endif
					disconnect(errors::invalid_info_hash, op_bittorrent, 1);
					return;
				}

#ifdef TORRENT_LOGGING
				peer_log("<<< info_hash received");
#endif
			}

			t = associated_torrent().lock();
			TORRENT_ASSERT(t);
			
			// if this is a local connection, we have already
			// sent the handshake
			if (!is_outgoing()) write_handshake();
			TORRENT_ASSERT(m_sent_handshake);

			if (is_disconnecting()) return;

			m_state = read_peer_id;
			m_recv_buffer.reset(20);
		}

		// fall through
		if (m_state == read_peer_id)
		{
			TORRENT_ASSERT(m_sent_handshake);
			received_bytes(0, bytes_transferred);
//			bytes_transferred = 0;
			t = associated_torrent().lock();
			if (!t)
			{
				TORRENT_ASSERT(!m_recv_buffer.packet_finished()); // TODO
				return;
			}
			TORRENT_ASSERT(m_recv_buffer.packet_size() == 20);
			
			if (!m_recv_buffer.packet_finished()) return;
			recv_buffer = m_recv_buffer.get();

#ifdef TORRENT_LOGGING
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
				peer_log("<<< received peer_id: %s client: %s ascii: \"%s\""
					, hex_pid, identify_client(peer_id(recv_buffer.begin)).c_str(), ascii_pid);
			}
#endif
			peer_id pid;
			std::copy(recv_buffer.begin, recv_buffer.begin + 20, (char*)pid.begin());

			if (t->settings().get_bool(settings_pack::allow_multiple_connections_per_ip))
			{
				// now, let's see if this connection should be closed
				peer_connection* p = t->find_peer(pid);
				if (p)
				{
					TORRENT_ASSERT(p->pid() == pid);
					// we found another connection with the same peer-id
					// which connection should be closed in order to be
					// sure that the other end closes the same connection?
					// the peer with greatest peer-id is the one allowed to
					// initiate connections. So, if our peer-id is greater than
					// the others, we should close the incoming connection,
					// if not, we should close the outgoing one.
					if (pid < m_our_peer_id && is_outgoing())
					{
						p->disconnect(errors::duplicate_peer_id, op_bittorrent);
					}
					else
					{
						disconnect(errors::duplicate_peer_id, op_bittorrent);
						return;
					}
				}
			}

			set_pid(pid);

			// disconnect if the peer has the same peer-id as ourself
			// since it most likely is ourself then
			if (pid == m_our_peer_id)
			{
				if (peer_info_struct()) t->ban_peer(peer_info_struct());
				disconnect(errors::self_connection, op_bittorrent, 1);
				return;
			}
 
			m_client_version = identify_client(pid);
			boost::optional<fingerprint> f = client_fingerprint(pid);
			if (f && std::equal(f->name, f->name + 2, "BC"))
			{
				// if this is a bitcomet client, lower the request queue size limit
				if (max_out_request_queue() > 50) max_out_request_queue(50);
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

#ifdef TORRENT_LOGGING
			peer_log("<== HANDSHAKE");
#endif
			// consider this a successful connection, reset the failcount
			if (peer_info_struct())
				t->clear_failcount(peer_info_struct());
			
#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
			// Toggle pe_support back to false if this is a
			// standard successful connection
			if (is_outgoing() && !m_encrypted &&
				m_settings.get_int(settings_pack::out_enc_policy)
					== settings_pack::pe_enabled)
			{
				torrent_peer* pi = peer_info_struct();
				TORRENT_ASSERT(pi);

				pi->pe_support = false;
			}
#endif

			m_state = read_packet_size;
			m_recv_buffer.reset(5);

			TORRENT_ASSERT(!m_recv_buffer.packet_finished());
			return;
		}

		// cannot fall through into
		if (m_state == read_packet_size)
		{
			// Make sure this is not fallen though into
			TORRENT_ASSERT(recv_buffer == m_recv_buffer.get());
			TORRENT_ASSERT(m_recv_buffer.packet_size() == 5);

			if (!t) return;

			// the 5th byte (if one) should not count as protocol
			// byte here, instead it's counted in the message
			// handler itself, for the specific message
			TORRENT_ASSERT(bytes_transferred <= 5);
			int used_bytes = recv_buffer.left() > 4 ? bytes_transferred - 1: bytes_transferred;
			received_bytes(0, used_bytes);
			bytes_transferred -= used_bytes;
			if (recv_buffer.left() < 4) return;

			TORRENT_ASSERT(bytes_transferred <= 1);

			const char* ptr = recv_buffer.begin;
			int packet_size = detail::read_int32(ptr);

			// don't accept packets larger than 1 MB
			if (packet_size > 1024*1024 || packet_size < 0)
			{
				// packet too large
				received_bytes(0, bytes_transferred);
				disconnect(errors::packet_too_large, op_bittorrent, 2);
				return;
			}

			if (packet_size == 0)
			{
				TORRENT_ASSERT(bytes_transferred <= 1);
				received_bytes(0, bytes_transferred);
				incoming_keepalive();
				if (is_disconnecting()) return;
				// keepalive message
				m_state = read_packet_size;
				m_recv_buffer.cut(4, 5);
				return;
			}
			if (recv_buffer.left() < 5) return;

			m_state = read_packet;
			m_recv_buffer.cut(4, packet_size);
			recv_buffer = m_recv_buffer.get();
			TORRENT_ASSERT(recv_buffer.left() == 1);
			TORRENT_ASSERT(bytes_transferred == 1);
		}

		if (m_state == read_packet)
		{
			TORRENT_ASSERT(recv_buffer == m_recv_buffer.get());
			if (!t)
			{
				received_bytes(0, bytes_transferred);
				disconnect(errors::torrent_removed, op_bittorrent, 1);
				return;
			}
#ifdef TORRENT_DEBUG
			boost::int64_t cur_payload_dl = statistics().last_payload_downloaded();
			boost::int64_t cur_protocol_dl = statistics().last_protocol_downloaded();
#endif
			if (dispatch_message(bytes_transferred))
			{
				m_state = read_packet_size;
				m_recv_buffer.reset(5);
			}
#ifdef TORRENT_DEBUG
			TORRENT_ASSERT(statistics().last_payload_downloaded() - cur_payload_dl >= 0);
			TORRENT_ASSERT(statistics().last_protocol_downloaded() - cur_protocol_dl >= 0);
			boost::int64_t stats_diff = statistics().last_payload_downloaded() - cur_payload_dl +
				statistics().last_protocol_downloaded() - cur_protocol_dl;
			TORRENT_ASSERT(stats_diff == boost::int64_t(bytes_transferred));
#endif
			TORRENT_ASSERT(!m_recv_buffer.packet_finished());
			return;
		}

		TORRENT_ASSERT(!m_recv_buffer.packet_finished());
	}

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
	int bt_peer_connection::hit_send_barrier(std::vector<asio::mutable_buffer>& iovec)
	{
		int next_barrier = m_enc_handler.encrypt(iovec);
#ifdef TORRENT_LOGGING
		if (next_barrier != 0)
			peer_log("==> encrypted block [ s = %d ]", next_barrier);
#endif
		return next_barrier;
	}
#endif

	// --------------------------
	// SEND DATA
	// --------------------------

	void bt_peer_connection::on_sent(error_code const& error
		, std::size_t bytes_transferred)
	{
		INVARIANT_CHECK;

		if (error)
		{
			sent_bytes(0, bytes_transferred);
			return;
		}

		// manage the payload markers
		int amount_payload = 0;
		if (!m_payloads.empty())
		{
			// this points to the first entry to not erase. i.e.
			// [begin, first_to_keep) will be erased because
			// the payload ranges they represent have been sent
			std::vector<range>::iterator first_to_keep = m_payloads.begin();

			for (std::vector<range>::iterator i = m_payloads.begin();
				i != m_payloads.end(); ++i)
			{
				i->start -= bytes_transferred;
				if (i->start < 0)
				{
					if (i->start + i->length <= 0)
					{
						amount_payload += i->length;
						TORRENT_ASSERT(first_to_keep == i);
						++first_to_keep;
					}
					else
					{
						amount_payload += -i->start;
						i->length -= -i->start;
						i->start = 0;
					}
				}
			}

			// remove all payload ranges that have been sent
			m_payloads.erase(m_payloads.begin(), first_to_keep);
		}

		TORRENT_ASSERT(amount_payload <= (int)bytes_transferred);
		sent_bytes(amount_payload, bytes_transferred - amount_payload);
		
		if (amount_payload > 0)
		{
			boost::shared_ptr<torrent> t = associated_torrent().lock();
			TORRENT_ASSERT(t);
			if (t) t->update_last_upload();
		}
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void bt_peer_connection::check_invariant() const
	{
		boost::shared_ptr<torrent> t = associated_torrent().lock();

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)
		TORRENT_ASSERT( (bool(m_state != read_pe_dhkey) || m_dh_key_exchange.get())
				|| !is_outgoing());

		TORRENT_ASSERT(!m_rc4_encrypted || (!m_encrypted && m_rc4) || (m_encrypted && !m_enc_handler.is_send_plaintext()));
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

