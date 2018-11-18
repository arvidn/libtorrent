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

#include "libtorrent/kademlia/dht_storage.hpp"
#include "libtorrent/kademlia/dht_settings.hpp"

#include <tuple>
#include <algorithm>
#include <utility>
#include <map>
#include <set>
#include <string>

#include <libtorrent/socket_io.hpp>
#include <libtorrent/aux_/time.hpp>
#include <libtorrent/config.hpp>
#include <libtorrent/bloom_filter.hpp>
#include <libtorrent/random.hpp>
#include <libtorrent/aux_/vector.hpp>
#include <libtorrent/aux_/numeric_cast.hpp>
#include <libtorrent/broadcast_socket.hpp> // for ip_v4
#include <libtorrent/bdecode.hpp>

namespace libtorrent { namespace dht {
namespace {

	// this is the entry for every peer
	// the timestamp is there to make it possible
	// to remove stale peers
	struct peer_entry
	{
		time_point added;
		tcp::endpoint addr;
		bool seed = 0;
	};

	// internal
	bool operator<(peer_entry const& lhs, peer_entry const& rhs)
	{
		return lhs.addr.address() == rhs.addr.address()
			? lhs.addr.port() < rhs.addr.port()
			: lhs.addr.address() < rhs.addr.address();
	}

	// this is a group. It contains a set of group members
	struct torrent_entry
	{
		std::string name;
		std::vector<peer_entry> peers4;
		std::vector<peer_entry> peers6;
	};

	// TODO: 2 make this configurable in dht_settings
	constexpr time_duration announce_interval = minutes(30);

	struct dht_immutable_item
	{
		// the actual value
		std::unique_ptr<char[]> value;
		// this counts the number of IPs we have seen
		// announcing this item, this is used to determine
		// popularity if we reach the limit of items to store
		bloom_filter<128> ips;
		// the last time we heard about this item
		// the correct interpretation of this field
		// requires a time reference
		time_point last_seen;
		// number of IPs in the bloom filter
		int num_announcers = 0;
		// size of malloced space pointed to by value
		int size = 0;
	};

	struct dht_mutable_item : dht_immutable_item
	{
		signature sig{};
		sequence_number seq{};
		public_key key{};
		std::string salt;
	};

	void set_value(dht_immutable_item& item, span<char const> buf)
	{
		int const size = int(buf.size());
		if (item.size != size)
		{
			item.value.reset(new char[std::size_t(size)]);
			item.size = size;
		}
		std::copy(buf.begin(), buf.end(), item.value.get());
	}

	void touch_item(dht_immutable_item& f, address const& addr)
	{
		f.last_seen = aux::time_now();

		// maybe increase num_announcers if we haven't seen this IP before
		sha1_hash const iphash = hash_address(addr);
		if (!f.ips.find(iphash))
		{
			f.ips.set(iphash);
			++f.num_announcers;
		}
	}

	// return true of the first argument is a better candidate for removal, i.e.
	// less important to keep
	struct immutable_item_comparator
	{
		explicit immutable_item_comparator(std::vector<node_id> const& node_ids) : m_node_ids(node_ids) {}
		immutable_item_comparator(immutable_item_comparator const&) = default;

		template <typename Item>
		bool operator()(std::pair<node_id const, Item> const& lhs
			, std::pair<node_id const, Item> const& rhs) const
		{
			int const l_distance = min_distance_exp(lhs.first, m_node_ids);
			int const r_distance = min_distance_exp(rhs.first, m_node_ids);

			// this is a score taking the popularity (number of announcers) and the
			// fit, in terms of distance from ideal storing node, into account.
			// each additional 5 announcers is worth one extra bit in the distance.
			// that is, an item with 10 announcers is allowed to be twice as far
			// from another item with 5 announcers, from our node ID. Twice as far
			// because it gets one more bit.
			return lhs.second.num_announcers / 5 - l_distance < rhs.second.num_announcers / 5 - r_distance;
		}

	private:

		// explicitly disallow assignment, to silence msvc warning
		immutable_item_comparator& operator=(immutable_item_comparator const&) = delete;

		std::vector<node_id> const& m_node_ids;
	};

	// picks the least important one (i.e. the one
	// the fewest peers are announcing, and farthest
	// from our node IDs)
	template<class Item>
	typename std::map<node_id, Item>::const_iterator pick_least_important_item(
		std::vector<node_id> const& node_ids, std::map<node_id, Item> const& table)
	{
		return std::min_element(table.begin(), table.end()
			, immutable_item_comparator(node_ids));
	}

