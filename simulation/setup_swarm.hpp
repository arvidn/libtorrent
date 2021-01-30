/*

Copyright (c) 2015, Arvid Norberg
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

#include "simulator/simulator.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/fwd.hpp"
#include "libtorrent/flags.hpp"
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
	constexpr static swarm_test_t no_storage = 4_bit;
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
	dsl_config(int kb_per_second = 0, int send_queue_size = 0);
	virtual sim::route incoming_route(lt::address ip) override;
	virtual sim::route outgoing_route(lt::address ip) override;
private:
	int m_rate; // kilobytes per second
	int m_queue_size; // bytes
};

#endif

