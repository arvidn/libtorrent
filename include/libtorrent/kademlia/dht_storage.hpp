/*

Copyright (c) 2012-2018, Arvid Norberg, Alden Torres
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

#include <functional>

#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/types.hpp>

#include <libtorrent/socket.hpp>
#include <libtorrent/address.hpp>
#include <libtorrent/span.hpp>
#include <libtorrent/string_view.hpp>

namespace libtorrent {
	class entry;
}

namespace libtorrent { namespace dht {
	struct dht_settings;

	// This structure hold the relevant counters for the storage
	struct TORRENT_EXPORT dht_storage_counters
	{
		std::int32_t torrents = 0;
		std::int32_t peers = 0;
		std::int32_t immutable_data = 0;
		std::int32_t mutable_data = 0;

		// This member function set the counters to zero.
		void reset();
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
	// You should know that if this storage becomes full of DHT items,
	// the current implementation could degrade in performance.
	struct TORRENT_EXPORT dht_storage_interface
	{
#if TORRENT_ABI_VERSION == 1
		// This function returns the number of torrents tracked by
		// the DHT at the moment. It's used to fill session_status.
		// It's deprecated.
		virtual size_t num_torrents() const = 0;

		// This function returns the sum of all of peers per torrent
		// tracker byt the DHT at the moment.
		// It's deprecated.
		virtual size_t num_peers() const = 0;
#endif

		// This member function notifies the list of all node's ids
		// of each DHT running inside libtorrent. It's advisable
		// that the concrete implementation keeps a copy of this list
		// for an eventual prioritization when deleting an element
		// to make room for a new one.
		virtual void update_node_ids(std::vector<node_id> const& ids) = 0;

		// This function retrieve the peers tracked by the DHT
		// corresponding to the given info_hash. You can specify if
		// you want only seeds and/or you are scraping the data.
		//
		// For future implementers:
		// If the torrent tracked contains a name, such a name
		// must be stored as a string in peers["n"]
		//
		// If the scrape parameter is true, you should fill these keys:
		//
		//    peers["BFpe"]
		//       with the standard bit representation of a
		//       256 bloom filter containing the downloaders
		//    peers["BFsd"]
		//       with the standard bit representation of a
		//       256 bloom filter containing the seeders
		//
		// If the scrape parameter is false, you should fill the
		// key peers["values"] with a list containing a subset of
		// peers tracked by the given info_hash. Such a list should
		// consider the value of dht_settings::max_peers_reply.
		// If noseed is true only peers marked as no seed should be included.
		//
		// returns true if the maximum number of peers are stored
		// for this info_hash.
		virtual bool get_peers(sha1_hash const& info_hash
			, bool noseed, bool scrape, address const& requester
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
		virtual void announce_peer(sha1_hash const& info_hash
			, tcp::endpoint const& endp
			, string_view name, bool seed) = 0;

		// This function retrieves the immutable item given its target hash.
		//
		// For future implementers:
		// The value should be returned as an entry in the key item["v"].
		//
		// returns true if the item is found and the data is returned
		// inside the (entry) out parameter item.
		virtual bool get_immutable_item(sha1_hash const& target
			, entry& item) const = 0;

		// Store the item's data. This layer is only for storage.
		// The authentication of the item is performed by the upper layer.
		//
		// For implementers:
		// This data can be stored only if the target is not already
		// present. The implementation should consider the value of
		// dht_settings::max_dht_items.
		virtual void put_immutable_item(sha1_hash const& target
			, span<char const> buf
			, address const& addr) = 0;

		// This function retrieves the sequence number of a mutable item.
		//
		// returns true if the item is found and the data is returned
		// inside the out parameter seq.
		virtual bool get_mutable_item_seq(sha1_hash const& target
			, sequence_number& seq) const = 0;

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
		virtual bool get_mutable_item(sha1_hash const& target
			, sequence_number seq, bool force_fill
			, entry& item) const = 0;

		// Store the item's data. This layer is only for storage.
		// The authentication of the item is performed by the upper layer.
		//
		// For implementers:
		// The sequence number should be checked if the item is already
		// present. The implementation should consider the value of
		// dht_settings::max_dht_items.
		virtual void put_mutable_item(sha1_hash const& target
			, span<char const> buf
			, signature const& sig
			, sequence_number seq
			, public_key const& pk
			, span<char const> salt
			, address const& addr) = 0;

		// This function retrieves a sample info-hashes
		//
		// For implementers:
		// The info-hashes should be stored in ["samples"] (N x 20 bytes).
		// the following keys should be filled
		// item["interval"] - the subset refresh interval in seconds.
		// item["num"] - number of info-hashes in storage.
		//
		// Internally, this function is allowed to lazily evaluate, cache
		// and modify the actual sample to put in ``item``
		//
		// returns the number of info-hashes in the sample.
		virtual int get_infohashes_sample(entry& item) = 0;

		// This function is called periodically (non-constant frequency).
		//
		// For implementers:
		// Use this functions for expire peers or items or any other
		// storage cleanup.
		virtual void tick() = 0;

		// return stats counters for the store
		virtual dht_storage_counters counters() const = 0;

		// hidden
		virtual ~dht_storage_interface() {}
	};

	using dht_storage_constructor_type
		= std::function<std::unique_ptr<dht_storage_interface>(dht_settings const& settings)>;

	// constructor for the default DHT storage. The DHT storage is responsible
	// for maintaining peers and mutable and immutable items announced and
	// stored/put to the DHT node.
	TORRENT_EXPORT std::unique_ptr<dht_storage_interface> dht_default_storage_constructor(
		dht_settings const& settings);

} } // namespace libtorrent::dht

#endif //TORRENT_DHT_STORAGE_HPP
