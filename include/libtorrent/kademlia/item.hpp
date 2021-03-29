/*

Copyright (c) 2013-2019, Steven Siloti
Copyright (c) 2013-2016, 2018-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_ITEM_HPP
#define LIBTORRENT_ITEM_HPP

#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/span.hpp>
#include <libtorrent/kademlia/types.hpp>

namespace lt {
namespace dht {

// calculate the target hash for an immutable item.
TORRENT_EXTRA_EXPORT sha1_hash item_target_id(span<char const> v);

// calculate the target hash for a mutable item.
TORRENT_EXTRA_EXPORT sha1_hash item_target_id(span<char const> salt
	, public_key const& pk);

TORRENT_EXTRA_EXPORT bool verify_mutable_item(
	span<char const> v
	, span<char const> salt
	, sequence_number seq
	, public_key const& pk
	, signature const& sig);

// TODO: since this is a public function, it should probably be moved
// out of this header and into one with other public functions.

// given a byte range ``v`` and an optional byte range ``salt``, a
// sequence number, public key ``pk`` (must be 32 bytes) and a secret key
// ``sk`` (must be 64 bytes), this function produces a signature which
// is written into a 64 byte buffer pointed to by ``sig``. The caller
// is responsible for allocating the destination buffer that's passed in
// as the ``sig`` argument. Typically it would be allocated on the stack.
TORRENT_EXPORT signature sign_mutable_item(
	span<char const> v
	, span<char const> salt
	, sequence_number seq
	, public_key const& pk
	, secret_key const& sk);

class TORRENT_EXTRA_EXPORT item
{
public:
	item() {}
	item(public_key const& pk, span<char const> salt);
	explicit item(entry v);
	item(entry v
		, span<char const> salt
		, sequence_number seq
		, public_key const& pk
		, secret_key const& sk);
	explicit item(bdecode_node const& v);

	void assign(entry v);
	void assign(entry v, span<char const> salt
		, sequence_number seq
		, public_key const& pk
		, secret_key const& sk);
	void assign(bdecode_node const& v);
	bool assign(bdecode_node const& v, span<char const> salt
		, sequence_number seq
		, public_key const& pk
		, signature const& sig);
	void assign(entry v, span<char const> salt
		, sequence_number seq
		, public_key const& pk
		, signature const& sig);

	void clear() { m_value = entry(); }
	bool empty() const { return m_value.type() == entry::undefined_t; }

	bool is_mutable() const { return m_mutable; }

	entry const& value() const { return m_value; }
	public_key const& pk() const
	{ return m_pk; }
	signature const& sig() const
	{ return m_sig; }
	sequence_number seq() const { return m_seq; }
	std::string const& salt() const { return m_salt; }

private:
	entry m_value;
	std::string m_salt;
	public_key m_pk;
	signature m_sig;
	sequence_number m_seq{0};
	bool m_mutable = false;
};

} // namespace dht
} // namespace lt

#endif // LIBTORRENT_ITEM_HPP
