/*

Copyright (c) 2017, BitTorrent Inc.
Copyright (c) 2019, Steven Siloti
Copyright (c) 2019-2020, Arvid Norberg
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
#include "libtorrent/aux_/merkle_tree.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/time.hpp"
#include <deque>
#include <map>

namespace libtorrent
{
	struct torrent_peer;

	struct set_block_hash_result
	{
		enum class result
		{
			// hash is verified
			success,
			// hash cannot be verified yet
			unknown,
			// hash conflict in leaf node
			block_hash_failed,
			// hash conflict in a parent node
			piece_hash_failed
		};

		explicit set_block_hash_result(result s) : status(s), first_verified_block(0), num_verified(0) {}
		set_block_hash_result(int first_block, int num) : status(result::success), first_verified_block(first_block), num_verified(num) {}

		static set_block_hash_result unknown() { return set_block_hash_result(result::unknown); }
		static set_block_hash_result block_hash_failed() { return set_block_hash_result(result::block_hash_failed); }
		static set_block_hash_result piece_hash_failed() { return set_block_hash_result(result::piece_hash_failed); }

		result status;
		// if status is success, this will hold the index of the first verified
		// block hash as an offset from the index of the first block in the piece
		int first_verified_block;
		int num_verified;
	};

	struct add_hashes_result
	{
		explicit add_hashes_result(bool const v) : valid(v) {}

		bool valid;
		// the vector contains the block indices (within the piece) that failed
		// the hash check
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

	// the hash request represents a range of hashes in the merkle hash tree for
	// a specific file ('file').
	struct TORRENT_EXTRA_EXPORT hash_request
	{
		hash_request() = default;
		hash_request(file_index_t const f, int const b, int const i, int const c, int const p)
			: file(f), base(b), index(i), count(c), proof_layers(p)
		{}

		hash_request(hash_request const&) = default;
		hash_request& operator=(hash_request const& o) = default;

		bool operator==(hash_request const& o) const
		{
			return file == o.file && base == o.base && index == o.index && count == o.count
				&& proof_layers == o.proof_layers;
		}

		file_index_t file{0};
		// indicates which *level* of the tree we're referring to. 0 means the
		// leaf level.
		int base = 0;
		// the index of the first hash at the specified level.
		int index = 0;
		// the number of hashes in the range
		int count = 0;
		int proof_layers = 0;
	};

	// validates the hash_request, to ensure its invariant as well as matching
	// the torrent's file_storage and the number of hashes accompanying the
	// request
	TORRENT_EXTRA_EXPORT
	bool validate_hash_request(hash_request const& hr, file_storage const& fs);

	class TORRENT_EXTRA_EXPORT hash_picker
	{
	public:
		hash_picker(file_storage const& files
			, aux::vector<aux::merkle_tree, file_index_t>& trees
			, aux::vector<std::vector<bool>, file_index_t> verified = {}
			, bool all_verified = false);

		hash_request pick_hashes(typed_bitfield<piece_index_t> const& pieces);

		add_hashes_result add_hashes(hash_request const& req, span<sha256_hash const> hashes);
		// TODO: support batched adding of block hashes for reduced overhead?
		set_block_hash_result set_block_hash(piece_index_t piece, int offset, sha256_hash const& h);
		void hashes_rejected(hash_request const& req);
		void verify_block_hashes(piece_index_t index);

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

		struct piece_hash_request
		{
			time_point last_request = min_time();
			int num_requests = 0;
			bool have = false;
		};

		struct priority_block_request
		{
			priority_block_request(file_index_t const f, int const b)
				: file(f), block(b) {}
			file_index_t file;
			int block;
			int num_requests = 0;
			bool operator==(priority_block_request const& o) const
			{ return file == o.file && block == o.block; }
			bool operator!=(priority_block_request const& o) const
			{ return !(*this == o); }
			bool operator<(priority_block_request const& o) const
			{ return num_requests < o.num_requests; }
		};

		struct piece_block_request
		{
			piece_block_request(file_index_t const f, piece_index_t::diff_type const p) : file(f), piece(p) {}
			file_index_t file;
			// the piece from the start of the file
			piece_index_t::diff_type piece;
			time_point last_request;
			int num_requests = 0;
			bool operator==(piece_block_request const& o) const
			{ return file == o.file && piece == o.piece; }
			bool operator!=(piece_block_request const& o) const
			{ return !(*this == o); }
			bool operator<(piece_block_request const& o) const
			{ return num_requests < o.num_requests; }
		};

		file_storage const& m_files;
		aux::vector<aux::merkle_tree, file_index_t>& m_merkle_trees;
		aux::vector<std::vector<bool>, file_index_t> m_hash_verified;

		// information about every 512-piece span of each file. We request hashes
		// for 512 pieces at a time
		aux::vector<aux::vector<piece_hash_request>, file_index_t> m_piece_hash_requested;

		// this is for a future per-block request feature
#if 0
		// blocks are only added to this list if there is a time critial block which
		// has been downloaded but we don't have its hash or if the initial request
		// for the hash was rejected
		// this block hash will be requested from every peer possible until the hash
		// is received
		// the vector is sorted by the number of requests sent for each block
		aux::vector<priority_block_request> m_priority_block_requests;
#endif

		// when a piece fails hash check a request is queued to download the piece's
		// block hashes
		aux::vector<piece_block_request> m_piece_block_requests;

		// this is the number of tree levels in a piece. if the piece size is 16
		// kiB, this is 0, since there is no tree per piece. If the piece size is
		// 32 kiB, it's 1, and so on.
		int const m_piece_layer;

		// this is the number of tree layers for a 512-piece range, which is
		// the granularity with which we send hash requests. The number of layers
		// all the way down the the block level.
		int const m_piece_tree_root_layer;
	};
} // namespace libtorrent

#endif // TORRENT_HASH_PICKER_HPP_INCLUDED
