/*

Copyright (c) 2015-2016, 2018, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "simulator/simulator.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/time.hpp"
#include <functional>

#ifndef TORRENT_SETUP_SWARM_HPP_INCLUDED
#define TORRENT_SETUP_SWARM_HPP_INCLUDED

using lt::operator""_bit;
using swarm_test_t = lt::flags::bitfield_flag<std::uint64_t, struct swarm_test_type_tag>;

struct swarm_test
{
	constexpr static swarm_test_t download = 0_bit;
	constexpr static swarm_test_t upload = 1_bit;
	constexpr static swarm_test_t no_auto_stop = 2_bit;
	constexpr static swarm_test_t large_torrent = 3_bit;
	constexpr static swarm_test_t real_disk = 4_bit;
};

void setup_swarm(int num_nodes
	, swarm_test_t type
	, std::function<void(lt::settings_pack&)> new_session
	, std::function<void(lt::add_torrent_params&)> add_torrent
	, std::function<void(lt::alert const*, lt::session&)> on_alert
	, std::function<bool(int, lt::session&)> terminate);

void setup_swarm(int num_nodes
	, swarm_test_t type
	, sim::simulation& sim
	, std::function<void(lt::settings_pack&)> new_session
	, std::function<void(lt::add_torrent_params&)> add_torrent
	, std::function<void(lt::alert const*, lt::session&)> on_alert
	, std::function<bool(int, lt::session&)> terminate);

void setup_swarm(int num_nodes
	, swarm_test_t type
	, sim::simulation& sim
	, lt::settings_pack const& default_settings
	, lt::add_torrent_params const& default_add_torrent
	, std::function<void(lt::settings_pack&)> new_session
	, std::function<void(lt::add_torrent_params&)> add_torrent
	, std::function<void(lt::alert const*, lt::session&)> on_alert
	, std::function<bool(int, lt::session&)> terminate);

void setup_swarm(int num_nodes
	, swarm_test_t type
	, sim::simulation& sim
	, lt::settings_pack const& default_settings
	, lt::add_torrent_params const& default_add_torrent
	, std::function<void(lt::session&)> init_session
	, std::function<void(lt::settings_pack&)> new_session
	, std::function<void(lt::add_torrent_params&)> add_torrent
	, std::function<void(lt::alert const*, lt::session&)> on_alert
	, std::function<bool(int, lt::session&)> terminate);

struct dsl_config : sim::default_config
{
	dsl_config(int kb_per_second = 0, int send_queue_size = 0
		, lt::milliseconds latency = lt::milliseconds(0));
	virtual sim::route incoming_route(lt::address ip) override;
	virtual sim::route outgoing_route(lt::address ip) override;
private:
	int m_rate; // kilobytes per second
	int m_queue_size; // bytes
	lt::milliseconds m_latency;
};

#endif

