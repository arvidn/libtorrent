/*

Copyright (c) 2014-2020, Arvid Norberg
Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2017, Steven Siloti
Copyright (c) 2017, Andrei Kurushin
Copyright (c) 2018, TheOriginalWinCat
Copyright (c) 2019, Amir Abrams
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

#ifndef TORRENT_SETTINGS_PACK_HPP_INCLUDED
#define TORRENT_SETTINGS_PACK_HPP_INCLUDED

#include "libtorrent/entry.hpp"
#include "libtorrent/string_view.hpp"
#include "libtorrent/flags.hpp"

#include <vector>
#include <memory>

// OVERVIEW
//
// You have some control over session configuration through the session::apply_settings()
// member function. To change one or more configuration options, create a settings_pack
// object and fill it with the settings to be set and pass it in to session::apply_settings().
//
// The settings_pack object is a collection of settings updates that are applied
// to the session when passed to session::apply_settings(). It's empty when
// constructed.
//
// You have control over proxy and authorization settings and also the user-agent
// that will be sent to the tracker. The user-agent will also be used to identify the
// client with other peers.
//
// Each configuration option is named with an enum value inside the
// settings_pack class. These are the available settings:
namespace libtorrent {

namespace aux {
	struct session_impl;
	struct session_settings;
	struct session_settings_single_thread;
}

	struct settings_pack;
	struct bdecode_node;

	TORRENT_EXTRA_EXPORT settings_pack load_pack_from_dict(bdecode_node const& settings);

	TORRENT_EXTRA_EXPORT void save_settings_to_dict(settings_pack const& sett, entry::dictionary_type& out);
	TORRENT_EXTRA_EXPORT settings_pack non_default_settings(aux::session_settings const& sett);
	TORRENT_EXTRA_EXPORT void apply_pack(settings_pack const* pack, aux::session_settings& sett
		, aux::session_impl* ses = nullptr);
	TORRENT_EXTRA_EXPORT void apply_pack_impl(settings_pack const* pack
		, aux::session_settings_single_thread& sett
		, std::vector<void(aux::session_impl::*)()>* callbacks = nullptr);
	TORRENT_EXTRA_EXPORT void run_all_updates(aux::session_impl& ses);

	// converts a setting integer (from the enums string_types, int_types or
	// bool_types) to a string, and vice versa.
	TORRENT_EXPORT int setting_by_name(string_view name);
	TORRENT_EXPORT char const* name_for_setting(int s);

	// returns a settings_pack with every setting set to its default value
	TORRENT_EXPORT settings_pack default_settings();

	// the common interface to settings_pack and the internal representation of
	// settings.
	struct TORRENT_EXPORT settings_interface
	{
		virtual void set_str(int name, std::string val) = 0;
		virtual void set_int(int name, int val) = 0;
		virtual void set_bool(int name, bool val) = 0;
		virtual bool has_val(int name) const = 0;

		virtual std::string const& get_str(int name) const = 0;
		virtual int get_int(int name) const = 0;
		virtual bool get_bool(int name) const = 0;

		template <typename Type, typename Tag>
		// hidden
		void set_int(int name, flags::bitfield_flag<Type, Tag> const val)
		{ set_int(name, static_cast<int>(static_cast<Type>(val))); }

		// hidden
		// these are here just to suppress the warning about virtual destructors
		// internal
		settings_interface() = default;
		settings_interface(settings_interface const&) = default;
		settings_interface(settings_interface&&) = default;
		settings_interface& operator=(settings_interface const&) = default;
		settings_interface& operator=(settings_interface&&) = default;
	protected:
		~settings_interface() = default;
	};

	// The ``settings_pack`` struct, contains the names of all settings as
	// enum values. These values are passed in to the ``set_str()``,
	// ``set_int()``, ``set_bool()`` functions, to specify the setting to
	// change.
	//
	// .. include:: settings-ref.rst
	//
	struct TORRENT_EXPORT settings_pack final : settings_interface
	{
		friend TORRENT_EXTRA_EXPORT void apply_pack_impl(settings_pack const*
			, aux::session_settings_single_thread&
			, std::vector<void(aux::session_impl::*)()>*);

		// hidden
		settings_pack() = default;
		settings_pack(settings_pack const&) = default;
		settings_pack(settings_pack&&) noexcept = default;
		settings_pack& operator=(settings_pack const&) = default;
		settings_pack& operator=(settings_pack&&) noexcept = default;

		// set a configuration option in the settings_pack. ``name`` is one of
		// the enum values from string_types, int_types or bool_types. They must
		// match the respective type of the set_* function.
		void set_str(int name, std::string val) override;
		void set_int(int name, int val) override;
		void set_bool(int name, bool val) override;
		template <typename Type, typename Tag>
		void set_int(int name, flags::bitfield_flag<Type, Tag> const val)
		{ set_int(name, static_cast<int>(static_cast<Type>(val))); }

		// queries whether the specified configuration option has a value set in
		// this pack. ``name`` can be any enumeration value from string_types,
		// int_types or bool_types.
		bool has_val(int name) const override;

		// clear the settings pack from all settings
		void clear();

		// clear a specific setting from the pack
		void clear(int name);

		// queries the current configuration option from the settings_pack.
		// ``name`` is one of the enumeration values from string_types, int_types
		// or bool_types. The enum value must match the type of the get_*
		// function.
		std::string const& get_str(int name) const override;
		int get_int(int name) const override;
		bool get_bool(int name) const override;

		// setting names (indices) are 16 bits. The two most significant
		// bits indicate what type the setting has. (string, int, bool)
		enum type_bases
		{
			string_type_base = 0x0000,
			int_type_base =    0x4000,
			bool_type_base =   0x8000,
			type_mask =        0xc000,
			index_mask =       0x3fff
		};

		// internal
		template <typename Fun>
		void for_each(Fun&& f) const
		{
			for (auto const& s : m_strings) f(s.first, s.second);
			for (auto const& i : m_ints) f(i.first, i.second);
			for (auto const& b : m_bools) f(b.first, b.second);
		}

		// hidden
		enum string_types
		{
			// this is the client identification to the tracker. The recommended
			// format of this string is: "client-name/client-version
			// libtorrent/libtorrent-version". This name will not only be used when
			// making HTTP requests, but also when sending extended headers to
			// peers that support that extension. It may not contain \r or \n
			user_agent = string_type_base,

			// ``announce_ip`` is the ip address passed along to trackers as the
			// ``&ip=`` parameter. If left as the default, that parameter is
			// omitted.
			//
			// .. note::
			//    This setting is only meant for very special cases where a seed is
			//    running on the same host as the tracker, and the tracker accepts
			//    the IP parameter (which normal trackers don't). Do not set this
			//    option unless you also control the tracker.
			announce_ip,

#if TORRENT_ABI_VERSION == 1
			// ``mmap_cache`` may be set to a filename where the disk cache will
			// be mmapped to. This could be useful, for instance, to map the disk
			// cache from regular rotating hard drives onto an SSD drive. Doing
			// that effectively introduces a second layer of caching, allowing the
			// disk cache to be as big as can fit on an SSD drive (probably about
			// one order of magnitude more than the available RAM). The intention
			// of this setting is to set it up once at the start up and not change
			// it while running. The setting may not be changed as long as there
			// are any disk buffers in use. This default to the empty string,
			// which means use regular RAM allocations for the disk cache. The
			// file specified will be created and truncated to the disk cache size
			// (``cache_size``). Any existing file with the same name will be
			// replaced.
			//
			// This feature requires the ``mmap`` system call, on systems that
			// don't have ``mmap`` this setting is ignored.
			mmap_cache TORRENT_DEPRECATED_ENUM,
#else
			deprecated_mmap_cache,
#endif

			// this is the client name and version identifier sent to peers in the
			// handshake message. If this is an empty string, the user_agent is
			// used instead. This string must be a UTF-8 encoded unicode string.
			handshake_client_version,

			// This controls which IP address outgoing TCP peer connections are bound
			// to, in addition to controlling whether such connections are also
			// bound to a specific network interface/adapter (*bind-to-device*).
			// This string is a comma-separated list of IP addresses and
			// interface names. An empty string will not bind TCP sockets to a
			// device, and let the network stack assign the local address. A
			// list of names will be used to bind outgoing TCP sockets in a
			// round-robin fashion. An IP address will simply be used to `bind()`
			// the socket. An interface name will attempt to bind the socket to
			// that interface. If that fails, or is unsupported, one of the IP
			// addresses configured for that interface is used to `bind()` the
			// socket to. If the interface or adapter doesn't exist, the
			// outgoing peer connection will fail with an error message suggesting
			// the device cannot be found. Adapter names on Unix systems are of
			// the form "eth0", "eth1", "tun0", etc. This may be useful for
			// clients that are multi-homed. Binding an outgoing connection to a
			// local IP does not necessarily make the connection via the
			// associated NIC/Adapter.
			outgoing_interfaces,

			// a comma-separated list of (IP or device name, port) pairs. These are
			// the listen ports that will be opened for accepting incoming uTP and
			// TCP peer connections. These are also used for *outgoing* uTP and UDP
			// tracker connections and DHT nodes.
			//
			// It is possible to listen on multiple interfaces and
			// multiple ports. Binding to port 0 will make the operating system
			// pick the port.
			//
			// .. note::
			//    There are reasons to stick to the same port across sessions,
			//    which would mean only using port 0 on the first start, and
			//    recording the port that was picked for subsequent startups.
			//    Trackers, the DHT and other peers will remember the port they see
			//    you use and hand that port out to other peers trying to connect
			//    to you, as well as trying to connect to you themselves.
			//
			// A port that has an "s" suffix will accept SSL peer connections. (note
			// that SSL sockets are only available in builds with SSL support)
			//
			// A port that has an "l" suffix will be considered a local network.
			// i.e. it's assumed to only be able to reach hosts in the same local
			// network as the IP address (based on the netmask associated with the
			// IP, queried from the operating system).
			//
			// if binding fails, the listen_failed_alert is posted. Once a
			// socket binding succeeds (if it does), the listen_succeeded_alert
			// is posted. There may be multiple failures before a success.
			//
			// If a device name that does not exist is configured, no listen
			// socket will be opened for that interface. If this is the only
			// interface configured, it will be as if no listen ports are
			// configured.
			//
			// If no listen ports are configured (e.g. listen_interfaces is an
			// empty string), networking will be disabled. No DHT will start, no
			// outgoing uTP or tracker connections will be made. No incoming TCP
			// or uTP connections will be accepted. (outgoing TCP connections
			// will still be possible, depending on
			// settings_pack::outgoing_interfaces).
			//
			// For example:
			// ``[::1]:8888`` - will only accept connections on the IPv6 loopback
			// address on port 8888.
			//
			// ``eth0:4444,eth1:4444`` - will accept connections on port 4444 on
			// any IP address bound to device ``eth0`` or ``eth1``.
			//
			// ``[::]:0s`` - will accept SSL connections on a port chosen by the
			// OS. And not accept non-SSL connections at all.
			//
			// ``0.0.0.0:6881,[::]:6881`` - binds to all interfaces on port 6881.
			//
			// ``10.0.1.13:6881l`` - binds to the local IP address, port 6881, but
			// only allow talking to peers on the same local network. The netmask
			// is queried from the operating system. Interfaces marked ``l`` are
			// not announced to trackers, unless the tracker is also on the same
			// local network.
			//
			// Windows OS network adapter device name must be specified with GUID.
			// It can be obtained from "netsh lan show interfaces" command output.
			// GUID must be uppercased string embraced in curly brackets.
			// ``{E4F0B674-0DFC-48BB-98A5-2AA730BDB6D6}:7777`` - will accept
			// connections on port 7777 on adapter with this GUID.
			//
			// For more information, see the `Multi-homed hosts`_ section.
			//
			// .. _`Multi-homed hosts`: manual-ref.html#multi-homed-hosts
			listen_interfaces,

			// when using a proxy, this is the hostname where the proxy is running
			// see proxy_type. Note that when using a proxy, the
			// settings_pack::listen_interfaces setting is overridden and only a
			// single interface is created, just to contact the proxy. This
			// means a proxy cannot be combined with SSL torrents or multiple
			// listen interfaces. This proxy listen interface will not accept
			// incoming TCP connections, will not map ports with any gateway and
			// will not enable local service discovery. All traffic is supposed
			// to be channeled through the proxy.
			proxy_hostname,

			// when using a proxy, these are the credentials (if any) to use when
			// connecting to it. see proxy_type
			proxy_username,
			proxy_password,

			// sets the i2p_ SAM bridge to connect to. set the port with the
			// ``i2p_port`` setting.
			//
			// .. _i2p: http://www.i2p2.de
			i2p_hostname,

			// this is the fingerprint for the client. It will be used as the
			// prefix to the peer_id. If this is 20 bytes (or longer) it will be
			// truncated to 20 bytes and used as the entire peer-id
			//
			// There is a utility function, generate_fingerprint() that can be used
			// to generate a standard client peer ID fingerprint prefix.
			peer_fingerprint,

			// This is a comma-separated list of IP port-pairs. They will be added
			// to the DHT node (if it's enabled) as back-up nodes in case we don't
			// know of any.
			//
			// Changing these after the DHT has been started may not have any
			// effect until the DHT is restarted.
			dht_bootstrap_nodes,

			max_string_setting_internal
		};

		// hidden
		enum bool_types
		{
			// determines if connections from the same IP address as existing
			// connections should be rejected or not. Rejecting multiple connections
			// from the same IP address will prevent abusive
			// behavior by peers. The logic for determining whether connections are
			// to the same peer is more complicated with this enabled, and more
			// likely to fail in some edge cases. It is not recommended to enable
			// this feature.
			allow_multiple_connections_per_ip = bool_type_base,

#if TORRENT_ABI_VERSION == 1
			// if set to true, upload, download and unchoke limits are ignored for
			// peers on the local network. This option is *DEPRECATED*, please use
			// set_peer_class_filter() instead.
			ignore_limits_on_local_network TORRENT_DEPRECATED_ENUM,
#else
			deprecated_ignore_limits_on_local_network,
#endif

			// ``send_redundant_have`` controls if have messages will be sent to
			// peers that already have the piece. This is typically not necessary,
			// but it might be necessary for collecting statistics in some cases.
			send_redundant_have,

#if TORRENT_ABI_VERSION == 1
			// if this is true, outgoing bitfields will never be fuil. If the
			// client is seed, a few bits will be set to 0, and later filled in
			// with have messages. This is to prevent certain ISPs from stopping
			// people from seeding.
			lazy_bitfields TORRENT_DEPRECATED_ENUM,
#else
			deprecated_lazy_bitfield,
#endif

			// ``use_dht_as_fallback`` determines how the DHT is used. If this is
			// true, the DHT will only be used for torrents where all trackers in
			// its tracker list has failed. Either by an explicit error message or
			// a time out. If this is false, the DHT is used regardless of if the
			// trackers fail or not.
			use_dht_as_fallback,

			// ``upnp_ignore_nonrouters`` indicates whether or not the UPnP
			// implementation should ignore any broadcast response from a device
			// whose address is not on our subnet. i.e.
			// it's a way to not talk to other people's routers by mistake.
			upnp_ignore_nonrouters,

			// ``use_parole_mode`` specifies if parole mode should be used. Parole
			// mode means that peers that participate in pieces that fail the hash
			// check are put in a mode where they are only allowed to download
			// whole pieces. If the whole piece a peer in parole mode fails the
			// hash check, it is banned. If a peer participates in a piece that
			// passes the hash check, it is taken out of parole mode.
			use_parole_mode,

#if TORRENT_ABI_VERSION == 1
			// enable and disable caching of blocks read from disk. the purpose of
			// the read cache is partly read-ahead of requests but also to avoid
			// reading blocks back from the disk multiple times for popular
			// pieces.
			use_read_cache TORRENT_DEPRECATED_ENUM,
			use_write_cache TORRENT_DEPRECATED_ENUM,

			// this will make the disk cache never flush a write piece if it would
			// cause is to have to re-read it once we want to calculate the piece
			// hash
			dont_flush_write_cache TORRENT_DEPRECATED_ENUM,

			// allocate separate, contiguous, buffers for read and write calls.
			// Only used where writev/readv cannot be used will use more RAM but
			// may improve performance
			coalesce_reads TORRENT_DEPRECATED_ENUM,
			coalesce_writes TORRENT_DEPRECATED_ENUM,
#else
			deprecated_use_read_cache,
			deprecated_use_write_cache,
			deprecated_flush_write_cache,
			deprecated_coalesce_reads,
			deprecated_coalesce_writes,
#endif

			// if true, prefer seeding torrents when determining which torrents to give
			// active slots to. If false, give preference to downloading torrents
			auto_manage_prefer_seeds,

			// if ``dont_count_slow_torrents`` is true, torrents without any
			// payload transfers are not subject to the ``active_seeds`` and
			// ``active_downloads`` limits. This is intended to make it more
			// likely to utilize all available bandwidth, and avoid having
			// torrents that don't transfer anything block the active slots.
			dont_count_slow_torrents,

			// ``close_redundant_connections`` specifies whether libtorrent should
			// close connections where both ends have no utility in keeping the
			// connection open. For instance if both ends have completed their
			// downloads, there's no point in keeping it open.
			close_redundant_connections,

			// If ``prioritize_partial_pieces`` is true, partial pieces are picked
			// before pieces that are more rare. If false, rare pieces are always
			// prioritized, unless the number of partial pieces is growing out of
			// proportion.
			prioritize_partial_pieces,

			// if set to true, the estimated TCP/IP overhead is drained from the
			// rate limiters, to avoid exceeding the limits with the total traffic
			rate_limit_ip_overhead,

			// ``announce_to_all_trackers`` controls how multi tracker torrents
			// are treated. If this is set to true, all trackers in the same tier
			// are announced to in parallel. If all trackers in tier 0 fails, all
			// trackers in tier 1 are announced as well. If it's set to false, the
			// behavior is as defined by the multi tracker specification.
			//
			// ``announce_to_all_tiers`` also controls how multi tracker torrents
			// are treated. When this is set to true, one tracker from each tier
			// is announced to. This is the uTorrent behavior. To be compliant
			// with the Multi-tracker specification, set it to false.
			announce_to_all_tiers,
			announce_to_all_trackers,

			// ``prefer_udp_trackers``: true means that trackers
			// may be rearranged in a way that udp trackers are always tried
			// before http trackers for the same hostname. Setting this to false
			// means that the tracker's tier is respected and there's no
			// preference of one protocol over another.
			prefer_udp_trackers,

#if TORRENT_ABI_VERSION == 1
			// ``strict_super_seeding`` when this is set to true, a piece has to
			// have been forwarded to a third peer before another one is handed
			// out. This is the traditional definition of super seeding.
			strict_super_seeding TORRENT_DEPRECATED_ENUM,
#else
			deprecated_strict_super_seeding,
#endif

#if TORRENT_ABI_VERSION == 1
			// if this is set to true, the memory allocated for the disk cache
			// will be locked in physical RAM, never to be swapped out. Every time
			// a disk buffer is allocated and freed, there will be the extra
			// overhead of a system call.
			lock_disk_cache TORRENT_DEPRECATED_ENUM,
#else
			deprecated_lock_disk_cache,
#endif

			// when set to true, all data downloaded from peers will be assumed to
			// be correct, and not tested to match the hashes in the torrent this
			// is only useful for simulation and testing purposes (typically
			// combined with disabled_storage)
			disable_hash_checks,

			// if this is true, i2p torrents are allowed to also get peers from
			// other sources than the tracker, and connect to regular IPs, not
			// providing any anonymization. This may be useful if the user is not
			// interested in the anonymization of i2p, but still wants to be able
			// to connect to i2p peers.
			allow_i2p_mixed,

#if TORRENT_ABI_VERSION == 1
			// ``low_prio_disk`` determines if the disk I/O should use a normal or
			// low priority policy. True, means that it's
			// low priority by default. Other processes doing disk I/O will
			// normally take priority in this mode. This is meant to improve the
			// overall responsiveness of the system while downloading in the
			// background. For high-performance server setups, this might not be
			// desirable.
			low_prio_disk TORRENT_DEPRECATED_ENUM,
#else
			deprecated_low_prio_disk,
#endif

			// ``volatile_read_cache``, if this is set to true, read cache blocks
			// that are hit by peer read requests are removed from the disk cache
			// to free up more space. This is useful if you don't expect the disk
			// cache to create any cache hits from other peers than the one who
			// triggered the cache line to be read into the cache in the first
			// place.
			volatile_read_cache,

#if TORRENT_ABI_VERSION == 1
			// ``guided_read_cache`` enables the disk cache to adjust the size of
			// a cache line generated by peers to depend on the upload rate you
			// are sending to that peer. The intention is to optimize the RAM
			// usage of the cache, to read ahead further for peers that you're
			// sending faster to.
			guided_read_cache TORRENT_DEPRECATED_ENUM,
#else
			deprecated_guided_read_cache,
#endif

			// ``no_atime_storage`` this is a Linux-only option and passes in the
			// ``O_NOATIME`` to ``open()`` when opening files. This may lead to
			// some disk performance improvements.
			no_atime_storage,

			// ``incoming_starts_queued_torrents``.  If a torrent
			// has been paused by the auto managed feature in libtorrent, i.e. the
			// torrent is paused and auto managed, this feature affects whether or
			// not it is automatically started on an incoming connection. The main
			// reason to queue torrents, is not to make them unavailable, but to
			// save on the overhead of announcing to the trackers, the DHT and to
			// avoid spreading one's unchoke slots too thin. If a peer managed to
			// find us, even though we're no in the torrent anymore, this setting
			// can make us start the torrent and serve it.
			incoming_starts_queued_torrents,

			// when set to true, the downloaded counter sent to trackers will
			// include the actual number of payload bytes downloaded including
			// redundant bytes. If set to false, it will not include any redundancy
			// bytes
			report_true_downloaded,

			// ``strict_end_game_mode`` controls when a
			// block may be requested twice. If this is ``true``, a block may only
			// be requested twice when there's at least one request to every piece
			// that's left to download in the torrent. This may slow down progress
			// on some pieces sometimes, but it may also avoid downloading a lot
			// of redundant bytes. If this is ``false``, libtorrent attempts to
			// use each peer connection to its max, by always requesting
			// something, even if it means requesting something that has been
			// requested from another peer already.
			strict_end_game_mode,

#if TORRENT_ABI_VERSION == 1
			// if ``broadcast_lsd`` is set to true, the local peer discovery (or
			// Local Service Discovery) will not only use IP multicast, but also
			// broadcast its messages. This can be useful when running on networks
			// that don't support multicast. Since broadcast messages might be
			// expensive and disruptive on networks, only every 8th announce uses
			// broadcast.
			broadcast_lsd TORRENT_DEPRECATED_ENUM,
#else
			deprecated_broadcast_lsd,
#endif

			// Enables incoming and outgoing, TCP and uTP peer connections.
			// ``false`` is disabled and ``true`` is enabled. When outgoing
			// connections are disabled, libtorrent will simply not make
			// outgoing peer connections with the specific transport protocol.
			// Disabled incoming peer connections will simply be rejected.
			// These options only apply to peer connections, not tracker- or any
			// other kinds of connections.
			enable_outgoing_utp,
			enable_incoming_utp,
			enable_outgoing_tcp,
			enable_incoming_tcp,

#if TORRENT_ABI_VERSION == 1
			// ``ignore_resume_timestamps`` determines if the storage, when
			// loading resume data files, should verify that the file modification
			// time with the timestamps in the resume data. False, means timestamps
			// are taken into account, and resume
			// data is less likely to accepted (torrents are more likely to be
			// fully checked when loaded). It might be useful to set this to true
			// if your network is faster than your disk, and it would be faster to
			// redownload potentially missed pieces than to go through the whole
			// storage to look for them.
			ignore_resume_timestamps TORRENT_DEPRECATED_ENUM,
#else
			// hidden
			deprecated_ignore_resume_timestamps,
#endif

			// ``no_recheck_incomplete_resume`` determines if the storage should
			// check the whole files when resume data is incomplete or missing or
			// whether it should simply assume we don't have any of the data. If
			// false, any existing files will be checked.
			// By setting this setting to true, the files won't be checked, but
			// will go straight to download mode.
			no_recheck_incomplete_resume,

			// ``anonymous_mode``: When set to true, the client
			// tries to hide its identity to a certain degree. The user-agent will be
			// reset to an empty string (except for private torrents). Trackers
			// will only be used if they are using a proxy server.
			// The listen sockets are closed, and incoming
			// connections will only be accepted through a SOCKS5 or I2P proxy (if
			// a peer proxy is set up and is run on the same machine as the
			// tracker proxy). Since no incoming connections are accepted,
			// NAT-PMP, UPnP, DHT and local peer discovery are all turned off when
			// this setting is enabled.
			//
			// If you're using I2P, it might make sense to enable anonymous mode
			// as well.
			anonymous_mode,

			// specifies whether downloads from web seeds is reported to the
			// tracker or not. Turning it off also excludes web
			// seed traffic from other stats and download rate reporting via the
			// libtorrent API.
			report_web_seed_downloads,

#if TORRENT_ABI_VERSION == 1
			// set to true if uTP connections should be rate limited This option
			// is *DEPRECATED*, please use set_peer_class_filter() instead.
			rate_limit_utp TORRENT_DEPRECATED_ENUM,
#else
			deprecated_rate_limit_utp,
#endif

#if TORRENT_ABI_VERSION == 1
			// if this is true, the ``&ip=`` argument in tracker requests (unless
			// otherwise specified) will be set to the intermediate IP address if
			// the user is double NATed. If the user is not double NATed, this
			// option does not have an affect
			announce_double_nat TORRENT_DEPRECATED_ENUM,
#else
			deprecated_announce_double_nat,
#endif

			// ``seeding_outgoing_connections`` determines if seeding (and
			// finished) torrents should attempt to make outgoing connections or
			// not. It may be set to false in very
			// specific applications where the cost of making outgoing connections
			// is high, and there are no or small benefits of doing so. For
			// instance, if no nodes are behind a firewall or a NAT, seeds don't
			// need to make outgoing connections.
			seeding_outgoing_connections,

			// when this is true, libtorrent will not attempt to make outgoing
			// connections to peers whose port is < 1024. This is a safety
			// precaution to avoid being part of a DDoS attack
			no_connect_privileged_ports,

			// ``smooth_connects`` means the number of
			// connection attempts per second may be limited to below the
			// ``connection_speed``, in case we're close to bump up against the
			// limit of number of connections. The intention of this setting is to
			// more evenly distribute our connection attempts over time, instead
			// of attempting to connect in batches, and timing them out in
			// batches.
			smooth_connects,

			// always send user-agent in every web seed request. If false, only
			// the first request per http connection will include the user agent
			always_send_user_agent,

			// ``apply_ip_filter_to_trackers`` determines
			// whether the IP filter applies to trackers as well as peers. If this
			// is set to false, trackers are exempt from the IP filter (if there
			// is one). If no IP filter is set, this setting is irrelevant.
			apply_ip_filter_to_trackers,

#if TORRENT_ABI_VERSION == 1
			// ``use_disk_read_ahead`` if true will attempt to
			// optimize disk reads by giving the operating system heads up of disk
			// read requests as they are queued in the disk job queue.
			use_disk_read_ahead TORRENT_DEPRECATED_ENUM,
#else
			deprecated_use_disk_read_ahead,
#endif

#if TORRENT_ABI_VERSION == 1
			// ``lock_files`` determines whether or not to lock files which
			// libtorrent is downloading to or seeding from. This is implemented
			// using ``fcntl(F_SETLK)`` on Unix systems and by not passing in
			// ``SHARE_READ`` and ``SHARE_WRITE`` on windows. This might prevent
			// 3rd party processes from corrupting the files under libtorrent's
			// feet.
			lock_files TORRENT_DEPRECATED_ENUM,
#else
			deprecated_lock_files,
#endif

#if TORRENT_ABI_VERSION == 1
			// ``contiguous_recv_buffer`` determines whether or not libtorrent
			// should receive data from peers into a contiguous intermediate
			// buffer, to then copy blocks into disk buffers from, or to make many
			// smaller calls to ``read()``, each time passing in the specific
			// buffer the data belongs in. When downloading at high rates, the
			// latter may save some time copying data. When seeding at high rates,
			// all incoming traffic consists of a very large number of tiny
			// packets, and enabling ``contiguous_recv_buffer`` will provide
			// higher performance. When this is enabled, it will only be used when
			// seeding to peers, since that's when it provides performance
			// improvements.
			contiguous_recv_buffer TORRENT_DEPRECATED_ENUM,
#else
			deprecated_contiguous_recv_buffer,
#endif

			// when true, web seeds sending bad data will be banned
			ban_web_seeds,

			// when set to false, the ``write_cache_line_size`` will apply across
			// piece boundaries. this is a bad idea unless the piece picker also
			// is configured to have an affinity to pick pieces belonging to the
			// same write cache line as is configured in the disk cache.
			allow_partial_disk_writes,

#if TORRENT_ABI_VERSION == 1
			// If true, disables any communication that's not going over a proxy.
			// Enabling this requires a proxy to be configured as well, see
			// proxy_type and proxy_hostname settings. The listen sockets are
			// closed, and incoming connections will only be accepted through a
			// SOCKS5 or I2P proxy (if a peer proxy is set up and is run on the
			// same machine as the tracker proxy).
			force_proxy TORRENT_DEPRECATED_ENUM,
#else
			deprecated_force_proxy,
#endif

			// if false, prevents libtorrent to advertise share-mode support
			support_share_mode,

#if TORRENT_ABI_VERSION <= 2
			// support for BEP 30 merkle torrents has been removed

			// if this is false, don't advertise support for the Tribler merkle
			// tree piece message
			support_merkle_torrents TORRENT_DEPRECATED_ENUM,
#else
			deprecated_support_merkle_torrents,
#endif

			// if this is true, the number of redundant bytes is sent to the
			// tracker
			report_redundant_bytes,

			// if this is true, libtorrent will fall back to listening on a port
			// chosen by the operating system (i.e. binding to port 0). If a
			// failure is preferred, set this to false.
			listen_system_port_fallback,

#if TORRENT_ABI_VERSION == 1
			// ``use_disk_cache_pool`` enables using a pool allocator for disk
			// cache blocks. Enabling it makes the cache perform better at high
			// throughput. It also makes the cache less likely and slower at
			// returning memory back to the system, once allocated.
			use_disk_cache_pool TORRENT_DEPRECATED_ENUM,
#else
			deprecated_use_disk_cache_pool,
#endif

			// when this is true, and incoming encrypted connections are enabled,
			// &supportcrypt=1 is included in http tracker announces
			announce_crypto_support,

			// Starts and stops the UPnP service. When started, the listen port
			// and the DHT port are attempted to be forwarded on local UPnP router
			// devices.
			//
			// The upnp object returned by ``start_upnp()`` can be used to add and
			// remove arbitrary port mappings. Mapping status is returned through
			// the portmap_alert and the portmap_error_alert. The object will be
			// valid until ``stop_upnp()`` is called. See upnp-and-nat-pmp_.
			enable_upnp,

			// Starts and stops the NAT-PMP service. When started, the listen port
			// and the DHT port are attempted to be forwarded on the router
			// through NAT-PMP.
			//
			// The natpmp object returned by ``start_natpmp()`` can be used to add
			// and remove arbitrary port mappings. Mapping status is returned
			// through the portmap_alert and the portmap_error_alert. The object
			// will be valid until ``stop_natpmp()`` is called. See
			// upnp-and-nat-pmp_.
			enable_natpmp,

			// Starts and stops Local Service Discovery. This service will
			// broadcast the info-hashes of all the non-private torrents on the
			// local network to look for peers on the same swarm within multicast
			// reach.
			enable_lsd,

			// starts the dht node and makes the trackerless service available to
			// torrents.
			enable_dht,

			// if the allowed encryption level is both, setting this to true will
			// prefer RC4 if both methods are offered, plain text otherwise
			prefer_rc4,

			// if true, hostname lookups are done via the configured proxy (if
			// any). This is only supported by SOCKS5 and HTTP.
			proxy_hostnames,

			// if true, peer connections are made (and accepted) over the
			// configured proxy, if any. Web seeds as well as regular bittorrent
			// peer connections are considered "peer connections". Anything
			// transporting actual torrent payload (trackers and DHT traffic are
			// not considered peer connections).
			proxy_peer_connections,

			// if this setting is true, torrents with a very high availability of
			// pieces (and seeds) are downloaded sequentially. This is more
			// efficient for the disk I/O. With many seeds, the download order is
			// unlikely to matter anyway
			auto_sequential,

			// if true, tracker connections are made over the configured proxy, if
			// any.
			proxy_tracker_connections,

			// Starts and stops the internal IP table route changes notifier.
			//
			// The current implementation supports multiple platforms, and it is
			// recommended to have it enable, but you may want to disable it if
			// it's supported but unreliable, or if you have a better way to
			// detect the changes. In the later case, you should manually call
			// ``session_handle::reopen_network_sockets`` to ensure network
			// changes are taken in consideration.
			enable_ip_notifier,

			// when this is true, nodes whose IDs are derived from their source
			// IP according to `BEP 42`_ are preferred in the routing table.
			dht_prefer_verified_node_ids,

			// determines if the routing table entries should restrict entries to one
			// per IP. This defaults to true, which helps mitigate some attacks on
			// the DHT. It prevents adding multiple nodes with IPs with a very close
			// CIDR distance.
			//
			// when set, nodes whose IP address that's in the same /24 (or /64 for
			// IPv6) range in the same routing table bucket. This is an attempt to
			// mitigate node ID spoofing attacks also restrict any IP to only have a
			// single entry in the whole routing table
			dht_restrict_routing_ips,

			// determines if DHT searches should prevent adding nodes with IPs with
			// very close CIDR distance. This also defaults to true and helps
			// mitigate certain attacks on the DHT.
			dht_restrict_search_ips,

			// makes the first buckets in the DHT routing table fit 128, 64, 32 and
			// 16 nodes respectively, as opposed to the standard size of 8. All other
			// buckets have size 8 still.
			dht_extended_routing_table,

			// slightly changes the lookup behavior in terms of how many outstanding
			// requests we keep. Instead of having branch factor be a hard limit, we
			// always keep *branch factor* outstanding requests to the closest nodes.
			// i.e. every time we get results back with closer nodes, we query them
			// right away. It lowers the lookup times at the cost of more outstanding
			// queries.
			dht_aggressive_lookups,

			// when set, perform lookups in a way that is slightly more expensive,
			// but which minimizes the amount of information leaked about you.
			dht_privacy_lookups,

			// when set, node's whose IDs that are not correctly generated based on
			// its external IP are ignored. When a query arrives from such node, an
			// error message is returned with a message saying "invalid node ID".
			dht_enforce_node_id,

			// ignore DHT messages from parts of the internet we wouldn't expect to
			// see any traffic from
			dht_ignore_dark_internet,

			// when set, the other nodes won't keep this node in their routing
			// tables, it's meant for low-power and/or ephemeral devices that
			// cannot support the DHT, it is also useful for mobile devices which
			// are sensitive to network traffic and battery life.
			// this node no longer responds to 'query' messages, and will place a
			// 'ro' key (value = 1) in the top-level message dictionary of outgoing
			// query messages.
			dht_read_only,

			// when this is true, create an affinity for downloading 4 MiB extents
			// of adjacent pieces. This is an attempt to achieve better disk I/O
			// throughput by downloading larger extents of bytes, for torrents with
			// small piece sizes
			piece_extent_affinity,

			// when set to true, the certificate of HTTPS trackers and HTTPS web
			// seeds will be validated against the system's certificate store
			// (as defined by OpenSSL). If the system does not have a
			// certificate store, this option may have to be disabled in order
			// to get trackers and web seeds to work).
			validate_https_trackers,

			// when enabled, tracker and web seed requests are subject to
			// certain restrictions.
			//
			// An HTTP(s) tracker requests to localhost (loopback)
			// must have the request path start with "/announce". This is the
			// conventional bittorrent tracker request. Any other HTTP(S)
			// tracker request to loopback will be rejected. This applies to
			// trackers that redirect to loopback as well.
			//
			// Web seeds that end up on the client's local network (i.e. in a
			// private IP address range) may not include query string arguments.
			// This applies to web seeds redirecting to the local network as
			// well.
			//
			// Web seeds on global IPs (i.e. not local network) may not redirect
			// to a local network address
			ssrf_mitigation,

			// when disabled, any tracker or web seed with an IDNA hostname
			// (internationalized domain name) is ignored. This is a security
			// precaution to avoid various unicode encoding attacks that might
			// happen at the application level.
			allow_idna,

			// when set to true, enables the attempt to use SetFileValidData()
			// to pre-allocate disk space. This system call will only work when
			// running with Administrator privileges on Windows, and so this
			// setting is only relevant in that scenario. Using
			// SetFileValidData() poses a security risk, as it may reveal
			// previously deleted information from the disk.
			enable_set_file_valid_data,

			max_bool_setting_internal
		};

		// hidden
		enum int_types
		{
			// ``tracker_completion_timeout`` is the number of seconds the tracker
			// connection will wait from when it sent the request until it
			// considers the tracker to have timed-out.
			tracker_completion_timeout = int_type_base,

			// ``tracker_receive_timeout`` is the number of seconds to wait to
			// receive any data from the tracker. If no data is received for this
			// number of seconds, the tracker will be considered as having timed
			// out. If a tracker is down, this is the kind of timeout that will
			// occur.
			tracker_receive_timeout,

			// ``stop_tracker_timeout`` is the number of seconds to wait when
			// sending a stopped message before considering a tracker to have
			// timed out. This is usually shorter, to make the client quit faster.
			// If the value is set to 0, the connections to trackers with the
			// stopped event are suppressed.
			stop_tracker_timeout,

			// this is the maximum number of bytes in a tracker response. If a
			// response size passes this number of bytes it will be rejected and
			// the connection will be closed. On gzipped responses this size is
			// measured on the uncompressed data. So, if you get 20 bytes of gzip
			// response that'll expand to 2 megabytes, it will be interrupted
			// before the entire response has been uncompressed (assuming the
			// limit is lower than 2 MiB).
			tracker_maximum_response_length,

			// the number of seconds from a request is sent until it times out if
			// no piece response is returned.
			piece_timeout,

			// the number of seconds one block (16 kiB) is expected to be received
			// within. If it's not, the block is requested from a different peer
			request_timeout,

			// the length of the request queue given in the number of seconds it
			// should take for the other end to send all the pieces. i.e. the
			// actual number of requests depends on the download rate and this
			// number.
			request_queue_time,

			// the number of outstanding block requests a peer is allowed to queue
			// up in the client. If a peer sends more requests than this (before
			// the first one has been sent) the last request will be dropped. the
			// higher this is, the faster upload speeds the client can get to a
			// single peer.
			max_allowed_in_request_queue,

			// ``max_out_request_queue`` is the maximum number of outstanding
			// requests to send to a peer. This limit takes precedence over
			// ``request_queue_time``. i.e. no matter the download speed, the
			// number of outstanding requests will never exceed this limit.
			max_out_request_queue,

			// if a whole piece can be downloaded in this number of seconds, or
			// less, the peer_connection will prefer to request whole pieces at a
			// time from this peer. The benefit of this is to better utilize disk
			// caches by doing localized accesses and also to make it easier to
			// identify bad peers if a piece fails the hash check.
			whole_pieces_threshold,

			// ``peer_timeout`` is the number of seconds the peer connection
			// should wait (for any activity on the peer connection) before
			// closing it due to time out. 120 seconds is
			// specified in the protocol specification. After half
			// the time out, a keep alive message is sent.
			peer_timeout,

			// same as peer_timeout, but only applies to url-seeds. this is
			// usually set lower, because web servers are expected to be more
			// reliable.
			urlseed_timeout,

			// controls the pipelining size of url and http seeds. i.e. the number of HTTP
			// request to keep outstanding before waiting for the first one to
			// complete. It's common for web servers to limit this to a relatively
			// low number, like 5
			urlseed_pipeline_size,

			// number of seconds until a new retry of a url-seed takes place.
			// Default retry value for http-seeds that don't provide
			// a valid ``retry-after`` header.
			urlseed_wait_retry,

			// sets the upper limit on the total number of files this session will
			// keep open. The reason why files are left open at all is that some
			// anti virus software hooks on every file close, and scans the file
			// for viruses. deferring the closing of the files will be the
			// difference between a usable system and a completely hogged down
			// system. Most operating systems also has a limit on the total number
			// of file descriptors a process may have open.
			file_pool_size,

			// ``max_failcount`` is the maximum times we try to
			// connect to a peer before stop connecting again. If a
			// peer succeeds, the failure counter is reset. If a
			// peer is retrieved from a peer source (other than DHT)
			// the failcount is decremented by one, allowing another
			// try.
			max_failcount,

			// the number of seconds to wait to reconnect to a peer. this time is
			// multiplied with the failcount.
			min_reconnect_time,

			// ``peer_connect_timeout`` the number of seconds to wait after a
			// connection attempt is initiated to a peer until it is considered as
			// having timed out. This setting is especially important in case the
			// number of half-open connections are limited, since stale half-open
			// connection may delay the connection of other peers considerably.
			peer_connect_timeout,

			// ``connection_speed`` is the number of connection attempts that are
			// made per second. If a number < 0 is specified, it will default to
			// 200 connections per second. If 0 is specified, it means don't make
			// outgoing connections at all.
			connection_speed,

			// if a peer is uninteresting and uninterested for longer than this
			// number of seconds, it will be disconnected.
			inactivity_timeout,

			// ``unchoke_interval`` is the number of seconds between
			// chokes/unchokes. On this interval, peers are re-evaluated for being
			// choked/unchoked. This is defined as 30 seconds in the protocol, and
			// it should be significantly longer than what it takes for TCP to
			// ramp up to it's max rate.
			unchoke_interval,

			// ``optimistic_unchoke_interval`` is the number of seconds between
			// each *optimistic* unchoke. On this timer, the currently
			// optimistically unchoked peer will change.
			optimistic_unchoke_interval,

			// ``num_want`` is the number of peers we want from each tracker
			// request. It defines what is sent as the ``&num_want=`` parameter to
			// the tracker.
			num_want,

			// ``initial_picker_threshold`` specifies the number of pieces we need
			// before we switch to rarest first picking. The first
			// ``initial_picker_threshold`` pieces in any torrent are picked at random
			// , the following pieces are picked in rarest first order.
			initial_picker_threshold,

			// the number of allowed pieces to send to peers that supports the
			// fast extensions
			allowed_fast_set_size,

			// ``suggest_mode`` controls whether or not libtorrent will send out
			// suggest messages to create a bias of its peers to request certain
			// pieces. The modes are:
			//
			// * ``no_piece_suggestions`` which will not send out suggest messages.
			// * ``suggest_read_cache`` which will send out suggest messages for
			//   the most recent pieces that are in the read cache.
			suggest_mode,

			// ``max_queued_disk_bytes`` is the maximum number of bytes, to
			// be written to disk, that can wait in the disk I/O thread queue.
			// This queue is only for waiting for the disk I/O thread to receive
			// the job and either write it to disk or insert it in the write
			// cache. When this limit is reached, the peer connections will stop
			// reading data from their sockets, until the disk thread catches up.
			// Setting this too low will severely limit your download rate.
			max_queued_disk_bytes,

			// the number of seconds to wait for a handshake response from a peer.
			// If no response is received within this time, the peer is
			// disconnected.
			handshake_timeout,

			// ``send_buffer_low_watermark`` the minimum send buffer target size
			// (send buffer includes bytes pending being read from disk). For good
			// and snappy seeding performance, set this fairly high, to at least
			// fit a few blocks. This is essentially the initial window size which
			// will determine how fast we can ramp up the send rate
			//
			// if the send buffer has fewer bytes than ``send_buffer_watermark``,
			// we'll read another 16 kiB block onto it. If set too small, upload
			// rate capacity will suffer. If set too high, memory will be wasted.
			// The actual watermark may be lower than this in case the upload rate
			// is low, this is the upper limit.
			//
			// the current upload rate to a peer is multiplied by this factor to
			// get the send buffer watermark. The factor is specified as a
			// percentage. i.e. 50 -> 0.5 This product is clamped to the
			// ``send_buffer_watermark`` setting to not exceed the max. For high
			// speed upload, this should be set to a greater value than 100. For
			// high capacity connections, setting this higher can improve upload
			// performance and disk throughput. Setting it too high may waste RAM
			// and create a bias towards read jobs over write jobs.
			send_buffer_low_watermark,
			send_buffer_watermark,
			send_buffer_watermark_factor,

			// ``choking_algorithm`` specifies which algorithm to use to determine
			// how many peers to unchoke. The unchoking algorithm for
			// downloading torrents is always "tit-for-tat", i.e. the peers we
			// download the fastest from are unchoked.
			//
			// The options for choking algorithms are defined in the
			// choking_algorithm_t enum.
			//
			// ``seed_choking_algorithm`` controls the seeding unchoke behavior.
			// i.e. How we select which peers to unchoke for seeding torrents.
			// Since a seeding torrent isn't downloading anything, the
			// tit-for-tat mechanism cannot be used. The available options are
			// defined in the seed_choking_algorithm_t enum.
			choking_algorithm,
			seed_choking_algorithm,

#if TORRENT_ABI_VERSION == 1
			// ``cache_size`` is the disk write and read cache. It is specified
			// in units of 16 kiB blocks. Buffers that are part of a peer's send
			// or receive buffer also count against this limit. Send and receive
			// buffers will never be denied to be allocated, but they will cause
			// the actual cached blocks to be flushed or evicted. If this is set
			// to -1, the cache size is automatically set based on the amount of
			// physical RAM on the machine. If the amount of physical RAM cannot
			// be determined, it's set to 1024 (= 16 MiB).
			//
			// On 32 bit builds, the effective cache size will be limited to 3/4 of
			// 2 GiB to avoid exceeding the virtual address space limit.
			cache_size TORRENT_DEPRECATED_ENUM,

			// Disk buffers are allocated using a pool allocator, the number of
			// blocks that are allocated at a time when the pool needs to grow can
			// be specified in ``cache_buffer_chunk_size``. Lower numbers saves
			// memory at the expense of more heap allocations. If it is set to 0,
			// the effective chunk size is proportional to the total cache size,
			// attempting to strike a good balance between performance and memory
			// usage. It defaults to 0.
			cache_buffer_chunk_size TORRENT_DEPRECATED_ENUM,

			// ``cache_expiry`` is the number of seconds
			// from the last cached write to a piece in the write cache, to when
			// it's forcefully flushed to disk.
			cache_expiry TORRENT_DEPRECATED_ENUM,
#else
			deprecated_cache_size,
			deprecated_cache_buffer_chunk_size,
			deprecated_cache_expiry,
#endif

			// determines how files are opened when they're in read only mode
			// versus read and write mode. The options are:
			//
			// enable_os_cache
			//   Files are opened normally, with the OS caching reads and writes.
			// disable_os_cache
			//   This opens all files in no-cache mode. This corresponds to the
			//   OS not letting blocks for the files linger in the cache. This
			//   makes sense in order to avoid the bittorrent client to
			//   potentially evict all other processes' cache by simply handling
			//   high throughput and large files. If libtorrent's read cache is
			//   disabled, enabling this may reduce performance.
			//
			// One reason to disable caching is that it may help the operating
			// system from growing its file cache indefinitely.
			disk_io_write_mode,
			disk_io_read_mode,

			// this is the first port to use for binding outgoing connections to.
			// This is useful for users that have routers that allow QoS settings
			// based on local port. when binding outgoing connections to specific
			// ports, ``num_outgoing_ports`` is the size of the range. It should
			// be more than a few
			//
			// .. warning:: setting outgoing ports will limit the ability to keep
			//    multiple connections to the same client, even for different
			//    torrents. It is not recommended to change this setting. Its main
			//    purpose is to use as an escape hatch for cheap routers with QoS
			//    capability but can only classify flows based on port numbers.
			//
			// It is a range instead of a single port because of the problems with
			// failing to reconnect to peers if a previous socket to that peer and
			// port is in ``TIME_WAIT`` state.
			outgoing_port,
			num_outgoing_ports,

			// ``peer_tos`` determines the TOS byte set in the IP header of every
			// packet sent to peers (including web seeds). ``0x0`` means no marking,
			// ``0x20`` represents the *QBone scavenger service*. For more
			// details, see QBSS_.
			//
			// .. _`QBSS`: http://qbone.internet2.edu/qbss/
			peer_tos,

			// for auto managed torrents, these are the limits they are subject
			// to. If there are too many torrents some of the auto managed ones
			// will be paused until some slots free up. ``active_downloads`` and
			// ``active_seeds`` controls how many active seeding and downloading
			// torrents the queuing mechanism allows. The target number of active
			// torrents is ``min(active_downloads + active_seeds, active_limit)``.
			// ``active_downloads`` and ``active_seeds`` are upper limits on the
			// number of downloading torrents and seeding torrents respectively.
			// Setting the value to -1 means unlimited.
			//
			// For example if there are 10 seeding torrents and 10 downloading
			// torrents, and ``active_downloads`` is 4 and ``active_seeds`` is 4,
			// there will be 4 seeds active and 4 downloading torrents. If the
			// settings are ``active_downloads`` = 2 and ``active_seeds`` = 4,
			// then there will be 2 downloading torrents and 4 seeding torrents
			// active. Torrents that are not auto managed are not counted against
			// these limits.
			//
			// ``active_checking`` is the limit of number of simultaneous checking
			// torrents.
			//
			// ``active_limit`` is a hard limit on the number of active (auto
			// managed) torrents. This limit also applies to slow torrents.
			//
			// ``active_dht_limit`` is the max number of torrents to announce to
			// the DHT.
			//
			// ``active_tracker_limit`` is the max number of torrents to announce
			// to their trackers.
			//
			// ``active_lsd_limit`` is the max number of torrents to announce to
			// the local network over the local service discovery protocol.
			//
			// You can have more torrents *active*, even though they are not
			// announced to the DHT, lsd or their tracker. If some peer knows
			// about you for any reason and tries to connect, it will still be
			// accepted, unless the torrent is paused, which means it won't accept
			// any connections.
			active_downloads,
			active_seeds,
			active_checking,
			active_dht_limit,
			active_tracker_limit,
			active_lsd_limit,
			active_limit,

#if TORRENT_ABI_VERSION == 1
			// ``active_loaded_limit`` is the number of torrents that are allowed
			// to be *loaded* at any given time. Note that a torrent can be active
			// even though it's not loaded. If an unloaded torrents finds a peer
			// that wants to access it, the torrent will be loaded on demand,
			// using a user-supplied callback function. If the feature of
			// unloading torrents is not enabled, this setting have no effect. If
			// this limit is set to 0, it means unlimited. For more information,
			// see dynamic-loading-of-torrent-files_.
			active_loaded_limit TORRENT_DEPRECATED_ENUM,
#else
			deprecated_active_loaded_limit,
#endif

			// ``auto_manage_interval`` is the number of seconds between the
			// torrent queue is updated, and rotated.
			auto_manage_interval,

			// this is the limit on the time a torrent has been an active seed
			// (specified in seconds) before it is considered having met the seed
			// limit criteria. See queuing_.
			seed_time_limit,

			// ``auto_scrape_interval`` is the number of seconds between scrapes
			// of queued torrents (auto managed and paused torrents). Auto managed
			// torrents that are paused, are scraped regularly in order to keep
			// track of their downloader/seed ratio. This ratio is used to
			// determine which torrents to seed and which to pause.
			//
			// ``auto_scrape_min_interval`` is the minimum number of seconds
			// between any automatic scrape (regardless of torrent). In case there
			// are a large number of paused auto managed torrents, this puts a
			// limit on how often a scrape request is sent.
			auto_scrape_interval,
			auto_scrape_min_interval,

			// ``max_peerlist_size`` is the maximum number of peers in the list of
			// known peers. These peers are not necessarily connected, so this
			// number should be much greater than the maximum number of connected
			// peers. Peers are evicted from the cache when the list grows passed
			// 90% of this limit, and once the size hits the limit, peers are no
			// longer added to the list. If this limit is set to 0, there is no
			// limit on how many peers we'll keep in the peer list.
			//
			// ``max_paused_peerlist_size`` is the max peer list size used for
			// torrents that are paused. This can be used to save memory for paused
			// torrents, since it's not as important for them to keep a large peer
			// list.
			max_peerlist_size,
			max_paused_peerlist_size,

			// this is the minimum allowed announce interval for a tracker. This
			// is specified in seconds and is used as a sanity check on what is
			// returned from a tracker. It mitigates hammering mis-configured
			// trackers.
			min_announce_interval,

			// this is the number of seconds a torrent is considered active after
			// it was started, regardless of upload and download speed. This is so
			// that newly started torrents are not considered inactive until they
			// have a fair chance to start downloading.
			auto_manage_startup,

			// ``seeding_piece_quota`` is the number of pieces to send to a peer,
			// when seeding, before rotating in another peer to the unchoke set.
			seeding_piece_quota,

			// ``max_rejects`` is the number of piece requests we will reject in a
			// row while a peer is choked before the peer is considered abusive
			// and is disconnected.
			max_rejects,

			// specifies the buffer sizes set on peer sockets. 0 means the OS
			// default (i.e. don't change the buffer sizes).
			// The socket buffer sizes are changed using setsockopt() with
			// SOL_SOCKET/SO_RCVBUF and SO_SNDBUFFER.
			recv_socket_buffer_size,
			send_socket_buffer_size,

			// the max number of bytes a single peer connection's receive buffer is
			// allowed to grow to.
			max_peer_recv_buffer_size,

#if TORRENT_ABI_VERSION == 1
			// ``file_checks_delay_per_block`` is the number of milliseconds to
			// sleep in between disk read operations when checking torrents.
			// This can be set to higher numbers to slow down the
			// rate at which data is read from the disk while checking. This may
			// be useful for background tasks that doesn't matter if they take a
			// bit longer, as long as they leave disk I/O time for other
			// processes.
			file_checks_delay_per_block TORRENT_DEPRECATED_ENUM,
#else
			deprecated_file_checks_delay_per_block,
#endif

			// ``read_cache_line_size`` is the number of blocks to read into the
			// read cache when a read cache miss occurs. Setting this to 0 is
			// essentially the same thing as disabling read cache. The number of
			// blocks read into the read cache is always capped by the piece
			// boundary.
			//
			// When a piece in the write cache has ``write_cache_line_size``
			// contiguous blocks in it, they will be flushed. Setting this to 1
			// effectively disables the write cache.
			read_cache_line_size,
			write_cache_line_size,

			// ``optimistic_disk_retry`` is the number of seconds from a disk
			// write errors occur on a torrent until libtorrent will take it out
			// of the upload mode, to test if the error condition has been fixed.
			//
			// libtorrent will only do this automatically for auto managed
			// torrents.
			//
			// You can explicitly take a torrent out of upload only mode using
			// set_upload_mode().
			optimistic_disk_retry,

			// ``max_suggest_pieces`` is the max number of suggested piece indices
			// received from a peer that's remembered. If a peer floods suggest
			// messages, this limit prevents libtorrent from using too much RAM.
			max_suggest_pieces,

			// ``local_service_announce_interval`` is the time between local
			// network announces for a torrent.
			// This interval is specified in seconds.
			local_service_announce_interval,

			// ``dht_announce_interval`` is the number of seconds between
			// announcing torrents to the distributed hash table (DHT).
			dht_announce_interval,

			// ``udp_tracker_token_expiry`` is the number of seconds libtorrent
			// will keep UDP tracker connection tokens around for. This is
			// specified to be 60 seconds. The higher this
			// value is, the fewer packets have to be sent to the UDP tracker. In
			// order for higher values to work, the tracker needs to be configured
			// to match the expiration time for tokens.
			udp_tracker_token_expiry,

#if TORRENT_ABI_VERSION == 1
			// ``default_cache_min_age`` is the minimum number of seconds any read
			// cache line is kept in the cache. This
			// may be greater if ``guided_read_cache`` is enabled. Having a lower
			// bound on the time a cache line stays in the cache is an attempt
			// to avoid swapping the same pieces in and out of the cache in case
			// there is a shortage of spare cache space.
			default_cache_min_age TORRENT_DEPRECATED_ENUM,
#else
			deprecated_default_cache_min_age,
#endif

			// ``num_optimistic_unchoke_slots`` is the number of optimistic
			// unchoke slots to use.
			// Having a higher number of optimistic unchoke slots mean you will
			// find the good peers faster but with the trade-off to use up more
			// bandwidth. 0 means automatic, where libtorrent opens up 20% of your
			// allowed upload slots as optimistic unchoke slots.
			num_optimistic_unchoke_slots,

#if TORRENT_ABI_VERSION == 1
			// ``default_est_reciprocation_rate`` is the assumed reciprocation
			// rate from peers when using the BitTyrant choker. If set too high,
			// you will over-estimate your peers and be
			// more altruistic while finding the true reciprocation rate, if it's
			// set too low, you'll be too stingy and waste finding the true
			// reciprocation rate.
			//
			// ``increase_est_reciprocation_rate`` specifies how many percent the
			// estimated reciprocation rate should be increased by each unchoke
			// interval a peer is still choking us back.
			// This only applies to the BitTyrant choker.
			//
			// ``decrease_est_reciprocation_rate`` specifies how many percent the
			// estimated reciprocation rate should be decreased by each unchoke
			// interval a peer unchokes us. This only applies
			// to the BitTyrant choker.
			default_est_reciprocation_rate TORRENT_DEPRECATED_ENUM,
			increase_est_reciprocation_rate TORRENT_DEPRECATED_ENUM,
			decrease_est_reciprocation_rate TORRENT_DEPRECATED_ENUM,
#else
			deprecated_default_est_reciprocation_rate,
			deprecated_increase_est_reciprocation_rate,
			deprecated_decrease_est_reciprocation_rate,
#endif

			// the max number of peers we accept from pex messages from a single
			// peer. this limits the number of concurrent peers any of our peers
			// claims to be connected to. If they claim to be connected to more
			// than this, we'll ignore any peer that exceeds this limit
			max_pex_peers,

			// ``tick_interval`` specifies the number of milliseconds between
			// internal ticks. This is the frequency with which bandwidth quota is
			// distributed to peers. It should not be more than one second (i.e.
			// 1000 ms). Setting this to a low value (around 100) means higher
			// resolution bandwidth quota distribution, setting it to a higher
			// value saves CPU cycles.
			tick_interval,

			// ``share_mode_target`` specifies the target share ratio for share
			// mode torrents. If set to 3, we'll try to upload 3
			// times as much as we download. Setting this very high, will make it
			// very conservative and you might end up not downloading anything
			// ever (and not affecting your share ratio). It does not make any
			// sense to set this any lower than 2. For instance, if only 3 peers
			// need to download the rarest piece, it's impossible to download a
			// single piece and upload it more than 3 times. If the
			// share_mode_target is set to more than 3, nothing is downloaded.
			share_mode_target,

			// ``upload_rate_limit`` and ``download_rate_limit`` sets
			// the session-global limits of upload and download rate limits, in
			// bytes per second. By default peers on the local network are not rate
			// limited.
			//
			// A value of 0 means unlimited.
			//
			// For fine grained control over rate limits, including making them apply
			// to local peers, see peer-classes_.
			upload_rate_limit,
			download_rate_limit,
#if TORRENT_ABI_VERSION == 1
			local_upload_rate_limit TORRENT_DEPRECATED_ENUM,
			local_download_rate_limit TORRENT_DEPRECATED_ENUM,
#else
			deprecated_local_upload_rate_limit,
			deprecated_local_download_rate_limit,
#endif

			// the number of bytes per second (on average) the DHT is allowed to send.
			// If the incoming requests causes to many bytes to be sent in responses,
			// incoming requests will be dropped until the quota has been replenished.
			dht_upload_rate_limit,

			// ``unchoke_slots_limit`` is the max number of unchoked peers in the
			// session. The number of unchoke slots may be ignored depending on
			// what ``choking_algorithm`` is set to. Setting this limit to -1
			// means unlimited, i.e. all peers will always be unchoked.
			unchoke_slots_limit,

#if TORRENT_ABI_VERSION == 1
			// ``half_open_limit`` sets the maximum number of half-open
			// connections libtorrent will have when connecting to peers. A
			// half-open connection is one where connect() has been called, but
			// the connection still hasn't been established (nor failed). Windows
			// XP Service Pack 2 sets a default, system wide, limit of the number
			// of half-open connections to 10. So, this limit can be used to work
			// nicer together with other network applications on that system. The
			// default is to have no limit, and passing -1 as the limit, means to
			// have no limit. When limiting the number of simultaneous connection
			// attempts, peers will be put in a queue waiting for their turn to
			// get connected.
			half_open_limit TORRENT_DEPRECATED_ENUM,
#else
			deprecated_half_open_limit,
#endif

			// ``connections_limit`` sets a global limit on the number of
			// connections opened. The number of connections is set to a hard
			// minimum of at least two per torrent, so if you set a too low
			// connections limit, and open too many torrents, the limit will not
			// be met.
			connections_limit,

			// ``connections_slack`` is the number of incoming connections
			// exceeding the connection limit to accept in order to potentially
			// replace existing ones.
			connections_slack,

			// ``utp_target_delay`` is the target delay for uTP sockets in
			// milliseconds. A high value will make uTP connections more
			// aggressive and cause longer queues in the upload bottleneck. It
			// cannot be too low, since the noise in the measurements would cause
			// it to send too slow.
			// ``utp_gain_factor`` is the number of bytes the uTP congestion
			// window can increase at the most in one RTT.
			// If this is set too high, the congestion controller reacts
			// too hard to noise and will not be stable, if it's set too low, it
			// will react slow to congestion and not back off as fast.
			//
			// ``utp_min_timeout`` is the shortest allowed uTP socket timeout,
			// specified in milliseconds. The
			// timeout depends on the RTT of the connection, but is never smaller
			// than this value. A connection times out when every packet in a
			// window is lost, or when a packet is lost twice in a row (i.e. the
			// resent packet is lost as well).
			//
			// The shorter the timeout is, the faster the connection will recover
			// from this situation, assuming the RTT is low enough.
			// ``utp_syn_resends`` is the number of SYN packets that are sent (and
			// timed out) before giving up and closing the socket.
			// ``utp_num_resends`` is the number of times a packet is sent (and
			// lost or timed out) before giving up and closing the connection.
			// ``utp_connect_timeout`` is the number of milliseconds of timeout
			// for the initial SYN packet for uTP connections. For each timed out
			// packet (in a row), the timeout is doubled. ``utp_loss_multiplier``
			// controls how the congestion window is changed when a packet loss is
			// experienced. It's specified as a percentage multiplier for
			// ``cwnd``. Do not change this value unless you know what you're doing.
			// Never set it higher than 100.
			utp_target_delay,
			utp_gain_factor,
			utp_min_timeout,
			utp_syn_resends,
			utp_fin_resends,
			utp_num_resends,
			utp_connect_timeout,
#if TORRENT_ABI_VERSION == 1
			utp_delayed_ack TORRENT_DEPRECATED_ENUM,
#else
			deprecated_utp_delayed_ack,
#endif
			utp_loss_multiplier,

			// The ``mixed_mode_algorithm`` determines how to treat TCP
			// connections when there are uTP connections. Since uTP is designed
			// to yield to TCP, there's an inherent problem when using swarms that
			// have both TCP and uTP connections. If nothing is done, uTP
			// connections would often be starved out for bandwidth by the TCP
			// connections. This mode is ``prefer_tcp``. The ``peer_proportional``
			// mode simply looks at the current throughput and rate limits all TCP
			// connections to their proportional share based on how many of the
			// connections are TCP. This works best if uTP connections are not
			// rate limited by the global rate limiter (which they aren't by
			// default).
			mixed_mode_algorithm,

			// ``listen_queue_size`` is the value passed in to listen() for the
			// listen socket. It is the number of outstanding incoming connections
			// to queue up while we're not actively waiting for a connection to be
			// accepted. 5 should be sufficient for any
			// normal client. If this is a high performance server which expects
			// to receive a lot of connections, or used in a simulator or test, it
			// might make sense to raise this number. It will not take affect
			// until the ``listen_interfaces`` settings is updated.
			listen_queue_size,

			// ``torrent_connect_boost`` is the number of peers to try to connect
			// to immediately when the first tracker response is received for a
			// torrent. This is a boost to given to new torrents to accelerate
			// them starting up. The normal connect scheduler is run once every
			// second, this allows peers to be connected immediately instead of
			// waiting for the session tick to trigger connections.
			// This may not be set higher than 255.
			torrent_connect_boost,

			// ``alert_queue_size`` is the maximum number of alerts queued up
			// internally. If alerts are not popped, the queue will eventually
			// fill up to this level. Once the alert queue is full, additional
			// alerts will be dropped, and not delivered to the client. Once the
			// client drains the queue, new alerts may be delivered again. In order
			// to know that alerts have been dropped, see
			// session_handle::dropped_alerts().
			alert_queue_size,

			// ``max_metadata_size`` is the maximum allowed size (in bytes) to be
			// received by the metadata extension, i.e. magnet links.
			max_metadata_size,

			// ``hashing_threads`` is the number of disk I/O threads to use for
			// piece hash verification. These threads are *in addition* to the
			// regular disk I/O threads specified by settings_pack::aio_threads.
			// The hasher threads do not only compute hashes, but also perform
			// the read from disk. On storage optimal for sequential access,
			// such as hard drives, this setting should probably be set to 1.
			hashing_threads,

			// the number of blocks to keep outstanding at any given time when
			// checking torrents. Higher numbers give faster re-checks but uses
			// more memory. Specified in number of 16 kiB blocks
			checking_mem_usage,

			// if set to > 0, pieces will be announced to other peers before they
			// are fully downloaded (and before they are hash checked). The
			// intention is to gain 1.5 potential round trip times per downloaded
			// piece. When non-zero, this indicates how many milliseconds in
			// advance pieces should be announced, before they are expected to be
			// completed.
			predictive_piece_announce,

			// for some aio back-ends, ``aio_threads`` specifies the number of
			// io-threads to use.
			aio_threads,

#if TORRENT_ABI_VERSION == 1
			// for some aio back-ends, ``aio_max`` specifies the max number of
			// outstanding jobs.
			aio_max TORRENT_DEPRECATED_ENUM,

			// .. note:: This is not implemented
			//
			// ``network_threads`` is the number of threads to use to call
			// ``async_write_some`` (i.e. send) on peer connection sockets. When
			// seeding at extremely high rates, this may become a bottleneck, and
			// setting this to 2 or more may parallelize that cost. When using SSL
			// torrents, all encryption for outgoing traffic is done within the
			// socket send functions, and this will help parallelizing the cost of
			// SSL encryption as well.
			network_threads TORRENT_DEPRECATED_ENUM,

			// ``ssl_listen`` sets the listen port for SSL connections. If this is
			// set to 0, no SSL listen port is opened. Otherwise a socket is
			// opened on this port. This setting is only taken into account when
			// opening the regular listen port, and won't re-open the listen
			// socket simply by changing this setting.
			ssl_listen TORRENT_DEPRECATED_ENUM,
#else
			// hidden
			deprecated_aio_max,
			deprecated_network_threads,
			deprecated_ssl_listen,
#endif

			// ``tracker_backoff`` determines how aggressively to back off from
			// retrying failing trackers. This value determines *x* in the
			// following formula, determining the number of seconds to wait until
			// the next retry:
			//
			//    delay = 5 + 5 * x / 100 * fails^2
			//
			// This setting may be useful to make libtorrent more or less
			// aggressive in hitting trackers.
			tracker_backoff,

			// when a seeding torrent reaches either the share ratio (bytes up /
			// bytes down) or the seed time ratio (seconds as seed / seconds as
			// downloader) or the seed time limit (seconds as seed) it is
			// considered done, and it will leave room for other torrents. These
			// are specified as percentages. Torrents that are considered done will
			// still be allowed to be seeded, they just won't have priority anymore.
			// For more, see queuing_.
			share_ratio_limit,
			seed_time_ratio_limit,

			// peer_turnover is the percentage of peers to disconnect every
			// turnover peer_turnover_interval (if we're at the peer limit), this
			// is specified in percent when we are connected to more than limit *
			// peer_turnover_cutoff peers disconnect peer_turnover fraction of the
			// peers. It is specified in percent peer_turnover_interval is the
			// interval (in seconds) between optimistic disconnects if the
			// disconnects happen and how many peers are disconnected is
			// controlled by peer_turnover and peer_turnover_cutoff
			peer_turnover,
			peer_turnover_cutoff,
			peer_turnover_interval,

			// this setting controls the priority of downloading torrents over
			// seeding or finished torrents when it comes to making peer
			// connections. Peer connections are throttled by the connection_speed
			// and the half-open connection limit. This makes peer connections a
			// limited resource. Torrents that still have pieces to download are
			// prioritized by default, to avoid having many seeding torrents use
			// most of the connection attempts and only give one peer every now
			// and then to the downloading torrent. libtorrent will loop over the
			// downloading torrents to connect a peer each, and every n:th
			// connection attempt, a finished torrent is picked to be allowed to
			// connect to a peer. This setting controls n.
			connect_seed_every_n_download,

			// the max number of bytes to allow an HTTP response to be when
			// announcing to trackers or downloading .torrent files via the
			// ``url`` provided in ``add_torrent_params``.
			max_http_recv_buffer_size,

			// if binding to a specific port fails, should the port be incremented
			// by one and tried again? This setting specifies how many times to
			// retry a failed port bind
			max_retry_port_bind,

			// a bitmask combining flags from alert_category_t defining which
			// kinds of alerts to receive
			alert_mask,

			// control the settings for incoming and outgoing connections
			// respectively. see enc_policy enum for the available options.
			// Keep in mind that protocol encryption degrades performance in
			// several respects:
			//
			// 1. It prevents "zero copy" disk buffers being sent to peers, since
			//    each peer needs to mutate the data (i.e. encrypt it) the data
			//    must be copied per peer connection rather than sending the same
			//    buffer to multiple peers.
			// 2. The encryption itself requires more CPU than plain bittorrent
			//    protocol. The highest cost is the Diffie Hellman exchange on
			//    connection setup.
			// 3. The encryption handshake adds several round-trips to the
			//    connection setup, and delays transferring data.
			out_enc_policy,
			in_enc_policy,

			// determines the encryption level of the connections. This setting
			// will adjust which encryption scheme is offered to the other peer,
			// as well as which encryption scheme is selected by the client. See
			// enc_level enum for options.
			allowed_enc_level,

			// the download and upload rate limits for a torrent to be considered
			// active by the queuing mechanism. A torrent whose download rate is
			// less than ``inactive_down_rate`` and whose upload rate is less than
			// ``inactive_up_rate`` for ``auto_manage_startup`` seconds, is
			// considered inactive, and another queued torrent may be started.
			// This logic is disabled if ``dont_count_slow_torrents`` is false.
			inactive_down_rate,
			inactive_up_rate,

			// proxy to use. see proxy_type_t.
			proxy_type,

			// the port of the proxy server
			proxy_port,

			// sets the i2p_ SAM bridge port to connect to. set the hostname with
			// the ``i2p_hostname`` setting.
			//
			// .. _i2p: http://www.i2p2.de
			i2p_port,

#if TORRENT_ABI_VERSION == 1
			// this determines the max number of volatile disk cache blocks. If the
			// number of volatile blocks exceed this limit, other volatile blocks
			// will start to be evicted. A disk cache block is volatile if it has
			// low priority, and should be one of the first blocks to be evicted
			// under pressure. For instance, blocks pulled into the cache as the
			// result of calculating a piece hash are volatile. These blocks don't
			// represent potential interest among peers, so the value of keeping
			// them in the cache is limited.
			cache_size_volatile,
#else
			deprecated_cache_size_volatile,
#endif

			// The maximum request range of an url seed in bytes. This value
			// defines the largest possible sequential web seed request. Lower values
			// are possible but will be ignored if they are lower then piece size.
			// This value should be related to your download speed to prevent
			// libtorrent from creating too many expensive http requests per
			// second. You can select a value as high as you want but keep in mind
			// that libtorrent can't create parallel requests if the first request
			// did already select the whole file.
			// If you combine bittorrent seeds with web seeds and pick strategies
			// like rarest first you may find your web seed requests split into
			// smaller parts because we don't download already picked pieces
			// twice.
			urlseed_max_request_bytes,

			// time to wait until a new retry of a web seed name lookup
			web_seed_name_lookup_retry,

			// the number of seconds between closing the file opened the longest
			// ago. 0 means to disable the feature. The purpose of this is to
			// periodically close files to trigger the operating system flushing
			// disk cache. Specifically it has been observed to be required on
			// windows to not have the disk cache grow indefinitely.
			// This defaults to 120 seconds on windows, and disabled on other
			// systems.
			close_file_interval,

			// When uTP experiences packet loss, it will reduce the congestion
			// window, and not reduce it again for this many milliseconds, even if
			// experiencing another lost packet.
			utp_cwnd_reduce_timer,

			// the max number of web seeds to have connected per torrent at any
			// given time.
			max_web_seed_connections,

			// the number of seconds before the internal host name resolver
			// considers a cache value timed out, negative values are interpreted
			// as zero.
			resolver_cache_timeout,

			// specify the not-sent low watermark for socket send buffers. This
			// corresponds to the, Linux-specific, ``TCP_NOTSENT_LOWAT`` TCP socket
			// option.
			send_not_sent_low_watermark,

			// the rate based choker compares the upload rate to peers against a
			// threshold that increases proportionally by its size for every
			// peer it visits, visiting peers in decreasing upload rate. The
			// number of upload slots is determined by the number of peers whose
			// upload rate exceeds the threshold. This option sets the start
			// value for this threshold. A higher value leads to fewer unchoke
			// slots, a lower value leads to more.
			rate_choker_initial_threshold,

			// The expiration time of UPnP port-mappings, specified in seconds. 0
			// means permanent lease. Some routers do not support expiration times
			// on port-maps (nor correctly returning an error indicating lack of
			// support). In those cases, set this to 0. Otherwise, don't set it any
			// lower than 5 minutes.
			upnp_lease_duration,

			// limits the number of concurrent HTTP tracker announces. Once the
			// limit is hit, tracker requests are queued and issued when an
			// outstanding announce completes.
			max_concurrent_http_announces,

			// the maximum number of peers to send in a reply to ``get_peers``
			dht_max_peers_reply,

			// the number of concurrent search request the node will send when
			// announcing and refreshing the routing table. This parameter is called
			// alpha in the kademlia paper
			dht_search_branching,

			// the maximum number of failed tries to contact a node before it is
			// removed from the routing table. If there are known working nodes that
			// are ready to replace a failing node, it will be replaced immediately,
			// this limit is only used to clear out nodes that don't have any node
			// that can replace them.
			dht_max_fail_count,

			// the total number of torrents to track from the DHT. This is simply an
			// upper limit to make sure malicious DHT nodes cannot make us allocate
			// an unbounded amount of memory.
			dht_max_torrents,

			// max number of items the DHT will store
			dht_max_dht_items,

			// the max number of peers to store per torrent (for the DHT)
			dht_max_peers,

			// the max number of torrents to return in a torrent search query to the
			// DHT
			dht_max_torrent_search_reply,

			// the number of seconds a DHT node is banned if it exceeds the rate
			// limit. The rate limit is averaged over 10 seconds to allow for bursts
			// above the limit.
			dht_block_timeout,

			// the max number of packets per second a DHT node is allowed to send
			// without getting banned.
			dht_block_ratelimit,

			// the number of seconds a immutable/mutable item will be expired.
			// default is 0, means never expires.
			dht_item_lifetime,

			// the info-hashes sample recomputation interval (in seconds).
			// The node will precompute a subset of the tracked info-hashes and return
			// that instead of calculating it upon each request. The permissible range
			// is between 0 and 21600 seconds (inclusive).
			dht_sample_infohashes_interval,

			// the maximum number of elements in the sampled subset of info-hashes.
			// If this number is too big, expect the DHT storage implementations
			// to clamp it in order to allow UDP packets go through
			dht_max_infohashes_sample_count,

			// ``max_piece_count`` is the maximum allowed number of pieces in
			// metadata received via magnet links. Loading large torrents (with
			// more pieces than the default limit) may also require passing in
			// a higher limit to read_resume_data() and
			// torrent_info::parse_info_section(), if those are used.
			max_piece_count,

			max_int_setting_internal
		};

		// hidden
		constexpr static int num_string_settings = int(max_string_setting_internal) - int(string_type_base);
		constexpr static int num_bool_settings = int(max_bool_setting_internal) - int(bool_type_base);
		constexpr static int num_int_settings = int(max_int_setting_internal) - int(int_type_base);

		enum suggest_mode_t : std::uint8_t { no_piece_suggestions = 0, suggest_read_cache = 1 };

		enum choking_algorithm_t : std::uint8_t
		{
			// This is the traditional choker with a fixed number of unchoke
			// slots (as specified by settings_pack::unchoke_slots_limit).
			fixed_slots_choker = 0,

			// This opens up unchoke slots based on the upload rate achieved to
			// peers. The more slots that are opened, the marginal upload rate
			// required to open up another slot increases. Configure the initial
			// threshold with settings_pack::rate_choker_initial_threshold.
			//
			// For more information, see `rate based choking`_.
			rate_based_choker = 2,
#if TORRENT_ABI_VERSION == 1
			bittyrant_choker TORRENT_DEPRECATED_ENUM = 3
#else
			deprecated_bittyrant_choker = 3
#endif
		};

		enum seed_choking_algorithm_t : std::uint8_t
		{
			// which round-robins the peers that are unchoked
			// when seeding. This distributes the upload bandwidth uniformly and
			// fairly. It minimizes the ability for a peer to download everything
			// without redistributing it.
			round_robin,

			// unchokes the peers we can send to the fastest. This might be a
			// bit more reliable in utilizing all available capacity.
			fastest_upload,

			// prioritizes peers who have just started or are
			// just about to finish the download. The intention is to force
			// peers in the middle of the download to trade with each other.
			// This does not just take into account the pieces a peer is
			// reporting having downloaded, but also the pieces we have sent
			// to it.
			anti_leech
		};

		enum io_buffer_mode_t : std::uint8_t
		{
			enable_os_cache = 0,
#if TORRENT_ABI_VERSION == 1
			disable_os_cache_for_aligned_files TORRENT_DEPRECATED_ENUM = 2,
#else
			deprecated_disable_os_cache_for_aligned_files = 1,
#endif
			disable_os_cache = 2
		};

		enum bandwidth_mixed_algo_t : std::uint8_t
		{
			// disables the mixed mode bandwidth balancing
			prefer_tcp = 0,

			// does not throttle uTP, throttles TCP to the same proportion
			// of throughput as there are TCP connections
			peer_proportional = 1
		};

		// the encoding policy options for use with
		// settings_pack::out_enc_policy and settings_pack::in_enc_policy.
		enum enc_policy : std::uint8_t
		{
			// Only encrypted connections are allowed. Incoming connections that
			// are not encrypted are closed and if the encrypted outgoing
			// connection fails, a non-encrypted retry will not be made.
			pe_forced,

			// encrypted connections are enabled, but non-encrypted connections
			// are allowed. An incoming non-encrypted connection will be accepted,
			// and if an outgoing encrypted connection fails, a non- encrypted
			// connection will be tried.
			pe_enabled,

			// only non-encrypted connections are allowed.
			pe_disabled
		};

		// the encryption levels, to be used with
		// settings_pack::allowed_enc_level.
		enum enc_level : std::uint8_t
		{
			// use only plain text encryption
			pe_plaintext = 1,
			// use only RC4 encryption
			pe_rc4 = 2,
			// allow both
			pe_both = 3
		};

		enum proxy_type_t : std::uint8_t
		{
			// No proxy server is used and all other fields are ignored.
			none,

			// The server is assumed to be a `SOCKS4 server`_ that requires a
			// username.
			//
			// .. _`SOCKS4 server`: http://www.ufasoft.com/doc/socks4_protocol.htm
			socks4,

			// The server is assumed to be a SOCKS5 server (`RFC 1928`_) that does
			// not require any authentication. The username and password are
			// ignored.
			//
			// .. _`RFC 1928`: http://www.faqs.org/rfcs/rfc1928.html
			socks5,

			// The server is assumed to be a SOCKS5 server that supports plain
			// text username and password authentication (`RFC 1929`_). The
			// username and password specified may be sent to the proxy if it
			// requires.
			//
			// .. _`RFC 1929`: http://www.faqs.org/rfcs/rfc1929.html
			socks5_pw,

			// The server is assumed to be an HTTP proxy. If the transport used
			// for the connection is non-HTTP, the server is assumed to support
			// the CONNECT_ method. i.e. for web seeds and HTTP trackers, a plain
			// proxy will suffice. The proxy is assumed to not require
			// authorization. The username and password will not be used.
			//
			// .. _CONNECT: http://tools.ietf.org/html/draft-luotonen-web-proxy-tunneling-01
			http,

			// The server is assumed to be an HTTP proxy that requires user
			// authorization. The username and password will be sent to the proxy.
			http_pw,

			// route through a i2p SAM proxy
			i2p_proxy
		};
	private:

		std::vector<std::pair<std::uint16_t, std::string>> m_strings;
		std::vector<std::pair<std::uint16_t, int>> m_ints;
		std::vector<std::pair<std::uint16_t, bool>> m_bools;
	};
}

#endif
