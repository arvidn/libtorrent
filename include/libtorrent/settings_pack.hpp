/*

Copyright (c) 2012-2016, Arvid Norberg
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
#include <vector>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/smart_ptr.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

// OVERVIEW
// 
// You have some control over session configuration through the session::apply_settings()
// member function. To change one or more configuration options, create a settings_pack.
// object and fill it with the settings to be set and pass it in to session::apply_settings().
// 
// You have control over proxy and authorization settings and also the user-agent
// that will be sent to the tracker. The user-agent will also be used to identify the
// client with other peers.
// 
namespace libtorrent
{
	namespace aux { struct session_impl; struct session_settings; }

	struct settings_pack;
	struct bdecode_node;

	TORRENT_EXTRA_EXPORT boost::shared_ptr<settings_pack> load_pack_from_dict(bdecode_node const& settings);
	TORRENT_EXTRA_EXPORT void save_settings_to_dict(aux::session_settings const& s, entry::dictionary_type& sett);
	TORRENT_EXTRA_EXPORT void apply_pack(settings_pack const* pack, aux::session_settings& sett, aux::session_impl* ses = 0);

	TORRENT_EXPORT int setting_by_name(std::string const& name);
	TORRENT_EXPORT char const* name_for_setting(int s);

#ifndef TORRENT_NO_DEPRECATE
	struct session_settings;
	boost::shared_ptr<settings_pack> load_pack_from_struct(aux::session_settings const& current, session_settings const& s);
	void load_struct_from_settings(aux::session_settings const& current, session_settings& ret);
#endif

	// The ``settings_pack`` struct, contains the names of all settings as
	// enum values. These values are passed in to the ``set_str()``,
	// ``set_int()``, ``set_bool()`` functions, to specify the setting to
	// change.
	//	
	// These are the available settings:
	// 
	// .. include:: settings-ref.rst
	//
	struct TORRENT_EXPORT settings_pack
	{
		friend void apply_pack(settings_pack const* pack, aux::session_settings& sett, aux::session_impl* ses);

		void set_str(int name, std::string val);
		void set_int(int name, int val);
		void set_bool(int name, bool val);
		bool has_val(int name) const;
		void clear();

		std::string get_str(int name) const;
		int get_int(int name) const;
		bool get_bool(int name) const;

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

		enum string_types
		{
			// this is the client identification to the tracker. The recommended
			// format of this string is: "ClientName/ClientVersion
			// libtorrent/libtorrentVersion". This name will not only be used when
			// making HTTP requests, but also when sending extended headers to
			// peers that support that extension. It may not contain \r or \n
			user_agent = string_type_base,

			// ``announce_ip`` is the ip address passed along to trackers as the
			// ``&ip=`` parameter. If left as the default, that parameter is
			// omitted.
			announce_ip,

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
			// Since this setting sets a hard upper limit on cache usage, it
			// cannot be combined with
			// ``settings_pack::contiguous_recv_buffer``, since that feature
			// treats the ``cache_size`` setting as a soft (but still pretty hard)
			// limit. The result of combining the two is peers being disconnected
			// after failing to allocate more disk buffers.
			// 
			// This feature requires the ``mmap`` system call, on systems that
			// don't have ``mmap`` this setting is ignored.
			mmap_cache,

			// this is the client name and version identifier sent to peers in the
			// handshake message. If this is an empty string, the user_agent is
			// used instead
			handshake_client_version,

			// sets the network interface this session will use when it opens
			// outgoing connections. By default, it binds outgoing connections to
			// INADDR_ANY and port 0 (i.e. let the OS decide). Ths parameter must
			// be a string containing one or more, comma separated, adapter names.
			// Adapter names on unix systems are of the form "eth0", "eth1",
			// "tun0", etc. When specifying multiple interfaces, they will be
			// assigned in round-robin order. This may be useful for clients that
			// are multi-homed. Binding an outgoing connection to a local IP does
			// not necessarily make the connection via the associated NIC/Adapter.
			// Setting this to an empty string will disable binding of outgoing
			// connections.
			outgoing_interfaces,

			// a comma-separated list of IP port-pairs. These
			// are the listen ports that will be opened for accepting incoming uTP
			// and TCP connections. It is possible to listen on multiple
			// IPs and multiple ports. Binding to port 0 will make the
			// operating system pick the port. The default is "0.0.0.0:6881", which
			// binds to all interfaces on port 6881.
			//
			// if binding fails, the listen_failed_alert is posted, potentially
			// more than once. Once/if binding the listen socket(s) succeed,
			// listen_succeeded_alert is posted.
			//
			// Each port will attempt to open both a UDP and a TCP listen socket,
			// to allow accepting uTP connections as well as TCP. If using the DHT,
			// this will also make the DHT use the same UDP ports.
			// 
			// .. note::
			//   The current support for opening arbitrary UDP sockets is limited.
			//   In this version of libtorrent, there will only ever be two UDP
			//   sockets, one for IPv4 and one for IPv6.
			listen_interfaces,

			// when using a poxy, this is the hostname where the proxy is running
			// see proxy_type.
			proxy_hostname,

			// when using a proxy, these are the credentials (if any) to use whne
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
			// used as the peer-id
			peer_fingerprint,

			max_string_setting_internal
		};

		enum bool_types
		{
			// determines if connections from the same IP address as existing
			// connections should be rejected or not. Multiple connections from
			// the same IP address is not allowed by default, to prevent abusive
			// behavior by peers. It may be useful to allow such connections in
			// cases where simulations are run on the same machie, and all peers
			// in a swarm has the same IP address.
			allow_multiple_connections_per_ip = bool_type_base,

			// if set to true, upload, download and unchoke limits are ignored for
			// peers on the local network. This option is *DEPRECATED*, please use
			// set_peer_class_filter() instead.
#ifndef TORRENT_NO_DEPRECATE
			ignore_limits_on_local_network,
#else
			deprecated1,
#endif

			// ``send_redundant_have`` controls if have messages will be sent to
			// peers that already have the piece. This is typically not necessary,
			// but it might be necessary for collecting statistics in some cases.
			// Default is false.
			send_redundant_have,

			// if this is true, outgoing bitfields will never be fuil. If the
			// client is seed, a few bits will be set to 0, and later filled in
			// with have messages. This is to prevent certain ISPs from stopping
			// people from seeding.
			lazy_bitfields,

			// ``use_dht_as_fallback`` determines how the DHT is used. If this is
			// true, the DHT will only be used for torrents where all trackers in
			// its tracker list has failed. Either by an explicit error message or
			// a time out. This is false by default, which means the DHT is used
			// by default regardless of if the trackers fail or not.
			use_dht_as_fallback,

			// ``upnp_ignore_nonrouters`` indicates whether or not the UPnP
			// implementation should ignore any broadcast response from a device
			// whose address is not the configured router for this machine. i.e.
			// it's a way to not talk to other people's routers by mistake.
			upnp_ignore_nonrouters,

			// ``use_parole_mode`` specifies if parole mode should be used. Parole
			// mode means that peers that participate in pieces that fail the hash
			// check are put in a mode where they are only allowed to download
			// whole pieces. If the whole piece a peer in parole mode fails the
			// hash check, it is banned. If a peer participates in a piece that
			// passes the hash check, it is taken out of parole mode.
			use_parole_mode,

			// enable and disable caching of blocks read from disk. the purpose of
			// the read cache is partly read-ahead of requests but also to avoid
			// reading blocks back from the disk multiple times for popular
			// pieces.
			use_read_cache,
#ifndef TORRENT_NO_DEPRECATE
			use_write_cache,
#else
			deprecated7,
#endif

			// this will make the disk cache never flush a write piece if it would
			// cause is to have to re-read it once we want to calculate the piece
			// hash
			dont_flush_write_cache,

#ifndef TORRENT_NO_DEPRECATE
			// ``explicit_read_cache`` defaults to 0. If set to something greater
			// than 0, the disk read cache will not be evicted by cache misses and
			// will explicitly be controlled based on the rarity of pieces. Rare
			// pieces are more likely to be cached. This would typically be used
			// together with ``suggest_mode`` set to ``suggest_read_cache``. The
			// value is the number of pieces to keep in the read cache. If the
			// actual read cache can't fit as many, it will essentially be
			// clamped.
			explicit_read_cache,
#else
			deprecated10,
#endif

			// allocate separate, contiguous, buffers for read and write calls.
			// Only used where writev/readv cannot be used will use more RAM but
			// may improve performance
			coalesce_reads,
			coalesce_writes,

			// prefer seeding torrents when determining which torrents to give
			// active slots to, the default is false which gives preference to
			// downloading torrents
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
			// behavior is as defined by the multi tracker specification. It
			// defaults to false, which is the same behavior previous versions of
			// libtorrent has had as well.
			// 
			// ``announce_to_all_tiers`` also controls how multi tracker torrents
			// are treated. When this is set to true, one tracker from each tier
			// is announced to. This is the uTorrent behavior. This is false by
			// default in order to comply with the multi-tracker specification.
			announce_to_all_tiers,
			announce_to_all_trackers,

			// ``prefer_udp_trackers`` is true by default. It means that trackers
			// may be rearranged in a way that udp trackers are always tried
			// before http trackers for the same hostname. Setting this to false
			// means that the trackers' tier is respected and there's no
			// preference of one protocol over another.
			prefer_udp_trackers,

			// ``strict_super_seeding`` when this is set to true, a piece has to
			// have been forwarded to a third peer before another one is handed
			// out. This is the traditional definition of super seeding.
			strict_super_seeding,

#ifndef TORRENT_NO_DEPRECATE
			// if this is set to true, the memory allocated for the disk cache
			// will be locked in physical RAM, never to be swapped out. Every time
			// a disk buffer is allocated and freed, there will be the extra
			// overhead of a system call.
			lock_disk_cache,
#else
			deprecated8,
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

			// ``low_prio_disk`` determines if the disk I/O should use a normal or
			// low priority policy. This defaults to true, which means that it's
			// low priority by default. Other processes doing disk I/O will
			// normally take priority in this mode. This is meant to improve the
			// overall responsiveness of the system while downloading in the
			// background. For high-performance server setups, this might not be
			// desirable.
			low_prio_disk,

			// ``volatile_read_cache``, if this is set to true, read cache blocks
			// that are hit by peer read requests are removed from the disk cache
			// to free up more space. This is useful if you don't expect the disk
			// cache to create any cache hits from other peers than the one who
			// triggered the cache line to be read into the cache in the first
			// place.
			volatile_read_cache,

			// ``guided_read_cache`` enables the disk cache to adjust the size of
			// a cache line generated by peers to depend on the upload rate you
			// are sending to that peer. The intention is to optimize the RAM
			// usage of the cache, to read ahead further for peers that you're
			// sending faster to.
			guided_read_cache,

			// ``no_atime_storage`` this is a linux-only option and passes in the
			// ``O_NOATIME`` to ``open()`` when opening files. This may lead to
			// some disk performance improvements.
			no_atime_storage,

			// ``incoming_starts_queued_torrents`` defaults to false. If a torrent
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

			// ``strict_end_game_mode`` defaults to true, and controls when a
			// block may be requested twice. If this is ``true``, a block may only
			// be requested twice when there's ay least one request to every piece
			// that's left to download in the torrent. This may slow down progress
			// on some pieces sometimes, but it may also avoid downloading a lot
			// of redundant bytes. If this is ``false``, libtorrent attempts to
			// use each peer connection to its max, by always requesting
			// something, even if it means requesting something that has been
			// requested from another peer already.
			strict_end_game_mode,

			// if ``broadcast_lsd`` is set to true, the local peer discovery (or
			// Local Service Discovery) will not only use IP multicast, but also
			// broadcast its messages. This can be useful when running on networks
			// that don't support multicast. Since broadcast messages might be
			// expensive and disruptive on networks, only every 8th announce uses
			// broadcast.
			broadcast_lsd,

			// when set to true, libtorrent will try to make outgoing utp
			// connections controls whether libtorrent will accept incoming
			// connections or make outgoing connections of specific type.
			enable_outgoing_utp,
			enable_incoming_utp,
			enable_outgoing_tcp,
			enable_incoming_tcp,

			// ``ignore_resume_timestamps`` determines if the storage, when
			// loading resume data files, should verify that the file modification
			// time with the timestamps in the resume data. This defaults to
			// false, which means timestamps are taken into account, and resume
			// data is less likely to accepted (torrents are more likely to be
			// fully checked when loaded). It might be useful to set this to true
			// if your network is faster than your disk, and it would be faster to
			// redownload potentially missed pieces than to go through the whole
			// storage to look for them.
			ignore_resume_timestamps,

			// ``no_recheck_incomplete_resume`` determines if the storage should
			// check the whole files when resume data is incomplete or missing or
			// whether it should simply assume we don't have any of the data. By
			// default, this is determined by the existence of any of the files.
			// By setting this setting to true, the files won't be checked, but
			// will go straight to download mode.
			no_recheck_incomplete_resume,

			// ``anonymous_mode`` defaults to false. When set to true, the client
			// tries to hide its identity to a certain degree. The peer-ID will no
			// longer include the client's fingerprint. The user-agent will be
			// reset to an empty string. Trackers will only be used if they are
			// using a proxy server. The listen sockets are closed, and incoming
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
			// tracker or not. Defaults to on. Turning it off also excludes web
			// seed traffic from other stats and download rate reporting via the
			// libtorrent API.
			report_web_seed_downloads,

			// set to true if uTP connections should be rate limited This option
			// is *DEPRECATED*, please use set_peer_class_filter() instead.
#ifndef TORRENT_NO_DEPRECATE
			rate_limit_utp,
#else
			deprecated2,
#endif

			// if this is true, the ``&ip=`` argument in tracker requests (unless
			// otherwise specified) will be set to the intermediate IP address if
			// the user is double NATed. If the user is not double NATed, this
			// option does not have an affect
			announce_double_nat,

			// ``seeding_outgoing_connections`` determines if seeding (and
			// finished) torrents should attempt to make outgoing connections or
			// not. By default this is true. It may be set to false in very
			// specific applications where the cost of making outgoing connections
			// is high, and there are no or small benefits of doing so. For
			// instance, if no nodes are behind a firewall or a NAT, seeds don't
			// need to make outgoing connections.
			seeding_outgoing_connections,

			// when this is true, libtorrent will not attempt to make outgoing
			// connections to peers whose port is < 1024. This is a safety
			// precaution to avoid being part of a DDoS attack
			no_connect_privileged_ports,

			// ``smooth_connects`` is true by default, which means the number of
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

			// ``apply_ip_filter_to_trackers`` defaults to true. It determines
			// whether the IP filter applies to trackers as well as peers. If this
			// is set to false, trackers are exempt from the IP filter (if there
			// is one). If no IP filter is set, this setting is irrelevant.
			apply_ip_filter_to_trackers,

			// ``use_disk_read_ahead`` defaults to true and will attempt to
			// optimize disk reads by giving the operating system heads up of disk
			// read requests as they are queued in the disk job queue.
			use_disk_read_ahead,

			// ``lock_files`` determines whether or not to lock files which
			// libtorrent is downloading to or seeding from. This is implemented
			// using ``fcntl(F_SETLK)`` on unix systems and by not passing in
			// ``SHARE_READ`` and ``SHARE_WRITE`` on windows. This might prevent
			// 3rd party processes from corrupting the files under libtorrent's
			// feet.
			lock_files,

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
			contiguous_recv_buffer,

			// when true, web seeds sending bad data will be banned
			ban_web_seeds,

			// when set to false, the ``write_cache_line_size`` will apply across
			// piece boundaries. this is a bad idea unless the piece picker also
			// is configured to have an affinity to pick pieces belonging to the
			// same write cache line as is configured in the disk cache.
			allow_partial_disk_writes,

			// If true, disables any communication that's not going over a proxy.
			// Enabling this requires a proxy to be configured as well, see
			// proxy_type and proxy_hostname settings. The listen sockets are
			// closed, and incoming connections will only be accepted through a
			// SOCKS5 or I2P proxy (if a peer proxy is set up and is run on the
			// same machine as the tracker proxy). This setting also disabled peer
			// country lookups, since those are done via DNS lookups that aren't
			// supported by proxies.
			force_proxy,

			// if false, prevents libtorrent to advertise share-mode support
			support_share_mode,

			// if this is false, don't advertise support for the Tribler merkle
			// tree piece message
			support_merkle_torrents,

			// if this is true, the number of redundant bytes is sent to the
			// tracker
			report_redundant_bytes,

			// if this is true, libtorrent will fall back to listening on a port
			// chosen by the operating system (i.e. binding to port 0). If a
			// failure is preferred, set this to false.
			listen_system_port_fallback,

			// ``use_disk_cache_pool`` enables using a pool allocator for disk
			// cache blocks. Enabling it makes the cache perform better at high
			// throughput. It also makes the cache less likely and slower at
			// returning memory back to the system, once allocated.
			use_disk_cache_pool,

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
			// broadcast the infohashes of all the non-private torrents on the
			// local network to look for peers on the same swarm within multicast
			// reach.
			enable_lsd,

			// starts the dht node and makes the trackerless service available to
			// torrents.
			enable_dht,

			// if the allowed encryption level is both, setting this to true will
			// prefer rc4 if both methods are offered, plaintext otherwise
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

			max_bool_setting_internal
		};

		enum int_types
		{
			// ``tracker_completion_timeout`` is the number of seconds the tracker
			// connection will wait from when it sent the request until it
			// considers the tracker to have timed-out. Default value is 60
			// seconds.
			tracker_completion_timeout = int_type_base,

			// ``tracker_receive_timeout`` is the number of seconds to wait to
			// receive any data from the tracker. If no data is received for this
			// number of seconds, the tracker will be considered as having timed
			// out. If a tracker is down, this is the kind of timeout that will
			// occur.
			tracker_receive_timeout,

			// the time to wait when sending a stopped message before considering
			// a tracker to have timed out. this is usually shorter, to make the
			// client quit faster
			stop_tracker_timeout,

			// this is the maximum number of bytes in a tracker response. If a
			// response size passes this number of bytes it will be rejected and
			// the connection will be closed. On gzipped responses this size is
			// measured on the uncompressed data. So, if you get 20 bytes of gzip
			// response that'll expand to 2 megabytes, it will be interrupted
			// before the entire response has been uncompressed (assuming the
			// limit is lower than 2 megs).
			tracker_maximum_response_length,

			// the number of seconds from a request is sent until it times out if
			// no piece response is returned.
			piece_timeout,

			// the number of seconds one block (16kB) is expected to be received
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
			// closing it due to time out. This defaults to 120 seconds, since
			// that's what's specified in the protocol specification. After half
			// the time out, a keep alive message is sent.
			peer_timeout,

			// same as peer_timeout, but only applies to url-seeds. this is
			// usually set lower, because web servers are expected to be more
			// reliable.
			urlseed_timeout,

			// controls the pipelining size of url-seeds. i.e. the number of HTTP
			// request to keep outstanding before waiting for the first one to
			// complete. It's common for web servers to limit this to a relatively
			// low number, like 5
			urlseed_pipeline_size,

			// time to wait until a new retry of a web seed takes place
			urlseed_wait_retry,

			// sets the upper limit on the total number of files this session will
			// keep open. The reason why files are left open at all is that some
			// anti virus software hooks on every file close, and scans the file
			// for viruses. deferring the closing of the files will be the
			// difference between a usable system and a completely hogged down
			// system. Most operating systems also has a limit on the total number
			// of file descriptors a process may have open. It is usually a good
			// idea to find this limit and set the number of connections and the
			// number of files limits so their sum is slightly below it.
			file_pool_size,

			// ``max_failcount`` is the maximum times we try to connect to a peer
			// before stop connecting again. If a peer succeeds, the failcounter
			// is reset. If a peer is retrieved from a peer source (other than
			// DHT) the failcount is decremented by one, allowing another try.
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
			// number of seconds, it will be disconnected. default is 10 minutes
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
			// before we switch to rarest first picking. This defaults to 4, which
			// means the 4 first pieces in any torrent are picked at random, the
			// following pieces are picked in rarest first order.
			initial_picker_threshold,

			// the number of allowed pieces to send to peers that supports the
			// fast extensions
			allowed_fast_set_size,

			// ``suggest_mode`` controls whether or not libtorrent will send out
			// suggest messages to create a bias of its peers to request certain
			// pieces. The modes are:
			// 
			// * ``no_piece_suggestsions`` which is the default and will not send
			//   out suggest messages.
			// * ``suggest_read_cache`` which will send out suggest messages for
			//   the most recent pieces that are in the read cache.
			suggest_mode,

			// ``max_queued_disk_bytes`` is the number maximum number of bytes, to
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
			// we'll read another 16kB block onto it. If set too small, upload
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
			// which peers to unchoke.
			// 
			// The options for choking algorithms are:
			// 
			// * ``fixed_slots_choker`` is the traditional choker with a fixed
			//   number of unchoke slots (as specified by
			//   ``session::set_max_uploads()``).
			// 
			// * ``rate_based_choker`` opens up unchoke slots based on the upload
			//   rate achieved to peers. The more slots that are opened, the
			//   marginal upload rate required to open up another slot increases.
			// 
			// * ``bittyrant_choker`` attempts to optimize download rate by
			//   finding the reciprocation rate of each peer individually and
			//   prefers peers that gives the highest *return on investment*. It
			//   still allocates all upload capacity, but shuffles it around to
			//   the best peers first. For this choker to be efficient, you need
			//   to set a global upload rate limit
			//   (``session::set_upload_rate_limit()``). For more information
			//   about this choker, see the paper_. This choker is not fully
			//   implemented nor tested.
			// 
			// .. _paper: http://bittyrant.cs.washington.edu/#papers
			// 
			// ``seed_choking_algorithm`` controls the seeding unchoke behavior.
			// The available options are:
			// 
			// * ``round_robin`` which round-robins the peers that are unchoked
			//   when seeding. This distributes the upload bandwidht uniformly and
			//   fairly. It minimizes the ability for a peer to download everything
			//   without redistributing it.
			// 
			// * ``fastest_upload`` unchokes the peers we can send to the fastest.
			//   This might be a bit more reliable in utilizing all available
			//   capacity.
			// 
			// * ``anti_leech`` prioritizes peers who have just started or are
			//   just about to finish the download. The intention is to force
			//   peers in the middle of the download to trade with each other.
			choking_algorithm,
			seed_choking_algorithm,

			// ``cache_size`` is the disk write and read  cache. It is specified
			// in units of 16 KiB blocks. Buffers that are part of a peer's send
			// or receive buffer also count against this limit. Send and receive
			// buffers will never be denied to be allocated, but they will cause
			// the actual cached blocks to be flushed or evicted. If this is set
			// to -1, the cache size is automatically set to the amount of
			// physical RAM available in the machine divided by 8. If the amount
			// of physical RAM cannot be determined, it's set to 1024 (= 16 MiB).
			// 
			// Disk buffers are allocated using a pool allocator, the number of
			// blocks that are allocated at a time when the pool needs to grow can
			// be specified in ``cache_buffer_chunk_size``. Lower numbers saves
			// memory at the expense of more heap allocations. If it is set to 0,
			// the effective chunk size is proportional to the total cache size,
			// attempting to strike a good balance between performance and memory
			// usage. It defaults to 0. ``cache_expiry`` is the number of seconds
			// from the last cached write to a piece in the write cache, to when
			// it's forcefully flushed to disk. Default is 60 second.
			// 
			// On 32 bit builds, the effective cache size will be limited to 3/4 of
			// 2 GiB to avoid exceeding the virtual address space limit.
			cache_size,
			cache_buffer_chunk_size,
			cache_expiry,

#ifndef TORRENT_NO_DEPRECATE
			// ``explicit_cache_interval`` is the number of seconds in between
			// each refresh of a part of the explicit read cache. Torrents take
			// turns in refreshing and this is the time in between each torrent
			// refresh. Refreshing a torrent's explicit read cache means scanning
			// all pieces and picking a random set of the rarest ones. There is an
			// affinity to pick pieces that are already in the cache, so that
			// subsequent refreshes only swaps in pieces that are rarer than
			// whatever is in the cache at the time.
			explicit_cache_interval,
#else
			deprecated11,
#endif

			// determines how files are opened when they're in read only mode
			// versus read and write mode. The options are:
			// 
			// enable_os_cache
			//   This is the default and files are opened normally, with the OS
			//   caching reads and writes.
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
			// packet sent to peers (including web seeds). The default value for
			// this is ``0x0`` (no marking). One potentially useful TOS mark is
			// ``0x20``, this represents the *QBone scavenger service*. For more
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
			// the DHT. By default this is set to 88, which is no more than one
			// DHT announce every 10 seconds.
			// 
			// ``active_tracker_limit`` is the max number of torrents to announce
			// to their trackers. By default this is 360, which is no more than
			// one announce every 5 seconds.
			// 
			// ``active_lsd_limit`` is the max number of torrents to announce to
			// the local network over the local service discovery protocol. By
			// default this is 80, which is no more than one announce every 5
			// seconds (assuming the default announce interval of 5 minutes).
			// 
			// You can have more torrents *active*, even though they are not
			// announced to the DHT, lsd or their tracker. If some peer knows
			// about you for any reason and tries to connect, it will still be
			// accepted, unless the torrent is paused, which means it won't accept
			// any connections.
			// 
			// ``active_loaded_limit`` is the number of torrents that are allowed
			// to be *loaded* at any given time. Note that a torrent can be active
			// even though it's not loaded. if an unloaded torrents finds a peer
			// that wants to access it, the torrent will be loaded on demand,
			// using a user-supplied callback function. If the feature of
			// unloading torrents is not enabled, this setting have no effect. If
			// this limit is set to 0, it means unlimited. For more information,
			// see dynamic-loading-of-torrent-files_.
			active_downloads,
			active_seeds,
			active_checking,
			active_dht_limit,
			active_tracker_limit,
			active_lsd_limit,
			active_limit,
			active_loaded_limit,

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
			// torrents that are paused. This default to the same as
			// ``max_peerlist_size``, but can be used to save memory for paused
			// torrents, since it's not as important for them to keep a large peer
			// list.
			max_peerlist_size,
			max_paused_peerlist_size,

			// this is the minimum allowed announce interval for a tracker. This
			// is specified in seconds and is used as a sanity check on what is
			// returned from a tracker. It mitigates hammering misconfigured
			// trackers.
			min_announce_interval,

			// this is the number of seconds a torrent is considered active after
			// it was started, regardless of upload and download speed. This is so
			// that newly started torrents are not considered inactive until they
			// have a fair chance to start downloading.
			auto_manage_startup,

			// ``seeding_piece_quota`` is the number of pieces to send to a peer,
			// when seeding, before rotating in another peer to the unchoke set.
			// It defaults to 3 pieces, which means that when seeding, any peer
			// we've sent more than this number of pieces to will be unchoked in
			// favour of a choked peer.
			seeding_piece_quota,

			// TODO: deprecate this
			// ``max_rejects`` is the number of piece requests we will reject in a
			// row while a peer is choked before the peer is considered abusive
			// and is disconnected.
			max_rejects,

			// ``recv_socket_buffer_size`` and ``send_socket_buffer_size``
			// specifies the buffer sizes set on peer sockets. 0 (which is the
			// default) means the OS default (i.e. don't change the buffer sizes).
			// The socket buffer sizes are changed using setsockopt() with
			// SOL_SOCKET/SO_RCVBUF and SO_SNDBUFFER.
			recv_socket_buffer_size,
			send_socket_buffer_size,

			// ``file_checks_delay_per_block`` is the number of milliseconds to
			// sleep in between disk read operations when checking torrents. This
			// defaults to 0, but can be set to higher numbers to slow down the
			// rate at which data is read from the disk while checking. This may
			// be useful for background tasks that doesn't matter if they take a
			// bit longer, as long as they leave disk I/O time for other
			// processes.
			file_checks_delay_per_block,

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
			// It defaults to 10.
			max_suggest_pieces,

			// ``local_service_announce_interval`` is the time between local
			// network announces for a torrent. By default, when local service
			// discovery is enabled a torrent announces itself every 5 minutes.
			// This interval is specified in seconds.
			local_service_announce_interval,

			// ``dht_announce_interval`` is the number of seconds between
			// announcing torrents to the distributed hash table (DHT).
			dht_announce_interval,

			// ``udp_tracker_token_expiry`` is the number of seconds libtorrent
			// will keep UDP tracker connection tokens around for. This is
			// specified to be 60 seconds, and defaults to that. The higher this
			// value is, the fewer packets have to be sent to the UDP tracker. In
			// order for higher values to work, the tracker needs to be configured
			// to match the expiration time for tokens.
			udp_tracker_token_expiry,

			// ``default_cache_min_age`` is the minimum number of seconds any read
			// cache line is kept in the cache. This defaults to one second but
			// may be greater if ``guided_read_cache`` is enabled. Having a lower
			// bound on the time a cache line stays in the cache is an attempt
			// to avoid swapping the same pieces in and out of the cache in case
			// there is a shortage of spare cache space.
			default_cache_min_age,

			// ``num_optimistic_unchoke_slots`` is the number of optimistic
			// unchoke slots to use. It defaults to 0, which means automatic.
			// Having a higher number of optimistic unchoke slots mean you will
			// find the good peers faster but with the trade-off to use up more
			// bandwidth. When this is set to 0, libtorrent opens up 20% of your
			// allowed upload slots as optimistic unchoke slots.
			num_optimistic_unchoke_slots,

			// ``default_est_reciprocation_rate`` is the assumed reciprocation
			// rate from peers when using the BitTyrant choker. This defaults to
			// 14 kiB/s. If set too high, you will over-estimate your peers and be
			// more altruistic while finding the true reciprocation rate, if it's
			// set too low, you'll be too stingy and waste finding the true
			// reciprocation rate.
			// 
			// ``increase_est_reciprocation_rate`` specifies how many percent the
			// estimated reciprocation rate should be increased by each unchoke
			// interval a peer is still choking us back. This defaults to 20%.
			// This only applies to the BitTyrant choker.
			// 
			// ``decrease_est_reciprocation_rate`` specifies how many percent the
			// estimated reciprocation rate should be decreased by each unchoke
			// interval a peer unchokes us. This default to 3%. This only applies
			// to the BitTyrant choker.
			default_est_reciprocation_rate,
			increase_est_reciprocation_rate,
			decrease_est_reciprocation_rate,

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
			// mode torrents. This defaults to 3, meaning we'll try to upload 3
			// times as much as we download. Setting this very high, will make it
			// very conservative and you might end up not downloading anything
			// ever (and not affecting your share ratio). It does not make any
			// sense to set this any lower than 2. For instance, if only 3 peers
			// need to download the rarest piece, it's impossible to download a
			// single piece and upload it more than 3 times. If the
			// share_mode_target is set to more than 3, nothing is downloaded.
			share_mode_target,

			// ``upload_rate_limit``, ``download_rate_limit``,
			// ``local_upload_rate_limit`` and ``local_download_rate_limit`` sets
			// the session-global limits of upload and download rate limits, in
			// bytes per second. The local rates refer to peers on the local
			// network. By default peers on the local network are not rate
			// limited.
			// 
			// These rate limits are only used for local peers (peers within the
			// same subnet as the client itself) and it is only used when
			// ``ignore_limits_on_local_network`` is set to true (which it is by
			// default). These rate limits default to unthrottled, but can be
			// useful in case you want to treat local peers preferentially, but
			// not quite unthrottled.
			// 
			// A value of 0 means unlimited.
			upload_rate_limit,
			download_rate_limit,
#ifndef TORRENT_NO_DEPRECATE
			local_upload_rate_limit,
			local_download_rate_limit,
#else
			deprecated3,
			deprecated4,
#endif

			// ``dht_upload_rate_limit`` sets the rate limit on the DHT. This is
			// specified in bytes per second and defaults to 4000. For busy boxes
			// with lots of torrents that requires more DHT traffic, this should
			// be raised.
			dht_upload_rate_limit,

			// ``unchoke_slots_limit`` is the max number of unchoked peers in the
			// session. The number of unchoke slots may be ignored depending on
			// what ``choking_algorithm`` is set to.
			unchoke_slots_limit,

#ifndef TORRENT_NO_DEPRECATE
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
			half_open_limit,
#else
			deprecated5,
#endif

			// ``connections_limit`` sets a global limit on the number of
			// connections opened. The number of connections is set to a hard
			// minimum of at least two per torrent, so if you set a too low
			// connections limit, and open too many torrents, the limit will not
			// be met.
			connections_limit,

			// ``connections_slack`` is the the number of incoming connections
			// exceeding the connection limit to accept in order to potentially
			// replace existing ones.
			connections_slack,

			// ``utp_target_delay`` is the target delay for uTP sockets in
			// milliseconds. A high value will make uTP connections more
			// aggressive and cause longer queues in the upload bottleneck. It
			// cannot be too low, since the noise in the measurements would cause
			// it to send too slow. The default is 50 milliseconds.
			// ``utp_gain_factor`` is the number of bytes the uTP congestion
			// window can increase at the most in one RTT. This defaults to 300
			// bytes. If this is set too high, the congestion controller reacts
			// too hard to noise and will not be stable, if it's set too low, it
			// will react slow to congestion and not back off as fast.
			//
			// ``utp_min_timeout`` is the shortest allowed uTP socket timeout,
			// specified in milliseconds. This defaults to 500 milliseconds. The
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
			// lossed or timed out) before giving up and closing the connection.
			// ``utp_connect_timeout`` is the number of milliseconds of timeout
			// for the initial SYN packet for uTP connections. For each timed out
			// packet (in a row), the timeout is doubled. ``utp_loss_multiplier``
			// controls how the congestion window is changed when a packet loss is
			// experienced. It's specified as a percentage multiplier for
			// ``cwnd``. By default it's set to 50 (i.e. cut in half). Do not
			// change this value unless you know what you're doing. Never set it
			// higher than 100.
			utp_target_delay,
			utp_gain_factor,
			utp_min_timeout,
			utp_syn_resends,
			utp_fin_resends,
			utp_num_resends,
			utp_connect_timeout,
#ifndef TORRENT_NO_DEPRECATE
			utp_delayed_ack,
#else
			deprecated6,
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
			// accepted. The default is 5 which should be sufficient for any
			// normal client. If this is a high performance server which expects
			// to receive a lot of connections, or used in a simulator or test, it
			// might make sense to raise this number. It will not take affect
			// until listen_on() is called again (or for the first time).
			listen_queue_size,

			// ``torrent_connect_boost`` is the number of peers to try to connect
			// to immediately when the first tracker response is received for a
			// torrent. This is a boost to given to new torrents to accelerate
			// them starting up. The normal connect scheduler is run once every
			// second, this allows peers to be connected immediately instead of
			// waiting for the session tick to trigger connections.
			torrent_connect_boost,

			// ``alert_queue_size`` is the maximum number of alerts queued up
			// internally. If alerts are not popped, the queue will eventually
			// fill up to this level.
			alert_queue_size,

			// ``max_metadata_size`` is the maximum allowed size (in bytes) to be
			// received by the metadata extension, i.e. magnet links.
			max_metadata_size,

#ifndef TORRENT_NO_DEPRECATE
			// DEPRECTED: use aio_threads instead

			// ``hashing_threads`` is the number of threads to use for piece hash
			// verification. It defaults to 1. For very high download rates, on
			// machines with multiple cores, this could be incremented. Setting it
			// higher than the number of CPU cores would presumably not provide
			// any benefit of setting it to the number of cores. If it's set to 0,
			// hashing is done in the disk thread.
			hashing_threads,
#else
			deprecated9,
#endif

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
			// io-threads to use,  and ``aio_max`` the max number of outstanding
			// jobs.
			aio_threads,
			aio_max,

			// ``network_threads`` is the number of threads to use to call
			// ``async_write_some`` (i.e. send) on peer connection sockets. When
			// seeding at extremely high rates, this may become a bottleneck, and
			// setting this to 2 or more may parallelize that cost. When using SSL
			// torrents, all encryption for outgoing traffic is done within the
			// socket send functions, and this will help parallelizing the cost of
			// SSL encryption as well.
			network_threads,

			// ``ssl_listen`` sets the listen port for SSL connections. If this is
			// set to 0, no SSL listen port is opened. Otherwise a socket is
			// opened on this port. This setting is only taken into account when
			// opening the regular listen port, and won't re-open the listen
			// socket simply by changing this setting.
			ssl_listen,

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
			// considered done, and it will leave room for other torrents these
			// are specified as percentages
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

			// a bitmask combining flags from alert::category_t defining which
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

			// determines the encryption level of the connections.  This setting
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

			// proxy to use, defaults to none. see proxy_type_t.
			proxy_type,

			// the port of the proxy server
			proxy_port,

			// sets the i2p_ SAM bridge port to connect to. set the hostname with
			// the ``i2p_hostname`` setting.
			// 
			// .. _i2p: http://www.i2p2.de
			i2p_port,

			// this determines the max number of volatile disk cache blocks. If the
			// number of volatile blocks exceed this limit, other volatile blocks
			// will start to be evicted. A disk cache block is volatile if it has
			// low priority, and should be one of the first blocks to be evicted
			// under pressure. For instance, blocks pulled into the cache as the
			// result of calculating a piece hash are volatile. These blocks don't
			// represent potential interest among peers, so the value of keeping
			// them in the cache is limited.
			cache_size_volatile,

			max_int_setting_internal
		};

		enum settings_counts_t
		{
			num_string_settings = max_string_setting_internal - string_type_base,
			num_bool_settings = max_bool_setting_internal - bool_type_base,
			num_int_settings = max_int_setting_internal - int_type_base
		};

		enum suggest_mode_t { no_piece_suggestions = 0, suggest_read_cache = 1 };

		enum choking_algorithm_t
		{
			fixed_slots_choker = 0,
			rate_based_choker = 2,
			bittyrant_choker = 3
		};

		enum seed_choking_algorithm_t
		{
			round_robin,
			fastest_upload,
			anti_leech
		};

		enum io_buffer_mode_t
		{
			enable_os_cache = 0,
#ifndef TORRENT_NO_DEPRECATE
			disable_os_cache_for_aligned_files = 2,
#else
			deprecated = 1,
#endif
			disable_os_cache = 2
		};

		enum bandwidth_mixed_algo_t
		{
			// disables the mixed mode bandwidth balancing
			prefer_tcp = 0,

			// does not throttle uTP, throttles TCP to the same proportion
			// of throughput as there are TCP connections
			peer_proportional = 1
		};

		// the encoding policy options for use with
		// settings_pack::out_enc_policy and settings_pack::in_enc_policy.
		enum enc_policy
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
		enum enc_level
		{
			// use only plaintext encryption
			pe_plaintext = 1,
			// use only rc4 encryption
			pe_rc4 = 2,
			// allow both
			pe_both = 3
		};

		enum proxy_type_t
		{
			// This is the default, no proxy server is used, all other fields are
			// ignored.
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

		std::vector<std::pair<boost::uint16_t, std::string> > m_strings;
		std::vector<std::pair<boost::uint16_t, int> > m_ints;
		std::vector<std::pair<boost::uint16_t, bool> > m_bools;
	};
}

#endif

