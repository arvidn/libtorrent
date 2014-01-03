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
#include <libtorrent/lazy_entry.hpp>
#include <libtorrent/entry.hpp>
#include <vector>
#include <exception>

namespace libtorrent { namespace dht
{

sha1_hash TORRENT_EXTRA_EXPORT item_target_id(
	std::pair<char const*, int> v
	, std::pair<char const*, int> salt
	, char const* pk);

bool TORRENT_EXTRA_EXPORT verify_mutable_item(
	std::pair<char const*, int> v,
	std::pair<char const*, int> salt,
	boost::uint64_t seq,
	char const* pk,
	char const* sig);

void TORRENT_EXTRA_EXPORT sign_mutable_item(
	std::pair<char const*, int> v,
	std::pair<char const*, int> salt,
	boost::uint64_t seq,
	char const* pk,
	char const* sk,
	char* sig);

sha1_hash TORRENT_EXTRA_EXPORT mutable_item_cas(
	std::pair<char const*, int> v
	, std::pair<char const*, int> salt
	, boost::uint64_t seq);

struct TORRENT_EXTRA_EXPORT invalid_item : std::exception
{
	virtual const char* what() const throw() { return "invalid DHT item"; }
};

enum
{
	item_pk_len = 32,
	item_sk_len = 64,
	item_sig_len = 64
};

class TORRENT_EXTRA_EXPORT item
{
public:
	item() : m_mutable(false) {}
	item(entry const& v) { assign(v); }
	item(entry const& v
		, std::pair<char const*, int> salt
		, boost::uint64_t seq, char const* pk, char const* sk);
	item(lazy_entry const* v) { assign(v); }
	item(lazy_entry const* v, std::pair<char const*, int> salt
		, boost::uint64_t seq, char const* pk, char const* sig);

	void assign(entry const& v)
	{
		assign(v, std::pair<char const*, int>(NULL, 0), 0, NULL, NULL);
	}
	void assign(entry const& v
		, std::pair<char const*, int> salt
		, boost::uint64_t seq, char const* pk, char const* sk);
	void assign(lazy_entry const* v)
	{
		assign(v, std::pair<char const*, int>(NULL, 0), 0, NULL, NULL);
	}
	bool assign(lazy_entry const* v
		, std::pair<char const*, int> salt
		, boost::uint64_t seq, char const* pk, char const* sig);

	void clear() { m_value = entry(); }
	bool empty() const { return m_value.type() == entry::undefined_t; }

	bool is_mutable() const { return m_mutable; }

	sha1_hash cas();

	entry const& value() const { return m_value; }
	char const* pk() { TORRENT_ASSERT(m_mutable); return m_pk; }
	char const* sig() { TORRENT_ASSERT(m_mutable); return m_sig; }
	boost::uint64_t seq() { TORRENT_ASSERT(m_mutable); return m_seq; }

private:
	entry m_value;
	std::string m_salt;
	char m_pk[item_pk_len];
	char m_sig[item_sig_len];
	boost::uint64_t m_seq;
	bool m_mutable;
};

} } // namespace libtorrent::dht

#endif // LIBTORRENT_ITEM_HPP
