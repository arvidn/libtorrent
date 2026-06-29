/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// Fuzzer for aux::merkle_tree (src/merkle_tree.cpp). Builds a deterministic
// reference flat tree from the fuzz input, constructs a merkle_tree with the
// matching root, then drives a sequence of fuzz-controlled operations
// (set_block, load_tree, load_sparse_tree, add_hashes with valid and corrupted
// proofs, load_piece_layer) against it. After every operation it verifies
// that every node the tree claims to know matches the reference, that padding
// positions report the implied pad, and that round-tripping through
// build_sparse_vector / load_sparse_tree preserves the state.
//
// For add_hashes() in particular, the fuzzer snapshots the tree's canonical
// representation (build_vector) before each call and, if the call returns
// nullopt, verifies that the post-call representation is bit-for-bit
// identical -- i.e. that a failed proof leaves the tree fully restored to
// its prior state. Any invariant violation or assertion failure terminates
// the run.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "libtorrent/aux_/merkle.hpp"
#include "libtorrent/aux_/merkle_tree.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/sha1_hash.hpp"

namespace {

	namespace lt = libtorrent;

	// Build a deterministic standard-layout reference tree. The leaf
	// values just need to be reproducible and varied; std::mt19937_64
	// seeded from the fuzz input gives us that with no cryptographic
	// cost. Interior nodes still get computed with merkle_fill_tree's
	// real sha256 -- those must match the merkle-tree algorithm.
	std::vector<lt::sha256_hash> build_reference(int const num_blocks, std::uint32_t const seed)
	{
		int const num_leafs = lt::merkle_num_leafs(num_blocks);
		int const num_nodes = lt::merkle_num_nodes(num_leafs);
		std::vector<lt::sha256_hash> tree;
		tree.resize(std::size_t(num_nodes));
		int const first_leaf = lt::merkle_first_leaf(num_leafs);

		std::mt19937_64 rng(seed);
		for (int i = 0; i < num_blocks; ++i)
		{
			lt::sha256_hash& h = tree[std::size_t(first_leaf + i)];
			char* dst = h.data();
			for (int k = 0; k < 32; k += 8)
			{
				std::uint64_t v = rng();
				std::memcpy(dst + k, &v, 8);
			}
		}

		lt::merkle_fill_tree(tree, num_leafs);
		return tree;
	}

	// Validate consistency between the tree and the reference. Aborts on any
	// violation. Interior nodes that has_node reports must match the reference
	// (the class invariant is: interior nodes are either set+valid or cleared).
	// Leaf-layer slots can carry an unverified speculative hash from
	// set_block; for those, only validated blocks must match.
	//
	// For small trees (<= 256 nodes) every position is checked. For larger
	// trees we check the per-layer boundary positions (last live + first
	// padding) deterministically and then sample a fixed number of random
	// (L, O) positions from `rng`. This keeps validate() O(1) regardless
	// of tree size, while preserving full coverage where it's affordable.
	void validate(lt::aux::merkle_tree const& t,
		int const num_blocks,
		std::vector<lt::sha256_hash> const& reference,
		std::mt19937_64& rng)
	{
		int const N = t.num_layers();
		if (N != lt::merkle_num_layers(lt::merkle_num_leafs(num_blocks))) std::abort();
		for (int L = 0; L <= N; ++L)
		{
			// layer_size_live should be ceil(num_blocks / 2^(N - L)) -- the
			// number of live (non-padding) nodes in layer L when the leaf
			// layer carries num_blocks live entries.
			int const shift = N - L;
			int const expected_live = (num_blocks + (1 << shift) - 1) >> shift;
			if (t.layer_size_live(L) != expected_live) std::abort();
		}

		auto check = [&](int const L, int const O) {
			int const flat_start = lt::merkle_layer_start(L);
			lt::sha256_hash const ref = reference[std::size_t(flat_start + O)];
			lt::sha256_hash const got = t.node_at(L, O);

			// is_padding / live_count consistency
			if (t.is_padding(L, O) != (O >= t.layer_size_live(L))) std::abort();

			if (L == N)
			{
				// leaf layer: a non-zero slot may be unverified (set_block
				// stores speculatively); only require a reference match
				// when blocks_verified says the slot was validated.
				if (O >= num_blocks) return; // padding
				if (t.blocks_verified(O, 1) && got != ref) std::abort();
			}
			else if (t.has_node(L, O))
			{
				// interior nodes that we have are validated by class
				// invariant: they must match the reference.
				if (got != ref) std::abort();
			}
			else
			{
				// a not-known interior slot must be zero (or the implied
				// pad if it's actually a padding offset).
				if (!got.is_all_zeros() && !t.is_padding(L, O)) std::abort();
			}
		};

		int const num_leafs = lt::merkle_num_leafs(num_blocks);
		int const total_nodes = lt::merkle_num_nodes(num_leafs);

		// Always check the live/padding boundary at every layer -- these
		// are the most failure-prone positions and there are only O(N) of
		// them.
		for (int L = 0; L <= N; ++L)
		{
			int const live = t.layer_size_live(L);
			int const layer_size = 1 << L;
			if (live > 0) check(L, live - 1);
			if (live < layer_size) check(L, live);
		}

		constexpr int full_check_threshold = 256;
		if (total_nodes <= full_check_threshold)
		{
			for (int L = 0; L <= N; ++L)
			{
				int const layer_size = 1 << L;
				for (int O = 0; O < layer_size; ++O)
					check(L, O);
			}
		}
		else
		{
			constexpr int sample_count = 50;
			for (int s = 0; s < sample_count; ++s)
			{
				int const L = int(rng() % static_cast<std::uint64_t>(N + 1));
				int const O = int(rng() % static_cast<std::uint64_t>(1 << L));
				check(L, O);
			}
		}
	}

