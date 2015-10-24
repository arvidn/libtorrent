/*

Copyright (c) 2013, Steven Siloti
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

#include <libtorrent/hasher.hpp>
#include <libtorrent/kademlia/item.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/ed25519.hpp>

#ifdef TORRENT_DEBUG
#include "libtorrent/bdecode.hpp"
#endif

#ifdef TORRENT_USE_VALGRIND
#include <valgrind/memcheck.h>
#endif

namespace libtorrent { namespace dht
{

namespace
{
	enum { canonical_length = 1200 };
	int canonical_string(std::pair<char const*, int> v, boost::uint64_t seq
		, std::pair<char const*, int> salt, char out[canonical_length])
	{
		// v must be valid bencoding!
#ifdef TORRENT_DEBUG
		bdecode_node e;
		error_code ec;
		TORRENT_ASSERT(bdecode(v.first, v.first + v.second, e, ec) == 0);
#endif
		char* ptr = out;

		int left = canonical_length - (ptr - out);
		if (salt.second > 0)
		{
			ptr += snprintf(ptr, left, "4:salt%d:", salt.second);
			left = canonical_length - (ptr - out);
			memcpy(ptr, salt.first, (std::min)(salt.second, left));
			ptr += (std::min)(salt.second, left);
			left = canonical_length - (ptr - out);
		}
		ptr += snprintf(ptr, canonical_length - (ptr - out)
			, "3:seqi%" PRId64 "e1:v", seq);
		left = canonical_length - (ptr - out);
		memcpy(ptr, v.first, (std::min)(v.second, left));
		ptr += (std::min)(v.second, left);
		TORRENT_ASSERT((ptr - out) <= canonical_length);
		return ptr - out;
	}
}

// calculate the target hash for an immutable item.
sha1_hash item_target_id(std::pair<char const*, int> v)
{
	hasher h;
	h.update(v.first, v.second);
	return h.final();
}

// calculate the target hash for a mutable item.
sha1_hash item_target_id(std::pair<char const*, int> salt
	, char const* pk)
{
	hasher h;
	h.update(pk, item_pk_len);
	if (salt.second > 0) h.update(salt.first, salt.second);
	return h.final();
}

bool verify_mutable_item(
	std::pair<char const*, int> v
	, std::pair<char const*, int> salt
	, boost::uint64_t seq
	, char const* pk
	, char const* sig)
{
#ifdef TORRENT_USE_VALGRIND
	VALGRIND_CHECK_MEM_IS_DEFINED(v.first, v.second);
	VALGRIND_CHECK_MEM_IS_DEFINED(pk, item_pk_len);
	VALGRIND_CHECK_MEM_IS_DEFINED(sig, item_sig_len);
#endif

	char str[canonical_length];
	int len = canonical_string(v, seq, salt, str);

	return ed25519_verify(reinterpret_cast<unsigned char const*>(sig)
		, reinterpret_cast<unsigned char const*>(str)
		, len
		, reinterpret_cast<unsigned char const*>(pk)) == 1;
}

// given the bencoded buffer ``v``, the salt (which is optional and may have
// a length of zero to be omitted), sequence number ``seq``, public key (32
// bytes ed25519 key) ``pk`` and a secret/private key ``sk`` (64 bytes ed25519
// key) a signature ``sig`` is produced. The ``sig`` pointer must point to
// at least 64 bytes of available space. This space is where the signature is
// written.
void sign_mutable_item(
	std::pair<char const*, int> v
	, std::pair<char const*, int> salt
	, boost::uint64_t seq
	, char const* pk
	, char const* sk
	, char* sig)
{
#ifdef TORRENT_USE_VALGRIND
	VALGRIND_CHECK_MEM_IS_DEFINED(v.first, v.second);
	VALGRIND_CHECK_MEM_IS_DEFINED(sk, item_sk_len);
	VALGRIND_CHECK_MEM_IS_DEFINED(pk, item_pk_len);
#endif

	char str[canonical_length];
	int len = canonical_string(v, seq, salt, str);

	ed25519_sign(reinterpret_cast<unsigned char*>(sig)
		, reinterpret_cast<unsigned char const*>(str)
		, len
		, reinterpret_cast<unsigned char const*>(pk)
		, reinterpret_cast<unsigned char const*>(sk)
	);
}

item::item(char const* pk, std::string const& salt)
	: m_salt(salt)
	, m_seq(0)
	, m_mutable(true)
{
	memcpy(m_pk.data(), pk, item_pk_len);
}

item::item(entry const& v
	, std::pair<char const*, int> salt
	, boost::uint64_t seq, char const* pk, char const* sk)
{
	assign(v, salt, seq, pk, sk);
}

void item::assign(entry const& v, std::pair<char const*, int> salt
	, boost::uint64_t seq, char const* pk, char const* sk)
{
	m_value = v;
	if (pk && sk)
	{
		char buffer[1000];
		int bsize = bencode(buffer, v);
		TORRENT_ASSERT(bsize <= 1000);
		sign_mutable_item(std::make_pair(buffer, bsize)
			, salt, seq, pk, sk, m_sig.c_array());
		m_salt.assign(salt.first, salt.second);
		memcpy(m_pk.c_array(), pk, item_pk_len);
		m_seq = seq;
		m_mutable = true;
	}
	else
		m_mutable = false;
}

bool item::assign(bdecode_node const& v
	, std::pair<char const*, int> salt
	, boost::uint64_t seq, char const* pk, char const* sig)
{
	TORRENT_ASSERT(v.data_section().second <= 1000);
	if (pk && sig)
	{
		if (!verify_mutable_item(v.data_section(), salt, seq, pk, sig))
			return false;
		memcpy(m_pk.c_array(), pk, item_pk_len);
		memcpy(m_sig.c_array(), sig, item_sig_len);
		if (salt.second > 0)
			m_salt.assign(salt.first, salt.second);
		else
			m_salt.clear();
		m_seq = seq;
		m_mutable = true;
	}
	else
		m_mutable = false;

	m_value = v;
	return true;
}

void item::assign(entry const& v, std::string salt, boost::uint64_t seq
	, char const* pk, char const* sig)
{
#if TORRENT_USE_ASSERTS
	TORRENT_ASSERT(pk && sig);
	char buffer[1000];
	int bsize = bencode(buffer, v);
	TORRENT_ASSERT(bsize <= 1000);
	TORRENT_ASSERT(verify_mutable_item(
		std::make_pair(buffer, bsize)
		, std::make_pair(salt.data(), int(salt.size()))
		, seq, pk, sig));
#endif

	memcpy(m_pk.c_array(), pk, item_pk_len);
	memcpy(m_sig.c_array(), sig, item_sig_len);
	m_salt = salt;
	m_seq = seq;
	m_mutable = true;
	m_value = v;
}

} } // namespace libtorrent::dht
