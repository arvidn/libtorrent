/*

Copyright (c) 2016, 2020, 2022, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_STORE_BUFFER
#define TORRENT_STORE_BUFFER

#include <unordered_map>
#include <mutex>

#include "libtorrent/storage_defs.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/functional/hash.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"


namespace libtorrent {
namespace aux {

// uniquely identifies a torrent and offset. It is used as the key in the
// dictionary mapping locations to write jobs
struct torrent_location
{
	torrent_location(storage_index_t const t, piece_index_t const p, int o)
		: torrent(t), piece(p), offset(o) {}
	storage_index_t torrent;
	piece_index_t piece;
	int offset;
	bool operator==(torrent_location const& rhs) const
	{
		return std::tie(torrent, piece, offset)
			== std::tie(rhs.torrent, rhs.piece, rhs.offset);
	}
};

}
}

namespace std {

template <>
struct hash<libtorrent::aux::torrent_location>
{
	using argument_type = libtorrent::aux::torrent_location;
	using result_type = std::size_t;
	std::size_t operator()(argument_type const& l) const
	{
		using namespace libtorrent;
		std::size_t ret = 0;
		boost::hash_combine(ret, std::hash<storage_index_t>{}(l.torrent));
		boost::hash_combine(ret, std::hash<piece_index_t>{}(l.piece));
		boost::hash_combine(ret, std::hash<int>{}(l.offset));
		return ret;
	}
};

}

namespace libtorrent {
namespace aux {

struct store_buffer
{
	template <typename Fun>
	bool get(torrent_location const loc, Fun f) const
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
	int get2(torrent_location const loc1, torrent_location const loc2, Fun f) const
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

	void insert(torrent_location const loc, char const* buf)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		m_store_buffer.insert({loc, buf});
	}

	void erase(torrent_location const loc)
	{
		std::lock_guard<std::mutex> l(m_mutex);
		auto it = m_store_buffer.find(loc);
		TORRENT_ASSERT(it != m_store_buffer.end());
		m_store_buffer.erase(it);
	}

	std::size_t size() const
	{
		return m_store_buffer.size();
	}

private:

	mutable std::mutex m_mutex;
	std::unordered_map<torrent_location, char const*> m_store_buffer;
};

}
}

#endif

