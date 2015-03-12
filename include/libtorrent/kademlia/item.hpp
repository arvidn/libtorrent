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

#ifndef LIBTORRENT_ITEM_HPP
#define LIBTORRENT_ITEM_HPP

#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/entry.hpp>
#include <vector>
#include <exception>
#include <boost/array.hpp>

namespace libtorrent { namespace dht
{

// calculate the target hash for an immutable item.
sha1_hash TORRENT_EXTRA_EXPORT item_target_id(
	std::pair<char const*, int> v);

// calculate the target hash for a mutable item.
sha1_hash TORRENT_EXTRA_EXPORT item_target_id(std::pair<char const*, int> salt
	, char const* pk);

bool TORRENT_EXTRA_EXPORT verify_mutable_item(
	std::pair<char const*, int> v
	, std::pair<char const*, int> salt
	, boost::uint64_t seq
	, char const* pk
	, char const* sig);

// TODO: since this is a public function, it should probably be moved
// out of this header and into one with other public functions.

// given a byte range ``v`` and an optional byte range ``salt``, a
// sequence number, public key ``pk`` (must be 32 bytes) and a secret key
// ``sk`` (must be 64 bytes), this function produces a signature which
// is written into a 64 byte buffer pointed to by ``sig``. The caller
// is responsible for allocating the destination buffer that's passed in
// as the ``sig`` argument. Typically it would be allocated on the stack.
void TORRENT_EXPORT sign_mutable_item(
	std::pair<char const*, int> v
	, std::pair<char const*, int> salt
	, boost::uint64_t seq
	, char const* pk
	, char const* sk
	, char* sig);

enum
{
	item_pk_len = 32,
	item_sk_len = 64,
	item_sig_len = 64
};

class TORRENT_EXTRA_EXPORT item
{
public:
	item() : m_seq(0), m_mutable(false)  {}
	item(char const* pk, std::string const& salt);
	item(entry const& v) { assign(v); }
	item(entry const& v
		, std::pair<char const*, int> salt
		, boost::uint64_t seq, char const* pk, char const* sk);
	item(bdecode_node const& v) { assign(v); }

	void assign(entry const& v)
	{
		assign(v, std::pair<char const*, int>(static_cast<char const*>(NULL)
			, 0), 0, NULL, NULL);
	}
	void assign(entry const& v, std::pair<char const*, int> salt
		, boost::uint64_t seq, char const* pk, char const* sk);
	void assign(bdecode_node const& v)
	{
		assign(v, std::pair<char const*, int>(static_cast<char const*>(NULL)
			, 0), 0, NULL, NULL);
	}
	bool assign(bdecode_node const& v, std::pair<char const*, int> salt
		, boost::uint64_t seq, char const* pk, char const* sig);
	void assign(entry const& v, std::string salt, boost::uint64_t seq
		, char const* pk, char const* sig);

	void clear() { m_value = entry(); }
	bool empty() const { return m_value.type() == entry::undefined_t; }

	bool is_mutable() const { return m_mutable; }

	entry const& value() const { return m_value; }
	boost::array<char, item_pk_len> const& pk() const
	{ return m_pk; }
	boost::array<char, item_sig_len> const& sig() const
	{ return m_sig; }
	boost::uint64_t seq() const { return m_seq; }
	std::string const& salt() const { return m_salt; }

private:
	entry m_value;
	std::string m_salt;
	boost::array<char, item_pk_len> m_pk;
	boost::array<char, item_sig_len> m_sig;
	boost::uint64_t m_seq;
	bool m_mutable;
};

} } // namespace libtorrent::dht

#endif // LIBTORRENT_ITEM_HPP
