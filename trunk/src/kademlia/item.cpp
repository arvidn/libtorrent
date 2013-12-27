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

#include "ed25519.h"

namespace libtorrent { namespace dht
{

namespace
{
	enum { canonical_length = 1100 };
	int canonical_string(std::pair<char const*, int> v, boost::uint64_t seq, char out[canonical_length])
	{
		int len = snprintf(out, canonical_length, "3:seqi%" PRId64 "e1:v", seq);
		memcpy(out + len, v.first, v.second);
		len += v.second;
		TORRENT_ASSERT(len <= canonical_length);
		return len;
	}
}

bool verify_mutable_item(std::pair<char const*, int> v,
	boost::uint64_t seq,
	char const* pk,
	char const* sig)
{
#ifdef TORRENT_USE_VALGRIND
	VALGRIND_CHECK_MEM_IS_DEFINED(v.first, v.second);
	VALGRIND_CHECK_MEM_IS_DEFINED(pk, item_pk_len);
	VALGRIND_CHECK_MEM_IS_DEFINED(sig, item_sig_len);
#endif

	char str[canonical_length];
	int len = canonical_string(v, seq, str);

	return ed25519_verify((unsigned char const*)sig,
		(unsigned char const*)str,
		len,
		(unsigned char const*)pk) == 1;
}

void sign_mutable_item(std::pair<char const*, int> v,
	boost::uint64_t seq,
	char const* pk,
	char const* sk,
	char* sig)
{
#ifdef TORRENT_USE_VALGRIND
	VALGRIND_CHECK_MEM_IS_DEFINED(v.first, v.second);
	VALGRIND_CHECK_MEM_IS_DEFINED(sk, item_sk_len);
	VALGRIND_CHECK_MEM_IS_DEFINED(pk, item_pk_len);
#endif

	char str[canonical_length];
	int len = canonical_string(v, seq, str);

	ed25519_sign((unsigned char*)sig,
		(unsigned char const*)str,
		len,
		(unsigned char const*)pk,
		(unsigned char const*)sk
	);
}

sha1_hash mutable_item_cas(std::pair<char const*, int> v, boost::uint64_t seq)
{
	char str[canonical_length];
	int len = canonical_string(v, seq, str);
	return hasher(str, len).final();
}

item::item(entry const& v, boost::uint64_t seq, char const* pk, char const* sk)
{
	assign(v, seq, pk, sk);
}

item::item(lazy_entry const* v, boost::uint64_t seq, char const* pk, char const* sig)
{
	if (!assign(v, seq, pk, sig))
		throw invalid_item();
}

item::item(lazy_item const& i)
	: m_seq(i.seq)
	, m_mutable(i.is_mutable())
{
	m_value = *i.value;
	// if this is a mutable item lazy_item will have already verified it
	memcpy(m_pk, i.pk, item_pk_len);
	memcpy(m_sig, i.sig, item_sig_len);
}

void item::assign(entry const& v, boost::uint64_t seq, char const* pk, char const* sk)
{
	m_value = v;
	if (pk && sk)
	{
		char buffer[1000];
		int bsize = bencode(buffer, v);
		TORRENT_ASSERT(bsize <= 1000);
		sign_mutable_item(std::make_pair(buffer, bsize), seq, pk, sk, m_sig);
		memcpy(m_pk, pk, item_pk_len);
		m_seq = seq;
		m_mutable = true;
	}
	else
		m_mutable = false;
}

bool item::assign(lazy_entry const* v, boost::uint64_t seq, char const* pk, char const* sig)
{
	TORRENT_ASSERT(v->data_section().second <= 1000);
	if (pk && sig)
	{
		if (!verify_mutable_item(v->data_section(), seq, pk, sig))
			return false;
		memcpy(m_pk, pk, item_pk_len);
		memcpy(m_sig, sig, item_sig_len);
		m_seq = seq;
		m_mutable = true;
	}
	else
		m_mutable = false;

	m_value = *v;
	return true;
}

sha1_hash item::cas()
{
	TORRENT_ASSERT(m_mutable);
	char buffer[1000];
	int bsize = bencode(buffer, m_value);
	return mutable_item_cas(std::make_pair(buffer, bsize), m_seq);
}

lazy_item::lazy_item(lazy_entry const* v, char const* pk, char const* sig, boost::uint64_t seq)
	: value(v), pk(pk), sig(sig), seq(seq)
{
	if (is_mutable() && !verify_mutable_item(v->data_section(), seq, pk, sig))
		throw invalid_item();
}

} } // namespace libtorrent::dht
