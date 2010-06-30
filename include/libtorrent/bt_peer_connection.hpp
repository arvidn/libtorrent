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

#ifndef TORRENT_BT_PEER_CONNECTION_HPP_INCLUDED
#define TORRENT_BT_PEER_CONNECTION_HPP_INCLUDED

#include <ctime>
#include <algorithm>
#include <vector>
#include <string>

#include "libtorrent/debug.hpp"

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/smart_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/array.hpp>
#include <boost/optional.hpp>
#include <boost/cstdint.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/buffer.hpp"
#include "libtorrent/peer_connection.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/peer_id.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/piece_block_progress.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/pe_crypto.hpp"

namespace libtorrent
{
	class torrent;

	namespace detail
	{
		struct session_impl;
	}

	class TORRENT_EXPORT bt_peer_connection
		: public peer_connection
	{
	friend class invariant_access;
	public:

		// this is the constructor where the we are the active part.
		// The peer_conenction should handshake and verify that the
		// other end has the correct id
		bt_peer_connection(
			aux::session_impl& ses
			, boost::weak_ptr<torrent> t
			, boost::shared_ptr<socket_type> s
			, tcp::endpoint const& remote
			, policy::peer* peerinfo);

		// with this constructor we have been contacted and we still don't
		// know which torrent the connection belongs to
		bt_peer_connection(
			aux::session_impl& ses
			, boost::shared_ptr<socket_type> s
			, tcp::endpoint const& remote
			, policy::peer* peerinfo);

		void start();

		enum { upload_only_msg = 2 };

		~bt_peer_connection();
		
#ifndef TORRENT_DISABLE_ENCRYPTION
		bool supports_encryption() const
		{ return m_encrypted; }
#endif

		enum message_type
		{
	// standard messages
			msg_choke = 0,
			msg_unchoke,
			msg_interested,
			msg_not_interested,
			msg_have,
			msg_bitfield,
			msg_request,
			msg_piece,
			msg_cancel,
			// DHT extension
			msg_dht_port,
			// FAST extension
			msg_suggest_piece = 0xd,
			msg_have_all,
			msg_have_none,
			msg_reject_request,
			msg_allowed_fast,

			// extension protocol message
			msg_extended = 20,

			num_supported_messages
		};

		// called from the main loop when this connection has any
		// work to do.

		void on_sent(error_code const& error
			, std::size_t bytes_transferred);
		void on_receive(error_code const& error
			, std::size_t bytes_transferred);
		
		virtual void get_specific_peer_info(peer_info& p) const;
		virtual bool in_handshake() const;

#ifndef TORRENT_DISABLE_EXTENSIONS
		bool support_extensions() const { return m_supports_extensions; }

		template <class T>
		T* supports_extension() const
		{
			for (extension_list_t::const_iterator i = m_extensions.begin()
				, end(m_extensions.end()); i != end; ++i)
			{
				T* ret = dynamic_cast<T*>(i->get());
				if (ret) return ret;
			}
			return 0;
		}
#endif

		// the message handlers are called
		// each time a recv() returns some new
		// data, the last time it will be called
		// is when the entire packet has been
		// received, then it will no longer
		// be called. i.e. most handlers need
		// to check how much of the packet they
		// have received before any processing
		void on_keepalive();
		void on_choke(int received);
		void on_unchoke(int received);
		void on_interested(int received);
		void on_not_interested(int received);
		void on_have(int received);
		void on_bitfield(int received);
		void on_request(int received);
		void on_piece(int received);
		void on_cancel(int received);

		// DHT extension
		void on_dht_port(int received);

		// FAST extension
		void on_suggest_piece(int received);
		void on_have_all(int received);
		void on_have_none(int received);
		void on_reject_request(int received);
		void on_allowed_fast(int received);

		void on_extended(int received);

		void on_extended_handshake();

		typedef void (bt_peer_connection::*message_handler)(int received);

		// the following functions appends messages
		// to the send buffer
		void write_choke();
		void write_unchoke();
		void write_interested();
		void write_not_interested();
		void write_request(peer_request const& r);
		void write_cancel(peer_request const& r);
		void write_bitfield();
		void write_have(int index);
		void write_piece(peer_request const& r, disk_buffer_holder& buffer);
		void write_handshake();
#ifndef TORRENT_DISABLE_EXTENSIONS
		void write_extensions();
		void write_upload_only();
#endif
		void write_metadata(std::pair<int, int> req);
		void write_metadata_request(std::pair<int, int> req);
		void write_keepalive();

		// DHT extension
		void write_dht_port(int listen_port);

		// FAST extension
		void write_have_all();
		void write_have_none();
		void write_reject_request(peer_request const&);
		void write_allow_fast(int piece);
		
		void on_connected();
		void on_metadata();

#ifdef TORRENT_DEBUG
		void check_invariant() const;
		ptime m_last_choke;
#endif

	private:

		bool dispatch_message(int received);
		// returns the block currently being
		// downloaded. And the progress of that
		// block. If the peer isn't downloading
		// a piece for the moment, the boost::optional
		// will be invalid.
		boost::optional<piece_block_progress> downloading_piece_progress() const;

#ifndef TORRENT_DISABLE_ENCRYPTION

		// if (is_local()), we are 'a' otherwise 'b'
		//
		// 1. a -> b dhkey, pad
		// 2. b -> a dhkey, pad
		// 3. a -> b sync, payload
		// 4. b -> a sync, payload
		// 5. a -> b payload

		void write_pe1_2_dhkey();
		void write_pe3_sync();
		void write_pe4_sync(int crypto_select);

		void write_pe_vc_cryptofield(buffer::interval& write_buf
			, int crypto_field, int pad_size);

		// stream key (info hash of attached torrent)
		// secret is the DH shared secret
		// initializes m_RC4_handler
		void init_pe_RC4_handler(char const* secret, sha1_hash const& stream_key);

public:

		// these functions encrypt the send buffer if m_rc4_encrypted
		// is true, otherwise it passes the call to the
		// peer_connection functions of the same names
		virtual void append_const_send_buffer(char const* buffer, int size);
		void send_buffer(char const* buf, int size, int flags = 0);
		buffer::interval allocate_send_buffer(int size);
		template <class Destructor>
		void append_send_buffer(char* buffer, int size, Destructor const& destructor)
		{
#ifndef TORRENT_DISABLE_ENCRYPTION
			if (m_rc4_encrypted)
			{
				TORRENT_ASSERT(send_buffer_size() == m_encrypted_bytes);
				m_RC4_handler->encrypt(buffer, size);
#ifdef TORRENT_DEBUG
				m_encrypted_bytes += size;
				TORRENT_ASSERT(m_encrypted_bytes == send_buffer_size() + size);
#endif
			}
#endif
			peer_connection::append_send_buffer(buffer, size, destructor);
		}
		void setup_send();

private:

		void encrypt_pending_buffer();

		// Returns offset at which bytestream (src, src + src_size)
		// matches bytestream(target, target + target_size).
		// If no sync found, return -1
		int get_syncoffset(char const* src, int src_size
			, char const* target, int target_size) const;
#endif

		enum state
		{
#ifndef TORRENT_DISABLE_ENCRYPTION
			read_pe_dhkey = 0,
			read_pe_syncvc,
			read_pe_synchash,
			read_pe_skey_vc,
			read_pe_cryptofield,
			read_pe_pad,
			read_pe_ia,
			init_bt_handshake,
			read_protocol_identifier,
#else
			read_protocol_identifier = 0,
#endif
			read_info_hash,
			read_peer_id,

			// handshake complete
			read_packet_size,
			read_packet
		};
		
#ifndef TORRENT_DISABLE_ENCRYPTION
		enum
		{
			handshake_len = 68,
			dh_key_len = 96
		};
#endif

		std::string m_client_version;

		// state of on_receive
		state m_state;

		static const message_handler m_message_handler[num_supported_messages];

		// this is a queue of ranges that describes
		// where in the send buffer actual payload
		// data is located. This is currently
		// only used to be able to gather statistics
		// seperately on payload and protocol data.
		struct range
		{
			range(int s, int l)
				: start(s)
				, length(l)
			{
				TORRENT_ASSERT(s >= 0);
				TORRENT_ASSERT(l > 0);
			}
			int start;
			int length;
		};
		static bool range_below_zero(const range& r)
		{ return r.start < 0; }
		std::vector<range> m_payloads;

#ifndef TORRENT_DISABLE_EXTENSIONS
		// the message ID for upload only message
		// 0 if not supported
		int m_upload_only_id;

		char m_reserved_bits[8];
		// this is set to true if the handshake from
		// the peer indicated that it supports the
		// extension protocol
		bool m_supports_extensions:1;
#endif
		bool m_supports_dht_port:1;
		bool m_supports_fast:1;

#ifndef TORRENT_DISABLE_ENCRYPTION
		// this is set to true after the encryption method has been
		// succesfully negotiated (either plaintext or rc4), to signal
		// automatic encryption/decryption.
		bool m_encrypted;

		// true if rc4, false if plaintext
		bool m_rc4_encrypted;

		// used to disconnect peer if sync points are not found within
		// the maximum number of bytes
		int m_sync_bytes_read;

		// hold information about latest allocated send buffer
		// need to check for non zero (begin, end) for operations with this
		buffer::interval m_enc_send_buffer;
		
		// initialized during write_pe1_2_dhkey, and destroyed on
		// creation of m_RC4_handler. Cannot reinitialize once
		// initialized.
		boost::scoped_ptr<dh_key_exchange> m_dh_key_exchange;
		
		// if RC4 is negotiated, this is used for
		// encryption/decryption during the entire session. Destroyed
		// if plaintext is selected
		boost::scoped_ptr<RC4_handler> m_RC4_handler;
		
		// (outgoing only) synchronize verification constant with
		// remote peer, this will hold RC4_decrypt(vc). Destroyed
		// after the sync step.
		boost::scoped_array<char> m_sync_vc;

		// (incoming only) synchronize hash with remote peer, holds
		// the sync hash (hash("req1",secret)). Destroyed after the
		// sync step.
		boost::scoped_ptr<sha1_hash> m_sync_hash;
#endif // #ifndef TORRENT_DISABLE_ENCRYPTION

#ifdef TORRENT_DEBUG
		// this is set to true when the client's
		// bitfield is sent to this peer
		bool m_sent_bitfield;

		bool m_in_constructor;
		
		bool m_sent_handshake;

		// the number of bytes in the send buffer
		// that have been encrypted (only used for
		// encrypted connections)
public:
		int m_encrypted_bytes;
#endif

	};
}

#endif // TORRENT_BT_PEER_CONNECTION_HPP_INCLUDED