	constexpr int sample_infohashes_interval_max = 21600;
	constexpr int infohashes_sample_count_max = 20;

	struct infohashes_sample
	{
		aux::vector<sha1_hash> samples;
		time_point created = min_time();

		int count() const { return int(samples.size()); }
	};

	class dht_default_storage final : public dht_storage_interface
	{
	public:

		explicit dht_default_storage(dht_settings const& settings)
			: m_settings(settings)
		{
			m_counters.reset();
		}

		~dht_default_storage() override = default;

		dht_default_storage(dht_default_storage const&) = delete;
		dht_default_storage& operator=(dht_default_storage const&) = delete;

#if TORRENT_ABI_VERSION == 1
		size_t num_torrents() const override { return m_map.size(); }
		size_t num_peers() const override
		{
			size_t ret = 0;
			for (auto const& t : m_map)
				ret += t.second.peers4.size() + t.second.peers6.size();
			return ret;
		}
#endif
		void update_node_ids(std::vector<node_id> const& ids) override
		{
			m_node_ids = ids;
		}

		bool get_peers(sha1_hash const& info_hash
			, bool const noseed, bool const scrape, address const& requester
			, entry& peers) const override
		{
			auto const i = m_map.find(info_hash);
			if (i == m_map.end()) return int(m_map.size()) >= m_settings.max_torrents;

			torrent_entry const& v = i->second;
			auto const& peersv = requester.is_v4() ? v.peers4 : v.peers6;

			if (!v.name.empty()) peers["n"] = v.name;

			if (scrape)
			{
				bloom_filter<256> downloaders;
				bloom_filter<256> seeds;

				for (auto const& p : peersv)
				{
					sha1_hash const iphash = hash_address(p.addr.address());
					if (p.seed) seeds.set(iphash);
					else downloaders.set(iphash);
				}

				peers["BFpe"] = downloaders.to_string();
				peers["BFsd"] = seeds.to_string();
			}
			else
			{
				tcp const protocol = requester.is_v4() ? tcp::v4() : tcp::v6();
				int to_pick = m_settings.max_peers_reply;
				TORRENT_ASSERT(to_pick >= 0);
				// if these are IPv6 peers their addresses are 4x the size of IPv4
				// so reduce the max peers 4 fold to compensate
				// max_peers_reply should probably be specified in bytes
				if (!peersv.empty() && protocol == tcp::v6())
					to_pick /= 4;
				entry::list_type& pe = peers["values"].list();

				int candidates = int(std::count_if(peersv.begin(), peersv.end()
					, [=](peer_entry const& e) { return !(noseed && e.seed); }));

				to_pick = std::min(to_pick, candidates);

				for (auto iter = peersv.begin(); to_pick > 0; ++iter)
				{
					// if the node asking for peers is a seed, skip seeds from the
					// peer list
					if (noseed && iter->seed) continue;

					TORRENT_ASSERT(candidates >= to_pick);

					// pick this peer with probability
					// <peers left to pick> / <peers left in the set>
					if (random(std::uint32_t(candidates--)) > std::uint32_t(to_pick))
						continue;

					pe.emplace_back();
					std::string& str = pe.back().string();

					str.resize(18);
					std::string::iterator out = str.begin();
					detail::write_endpoint(iter->addr, out);
					str.resize(std::size_t(out - str.begin()));

					--to_pick;
				}
			}

			if (int(peersv.size()) < m_settings.max_peers)
				return false;

			// we're at the max peers stored for this torrent
			// only send a write token if the requester is already in the set
			// only check for a match on IP because the peer may be announcing
			// a different port than the one it is using to send DHT messages
			peer_entry requester_entry;
			requester_entry.addr.address(requester);
			auto requester_iter = std::lower_bound(peersv.begin(), peersv.end(), requester_entry);
			return requester_iter == peersv.end()
				|| requester_iter->addr.address() != requester;
		}

