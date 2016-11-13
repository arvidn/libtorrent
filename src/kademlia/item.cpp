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
#include <libtorrent/kademlia/ed25519.hpp>

#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.
#include <cstring> // for memcpy

#if TORRENT_USE_ASSERTS
#include "libtorrent/bdecode.hpp"
#endif

namespace libtorrent { namespace dht
{

namespace
{
	int canonical_string(span<char const> v
		, sequence_number const seq
		, span<char const> salt
		, span<char> out)
	{
		// v must be valid bencoding!
#if TORRENT_USE_ASSERTS
		bdecode_node e;
		error_code ec;
		TORRENT_ASSERT(bdecode(v.data(), v.data() + v.size(), e, ec) == 0);
#endif
		char* ptr = out.data();

		size_t left = out.size() - (ptr - out.data());
		if (salt.size() > 0)
		{
			ptr += std::snprintf(ptr, left, "4:salt%d:", int(salt.size()));
			left = out.size() - (ptr - out.data());
			std::memcpy(ptr, salt.data(), std::min(salt.size(), left));
			ptr += std::min(salt.size(), left);
			left = out.size() - (ptr - out.data());
		}
		ptr += std::snprintf(ptr, left, "3:seqi%" PRId64 "e1:v", seq.value);
		left = out.size() - (ptr - out.data());
		std::memcpy(ptr, v.data(), std::min(v.size(), left));
		ptr += std::min(v.size(), left);
		TORRENT_ASSERT((ptr - out.data()) <= out.size());
		return ptr - out.data();
	}
}

// calculate the target hash for an immutable item.
sha1_hash item_target_id(span<char const> v)
{
	return hasher(v).final();
}

// calculate the target hash for a mutable item.
sha1_hash item_target_id(span<char const> salt
	, public_key const& pk)
{
	hasher h(pk.bytes);
	if (salt.size() > 0) h.update(salt);
	return h.final();
}

bool verify_mutable_item(
	span<char const> v
	, span<char const> salt
	, sequence_number const seq
	, public_key const& pk
	, signature const& sig)
{
	char str[1200];
	int len = canonical_string(v, seq, salt, str);

	return ed25519_verify(sig, {str, size_t(len)}, pk);
}

// given the bencoded buffer ``v``, the salt (which is optional and may have
// a length of zero to be omitted), sequence number ``seq``, public key (32
// bytes ed25519 key) ``pk`` and a secret/private key ``sk`` (64 bytes ed25519
// key) a signature ``sig`` is produced. The ``sig`` pointer must point to
// at least 64 bytes of available space. This space is where the signature is
// written.
signature sign_mutable_item(
	span<char const> v
	, span<char const> salt
	, sequence_number const seq
	, public_key const& pk
	, secret_key const& sk)
{
	char str[1200];
	int const len = canonical_string(v, seq, salt, str);

	return ed25519_sign({str, size_t(len)}, pk, sk);
}

item::item(public_key const& pk, span<char const> salt)
	: m_salt(salt.data(), salt.size())
	, m_pk(pk)
	, m_seq(0)
	, m_mutable(true)
{}

item::item(entry v)
	: m_value(std::move(v))
	, m_seq(0)
	, m_mutable(false)
{}

item::item(bdecode_node const& v)
	: m_value(std::move(v))
	, m_seq(0)
	, m_mutable(false)
{}

item::item(entry v, span<char const> salt
	, sequence_number const seq, public_key const& pk, secret_key const& sk)
{
	assign(std::move(v), salt, seq, pk, sk);
}

void item::assign(entry v)
{
	m_mutable = false;
	m_value = std::move(v);
}

void item::assign(entry v, span<char const> salt
	, sequence_number const seq, public_key const& pk, secret_key const& sk)
{
	char buffer[1000];
	int bsize = bencode(buffer, v);
	TORRENT_ASSERT(bsize <= 1000);
	m_sig = sign_mutable_item(span<char const>(buffer, bsize)
		, salt, seq, pk, sk);
	m_salt.assign(salt.data(), salt.size());
	m_pk = pk;
	m_seq = seq;
	m_mutable = true;
	m_value = std::move(v);
}

void item::assign(bdecode_node const& v)
{
	m_mutable = false;
	m_value = v;
}

bool item::assign(bdecode_node const& v, span<char const> salt
	, sequence_number const seq, public_key const& pk, signature const& sig)
{
	TORRENT_ASSERT(v.data_section().size() <= 1000);
	if (!verify_mutable_item(v.data_section(), salt, seq, pk, sig))
		return false;
	m_pk = pk;
	m_sig = sig;
	if (salt.size() > 0)
		m_salt.assign(salt.data(), salt.size());
	else
		m_salt.clear();
	m_seq = seq;
	m_mutable = true;

	m_value = v;
	return true;
}

void item::assign(entry v, span<char const> salt
	, sequence_number const seq
	, public_key const& pk, signature const& sig)
{
	m_pk = pk;
	m_sig = sig;
	m_salt.assign(salt.data(), salt.size());
	m_seq = seq;
	m_mutable = true;
	m_value = std::move(v);
}

} } // namespace libtorrent::dht
