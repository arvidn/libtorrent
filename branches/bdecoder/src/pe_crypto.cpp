/*

Copyright (c) 2007-2014, Un Shyam, Arvid Norberg, Steven Siloti
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

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)

#include <boost/cstdint.hpp>
#include <algorithm>

extern "C" {
#include "libtorrent/tommath.h"
}

#include "libtorrent/random.hpp"
#include "libtorrent/pe_crypto.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{
	namespace
	{
		const unsigned char dh_prime[96] = {
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2,
			0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
			0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74, 0x02, 0x0B, 0xBE, 0xA6,
			0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
			0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D,
			0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
			0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9,
			0xA6, 0x3A, 0x36, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x05, 0x63
		};
	}

	struct mp_bigint
	{
		mp_bigint()
		{ mp_init(&v); }
		mp_int* operator&() { return &v; }
		~mp_bigint() { mp_clear(&v); }
	private:
		// non-copyable
		mp_bigint(mp_bigint const&);
		mp_bigint const& operator=(mp_bigint const&);
		mp_int v;
	};

	// Set the prime P and the generator, generate local public key
	dh_key_exchange::dh_key_exchange()
	{
		// create local key
		for (int i = 0; i < int(sizeof(m_dh_local_secret)); ++i)
			m_dh_local_secret[i] = random() & 0xff;

		mp_bigint prime;
		mp_bigint secret;
		mp_bigint key;

		if (mp_read_unsigned_bin(&prime, dh_prime, sizeof(dh_prime)))
			return;
		if (mp_read_unsigned_bin(&secret, (unsigned char*)m_dh_local_secret, sizeof(m_dh_local_secret)))
			return;

		// generator is 2
		mp_set_int(&key, 2);
		// key = (2 ^ secret) % prime
		if (mp_exptmod(&key, &secret, &prime, &key))
			return;

		// key is now our local key
		int size = mp_unsigned_bin_size(&key);
		memset(m_dh_local_key, 0, sizeof(m_dh_local_key) - size);
		mp_to_unsigned_bin(&key, (unsigned char*)m_dh_local_key + sizeof(m_dh_local_key) - size);
	}

	char const* dh_key_exchange::get_local_key() const
	{
		return m_dh_local_key;
	}

	// compute shared secret given remote public key
	int dh_key_exchange::compute_secret(char const* remote_pubkey)
	{
		TORRENT_ASSERT(remote_pubkey);
		mp_bigint prime;
		mp_bigint secret;
		mp_bigint remote_key;

		if (mp_read_unsigned_bin(&prime, dh_prime, sizeof(dh_prime)))
			return -1;
		if (mp_read_unsigned_bin(&secret, (unsigned char*)m_dh_local_secret, sizeof(m_dh_local_secret)))
			return -1;
		if (mp_read_unsigned_bin(&remote_key, (unsigned char*)remote_pubkey, 96))
			return -1;

		if (mp_exptmod(&remote_key, &secret, &prime, &remote_key))
			return -1;

		// remote_key is now the shared secret
		int size = mp_unsigned_bin_size(&remote_key);
		memset(m_dh_shared_secret, 0, sizeof(m_dh_shared_secret) - size);
		mp_to_unsigned_bin(&remote_key, (unsigned char*)m_dh_shared_secret + sizeof(m_dh_shared_secret) - size);

		// calculate the xor mask for the obfuscated hash
		hasher h;
		h.update("req3", 4);
		h.update(m_dh_shared_secret, sizeof(m_dh_shared_secret));
		m_xor_mask = h.final();
		return 0;
	}

	int encryption_handler::encrypt(std::vector<asio::mutable_buffer>& iovec)
	{
		TORRENT_ASSERT(!m_send_barriers.empty());
		TORRENT_ASSERT(m_send_barriers.front().enc_handler);

		int to_process = m_send_barriers.front().next;

		if (to_process != INT_MAX)
		{
			for (std::vector<asio::mutable_buffer>::iterator i = iovec.begin();
				to_process >= 0; ++i)
			{
				if (to_process == 0)
				{
					iovec.erase(i, iovec.end());
					break;
				}
				else if (to_process < asio::buffer_size(*i))
				{
					*i = asio::mutable_buffer(asio::buffer_cast<void*>(*i), to_process);
					iovec.erase(++i, iovec.end());
					to_process = 0;
					break;
				}
				to_process -= asio::buffer_size(*i);
			}
			TORRENT_ASSERT(to_process == 0);
		}

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
		to_process = 0;
		for (std::vector<asio::mutable_buffer>::iterator i = iovec.begin();
			i != iovec.end(); ++i)
			to_process += asio::buffer_size(*i);
#endif

		int next_barrier = 0;
		if (iovec.empty() || (next_barrier = m_send_barriers.front().enc_handler->encrypt(iovec)))
		{
			if (m_send_barriers.front().next != INT_MAX)
			{
				if (m_send_barriers.size() == 1)
					// transitioning back to plaintext
					next_barrier = INT_MAX;
				m_send_barriers.pop_front();
			}

#if defined TORRENT_DEBUG || TORRENT_RELEASE_ASSERTS
			if (next_barrier != INT_MAX)
			{
				int overhead = 0;
				for (std::vector<asio::mutable_buffer>::iterator i = iovec.begin();
					i != iovec.end(); ++i)
					overhead += asio::buffer_size(*i);
				TORRENT_ASSERT(overhead + to_process == next_barrier);
			}
#endif
		}
		else
		{
			iovec.clear();
		}
		return next_barrier;
	}

	int encryption_handler::decrypt(crypto_receive_buffer& recv_buffer, std::size_t& bytes_transferred)
	{
		TORRENT_ASSERT(!is_recv_plaintext());
		int consume = 0;
		if (recv_buffer.crypto_packet_finished())
		{
			std::vector<asio::mutable_buffer> wr_buf;
			recv_buffer.mutable_buffers(wr_buf, bytes_transferred);
			int packet_size = 0;
			int produce = bytes_transferred;
			m_dec_handler->decrypt(wr_buf, consume, produce, packet_size);
			TORRENT_ASSERT(packet_size || produce);
			TORRENT_ASSERT(packet_size >= 0);
			bytes_transferred = produce;
			if (packet_size)
				recv_buffer.crypto_cut(consume, packet_size);
		}
		else
			bytes_transferred = 0;
		return consume;
	}

	bool encryption_handler::switch_send_crypto(boost::shared_ptr<crypto_plugin> crypto
		, int pending_encryption)
	{
		bool place_barrier = false;
		if (!m_send_barriers.empty())
		{
			std::list<barrier>::iterator end = m_send_barriers.end(); --end;
			for (std::list<barrier>::iterator b = m_send_barriers.begin();
				b != end; ++b)
				pending_encryption -= b->next;
			TORRENT_ASSERT(pending_encryption >= 0);
			m_send_barriers.back().next = pending_encryption;
		}
		else if (crypto)
			place_barrier = true;

		if (crypto)
			m_send_barriers.push_back(barrier(crypto, INT_MAX));

		return place_barrier;
	}

	void encryption_handler::switch_recv_crypto(boost::shared_ptr<crypto_plugin> crypto
		, crypto_receive_buffer& recv_buffer)
	{
		m_dec_handler = crypto;
		int packet_size = 0;
		if (crypto)
		{
			int consume = 0;
			int produce = 0;
			std::vector<asio::mutable_buffer> wr_buf;
			crypto->decrypt(wr_buf, consume, produce, packet_size);
			TORRENT_ASSERT(wr_buf.empty());
			TORRENT_ASSERT(consume == 0);
			TORRENT_ASSERT(produce == 0);
		}
		recv_buffer.crypto_reset(packet_size);
	}

	void rc4_handler::set_incoming_key(unsigned char const* key, int len)
	{
		m_decrypt = true;
		rc4_init(key, len, &m_rc4_incoming);
		// Discard first 1024 bytes
		char buf[1024];
		std::vector<boost::asio::mutable_buffer> vec(1, boost::asio::mutable_buffer(buf, 1024));
		int consume = 0;
		int produce = 0;
		int packet_size = 0;
		decrypt(vec, consume, produce, packet_size);
	}

	void rc4_handler::set_outgoing_key(unsigned char const* key, int len)
	{
		m_encrypt = true;
		rc4_init(key, len, &m_rc4_outgoing);
		// Discard first 1024 bytes
		char buf[1024];
		std::vector<boost::asio::mutable_buffer> vec(1, boost::asio::mutable_buffer(buf, 1024));
		encrypt(vec);
	}

	int rc4_handler::encrypt(std::vector<boost::asio::mutable_buffer>& buf)
	{
		if (!m_encrypt) return 0;
		if (buf.empty()) return 0;

		int bytes_processed = 0;
		for (std::vector<boost::asio::mutable_buffer>::iterator i = buf.begin();
			i != buf.end(); ++i)
		{
			char* pos = boost::asio::buffer_cast<char*>(*i);
			int len = boost::asio::buffer_size(*i);

			TORRENT_ASSERT(len >= 0);
			TORRENT_ASSERT(pos);

			bytes_processed += len;
			rc4_encrypt((unsigned char*)pos, len, &m_rc4_outgoing);
		}
		buf.clear();
		return bytes_processed;
	}

	void rc4_handler::decrypt(std::vector<boost::asio::mutable_buffer>& buf
		, int& consume
		, int& produce
		, int& packet_size)
	{
		if (!m_decrypt) return;

		int bytes_processed = 0;
		for (std::vector<boost::asio::mutable_buffer>::iterator i = buf.begin();
			i != buf.end(); ++i)
		{
			char* pos = boost::asio::buffer_cast<char*>(*i);
			int len = boost::asio::buffer_size(*i);

			TORRENT_ASSERT(len >= 0);
			TORRENT_ASSERT(pos);

			bytes_processed += len;
			rc4_encrypt((unsigned char*)pos, len, &m_rc4_incoming);
		}
		buf.clear();
		produce = bytes_processed;
	}

} // namespace libtorrent

// All this code is based on libTomCrypt (http://www.libtomcrypt.com/)
// this library is public domain and has been specially
// tailored for libtorrent by Arvid Norberg

void rc4_init(const unsigned char* in, unsigned long len, rc4 *state)
{
	unsigned char key[256], tmp, *s;
	int keylen, x, y, j;

	TORRENT_ASSERT(state != 0);
	TORRENT_ASSERT(len <= 256);

	state->x = 0;
	while (len--) {
		state->buf[state->x++] = *in++;
	}

	/* extract the key */
	s = state->buf;
	memcpy(key, s, 256);
	keylen = state->x;

	/* make RC4 perm and shuffle */
	for (x = 0; x < 256; x++) {
		s[x] = x;
	}

	for (j = x = y = 0; x < 256; x++) {
		y = (y + state->buf[x] + key[j++]) & 255;
		if (j == keylen) {
			j = 0; 
		}
		tmp = s[x]; s[x] = s[y]; s[y] = tmp;
	}
	state->x = 0;
	state->y = 0;
}

unsigned long rc4_encrypt(unsigned char *out, unsigned long outlen, rc4 *state)
{
	unsigned char x, y, *s, tmp;
	unsigned long n;

	TORRENT_ASSERT(out != 0);
	TORRENT_ASSERT(state != 0);

	n = outlen;
	x = state->x;
	y = state->y;
	s = state->buf;
	while (outlen--) {
		x = (x + 1) & 255;
		y = (y + s[x]) & 255;
		tmp = s[x]; s[x] = s[y]; s[y] = tmp;
		tmp = (s[x] + s[y]) & 255;
		*out++ ^= s[tmp];
	}
	state->x = x;
	state->y = y;
	return n;
}

#endif // #if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)