		void announce_peer(sha1_hash const& info_hash
			, tcp::endpoint const& endp
			, string_view name, bool const seed) override
		{
			auto const ti = m_map.find(info_hash);
			torrent_entry* v;
			if (ti == m_map.end())
			{
				if (int(m_map.size()) >= m_settings.max_torrents)
				{
					// we're at capacity, drop the announce
					return;
				}

				m_counters.torrents += 1;
				v = &m_map[info_hash];
			}
			else
			{
				v = &ti->second;
			}

			// the peer announces a torrent name, and we don't have a name
			// for this torrent. Store it.
			if (!name.empty() && v->name.empty())
			{
				v->name = name.substr(0, 100).to_string();
			}

			auto& peersv = is_v4(endp) ? v->peers4 : v->peers6;

			peer_entry peer;
			peer.addr = endp;
			peer.added = aux::time_now();
			peer.seed = seed;
			auto i = std::lower_bound(peersv.begin(), peersv.end(), peer);
			if (i != peersv.end() && i->addr == endp)
			{
				*i = peer;
			}
			else if (int(peersv.size()) >= m_settings.max_peers)
			{
				// we're at capacity, drop the announce
				return;
			}
			else
			{
				peersv.insert(i, peer);
				m_counters.peers += 1;
			}
		}

		bool get_immutable_item(sha1_hash const& target
			, entry& item) const override
		{
			auto const i = m_immutable_table.find(target);
			if (i == m_immutable_table.end()) return false;

			error_code ec;
			item["v"] = bdecode({i->second.value.get(), i->second.size}, ec);
			return true;
		}

		void put_immutable_item(sha1_hash const& target
			, span<char const> buf
			, address const& addr) override
		{
			TORRENT_ASSERT(!m_node_ids.empty());
			auto i = m_immutable_table.find(target);
			if (i == m_immutable_table.end())
			{
				// make sure we don't add too many items
				if (int(m_immutable_table.size()) >= m_settings.max_dht_items)
				{
					auto const j = pick_least_important_item(m_node_ids
						, m_immutable_table);

					TORRENT_ASSERT(j != m_immutable_table.end());
					m_immutable_table.erase(j);
					m_counters.immutable_data -= 1;
				}
				dht_immutable_item to_add;
				set_value(to_add, buf);

				std::tie(i, std::ignore) = m_immutable_table.insert(
					std::make_pair(target, std::move(to_add)));
				m_counters.immutable_data += 1;
			}

//			std::fprintf(stderr, "added immutable item (%d)\n", int(m_immutable_table.size()));

			touch_item(i->second, addr);
		}

		bool get_mutable_item_seq(sha1_hash const& target
			, sequence_number& seq) const override
		{
			auto const i = m_mutable_table.find(target);
			if (i == m_mutable_table.end()) return false;

			seq = i->second.seq;
			return true;
		}

		bool get_mutable_item(sha1_hash const& target
			, sequence_number const seq, bool const force_fill
			, entry& item) const override
		{
			auto const i = m_mutable_table.find(target);
			if (i == m_mutable_table.end()) return false;

			dht_mutable_item const& f = i->second;
			item["seq"] = f.seq.value;
			if (force_fill || (sequence_number(0) <= seq && seq < f.seq))
			{
				error_code ec;
				item["v"] = bdecode({f.value.get(), f.size}, ec);
				item["sig"] = f.sig.bytes;
				item["k"] = f.key.bytes;
			}
			return true;
		}

		void put_mutable_item(sha1_hash const& target
			, span<char const> buf
			, signature const& sig
			, sequence_number const seq
			, public_key const& pk
			, span<char const> salt
			, address const& addr) override
		{
			TORRENT_ASSERT(!m_node_ids.empty());
			auto i = m_mutable_table.find(target);
			if (i == m_mutable_table.end())
			{
				// this is the case where we don't have an item in this slot
				// make sure we don't add too many items
				if (int(m_mutable_table.size()) >= m_settings.max_dht_items)
				{
					auto const j = pick_least_important_item(m_node_ids
						, m_mutable_table);

					TORRENT_ASSERT(j != m_mutable_table.end());
					m_mutable_table.erase(j);
					m_counters.mutable_data -= 1;
				}
				dht_mutable_item to_add;
				set_value(to_add, buf);
				to_add.seq = seq;
				to_add.salt = {salt.begin(), salt.end()};
				to_add.sig = sig;
				to_add.key = pk;

				std::tie(i, std::ignore) = m_mutable_table.insert(
					std::make_pair(target, std::move(to_add)));
				m_counters.mutable_data += 1;
			}
			else
			{
				// this is the case where we already have an item in this slot
				dht_mutable_item& item = i->second;

				if (item.seq < seq)
				{
					set_value(item, buf);
					item.seq = seq;
					item.sig = sig;
				}
			}

			touch_item(i->second, addr);
		}

