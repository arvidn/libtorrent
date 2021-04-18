/*

Copyright (c) 2018, Steven Siloti
Copyright (c) 2019-2020, Arvid Norberg
Copyright (c) 2019, Pavel Pimenov
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

#ifndef TORRENT_TORRENT_LIST_HPP_INCLUDED
#define TORRENT_TORRENT_LIST_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/info_hash.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/aux_/array.hpp"
#include "libtorrent/aux_/invariant_check.hpp"
#include "libtorrent/aux_/scope_end.hpp"

#include <memory> // for shared_ptr
#include <vector>
#include <unordered_map>

#if TORRENT_USE_INVARIANT_CHECKS
#include <set>
#endif

namespace libtorrent {
namespace aux {

template <typename T>
struct torrent_list
{
	// These are non-owning pointers. Lifetime is managed by the `torrent_array`
	using torrent_map = std::unordered_map<sha1_hash, T*>;
	using torrent_array = std::vector<std::shared_ptr<T>>;

	using iterator = typename torrent_array::iterator;
	using const_iterator = typename torrent_array::const_iterator;

	using value_type = typename torrent_array::value_type;

	bool empty() const { return m_array.empty(); }

	iterator begin() { return m_array.begin(); }
	iterator end() { return m_array.end(); }

	const_iterator begin() const { return m_array.begin(); }
	const_iterator end() const { return m_array.end(); }

	std::size_t size() const { return m_array.size(); }

	T* operator[](std::size_t const idx)
	{
		TORRENT_ASSERT(idx < m_array.size());
		return m_array[idx].get();
	}

	T const* operator[](std::size_t const idx) const
	{
		TORRENT_ASSERT(idx < m_array.size());
		return m_array[idx].get();
	}

	bool insert(info_hash_t const& ih, std::shared_ptr<T> t)
	{
		TORRENT_ASSERT(t);

		bool duplicate = false;
		ih.for_each([&](sha1_hash const& hash, protocol_version)
		{
			if (m_index.find(hash) != m_index.end()) duplicate = true;
		});

		// if we already have a torrent with this hash, don't do anything
		if (duplicate) return false;

		aux::array<bool, 4> rollback({ false, false, false, false});
		auto abort_add = aux::scope_end([&]
		{
			ih.for_each([&](sha1_hash const& hash, protocol_version const v)
			{
				if (rollback[int(v)])
					m_index.erase(hash);

#if !defined TORRENT_DISABLE_ENCRYPTION
				if (rollback[2 + int(v)])
				{
					static char const req2[4] = { 'r', 'e', 'q', '2' };
					hasher h(req2);
					h.update(hash);
						m_obfuscated_index.erase(h.final());
				}
#endif
			});
		});

		ih.for_each([&](sha1_hash const& hash, protocol_version const v)
		{
			if (m_index.insert({hash, t.get()}).second)
				rollback[int(v)] = true;

#if !defined TORRENT_DISABLE_ENCRYPTION
			static char const req2[4] = { 'r', 'e', 'q', '2' };
			hasher h(req2);
			h.update(hash);
			// this is SHA1("req2" + info-hash), used for
			// encrypted hand shakes
			if (m_obfuscated_index.insert({h.final(), t.get()}).second)
				rollback[2 + int(v)] = true;
#endif
		});

		m_array.emplace_back(std::move(t));

		abort_add.disarm();

		return true;
	}

#if !defined TORRENT_DISABLE_ENCRYPTION
	T* find_obfuscated(sha1_hash const& ih)
	{
		auto const i = m_obfuscated_index.find(ih);
		if (i == m_obfuscated_index.end()) return nullptr;
		return i->second;
	}
#endif

	T* find(sha1_hash const& ih) const
	{
		auto const i = m_index.find(ih);
		if (i == m_index.end()) return nullptr;
		return i->second;
	}

	bool erase(info_hash_t const& ih)
	{
		INVARIANT_CHECK;

		T* found = nullptr;
		ih.for_each([&](sha1_hash const& hash, protocol_version)
		{
			auto const i = m_index.find(hash);
			if (i != m_index.end())
			{
				TORRENT_ASSERT(found == nullptr || found == i->second);
				found = i->second;
				m_index.erase(i);
			}

#if !defined TORRENT_DISABLE_ENCRYPTION
			static char const req2[4] = { 'r', 'e', 'q', '2' };
			hasher h(req2);
			h.update(hash);
			m_obfuscated_index.erase(h.final());
#endif
		});
		if (!found) return false;

		auto const array_iter = std::find_if(m_array.begin(), m_array.end()
			, [&](std::shared_ptr<T> const& p) { return p.get() == found; });
		TORRENT_ASSERT(array_iter != m_array.end());

		TORRENT_ASSERT(m_index.find(ih.v1) == m_index.end());

		if (array_iter != m_array.end() - 1)
			std::swap(*array_iter, m_array.back());

		// This is where we, potentially, remove the last reference
		m_array.pop_back();

		return true;
	}

	void clear()
	{
		INVARIANT_CHECK;

		m_array.clear();
		m_index.clear();
#if !defined TORRENT_DISABLE_ENCRYPTION
		m_obfuscated_index.clear();
#endif
	}

#if TORRENT_USE_INVARIANT_CHECKS
	void check_invariant() const
	{
		std::set<T*> all_torrents;
		std::set<T*> all_indexed_torrents;
#if !defined TORRENT_DISABLE_ENCRYPTION
		std::set<T*> all_obf_indexed_torrents;
#endif

		for (auto const& t : m_array)
			all_torrents.insert(t.get());

		for (auto const& t : m_index)
		{
			all_indexed_torrents.insert(t.second);
		}
#if !defined TORRENT_DISABLE_ENCRYPTION
		for (auto const& t : m_obfuscated_index)
		{
			all_obf_indexed_torrents.insert(t.second);
		}
#endif

		TORRENT_ASSERT(all_torrents == all_indexed_torrents);
		TORRENT_ASSERT(all_torrents == all_obf_indexed_torrents);
	}
#endif

private:

	torrent_array m_array;

	torrent_map m_index;

#if !defined TORRENT_DISABLE_ENCRYPTION
	// this maps obfuscated hashes to torrents. It's only
	// used when encryption is enabled
	torrent_map m_obfuscated_index;
#endif
};

}
}

#endif

