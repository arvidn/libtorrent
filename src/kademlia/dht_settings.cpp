/*

Copyright (c) 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/kademlia/dht_settings.hpp"
#include "libtorrent/bdecode.hpp"

namespace lt {
namespace dht {

#if TORRENT_ABI_VERSION <= 2
	dht_settings read_dht_settings(bdecode_node const& e)
	{
		dht_settings sett{};

		if (e.type() != bdecode_node::dict_t) return sett;

		bdecode_node val;
		val = e.dict_find_int("max_peers_reply");
		if (val) sett.max_peers_reply = int(val.int_value());
		val = e.dict_find_int("search_branching");
		if (val) sett.search_branching = int(val.int_value());
		val = e.dict_find_int("max_fail_count");
		if (val) sett.max_fail_count = int(val.int_value());
		val = e.dict_find_int("max_torrents");
		if (val) sett.max_torrents = int(val.int_value());
		val = e.dict_find_int("max_dht_items");
		if (val) sett.max_dht_items = int(val.int_value());
		val = e.dict_find_int("max_peers");
		if (val) sett.max_peers = int(val.int_value());
		val = e.dict_find_int("max_torrent_search_reply");
		if (val) sett.max_torrent_search_reply = int(val.int_value());
		val = e.dict_find_int("restrict_routing_ips");
		if (val) sett.restrict_routing_ips = (val.int_value() != 0);
		val = e.dict_find_int("restrict_search_ips");
		if (val) sett.restrict_search_ips = (val.int_value() != 0);
		val = e.dict_find_int("extended_routing_table");
		if (val) sett.extended_routing_table = (val.int_value() != 0);
		val = e.dict_find_int("aggressive_lookups");
		if (val) sett.aggressive_lookups = (val.int_value() != 0);
		val = e.dict_find_int("privacy_lookups");
		if (val) sett.privacy_lookups = (val.int_value() != 0);
		val = e.dict_find_int("enforce_node_id");
		if (val) sett.enforce_node_id = (val.int_value() != 0);
		val = e.dict_find_int("ignore_dark_internet");
		if (val) sett.ignore_dark_internet = (val.int_value() != 0);
		val = e.dict_find_int("block_timeout");
		if (val) sett.block_timeout = int(val.int_value());
		val = e.dict_find_int("block_ratelimit");
		if (val) sett.block_ratelimit = int(val.int_value());
		val = e.dict_find_int("read_only");
		if (val) sett.read_only = (val.int_value() != 0);
		val = e.dict_find_int("item_lifetime");
		if (val) sett.item_lifetime = int(val.int_value());

		return sett;
	}

	entry save_dht_settings(dht_settings const& settings)
	{
		entry e;
		entry::dictionary_type& dht_sett = e.dict();

		dht_sett["max_peers_reply"] = settings.max_peers_reply;
		dht_sett["search_branching"] = settings.search_branching;
		dht_sett["max_fail_count"] = settings.max_fail_count;
		dht_sett["max_torrents"] = settings.max_torrents;
		dht_sett["max_dht_items"] = settings.max_dht_items;
		dht_sett["max_peers"] = settings.max_peers;
		dht_sett["max_torrent_search_reply"] = settings.max_torrent_search_reply;
		dht_sett["restrict_routing_ips"] = settings.restrict_routing_ips;
		dht_sett["restrict_search_ips"] = settings.restrict_search_ips;
		dht_sett["extended_routing_table"] = settings.extended_routing_table;
		dht_sett["aggressive_lookups"] = settings.aggressive_lookups;
		dht_sett["privacy_lookups"] = settings.privacy_lookups;
		dht_sett["enforce_node_id"] = settings.enforce_node_id;
		dht_sett["ignore_dark_internet"] = settings.ignore_dark_internet;
		dht_sett["block_timeout"] = settings.block_timeout;
		dht_sett["block_ratelimit"] = settings.block_ratelimit;
		dht_sett["read_only"] = settings.read_only;
		dht_sett["item_lifetime"] = settings.item_lifetime;

		return e;
	}
#endif
}
}