	// Round-trip via build_sparse_vector / load_sparse_tree and verify
	// idempotency: a single round-trip may drop "orphan" data (hashes whose
	// validation chain doesn't reach the root, which load_sparse_tree's
	// merkle_fill_partial_tree clears intentionally), but a second round-trip
	// from that result must give the same tree.
	void round_trip(lt::aux::merkle_tree const& t,
		int const num_blocks,
		int const blocks_per_piece,
		std::vector<lt::sha256_hash> const& reference,
		std::mt19937_64& rng)
	{
		lt::bitfield const empty_verified(num_blocks);

		auto const [sparse, mask] = t.build_sparse_vector();
		lt::aux::merkle_tree t2(num_blocks, blocks_per_piece, reference[0].data());
		t2.load_sparse_tree(sparse, mask, empty_verified);
		validate(t2, num_blocks, reference, rng);

		auto const [sparse2, mask2] = t2.build_sparse_vector();
		lt::aux::merkle_tree t3(num_blocks, blocks_per_piece, reference[0].data());
		t3.load_sparse_tree(sparse2, mask2, empty_verified);
		validate(t3, num_blocks, reference, rng);

		if (t2.build_vector() != t3.build_vector()) std::abort();
	}

	// Drive add_hashes with a fuzz-controlled (layer, subtree size, slot,
	// corruption) tuple. The hashes and uncle proofs are taken from the
	// reference, then optionally corrupted in one of three ways. The key
	// invariant tested here: if add_hashes returns nullopt (failed proof),
	// the tree's canonical representation must be unchanged from before
	// the call -- merkle_validate_and_insert_proofs is responsible for
	// rolling back any partial inserts on failure.
	void do_add_hashes(lt::aux::merkle_tree& t,
		int const num_blocks,
		std::vector<lt::sha256_hash> const& reference,
		std::uint8_t const layer_byte,
		std::uint8_t const subtree_log_byte,
		std::uint16_t const slot_raw,
		std::uint8_t const corrupt_byte)
	{
		int const num_leafs = lt::merkle_num_leafs(num_blocks);
		int const N = lt::merkle_num_layers(num_leafs);
		// A single-block tree has only a root; no add_hashes call applies.
		if (N == 0) return;

		// Destination layer for the leaves of the subtree we're inserting.
		// Restricted to [1, N] so the subtree has at least one proof level
		// (an insertion at layer 0 is the root, which is already known).
		int const dest_L = (layer_byte % N) + 1;

		// Subtree-size exponent in [0, dest_L]: leaf_count = 1 << k. At
		// k = 0 we insert a single hash; at k = dest_L the subtree spans
		// the whole layer (its root is the tree root, no uncle hashes
		// needed).
		int const subtree_log = subtree_log_byte % (dest_L + 1);
		int const leaf_count = 1 << subtree_log;

		// Pick a subtree-aligned slot in the destination layer.
		int const layer_size = 1 << dest_L;
		int const num_subtrees = layer_size / leaf_count;
		int const subtree_idx = int(slot_raw) % num_subtrees;
		int const dest_O = subtree_idx * leaf_count;

		int const dest_start_idx = lt::merkle_layer_start(dest_L) + dest_O;

		std::vector<lt::sha256_hash> hashes;
		hashes.reserve(std::size_t(leaf_count));
		for (int i = 0; i < leaf_count; ++i)
			hashes.push_back(reference[std::size_t(dest_start_idx + i)]);

		// Build the uncle-proof chain from the subtree's root up to the
		// tree root: at each level we include the sibling needed to recompute
		// the parent. The subtree root sits at (dest_L - subtree_log,
		// subtree_idx); its flat index is what we walk up.
		int const subtree_root_L = dest_L - subtree_log;
		int const subtree_root_flat = lt::merkle_layer_start(subtree_root_L) + subtree_idx;

		std::vector<lt::sha256_hash> uncles;
		for (int target = subtree_root_flat; target > 0; target = lt::merkle_get_parent(target))
			uncles.push_back(reference[std::size_t(lt::merkle_get_sibling(target))]);

		// Corruption mode in the low 2 bits of corrupt_byte:
		//   bit 0 set = flip a byte in `hashes`
		//   bit 1 set = flip a byte in `uncles`
		// Both clear gives a clean (valid) call. The remaining 6 bits pick
		// which entry to corrupt.
		std::uint8_t const corrupt_mode = corrupt_byte & 0x3;
		int const which = (corrupt_byte >> 2);

		if ((corrupt_mode & 1) && !hashes.empty()) hashes[which % int(hashes.size())][0] ^= 0xff;
		if ((corrupt_mode & 2) && !uncles.empty()) uncles[which % int(uncles.size())][0] ^= 0xff;

		// Snapshot the canonical representation. build_vector depends only
		// on what the tree knows, not on its internal storage mode, so a
		// matched pre/post pair is the precise statement of "fully restored".
		auto const before = t.build_vector();

		auto const result =
			t.add_hashes(dest_start_idx, lt::piece_index_t::diff_type{0}, hashes, uncles);

		if (!result)
		{
			// Failed proof must leave the tree bit-identical to its prior
			// canonical state.
			if (t.build_vector() != before) std::abort();
		}
	}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	if (size < 4) return 0;

