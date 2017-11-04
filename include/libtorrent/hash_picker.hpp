/*

Copyright (c) 2017, BitTorrent Inc.
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

#ifndef TORRENT_HASH_PICKER_HPP_INCLUDED
#define TORRENT_HASH_PICKER_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/vector.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/bitfield.hpp"
#include <deque>
#include <map>

namespace libtorrent
{
	struct torrent_peer;
	struct peer_connection_interface;

	struct set_chunk_hash_result
	{
		enum result_status
		{
			// hash is verified
			success,
			// hash cannot be verified yet
			unknown,
			// hash conflict in leaf node
			chunk_hash_failed,
			// hash conflict in a parent node
			piece_hash_failed
		};

		set_chunk_hash_result(result_status s) : status(s), first_verified_chunk(0), num_verified(0) {}
		set_chunk_hash_result(int first_chunk, int num) : status(success), first_verified_chunk(first_chunk), num_verified(num) {}

		result_status status;
		// if status is success, this will hold the index of the first verified
		// block hash as an offset from the index of the first block in the piece
		int first_verified_chunk;
		int num_verified;
	};

	struct add_hashes_result
	{
		explicit add_hashes_result(bool valid) : valid(valid) {}

		bool valid;
		std::map<piece_index_t, std::vector<int>> hash_failed;
		std::vector<piece_index_t> hash_passed;
	};

	struct node_index
	{
		node_index(file_index_t f, std::int32_t n) : file(f), node(n) {}
		bool operator==(node_index const& o) const { return file == o.file && node == o.node; }
		file_index_t file;
		std::int32_t node;
	};

	struct TORRENT_EXTRA_EXPORT hash_request
	{
		hash_request(int file, int base, int index, int count, int proofs)
			: file(file), base(base), index(index), count(count), proof_layers(proofs)
		{
			TORRENT_ASSERT(file >= 0 && base >= 0 && index >= 0 && count >= 0 && proofs >= 0);
		}

		hash_request(hash_request const&) = default;
		hash_request& operator=(hash_request const& o)
		{
			new (this) hash_request(o);
			return *this;
		}

		bool operator==(hash_request const& o) const
		{
			return file == o.file && base == o.base && index == o.index && count == o.count
				&& proof_layers == o.proof_layers;
		}

		int const file;
		int const base;
		int const index;
		int const count;
		int const proof_layers;
	};

	class TORRENT_EXTRA_EXPORT hash_picker
	{
	public:
		hash_picker(file_storage const& files
			, aux::vector<std::vector<sha256_hash>, file_index_t>& trees
			, aux::vector<std::vector<bool>, file_index_t> verified = {}
			, bool all_verified = false);

		void set_verified(aux::vector<std::vector<bool>, file_index_t> const& verified);

		std::vector<hash_request> pick_hashes(typed_bitfield<piece_index_t> const& pieces, int num_blocks
			, peer_connection_interface* peer);

		add_hashes_result add_hashes(hash_request const& req, span<sha256_hash> hashes);
		// TODO: support batched adding of chunk hashes for reduced overhead?
		set_chunk_hash_result set_chunk_hash(piece_index_t piece, int offset, sha256_hash const& h);
		void hashes_rejected(peer_connection_interface* source, hash_request const& req);
		void peer_disconnected(peer_connection_interface* peer);
		void verify_chunk_hashes(piece_index_t index);

		// do we know the piece layer hash for a piece
		bool have_hash(piece_index_t index) const;
		// do we know all the block hashes for a file?
		bool have_all(file_index_t file) const;
		bool have_all() const;
		// get bits indicating if each leaf hash is verified
		aux::vector<std::vector<bool>, file_index_t> const& verified_leafs() const
		{ return m_hash_verified; }
		bool piece_verified(piece_index_t piece) const;

		int piece_layer() const { return m_piece_layer; }

	private:
		// returns the number of proof layers needed to verify the node's hash
		int layers_to_verify(node_index idx) const;
		int file_num_layers(file_index_t idx) const;

		struct chunks_request
		{
			explicit chunks_request(piece_index_t i)
				: index(i), peer(nullptr) {}
			chunks_request(piece_index_t i, peer_connection_interface* p)
				: index(i), peer(p) {}
			bool operator<(chunks_request const& rhs) const { return index < rhs.index; }
			piece_index_t index;
			peer_connection_interface* peer;
		};

		file_storage const& m_files;
		aux::vector<std::vector<sha256_hash>, file_index_t>& m_merkle_trees;
		aux::vector<std::vector<bool>, file_index_t> m_hash_verified;
		// stores the peer each chunk of piece hashes was requested from
		// or nullptr if the chunk has not been requested or received
		aux::vector<std::vector<torrent_peer*>, file_index_t> m_piece_hash_requested;
		std::deque<piece_index_t> m_priority_pieces;
		// pieces which we need to download chunk hashes for
		std::deque<chunks_request> m_chunk_requests;
		int const m_piece_layer;
		int const m_piece_tree_root_layer;
	};
} // namespace libtorrent

#endif // TORRENT_HASH_PICKER_HPP_INCLUDED
