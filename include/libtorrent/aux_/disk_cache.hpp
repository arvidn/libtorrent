/*

Copyright (c) 2023, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DISK_CACHE
#define TORRENT_DISK_CACHE

#include <unordered_map>
#include <mutex>

#include "libtorrent/storage_defs.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/functional/hash.hpp>

#define BOOST_BIND_NO_PLACEHOLDERS

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"


namespace libtorrent {
namespace aux {

// uniquely identifies a torrent and piece
struct piece_location
{
	peice_location(storage_index_t const t, piece_index_t const p)
		: torrent(t), piece(p) {}
	storage_index_t torrent;
	piece_index_t piece;
	bool operator==(piece_location const& rhs) const
	{
		return std::tie(torrent, piece)
			== std::tie(rhs.torrent, rhs.piece);
	}
};

}
}

namespace std {

template <>
struct hash<libtorrent::aux::piecelocation>
{
	using argument_type = libtorrent::aux::piece_location;
	using result_type = std::size_t;
	std::size_t operator()(argument_type const& l) const
	{
		using namespace libtorrent;
		std::size_t ret = 0;
		boost::hash_combine(ret, std::hash<storage_index_t>{}(l.torrent));
		boost::hash_combine(ret, std::hash<piece_index_t>{}(l.piece));
		return ret;
	}
};

}

namespace libtorrent {
namespace aux {

namespace mi = boost::multi_index;

struct cached_block_entry
{
	char const* buffer;
	#error write job
};

struct piece_hash_state
{
	// the current piece hash context
	hasher ctx;
	// the number of blocks that have been hashed so far
	int cursor;
};

struct cached_piece_entry
{
	piece_location piece;

	// the number of threads currently using this piece and its data. As long as
	// this is > 0, the piece may not be removed
	int references = 0;

	// this is set to true when the piece has been populated with all blocks
	bool ready_to_flush = false;

	// when this is true, there is a thread currently hashing blocks and
	// updating the piece_hash_state in "ph".
	bool hashing = false;

	std::unique_ptr<cached_block_entry[]> blocks;

	std::variant<sha1_hash, piece_hash_state> ph;
};

struct disk_cache
{
	using piece_container = mi::multi_index_container<
		cached_piece_entry,
		mi::indexed_by<
		// look up pieces by (torrent, piece-index) key
		mi::ordered_unique<mi::member<cached_piece_entry, piece_location, &cached_piece_entry::piece>>,
		// ordered by least recently used
		mi::sequenced<>,
		mi::ordered_non_unique<mi::member<cached_piece_entry, bool, &cached_piece_entry::ready_to_flush>>,
		>
	>;

	template <typename Fun>
	bool get(piece_location const loc, Fun f) const
	{
		std::unique_lock<std::mutex> l(m_mutex);
		auto const it = m_store_buffer.find(loc);
		if (it != m_store_buffer.end())
		{
			// TODO: it would be nice if this could be called without holding
			// the mutex. It would require a reference counter on the store
			// buffer entries and that we potentially erases it after this call.
			// it would also require the store buffer being able to take over
			// ownership of the buffer when the owner erases it. Perhase erase()
			// could be made to take a buffer_holder, which is held onto if the
			// refcount > 0
			f(it->second);
			return true;
		}
		return false;
	}

	template <typename Fun>
	int get2(piece_location const loc1, piece_location const loc2, Fun f) const
	{
		std::unique_lock<std::mutex> l(m_mutex);
		auto const it1 = m_store_buffer.find(loc1);
		auto const it2 = m_store_buffer.find(loc2);
		char const* buf1 = (it1 == m_store_buffer.end()) ? nullptr : it1->second;
		char const* buf2 = (it2 == m_store_buffer.end()) ? nullptr : it2->second;

		if (buf1 == nullptr && buf2 == nullptr)
			return 0;

		return f(buf1, buf2);
	}

	void insert(piece_location const loc, int block_idx, char const* buf)
	{
		std::lock_guard<std::mutex> l(m_mutex);

		auto& view = m_pieces.template get<0>();
		auto i = iew.find(loc);
		if (i == view.end())
		{

			i = m_pieces.insert({loc, buf});
		}
	}

	std::size_t size() const
	{
		return m_blocks;
	}

private:

	mutable std::mutex m_mutex;
	piece_container m_pieces;
	int m_blocks = 0;
};

}
}

#endif