	// Header: pick num_blocks in [1, 4096] and blocks_per_piece in
	// {1, 2, 4, 8, ..., 4096} (still a power of two, as the class API
	// asserts, but spanning a much wider range than the tiny piece sizes
	// the previous header gave us). num_blocks doesn't need to be a
	// multiple of blocks_per_piece -- the partial-last-piece case is
	// part of the coverage we want. The seed is only 8 bits because the
	// fuzzer doesn't care about the actual leaf hash values; 256 distinct
	// reference trees per (num_blocks, blocks_per_piece) is plenty.
	int const num_blocks = ((int(data[0]) << 8) | int(data[1])) % 4096 + 1;
	int const blocks_per_piece_log = int(data[2]) % 13; // 0..12 -> 1..4096
	int const blocks_per_piece = 1 << blocks_per_piece_log;
	std::uint32_t const seed = std::uint32_t(data[3]);

	auto const reference = build_reference(num_blocks, seed);

	lt::aux::merkle_tree t(num_blocks, blocks_per_piece, reference[0].data());

	int const num_leafs = lt::merkle_num_leafs(num_blocks);
	int const first_leaf = lt::merkle_first_leaf(num_leafs);
	lt::bitfield const empty_verified(num_blocks);

	// Validate-sampling rng -- seeded deterministically from the fuzz
	// header so a given input always picks the same sample positions
	// (libFuzzer-reproducible).
	std::mt19937_64 validate_rng(std::uint64_t(seed) ^ (std::uint64_t(num_blocks) << 32));

	validate(t, num_blocks, reference, validate_rng);