		int get_infohashes_sample(entry& item) override
		{
			item["interval"] = aux::clamp(m_settings.sample_infohashes_interval
				, 0, sample_infohashes_interval_max);
			item["num"] = int(m_map.size());

			refresh_infohashes_sample();

			aux::vector<sha1_hash> const& samples = m_infohashes_sample.samples;
			item["samples"] = span<char const>(
				reinterpret_cast<char const*>(samples.data()), static_cast<std::ptrdiff_t>(samples.size()) * 20);

			return m_infohashes_sample.count();
		}

		void tick() override
		{
			// look through all peers and see if any have timed out
			for (auto i = m_map.begin(), end(m_map.end()); i != end;)
			{
				torrent_entry& t = i->second;
				purge_peers(t.peers4);
				purge_peers(t.peers6);

				if (!t.peers4.empty() || !t.peers6.empty())
				{
					++i;
					continue;
				}

				// if there are no more peers, remove the entry altogether
				i = m_map.erase(i);
				m_counters.torrents -= 1;// peers is decreased by purge_peers
			}

			if (0 == m_settings.item_lifetime) return;

			time_point const now = aux::time_now();
			time_duration lifetime = seconds(m_settings.item_lifetime);
			// item lifetime must >= 120 minutes.
			if (lifetime < minutes(120)) lifetime = minutes(120);

			for (auto i = m_immutable_table.begin(); i != m_immutable_table.end();)
			{
				if (i->second.last_seen + lifetime > now)
				{
					++i;
					continue;
				}
				i = m_immutable_table.erase(i);
				m_counters.immutable_data -= 1;
			}

			for (auto i = m_mutable_table.begin(); i != m_mutable_table.end();)
			{
				if (i->second.last_seen + lifetime > now)
				{
					++i;
					continue;
				}
				i = m_mutable_table.erase(i);
				m_counters.mutable_data -= 1;
			}
		}

		dht_storage_counters counters() const override
		{
			return m_counters;
		}

	private:
		dht_settings const& m_settings;
		dht_storage_counters m_counters;

		std::vector<node_id> m_node_ids;
		std::map<node_id, torrent_entry> m_map;
		std::map<node_id, dht_immutable_item> m_immutable_table;
		std::map<node_id, dht_mutable_item> m_mutable_table;

		infohashes_sample m_infohashes_sample;

		void purge_peers(std::vector<peer_entry>& peers)
		{
			auto now = aux::time_now();
			auto new_end = std::remove_if(peers.begin(), peers.end()
				, [=](peer_entry const& e)
			{
				return e.added + announce_interval * 3 / 2 < now;
			});

			m_counters.peers -= std::int32_t(std::distance(new_end, peers.end()));
			peers.erase(new_end, peers.end());
			// if we're using less than 1/4 of the capacity free up the excess
			if (!peers.empty() && peers.capacity() / peers.size() >= 4U)
				peers.shrink_to_fit();
		}

		void refresh_infohashes_sample()
		{
			time_point const now = aux::time_now();
			int const interval = aux::clamp(m_settings.sample_infohashes_interval
				, 0, sample_infohashes_interval_max);

			int const max_count = aux::clamp(m_settings.max_infohashes_sample_count
				, 0, infohashes_sample_count_max);
			int const count = std::min(max_count, int(m_map.size()));

			if (interval > 0
				&& m_infohashes_sample.created + seconds(interval) > now
				&& m_infohashes_sample.count() >= max_count)
				return;

			aux::vector<sha1_hash>& samples = m_infohashes_sample.samples;
			samples.clear();
			samples.reserve(count);

			int to_pick = count;
			int candidates = int(m_map.size());

			for (auto const& t : m_map)
			{
				if (to_pick == 0)
					break;

				TORRENT_ASSERT(candidates >= to_pick);

				// pick this key with probability
				// <keys left to pick> / <keys left in the set>
				if (random(std::uint32_t(candidates--)) > std::uint32_t(to_pick))
					continue;

				samples.push_back(t.first);
				--to_pick;
			}

			TORRENT_ASSERT(int(samples.size()) == count);
			m_infohashes_sample.created = now;
		}
	};
}

void dht_storage_counters::reset()
{
	torrents = 0;
	peers = 0;
	immutable_data = 0;
	mutable_data = 0;
}

std::unique_ptr<dht_storage_interface> dht_default_storage_constructor(
	dht_settings const& settings)
{
	return std::unique_ptr<dht_default_storage>(new dht_default_storage(settings));
}

} } // namespace libtorrent::dht
