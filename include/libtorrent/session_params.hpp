/*

Copyright (c) 2015, Steven Siloti
Copyright (c) 2016-2018, Alden Torres
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

#ifndef TORRENT_SESSION_PARAMS_HPP_INCLUDED
#define TORRENT_SESSION_PARAMS_HPP_INCLUDED

#include <functional>
#include <memory>
#include <vector>

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/export.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/kademlia/dht_state.hpp"
#include "libtorrent/session_types.hpp"
#include "libtorrent/kademlia/dht_storage.hpp"
#include "libtorrent/ip_filter.hpp"

#if TORRENT_ABI_VERSION <= 2
#include "libtorrent/kademlia/dht_settings.hpp"
#endif

namespace libtorrent {

TORRENT_VERSION_NAMESPACE_3
struct plugin;
TORRENT_VERSION_NAMESPACE_3_END

struct disk_interface;
struct counters;

using disk_io_constructor_type = std::function<std::unique_ptr<disk_interface>(
	io_context&, settings_interface const&, counters&)>;

TORRENT_VERSION_NAMESPACE_3

// The session_params is a parameters pack for configuring the session
// before it's started.
struct TORRENT_EXPORT session_params
{
	// This constructor can be used to start with the default plugins
	// (ut_metadata, ut_pex and smart_ban). Pass a settings_pack to set the
	// initial settings when the session starts.
	session_params(settings_pack&& sp); // NOLINT
	session_params(settings_pack const& sp); // NOLINT
	session_params();

	// hidden
	~session_params();

	// This constructor helps to configure the set of initial plugins
	// to be added to the session before it's started.
	session_params(settings_pack&& sp
		, std::vector<std::shared_ptr<plugin>> exts);
	session_params(settings_pack const& sp
		, std::vector<std::shared_ptr<plugin>> exts);

	// hidden
	session_params(session_params const&);
	session_params(session_params&&);
	session_params& operator=(session_params const&) &;
	session_params& operator=(session_params&&) &;

	// The settings to configure the session with
	settings_pack settings;

	// the plugins to add to the session as it is constructed
	std::vector<std::shared_ptr<plugin>> extensions;

#if TORRENT_ABI_VERSION <= 2

#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

	// this is deprecated. Use the dht_* settings instead.
	dht::dht_settings dht_settings;

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#endif

	// DHT node ID and node addresses to bootstrap the DHT with.
	dht::dht_state dht_state;

	// function object to construct the storage object for DHT items.
	dht::dht_storage_constructor_type dht_storage_constructor;

	// function object to create the disk I/O subsystem. Defaults to
	// default_disk_io_constructor.
	disk_io_constructor_type disk_io_constructor;

	// this container can be used by extensions/plugins to store settings. It's
	// primarily here to make it convenient to save and restore state across
	// sessions, using read_session_params() and write_session_params().
	std::map<std::string, std::string> ext_state;

	// the IP filter to use for the session. This restricts which peers are allowed
	// to connect. As if passed to set_ip_filter().
	libtorrent::ip_filter ip_filter;
};

TORRENT_VERSION_NAMESPACE_3_END

// These functions serialize and de-serialize a ``session_params`` object to and
// from bencoded form. The session_params object is used to initialize a new
// session using the state from a previous one (or by programmatically configure
// the session up-front).
// The flags parameter can be used to only save and load certain aspects of the
// session's state.
// The ``_buf`` suffix indicates the function operates on buffer rather than the
// bencoded structure.
// The torrents in a session are not part of the session_params state, they have
// to be restored separately.
TORRENT_EXPORT session_params read_session_params(bdecode_node const& e
	, save_state_flags_t flags = save_state_flags_t::all());
TORRENT_EXPORT session_params read_session_params(span<char const> buf
	, save_state_flags_t flags = save_state_flags_t::all());
TORRENT_EXPORT entry write_session_params(session_params const& sp
	, save_state_flags_t flags = save_state_flags_t::all());
TORRENT_EXPORT std::vector<char> write_session_params_buf(session_params const& sp
	, save_state_flags_t flags = save_state_flags_t::all());

}

#endif