	// op stream; cap at 64 operations per fuzz case to keep individual
	// runs short.
	std::size_t pos = 4;
	int op_count = 0;
	while (pos < size && op_count < 64)
	{
		std::uint8_t const op = data[pos++];
		++op_count;

		switch (op % 7)
		{
			case 0: {
				// set_block with correct hash
				if (pos + 1 >= size) break;
				int const raw = int(data[pos]) | (int(data[pos + 1]) << 8);
				pos += 2;
				int const block_idx = raw % num_blocks;
				t.set_block(block_idx, reference[std::size_t(first_leaf + block_idx)]);
				break;
			}
			case 1: {
				// set_block with a derived-but-different hash (likely
				// triggers block_hash_failed or hash_failed paths)
				if (pos + 1 >= size) break;
				int const raw = int(data[pos]) | (int(data[pos + 1]) << 8);
				pos += 2;
				int const block_idx = raw % num_blocks;
				lt::sha256_hash bogus = reference[std::size_t(first_leaf + block_idx)];
				bogus[0] ^= 0xff;
				t.set_block(block_idx, bogus);
				break;
			}
			case 2: {
				// load_tree from the correct reference
				lt::bitfield verified(num_blocks);
				t.load_tree(reference, verified);
				break;
			}
			case 3: {
				// load_tree from a single-byte-flipped reference
				if (pos + 2 >= size) break;
				auto corrupted = reference;
				int const corrupt_idx =
					((int(data[pos]) << 8) | int(data[pos + 1])) % int(corrupted.size());
				pos += 2;
				corrupted[std::size_t(corrupt_idx)][0] ^= 0xff;
				lt::bitfield verified(num_blocks);
				t.load_tree(corrupted, verified);
				break;
			}
			case 4: {
				// load_sparse_tree from the reference, with a fuzz-controlled
				// mask byte selecting which nodes to include.
				if (pos + 1 >= size) break;
				int const total = lt::merkle_num_nodes(num_leafs);
				lt::bitfield mask(total, false);
				std::vector<lt::sha256_hash> sparse;
				std::uint8_t const mask_seed = data[pos++];
				for (int i = 0; i < total; ++i)
				{
					// pseudo-random based on the mask byte and the index
					if (((mask_seed * 31 + i) & 7) != 0) continue;
					mask.set_bit(i);
					sparse.push_back(reference[std::size_t(i)]);
				}
				t.load_sparse_tree(sparse, mask, empty_verified);
				break;
			}
			case 5: {
				// add_hashes: insert a subtree of hashes with an uncle-proof
				// chain. Consumes 5 sub-bytes selecting layer, subtree size,
				// slot, and a corruption mode that may flip a byte in the
				// hashes, the proof, or both. The helper additionally checks
				// that a failed proof leaves the tree fully restored.
				if (pos + 4 >= size) break;
				std::uint8_t const layer_byte = data[pos++];
				std::uint8_t const subtree_log_byte = data[pos++];
				std::uint16_t const slot_raw =
					std::uint16_t(int(data[pos]) | (int(data[pos + 1]) << 8));
				pos += 2;
				std::uint8_t const corrupt_byte = data[pos++];
				do_add_hashes(
					t, num_blocks, reference, layer_byte, subtree_log_byte, slot_raw, corrupt_byte);
				break;
			}
			case 6: {
				// load_piece_layer with the correct piece-layer hashes,
				// optionally with a single byte flipped (which must cause
				// the call to return false). Per its production callsite
				// in torrent.cpp, load_piece_layer is invoked on a freshly
				// constructed merkle_tree -- it doesn't currently reset
				// m_block_verified on mode transitions, so calling it on a
				// live tree that's been modified would leave the verified
				// bitfield in a state that violates the piece_layer/block_layer
				// invariant. We therefore drive it through a fresh, parallel
				// tree rather than the live one.
				if (pos + 2 >= size) break;
				int const num_pieces = (num_blocks + blocks_per_piece - 1) / blocks_per_piece;
				int const piece_layer_size = num_pieces * int(lt::sha256_hash::size());

				// Materialize the piece-layer bytes from the reference. If
				// blocks_per_piece == 1 the block and piece layers coincide,
				// so we read from the leaf layer instead.
				int const piece_layer_first = (blocks_per_piece == 1)
					? first_leaf
					: lt::merkle_first_leaf(lt::merkle_num_leafs(num_pieces));

				std::vector<char> piece_layer(std::size_t(piece_layer_size), '\0');
				for (int i = 0; i < num_pieces; ++i)
					std::memcpy(piece_layer.data() + i * int(lt::sha256_hash::size()),
						reference[std::size_t(piece_layer_first + i)].data(),
						lt::sha256_hash::size());

				std::uint8_t const corrupt = data[pos++];
				int const corrupt_pos_raw = int(data[pos++]);
				bool const want_fail = (corrupt & 1) != 0;
				if (want_fail)
				{
					int const cp = corrupt_pos_raw % piece_layer_size;
					piece_layer[std::size_t(cp)] ^= 0xff;
				}

				lt::aux::merkle_tree fresh(num_blocks, blocks_per_piece, reference[0].data());
				bool const ok = fresh.load_piece_layer(piece_layer);
				if (ok == want_fail) std::abort();
				validate(fresh, num_blocks, reference, validate_rng);
				break;
			}
		}

		// Round-trip after every op: it's an invariant the tree must
		// satisfy in every reachable state, not an action to take some
		// of the time.
		validate(t, num_blocks, reference, validate_rng);
		round_trip(t, num_blocks, blocks_per_piece, reference, validate_rng);
	}

	return 0;
}
