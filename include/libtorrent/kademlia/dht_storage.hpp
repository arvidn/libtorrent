/*

Copyright (c) 2012-2016, Arvid Norberg, Alden Torres
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

#ifndef TORRENT_DHT_STORAGE_HPP
#define TORRENT_DHT_STORAGE_HPP

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/function.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include <libtorrent/socket.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/address.hpp>

namespace libtorrent
{
	struct dht_settings;
	class entry;
}

namespace libtorrent {
namespace dht
{
	// This structure hold the relevant counters for the storage
	struct TORRENT_EXPORT dht_storage_counters
	{
		boost::int32_t torrents;
		boost::int32_t peers;
		boost::int32_t immutable_data;
		boost::int32_t mutable_data;
	};

	// The DHT storage interface is a pure virtual class that can
	// be implemented to customize how the data for the DHT is stored.
	//
	// The default storage implementation uses three maps in RAM to save
	// the peers, mutable and immutable items and it's designed to
	// provide a fast and fully compliant behavior of the BEPs.
	//
	// libtorrent comes with one built-in storage implementation:
	// ``dht_default_storage`` (private non-accessible class). Its
	// constructor function is called dht_default_storage_constructor().
	//
	struct TORRENT_EXPORT dht_storage_interface
	{
#ifndef TORRENT_NO_DEPRECATE
		// This function returns the number of torrents tracked by
		// the DHT at the moment. It's used to fill session_status.
		// It's deprecated.
		//
		virtual size_t num_torrents() const = 0;

		// This function returns the sum of all of peers per torrent
		// tracker byt the DHT at the moment.
		// It's deprecated.
		//
		virtual size_t num_peers() const = 0;
#endif

		// This function retrieve the peers tracked by the DHT
		// corresponding to the given info_hash. You can specify if
		// you want only seeds and/or you are scraping the data.
		//
		// For future implementers:
		// If the torrent tracked contains a name, such a name
		// must be stored as a string in peers["n"]
		//
		// If the scrape parameter is true, you should fill these keys::
		// 
		//    peers["BFpe"] - with the standard bit representation of a
		//                    256 bloom filter containing the downloaders
		//    peers["BFsd"] - with the standard bit representation of a
		//                    256 bloom filter containing the seeders
		//
		// If the scrape parameter is false, you should fill the
		// key peers["values"] with a list containing a subset of
		// peers tracked by the given info_hash. Such a list should
		// consider the value of dht_settings::max_peers_reply.
		// If noseed is true only peers marked as no seed should be included.
		//
		// returns true if an entry with the info_hash is found and
		// the data is returned inside the (entry) out parameter peers.
		//
		virtual bool get_peers(sha1_hash const& info_hash
			, bool noseed, bool scrape
			, entry& peers) const = 0;

		// This function is named announce_peer for consistency with the
		// upper layers, but has nothing to do with networking. Its only
		// responsibility is store the peer in such a way that it's returned
		// in the entry with the lookup_peers.
		//
		// The ``name`` parameter is the name of the torrent if provided in
		// the announce_peer DHT message. The length of this value should
		// have a maximum length in the final storage. The default
		// implementation truncate the value for a maximum of 50 characters.
		//
		virtual void announce_peer(sha1_hash const& info_hash
			, tcp::endpoint const& endp
			, std::string const& name, bool seed) = 0;

		// This function retrieves the immutable item given its target hash.
		//
		// For future implementers:
		// The value should be returned as an entry in the key item["v"].
		//
		// returns true if the item is found and the data is returned
		// inside the (entry) out parameter item.
		//
		virtual bool get_immutable_item(sha1_hash const& target
			, entry& item) const = 0;

		// Store the item's data. This layer is only for storage.
		// The authentication of the item is performed by the upper layer.
		//
		// For implementers:
		// This data can be stored only if the target is not already
		// present. The implementation should consider the value of
		// dht_settings::max_dht_items.
		//
		virtual void put_immutable_item(sha1_hash const& target
			, char const* buf, int size
			, address const& addr) = 0;

		// This function retrieves the sequence number of a mutable item.
		//
		// returns true if the item is found and the data is returned
		// inside the out parameter seq.
		//
		virtual bool get_mutable_item_seq(sha1_hash const& target
			, boost::int64_t& seq) const = 0;

		// This function retrieves the mutable stored in the DHT.
		//
		// For implementers:
		// The item sequence should be stored in the key item["seq"].
		// if force_fill is true or (0 <= seq and seq < item["seq"])
		// the following keys should be filled
		// item["v"] - with the value no encoded.
		// item["sig"] - with a string representation of the signature.
		// item["k"] - with a string representation of the public key.
		//
		// returns true if the item is found and the data is returned
		// inside the (entry) out parameter item.
		//
		virtual bool get_mutable_item(sha1_hash const& target
			, boost::int64_t seq, bool force_fill
			, entry& item) const = 0;

		// Store the item's data. This layer is only for storage.
		// The authentication of the item is performed by the upper layer.
		//
		// For implementers:
		// The sequence number should be checked if the item is already
		// present. The implementation should consider the value of
		// dht_settings::max_dht_items.
		//
		virtual void put_mutable_item(sha1_hash const& target
			, char const* buf, int size
			, char const* sig
			, boost::int64_t seq
			, char const* pk
			, char const* salt, int salt_size
			, address const& addr) = 0;

		// This function is called periodically (non-constant frequency).
		//
		// For implementers:
		// Use this functions for expire peers or items or any other
		// storage cleanup.
		//
		virtual void tick() = 0;

		virtual dht_storage_counters counters() const = 0;

		virtual ~dht_storage_interface() {}
	};

	typedef boost::function<dht_storage_interface*(sha1_hash const& id
		, dht_settings const& settings)> dht_storage_constructor_type;

	TORRENT_EXPORT dht_storage_interface* dht_default_storage_constructor(sha1_hash const& id
		, dht_settings const& settings);

} } // namespace libtorrent::dht

#endif //TORRENT_DHT_STORAGE_HPP
