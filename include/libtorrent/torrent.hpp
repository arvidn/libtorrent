/*

Copyright (c) 2003-2018, Arvid Norberg
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

#ifndef TORRENT_TORRENT_HPP_INCLUDE
#define TORRENT_TORRENT_HPP_INCLUDE

#include <algorithm>
#include <vector>
#include <set>
#include <list>
#include <deque>

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/limits.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/version.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/peer_list.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/bandwidth_limit.hpp"
#include "libtorrent/bandwidth_queue_entry.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/aux_/session_interface.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/peer_class_set.hpp"
#include "libtorrent/link.hpp"
#include "libtorrent/vector_utils.hpp"
#include "libtorrent/linked_list.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/aux_/file_progress.hpp"

#ifdef TORRENT_USE_OPENSSL
// there is no forward declaration header for asio
namespace boost {
namespace asio {
namespace ssl {
	struct context;
	class verify_context;
}
}
}
#endif

#if TORRENT_COMPLETE_TYPES_REQUIRED
#include "libtorrent/peer_connection.hpp"
#endif

// define as 0 to disable. 1 enables debug output of the pieces and requested
// blocks. 2 also enables trace output of the time critical piece picking
// logic
#define TORRENT_DEBUG_STREAMING 0

namespace libtorrent
{
	class http_parser;

	class piece_manager;
	struct torrent_plugin;
	struct bitfield;
	struct announce_entry;
	struct tracker_request;
	struct add_torrent_params;
	struct storage_interface;
	class bt_peer_connection;
	struct listen_socket_t;

	peer_id generate_peer_id(aux::session_settings const& sett);

	namespace aux
	{
		struct piece_checker_data;
	}

	struct resume_data_t
	{
		std::vector<char> buf;
		bdecode_node node;
	};

	struct time_critical_piece
	{
		// when this piece was first requested
		time_point first_requested;
		// when this piece was last requested
		time_point last_requested;
		// by what time we want this piece
		time_point deadline;
		// 1 = send alert with piece data when available
		int flags;
		// how many peers it's been requested from
		int peers;
		// the piece index
		int piece;
#if TORRENT_DEBUG_STREAMING > 0
		// the number of multiple requests are allowed
		// to blocks still not downloaded (debugging only)
		int timed_out;
#endif
		bool operator<(time_critical_piece const& rhs) const
		{ return deadline < rhs.deadline; }
	};

	// this is the internal representation of web seeds
	struct web_seed_t : web_seed_entry
	{
		web_seed_t(web_seed_entry const& wse);
		web_seed_t(std::string const& url_, web_seed_entry::type_t type_
			, std::string const& auth_ = std::string()
			, web_seed_entry::headers_t const& extra_headers_ = web_seed_entry::headers_t());

		// if this is > now, we can't reconnect yet
		time_point retry;

		// if the hostname of the web seed has been resolved,
		// these are its IP addresses
		std::vector<tcp::endpoint> endpoints;

		// this is the peer_info field used for the
		// connection, just to count hash failures
		// it's also used to hold the peer_connection
		// pointer, when the web seed is connected
		ipv4_peer peer_info;

		// this is initialized to true, but if we discover the
		// server not to support it, it's set to false, and we
		// make larger requests.
		bool supports_keepalive;

		// this indicates whether or not we're resolving the
		// hostname of this URL
		bool resolving;

		// if the user wanted to remove this while
		// we were resolving it. In this case, we set
		// the removed flag to true, to make the resolver
		// callback remove it
		bool removed;

		// if the web server doesn't support keepalive or a block request was
		// interrupted, the block received so far is kept here for the next
		// connection to pick up
		peer_request restart_request;
		std::vector<char> restart_piece;
	};

	struct TORRENT_EXTRA_EXPORT torrent_hot_members
	{
		torrent_hot_members(aux::session_interface& ses
			, add_torrent_params const& p, int block_size);

	protected:
		// the piece picker. This is allocated lazily. When we don't
		// have anything in the torrent (for instance, if it hasn't
		// been started yet) or if we have everything, there is no
		// picker. It's allocated on-demand the first time we need
		// it in torrent::need_picker(). In order to tell the
		// difference between having everything and nothing in
		// the case there is no piece picker, see m_have_all.
		boost::scoped_ptr<piece_picker> m_picker;

		// TODO: make this a raw pointer. perhaps keep the shared_ptr
		// around further down the object to maintain an owner
		boost::shared_ptr<torrent_info> m_torrent_file;

		// a back reference to the session
		// this torrent belongs to.
		aux::session_interface& m_ses;

		// this vector is sorted at all times, by the pointer value.
		// use sorted_insert() and sorted_find() on it. The GNU STL
		// implementation on Darwin uses significantly less memory to
		// represent a vector than a set, and this set is typically
		// relatively small, and it's cheap to copy pointers.
		std::vector<peer_connection*> m_connections;

		// the scrape data from the tracker response, this
		// is optional and may be 0xffffff
		boost::uint32_t m_complete:24;

		// set to true when this torrent may not download anything
		bool m_upload_mode:1;

		// this is set to false as long as the connections
		// of this torrent hasn't been initialized. If we
		// have metadata from the start, connections are
		// initialized immediately, if we didn't have metadata,
		// they are initialized right after files_checked().
		// valid_resume_data() will return false as long as
		// the connections aren't initialized, to avoid
		// them from altering the piece-picker before it
		// has been initialized with files_checked().
		bool m_connections_initialized:1;

		// is set to true when the torrent has
		// been aborted.
		bool m_abort:1;

		// is true if this torrent has allows having peers
		bool m_allow_peers:1;

		// this is set when the torrent is in share-mode
		bool m_share_mode:1;

		// this is true if we have all pieces. If it's false,
		// it means we either don't have any pieces, or, if
		// there is a piece_picker object present, it contans
		// the state of how many pieces we have
		bool m_have_all:1;

		// set to true when this torrent has been paused but
		// is waiting to finish all current download requests
		// before actually closing all connections, When in graceful pause mode,
		// m_allow_peers is also false.
		bool m_graceful_pause_mode:1;

		// state subscription. If set, a pointer to this torrent will be added
		// to the session_impl::m_torrent_lists[torrent_state_updates]
		// whenever this torrent's state changes (any state).
		bool m_state_subscription:1;

		// the maximum number of connections for this torrent
		boost::uint32_t m_max_connections:24;

		// the size of a request block
		// each piece is divided into these
		// blocks when requested. The block size is
		// 1 << m_block_size_shift
		boost::uint32_t m_block_size_shift:5;

		// the state of this torrent (queued, checking, downloading, etc.)
		boost::uint32_t m_state:3;

		boost::scoped_ptr<peer_list> m_peer_list;
	};

	// a torrent is a class that holds information
	// for a specific download. It updates itself against
	// the tracker
	class TORRENT_EXTRA_EXPORT torrent
		: private single_threaded
		, private torrent_hot_members
		, public request_callback
		, public peer_class_set
		, public boost::enable_shared_from_this<torrent>
		, public list_node<torrent> // used for torrent activity LRU
	{
	public:

		torrent(aux::session_interface& ses, int block_size
			, int seq, add_torrent_params const& p
			, sha1_hash const& info_hash);
		~torrent();

		// This may be called from multiple threads
		sha1_hash const& info_hash() const { return m_info_hash; }

		bool is_deleted() const { return m_deleted; }

		// starts the announce timer
		void start(add_torrent_params const& p);

		void start_download_url();

		// returns which stats gauge this torrent currently
		// has incremented.
		int current_stats_state() const;

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(boost::shared_ptr<torrent_plugin>);
		void remove_extension(boost::shared_ptr<torrent_plugin>);
		void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent_handle const&, void*)> const& ext
			, void* userdata);
		void notify_extension_add_peer(tcp::endpoint const& ip, int src, int flags);
#endif

		peer_connection* find_lowest_ranking_peer() const;

#if TORRENT_USE_ASSERTS
		bool has_peer(peer_connection const* p) const
		{ return sorted_find(m_connections, p) != m_connections.end(); }
		bool is_single_thread() const { return single_threaded::is_single_thread(); }
#endif

		// this is called when the torrent has metadata.
		// it will initialize the storage and the piece-picker
		void init();

		// called every time we actually need the torrent_info
		// object to be fully loaded. If it isn't, this triggers
		// loading it from disk
		// the return value indicates success. If it failed to
		// load, the torrent will be set to an error state and
		// return false
		bool need_loaded();

		// unload the torrent file to save memory
		void unload();
		// returns true if parsed successfully
		bool load(std::vector<char>& buffer);

		// pinned torrents may not be unloaded
		bool is_pinned() const { return m_pinned; }
		void set_pinned(bool p);
		bool is_loaded() const { return m_torrent_file->is_loaded(); }
		bool should_be_loaded() const { return m_should_be_loaded; }

		// find the peer that introduced us to the given endpoint. This is
		// used when trying to holepunch. We need the introducer so that we
		// can send a rendezvous connect message
		bt_peer_connection* find_introducer(tcp::endpoint const& ep) const;

		// if we're connected to a peer at ep, return its peer connection
		// only count BitTorrent peers
		bt_peer_connection* find_peer(tcp::endpoint const& ep) const;
		peer_connection* find_peer(peer_id const& pid);

		void on_resume_data_checked(disk_io_job const* j);
		void on_force_recheck(disk_io_job const* j);
		void on_piece_hashed(disk_io_job const* j);
		void files_checked();
		void start_checking();

		void start_announcing();
		void stop_announcing();

		void send_share_mode();
		void send_upload_only();

		void set_share_mode(bool s);
		bool share_mode() const { return m_share_mode; }

		// TOOD: make graceful pause also finish all sending blocks
		// before disconnecting
		bool graceful_pause() const { return m_graceful_pause_mode; }

		void set_upload_mode(bool b);
		bool upload_mode() const { return m_upload_mode || m_graceful_pause_mode; }
		bool is_upload_only() const { return is_finished() || upload_mode(); }

		int seed_rank(aux::session_settings const& s) const;

		enum flags_t { overwrite_existing = 1 };
		void add_piece(int piece, char const* data, int flags = 0);
		void on_disk_write_complete(disk_io_job const* j
			, peer_request p);
		void on_disk_cache_complete(disk_io_job const* j);
		void on_disk_tick_done(disk_io_job const* j);

		void schedule_storage_tick();

		void set_progress_ppm(int p) { m_progress_ppm = p; }
		struct read_piece_struct
		{
			boost::shared_array<char> piece_data;
			int blocks_left;
			bool fail;
			error_code error;
		};
		void read_piece(int piece);
		void on_disk_read_complete(disk_io_job const* j, peer_request r
			, boost::shared_ptr<read_piece_struct> rp);

		storage_mode_t storage_mode() const;
		storage_interface* get_storage();

		// this will flag the torrent as aborted. The main
		// loop in session_impl will check for this state
		// on all torrents once every second, and take
		// the necessary actions then.
		void abort();
		bool is_aborted() const { return m_abort; }

		void new_external_ip();

		torrent_status::state_t state() const
		{ return torrent_status::state_t(m_state); }
		void set_state(torrent_status::state_t s);

		aux::session_settings const& settings() const;
		aux::session_interface& session() { return m_ses; }

		void set_sequential_download(bool sd);
		bool is_sequential_download() const
		{ return m_sequential_download || m_auto_sequential; }

		void queue_up();
		void queue_down();
		void set_queue_position(int p);
		int queue_position() const { return m_sequence_number; }
		// used internally
		void set_queue_position_impl(int p) { m_sequence_number = p; }

		void second_tick(int tick_interval_ms);

		// see if we need to connect to web seeds, and if so,
		// connect to them
		void maybe_connect_web_seeds();

		std::string name() const;

		stat statistics() const { return m_stat; }
		boost::int64_t bytes_left() const;
		int block_bytes_wanted(piece_block const& p) const;
		void bytes_done(torrent_status& st, bool accurate) const;
		boost::int64_t quantized_bytes_done() const;

		void sent_bytes(int bytes_payload, int bytes_protocol);
		void received_bytes(int bytes_payload, int bytes_protocol);
		void trancieve_ip_packet(int bytes, bool ipv6);
		void sent_syn(bool ipv6);
		void received_synack(bool ipv6);

		void set_ip_filter(boost::shared_ptr<const ip_filter> ipf);
		void port_filter_updated();
		ip_filter const* get_ip_filter() { return m_ip_filter.get(); }

		std::string resolve_filename(int file) const;
		void handle_disk_error(disk_io_job const* j, peer_connection* c = 0);
		void clear_error();

		void set_error(error_code const& ec, int file);
		bool has_error() const { return !!m_error; }
		error_code error() const { return m_error; }

		void flush_cache();
		void pause(bool graceful = false);
		void resume();

		enum pause_flags_t
		{
			flag_graceful_pause = 1,
			flag_clear_disk_cache = 2
		};
		void set_allow_peers(bool b, int flags = flag_clear_disk_cache);
		void set_announce_to_dht(bool b) { m_announce_to_dht = b; }
		void set_announce_to_trackers(bool b) { m_announce_to_trackers = b; }
		void set_announce_to_lsd(bool b) { m_announce_to_lsd = b; }

		void stop_when_ready(bool b);

		int started() const { return m_started; }
		void step_session_time(int seconds);
		void do_pause(bool clear_disk_cache = true);
		void do_resume();

		int finished_time() const;
		int active_time() const;
		int seeding_time() const;

		bool is_paused() const;
		bool allows_peers() const { return m_allow_peers; }
		bool is_torrent_paused() const { return !m_allow_peers || m_graceful_pause_mode; }
		void force_recheck();
		void save_resume_data(int flags);
		bool do_async_save_resume_data();

		bool need_save_resume_data() const
		{
			// save resume data every 15 minutes regardless, just to
			// keep stats up to date
			return m_need_save_resume_data || m_ses.session_time() - m_last_saved_resume > 15 * 60;
		}

		void set_need_save_resume()
		{
			m_need_save_resume_data = true;
		}

		bool is_auto_managed() const { return m_auto_managed; }
		void auto_managed(bool a);

		bool should_check_files() const;

		bool delete_files(int options);
		void peers_erased(std::vector<torrent_peer*> const& peers);

		// ============ start deprecation =============
		void filter_piece(int index, bool filter);
		void filter_pieces(std::vector<bool> const& bitmask);
		bool is_piece_filtered(int index) const;
		void filtered_pieces(std::vector<bool>& bitmask) const;
		void filter_files(std::vector<bool> const& files);
#if !TORRENT_NO_FPU
		void file_progress(std::vector<float>& fp);
#endif
		// ============ end deprecation =============

		void piece_availability(std::vector<int>& avail) const;

		void set_piece_priority(int index, int priority);
		int piece_priority(int index) const;

		void prioritize_pieces(std::vector<int> const& pieces);
		void prioritize_piece_list(std::vector<std::pair<int, int> > const& pieces);
		void piece_priorities(std::vector<int>*) const;

		void set_file_priority(int index, int priority);
		int file_priority(int index) const;

		void on_file_priority();
		void prioritize_files(std::vector<int> const& files);
		void file_priorities(std::vector<int>*) const;

		void cancel_non_critical();
		void set_piece_deadline(int piece, int t, int flags);
		void reset_piece_deadline(int piece);
		void clear_time_critical();
		void update_piece_priorities();

		void status(torrent_status* st, boost::uint32_t flags);

		// this torrent changed state, if the user is subscribing to
		// it, add it to the m_state_updates list in session_impl
		void state_updated();

		void file_progress(std::vector<boost::int64_t>& fp, int flags = 0);

#ifndef TORRENT_NO_DEPRECATE
		void use_interface(std::string net_interface);
#endif

		void connect_to_url_seed(std::list<web_seed_t>::iterator url);
		bool connect_to_peer(torrent_peer* peerinfo, bool ignore_limit = false);

		int priority() const;
		void set_priority(int const prio);

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 1.1

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		void resolve_countries(bool r);
		bool resolving_countries() const;

		void resolve_peer_country(boost::shared_ptr<peer_connection> const& p) const;
		void on_country_lookup(error_code const& error
			, std::vector<address> const& host_list
			, boost::shared_ptr<peer_connection> p) const;
#endif
#endif // TORRENT_NO_DEPRECATE

// --------------------------------------------
		// BANDWIDTH MANAGEMENT

		void set_upload_limit(int limit);
		int upload_limit() const;
		void set_download_limit(int limit);
		int download_limit() const;

		peer_class_t peer_class() const { return m_peer_class; }

		void set_max_uploads(int limit, bool state_update = true);
		int max_uploads() const { return m_max_uploads; }
		void set_max_connections(int limit, bool state_update = true);
		int max_connections() const { return m_max_connections; }

// --------------------------------------------
		// PEER MANAGEMENT

		// add or remove a url that will be attempted for
		// finding the file(s) in this torrent.
		void add_web_seed(std::string const& url, web_seed_t::type_t type);
		void add_web_seed(std::string const& url, web_seed_t::type_t type
			, std::string const& auth, web_seed_t::headers_t const& extra_headers);

		void remove_web_seed(std::string const& url, web_seed_t::type_t type);
		void disconnect_web_seed(peer_connection* p);

		void retry_web_seed(peer_connection* p, int retry = 0);

		void remove_web_seed(peer_connection* p, error_code const& ec
			, operation_t op, int error = 0);

		std::set<std::string> web_seeds(web_seed_entry::type_t type) const;

		bool free_upload_slots() const
		{ return m_num_uploads < m_max_uploads; }

		bool choke_peer(peer_connection& c);
		bool unchoke_peer(peer_connection& c, bool optimistic = false);

		void trigger_unchoke();
		void trigger_optimistic_unchoke();

		// used by peer_connection to attach itself to a torrent
		// since incoming connections don't know what torrent
		// they're a part of until they have received an info_hash.
		// false means attach failed
		bool attach_peer(peer_connection* p);

		// this will remove the peer and make sure all
		// the pieces it had have their reference counter
		// decreased in the piece_picker
		void remove_peer(peer_connection* p);

		// cancel requests to this block from any peer we're
		// connected to on this torrent
		void cancel_block(piece_block block);

		bool want_tick() const;
		void update_want_tick();
		void update_state_list();

		bool want_peers() const;
		bool want_peers_download() const;
		bool want_peers_finished() const;

		void update_want_peers();
		void update_want_scrape();
		void update_gauge();

		bool try_connect_peer();
		torrent_peer* add_peer(tcp::endpoint const& adr, int source, int flags = 0);
		bool ban_peer(torrent_peer* tp);
		void update_peer_port(int port, torrent_peer* p, int src);
		void set_seed(torrent_peer* p, bool s);
		void clear_failcount(torrent_peer* p);
		std::pair<peer_list::iterator, peer_list::iterator> find_peers(address const& a);

		// the number of peers that belong to this torrent
		int num_peers() const { return int(m_connections.size()); }
		int num_seeds() const;
		int num_downloaders() const;

		typedef std::vector<peer_connection*>::iterator peer_iterator;
		typedef std::vector<peer_connection*>::const_iterator const_peer_iterator;

		const_peer_iterator begin() const { return m_connections.begin(); }
		const_peer_iterator end() const { return m_connections.end(); }

		peer_iterator begin() { return m_connections.begin(); }
		peer_iterator end() { return m_connections.end(); }

		void get_full_peer_list(std::vector<peer_list_entry>& v) const;
		void get_peer_info(std::vector<peer_info>& v);
		void get_download_queue(std::vector<partial_piece_info>* queue) const;

#ifndef TORRENT_NO_DEPRECATE
		void refresh_explicit_cache(int cache_size);
#endif

		void add_suggest_piece(int piece);
		void update_suggest_piece(int index, int change);
		void update_auto_sequential();

		void refresh_suggest_pieces();
		void do_refresh_suggest_pieces();

// --------------------------------------------
		// TRACKER MANAGEMENT

		// these are callbacks called by the tracker_connection instance
		// (either http_tracker_connection or udp_tracker_connection)
		// when this torrent got a response from its tracker request
		// or when a failure occured
		virtual void tracker_response(
			tracker_request const& r
			, address const& tracker_ip
			, std::list<address> const& ip_list
			, struct tracker_response const& resp) TORRENT_OVERRIDE;
		virtual void tracker_request_error(tracker_request const& r
			, int response_code, error_code const& ec, const std::string& msg
			, int retry_interval) TORRENT_OVERRIDE;
		virtual void tracker_warning(tracker_request const& req
			, std::string const& msg) TORRENT_OVERRIDE;
		virtual void tracker_scrape_response(tracker_request const& req
			, int complete, int incomplete, int downloaded, int downloaders) TORRENT_OVERRIDE;

		void update_scrape_state();

		// if no password and username is set
		// this will return an empty string, otherwise
		// it will concatenate the login and password
		// ready to be sent over http (but without
		// base64 encoding).
		std::string tracker_login() const;

		// generate the tracker key for this torrent.
		// The key is passed to http trackers as ``&key=``.
		boost::uint32_t tracker_key() const;

		// if we need a connect boost, connect some peers
		// immediately
		void do_connect_boost();

		// forcefully sets next_announce to the current time
		void force_tracker_request(time_point, int tracker_idx);
		void scrape_tracker(int idx, bool user_triggered);
		void announce_with_tracker(boost::uint8_t e
			= tracker_request::none);
		int seconds_since_last_scrape() const
		{
			return m_last_scrape == (std::numeric_limits<boost::int16_t>::min)()
				? -1 : int(m_ses.session_time() - m_last_scrape);
		}

#ifndef TORRENT_DISABLE_DHT
		void dht_announce();
#endif

#ifndef TORRENT_NO_DEPRECATE
		// sets the username and password that will be sent to
		// the tracker
		void set_tracker_login(std::string const& name, std::string const& pw);
#endif

		announce_entry* find_tracker(tracker_request const& r);

// --------------------------------------------
		// PIECE MANAGEMENT

		void recalc_share_mode();

		struct suggest_piece_t
		{
			int piece_index;
			int num_peers;
			bool operator<(suggest_piece_t const& p) const { return num_peers < p.num_peers; }
		};

		std::vector<suggest_piece_t> const& get_suggested_pieces() const
		{ return m_suggested_pieces; }

		bool super_seeding() const
		{
			// we're not super seeding if we're not a seed
			return m_super_seeding;
		}

		void super_seeding(bool on);
		int get_piece_to_super_seed(bitfield const&);

		// returns true if we have downloaded the given piece
		bool have_piece(int index) const
		{
			if (!valid_metadata()) return false;
			if (!has_picker()) return m_have_all;
			return m_picker->have_piece(index);
		}

		// returns true if we have downloaded the given piece
		bool has_piece_passed(int index) const
		{
			if (!valid_metadata()) return false;
			if (index < 0 || index >= torrent_file().num_pieces()) return false;
			if (!has_picker()) return m_have_all;
			return m_picker->has_piece_passed(index);
		}

		// a predictive piece is a piece that we might
		// not have yet, but still announced to peers, anticipating that
		// we'll have it very soon
		bool is_predictive_piece(int index) const
		{
			return std::binary_search(m_predictive_pieces.begin(), m_predictive_pieces.end(), index);
		}

	private:

		// called when we learn that we have a piece
		// only once per piece
		void we_have(int index);

	public:

		int num_have() const
		{
			// pretend we have every piece when in seed mode
			if (m_seed_mode) {
				return m_torrent_file->num_pieces();
			}

			return has_picker()
				? m_picker->num_have()
				: m_have_all ? m_torrent_file->num_pieces() : 0;
		}

		// the number of pieces that have passed
		// hash check, but aren't necessarily
		// flushed to disk yet
		int num_passed() const
		{
			return has_picker()
				? m_picker->num_passed()
				: m_have_all ? m_torrent_file->num_pieces() : 0;
		}

		// when we get a have message, this is called for that piece
		void peer_has(int index, peer_connection const* peer);

		// when we get a bitfield message, this is called for that piece
		void peer_has(bitfield const& bits, peer_connection const* peer);

		void peer_has_all(peer_connection const* peer);

		void peer_lost(int index, peer_connection const* peer);
		void peer_lost(bitfield const& bits, peer_connection const* peer);

		int block_size() const { TORRENT_ASSERT(m_block_size_shift > 0); return 1 << m_block_size_shift; }
		peer_request to_req(piece_block const& p) const;

		void disconnect_all(error_code const& ec, operation_t op);
		int disconnect_peers(int num, error_code const& ec);

		// called every time a block is marked as finished in the
		// piece picker. We might have completed the torrent and
		// we can delete the piece picker
		void maybe_done_flushing();

		// this is called wheh the torrent has completed
		// the download. It will post an event, disconnect
		// all seeds and let the tracker know we're finished.
		void completed();

#if TORRENT_USE_I2P
		void on_i2p_resolve(error_code const& ec, char const* dest);
		bool is_i2p() const { return m_torrent_file && m_torrent_file->is_i2p(); }
#endif

		// this is the asio callback that is called when a name
		// lookup for a PEER is completed.
		void on_peer_name_lookup(error_code const& e
			, std::vector<address> const& addrs
			, int port);

		// this is the asio callback that is called when a name
		// lookup for a WEB SEED is completed.
		void on_name_lookup(error_code const& e
			, std::vector<address> const& addrs
			, int port
			, std::list<web_seed_t>::iterator web);

		void connect_web_seed(std::list<web_seed_t>::iterator web, tcp::endpoint a);

		// this is the asio callback that is called when a name
		// lookup for a proxy for a web seed is completed.
		void on_proxy_name_lookup(error_code const& e
			, std::vector<address> const& addrs
			, std::list<web_seed_t>::iterator web, int port);

		// re-evaluates whether this torrent should be considered inactive or not
		void on_inactivity_tick(error_code const& ec);


		// calculate the instantaneous inactive state (the externally facing
		// inactive state is not instantaneous, but low-pass filtered)
		bool is_inactive_internal() const;

		// remove a web seed, or schedule it for removal in case there
		// are outstanding operations on it
		void remove_web_seed(std::list<web_seed_t>::iterator web);

		// this is called when the torrent has finished. i.e.
		// all the pieces we have not filtered have been downloaded.
		// If no pieces are filtered, this is called first and then
		// completed() is called immediately after it.
		void finished();

		// This is the opposite of finished. It is called if we used
		// to be finished but enabled some files for download so that
		// we wasn't finished anymore.
		void resume_download();

		void verify_piece(int piece);
		void on_piece_verified(disk_io_job const* j);

		// this is called whenever a peer in this swarm becomes interesting
		// it is responsible for issuing a block request, if appropriate
		void peer_is_interesting(peer_connection& c);

		// piece_passed is called when a piece passes the hash check
		// this will tell all peers that we just got his piece
		// and also let the piece picker know that we have this piece
		// so it wont pick it for download
		void piece_passed(int index);

		// piece_failed is called when a piece fails the hash check
		void piece_failed(int index);

		// this is the handler for hash failure piece synchronization
		// i.e. resetting the piece
		void on_piece_sync(disk_io_job const* j);

		// this is the handler for write failure piece synchronization
		void on_piece_fail_sync(disk_io_job const* j, piece_block b);

		enum wasted_reason_t
		{
			piece_timed_out, piece_cancelled, piece_unknown, piece_seed
			, piece_end_game, piece_closing
			, waste_reason_max
		};
		void add_redundant_bytes(int b, wasted_reason_t reason);
		void add_failed_bytes(int b);

		// this is true if we have all the pieces, but not necessarily flushed them to disk
		bool is_seed() const;

		// this is true if we have all the pieces that we want
		// the pieces don't necessarily need to be flushed to disk
		bool is_finished() const;

		bool is_inactive() const;

		std::string save_path() const;
		alert_manager& alerts() const;
		piece_picker& picker()
		{
			TORRENT_ASSERT(m_picker.get());
			return *m_picker;
		}
		piece_picker const& picker() const
		{
			TORRENT_ASSERT(m_picker.get());
			return *m_picker;
		}
		void need_picker();
		bool has_picker() const
		{
			return m_picker.get() != 0;
		}

		void update_max_failcount()
		{
			if (!m_peer_list) return;
			torrent_state st = get_peer_list_state();
			m_peer_list->set_max_failcount(&st);
		}
		int num_known_peers() const { return m_peer_list ? m_peer_list->num_peers() : 0; }
		int num_connect_candidates() const { return m_peer_list ? m_peer_list->num_connect_candidates() : 0; }

		piece_manager& storage();
		bool has_storage() const { return m_storage.get() != NULL; }

		torrent_info const& torrent_file() const
		{ return *m_torrent_file; }

		boost::shared_ptr<const torrent_info> get_torrent_copy();

		std::string const& uuid() const { return m_uuid; }
		void set_uuid(std::string const& s) { m_uuid = s; }
		std::string const& url() const { return m_url; }
		void set_url(std::string const& s) { m_url = s; }
		std::string const& source_feed_url() const { return m_source_feed_url; }
		void set_source_feed_url(std::string const& s) { m_source_feed_url = s; }

		std::vector<announce_entry> const& trackers() const
		{ return m_trackers; }

		void replace_trackers(std::vector<announce_entry> const& urls);

		// returns true if the tracker was added, and false if it was already
		// in the tracker list (in which case the source was added to the
		// entry in the list)
		bool add_tracker(announce_entry const& url);

		torrent_handle get_handle();

		void write_resume_data(entry& rd) const;
		void read_resume_data(bdecode_node const& rd);

		void seen_complete() { m_last_seen_complete = time(0); }
		int time_since_complete() const { return int(time(0) - m_last_seen_complete); }
		time_t last_seen_complete() const { return m_last_seen_complete; }

		// LOGGING
#ifndef TORRENT_DISABLE_LOGGING
		virtual void debug_log(const char* fmt, ...) const TORRENT_OVERRIDE TORRENT_FORMAT(2,3);

		void log_to_all_peers(char const* message);
		time_point m_dht_start_time;
#endif

		// DEBUG
#if TORRENT_USE_INVARIANT_CHECKS
		void check_invariant() const;
#endif

// --------------------------------------------
		// RESOURCE MANAGEMENT

		// flags are defined in storage.hpp
		void move_storage(std::string const& save_path, int flags);

		// renames the file with the given index to the new name
		// the name may include a directory path
		// posts alert to indicate success or failure
		void rename_file(int index, std::string const& name);

		// unless this returns true, new connections must wait
		// with their initialization.
		bool ready_for_connections() const
		{ return m_connections_initialized; }
		bool valid_metadata() const
		{ return m_torrent_file->is_valid(); }
		bool are_files_checked() const
		{ return m_files_checked; }
		bool valid_storage() const
		{ return m_storage.get() != NULL; }

		// parses the info section from the given
		// bencoded tree and moves the torrent
		// to the checker thread for initial checking
		// of the storage.
		// a return value of false indicates an error
		bool set_metadata(char const* metadata_buf, int metadata_size);

		void on_torrent_download(error_code const& ec, http_parser const& parser
			, char const* data, int size);

		int sequence_number() const { return m_sequence_number; }

		bool seed_mode() const { return m_seed_mode; }
		void leave_seed_mode(bool skip_checking);

		bool all_verified() const
		{ return int(m_num_verified) == m_torrent_file->num_pieces(); }
		bool verifying_piece(int piece) const
		{
			TORRENT_ASSERT(piece < int(m_verifying.size()));
			TORRENT_ASSERT(piece >= 0);
			return m_verifying.get_bit(piece);
		}
		void verifying(int piece)
		{
			TORRENT_ASSERT(piece < int(m_verifying.size()));
			TORRENT_ASSERT(piece >= 0);
			TORRENT_ASSERT(m_verifying.get_bit(piece) == false);
			m_verifying.set_bit(piece);
		}
		bool verified_piece(int piece) const
		{
			TORRENT_ASSERT(piece < int(m_verified.size()));
			TORRENT_ASSERT(piece >= 0);
			return m_verified.get_bit(piece);
		}
		void verified(int piece);

		bool add_merkle_nodes(std::map<int, sha1_hash> const& n, int piece);

		// this is called once periodically for torrents
		// that are not private
		void lsd_announce();

		void update_last_upload() { m_last_upload = m_ses.session_time(); }

		void set_apply_ip_filter(bool b);
		bool apply_ip_filter() const { return m_apply_ip_filter; }

		std::vector<int> const& predictive_pieces() const
		{ return m_predictive_pieces; }

		// this is called whenever we predict to have this piece
		// within one second
		void predicted_have_piece(int index, int milliseconds);

		void clear_in_state_update()
		{
			TORRENT_ASSERT(m_links[aux::session_interface::torrent_state_updates].in_list());
			m_links[aux::session_interface::torrent_state_updates].clear();
		}

		void dec_refcount(char const* purpose);
		void inc_refcount(char const* purpose);
		int refcount() const { return m_refcount; }

		void inc_num_connecting(torrent_peer* pp)
		{
			++m_num_connecting;
			TORRENT_ASSERT(m_num_connecting <= int(m_connections.size()));
			if (pp->seed)
			{
				++m_num_connecting_seeds;
				TORRENT_ASSERT(m_num_connecting_seeds <= int(m_connections.size()));
			}
		}
		void dec_num_connecting(torrent_peer* pp)
		{
			TORRENT_ASSERT(m_num_connecting > 0);
			--m_num_connecting;
			if (pp->seed)
			{
				TORRENT_ASSERT(m_num_connecting_seeds > 0);
				--m_num_connecting_seeds;
			}
			TORRENT_ASSERT(m_num_connecting <= int(m_connections.size()));
		}

		bool is_ssl_torrent() const { return m_ssl_torrent; }
#ifdef TORRENT_USE_OPENSSL
		void set_ssl_cert(std::string const& certificate
			, std::string const& private_key
			, std::string const& dh_params
			, std::string const& passphrase);
		void set_ssl_cert_buffer(std::string const& certificate
			, std::string const& private_key
			, std::string const& dh_params);
		boost::asio::ssl::context* ssl_ctx() const { return m_ssl_ctx.get(); }
#endif

		int num_time_critical_pieces() const
		{ return int(m_time_critical_pieces.size()); }

	private:

		void ip_filter_updated();

		void inc_stats_counter(int c, int value = 1);

		// initialize the torrent_state structure passed to peer_list
		// member functions. Don't forget to also call peers_erased()
		// on the erased member after the peer_list call
		torrent_state get_peer_list_state();

		void construct_storage();
		void update_list(int list, bool in);

		void on_files_deleted(disk_io_job const* j);
		void on_torrent_paused(disk_io_job const* j);
		void on_storage_moved(disk_io_job const* j);
		void on_save_resume_data(disk_io_job const* j);
		void on_file_renamed(disk_io_job const* j);
		void on_cache_flushed(disk_io_job const* j);

		// this is used when a torrent is being removed.It synchronizes with the
		// disk thread
		void on_torrent_aborted();

		// upload and download rate limits for the torrent
		void set_limit_impl(int limit, int channel, bool state_update = true);
		int limit_impl(int channel) const;

		void refresh_explicit_cache_impl(disk_io_job const* j, int cache_size);

		int prioritize_tracker(int tracker_index);
		int deprioritize_tracker(int tracker_index);

		bool request_bandwidth_from_session(int channel) const;

		void update_peer_interest(bool was_finished);
		void prioritize_udp_trackers();

		void update_tracker_timer(time_point now);

		static void on_tracker_announce_disp(boost::weak_ptr<torrent> p
			, error_code const& e);

		void on_tracker_announce();

#ifndef TORRENT_DISABLE_DHT
		static void on_dht_announce_response_disp(boost::weak_ptr<torrent> t
			, std::vector<tcp::endpoint> const& peers);
		void on_dht_announce_response(std::vector<tcp::endpoint> const& peers);
		bool should_announce_dht() const;
#endif

		void remove_time_critical_piece(int piece, bool finished = false);
		void remove_time_critical_pieces(std::vector<int> const& priority);
		void request_time_critical_pieces();

		void need_peer_list();

		boost::shared_ptr<const ip_filter> m_ip_filter;

		// all time totals of uploaded and downloaded payload
		// stored in resume data
		boost::int64_t m_total_uploaded;
		boost::int64_t m_total_downloaded;

		// if this pointer is 0, the torrent is in
		// a state where the metadata hasn't been
		// received yet, or during shutdown.
		// the piece_manager keeps the torrent object
		// alive by holding a shared_ptr to it and
		// the torrent keeps the piece manager alive
		// with this shared_ptr. This cycle is
		// broken when torrent::abort() is called
		// Then the torrent releases the piece_manager
		// and when the piece_manager is complete with all
		// outstanding disk io jobs (that keeps
		// the piece_manager alive) it will destruct
		// and release the torrent file. The reason for
		// this is that the torrent_info is used by
		// the piece_manager, and stored in the
		// torrent, so the torrent cannot destruct
		// before the piece_manager.
		boost::shared_ptr<piece_manager> m_storage;

#ifdef TORRENT_USE_OPENSSL
		boost::shared_ptr<boost::asio::ssl::context> m_ssl_ctx;

#if BOOST_VERSION >= 104700
		bool verify_peer_cert(bool preverified, boost::asio::ssl::verify_context& ctx);
#endif

		void init_ssl(std::string const& cert);
#endif

		void setup_peer_class();

		// The list of web seeds in this torrent. Seeds with fatal errors are
		// removed from the set. It's important that iteratores are not
		// invalidated as entries are added and removed from this list, hence the
		// std::list
		std::list<web_seed_t> m_web_seeds;

#ifndef TORRENT_DISABLE_EXTENSIONS
		typedef std::list<boost::shared_ptr<torrent_plugin> > extension_list_t;
		extension_list_t m_extensions;
#endif

		// used for tracker announces
		deadline_timer m_tracker_timer;

		// used to detect when we are active or inactive for long enough
		// to trigger the auto-manage logic
		deadline_timer m_inactivity_timer;

		// this is the upload and download statistics for the whole torrent.
		// it's updated from all its peers once every second.
		libtorrent::stat m_stat;

		// -----------------------------

		// this vector is allocated lazily. If no file priorities are
		// ever changed, this remains empty. Any unallocated slot
		// implicitly means the file has priority 1.
		// TODO: this wastes 5 bits per file
		std::vector<boost::uint8_t> m_file_priority;

		// this object is used to track download progress of individual files
		aux::file_progress m_file_progress;

		// these are the pieces we're currently
		// suggesting to peers.
		std::vector<suggest_piece_t> m_suggested_pieces;

		std::vector<announce_entry> m_trackers;
		// this is an index into m_trackers

		// this list is sorted by time_critical_piece::deadline
		std::vector<time_critical_piece> m_time_critical_pieces;

		std::string m_trackerid;
#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 1.1
		std::string m_username;
		std::string m_password;
#endif

		std::string m_save_path;

		// if we don't have the metadata, this is a url to
		// the torrent file
		std::string m_url;

		// if this was added from an RSS feed, this is the unique
		// identifier in the feed.
		std::string m_uuid;

		// if this torrent was added by an RSS feed, this is the
		// URL to that feed
		std::string m_source_feed_url;

		// this is used as temporary storage while downloading
		// the .torrent file from m_url
//		std::vector<char> m_torrent_file_buf;

		// this is a list of all pieces that we have announced
		// as having, without actually having yet. If we receive
		// a request for a piece in this list, we need to hold off
		// on responding until we have completed the piece and
		// verified its hash. If the hash fails, send reject to
		// peers with outstanding requests, and dont_have to other
		// peers. This vector is ordered, to make lookups fast.
		std::vector<int> m_predictive_pieces;

		// the performance counters of this session
		counters& m_stats_counters;

		// each bit represents a piece. a set bit means
		// the piece has had its hash verified. This
		// is only used in seed mode (when m_seed_mode
		// is true)

		// TODO: These two bitfields should probably be coalesced into one
		bitfield m_verified;
		// this means there is an outstanding, async, operation
		// to verify each piece that has a 1
		bitfield m_verifying;

		// set if there's an error on this torrent
		error_code m_error;

		// used if there is any resume data
		boost::scoped_ptr<resume_data_t> m_resume_data;

		// if the torrent is started without metadata, it may
		// still be given a name until the metadata is received
		// once the metadata is received this field will no
		// longer be used and will be reset
		boost::scoped_ptr<std::string> m_name;

		storage_constructor_type m_storage_constructor;

		// the posix time this torrent was added and when
		// it was completed. If the torrent isn't yet
		// completed, m_completed_time is 0
		time_t m_added_time;
		time_t m_completed_time;

		// this was the last time _we_ saw a seed in this swarm
		time_t m_last_seen_complete;

		// this is the time last any of our peers saw a seed
		// in this swarm
		time_t m_swarm_last_seen_complete;

		// keep a copy if the info-hash here, so it can be accessed from multiple
		// threads, and be cheap to access from the client
		sha1_hash m_info_hash;

	public:
		// these are the lists this torrent belongs to. For more
		// details about each list, see session_impl.hpp. Each list
		// represents a group this torrent belongs to and makes it
		// efficient to enumerate only torrents belonging to a specific
		// group. Such as torrents that want peer connections or want
		// to be ticked etc.

		// TODO: 3 factor out the links (as well as update_list() to a separate
		// class that torrent can inherit)
		link m_links[aux::session_interface::num_torrent_lists];

	private:

		// m_num_verified = m_verified.count()
		boost::uint32_t m_num_verified;

		// this timestamp is kept in session-time, to
		// make it fit in 16 bits
		boost::uint16_t m_last_saved_resume;

		// if this torrent is running, this was the time
		// when it was started. This is used to have a
		// bias towards keeping seeding torrents that
		// recently was started, to avoid oscillation
		// this is specified at a second granularity
		// in session-time. see session_impl for details.
		// the reference point is stepped forward every 4
		// hours to keep the timestamps fit in 16 bits
		boost::uint16_t m_started;

		// if we're a seed, this is the session time
		// timestamp of when we became one
		boost::uint16_t m_became_seed;

		// if we're finished, this is the session time
		// timestamp of when we finished
		boost::uint16_t m_became_finished;

		// when checking, this is the first piece we have not
		// issued a hash job for
		int m_checking_piece;

		// the number of pieces we completed the check of
		int m_num_checked_pieces;

		// the number of async. operations that need this torrent
		// loaded in RAM. having a refcount > 0 prevents it from
		// being unloaded.
		int m_refcount;

		// if the error ocurred on a file, this is the index of that file
		// there are a few special cases, when this is negative. See
		// set_error()
		int m_error_file;

		// the average time it takes to download one time critical piece
		boost::uint32_t m_average_piece_time;

		// the average piece download time deviation
		boost::uint32_t m_piece_time_deviation;

		// the number of bytes that has been
		// downloaded that failed the hash-test
		boost::uint32_t m_total_failed_bytes;
		boost::uint32_t m_total_redundant_bytes;

		// the sequence number for this torrent, this is a
		// monotonically increasing number for each added torrent
		int m_sequence_number;

		// for torrents who have a bandwidth limit, this is != 0
		// and refers to a peer_class in the session.
		peer_class_t m_peer_class;

		// of all peers in m_connections, this is the number
		// of peers that are outgoing and still waiting to
		// complete the connection. This is used to possibly
		// kick out these connections when we get incoming
		// connections (if we've reached the connection limit)
		boost::uint16_t m_num_connecting;

		// this is the peer id we generate when we add the torrent. Peers won't
		// use this (they generate their own peer ids) but this is used in case
		// the tracker returns peer IDs, to identify ourself in the peer list to
		// avoid connecting back to it.
		peer_id m_peer_id;

		// ==============================
		// The following members are specifically
		// ordered to make the 24 bit members
		// properly 32 bit aligned by inserting
		// 8 bits after each one
		// ==============================

		// the session time timestamp of when we entered upload mode
		// if we're currently in upload-mode
		boost::uint16_t m_upload_mode_time;

		// true when this torrent should anncounce to
		// trackers
		bool m_announce_to_trackers:1;

		// true when this torrent should anncounce to
		// the local network
		bool m_announce_to_lsd:1;

		// is set to true every time there is an incoming
		// connection to this torrent
		bool m_has_incoming:1;

		// this is set to true when the files are checked
		// before the files are checked, we don't try to
		// connect to peers
		bool m_files_checked:1;

		// determines the storage state for this torrent.
		unsigned int m_storage_mode:2;

		// this is true while tracker announcing is enabled
		// is is disabled while paused and checking files
		bool m_announcing:1;

		// this is > 0 while the tracker deadline timer
		// is in use. i.e. one or more trackers are waiting
		// for a reannounce
		boost::int8_t m_waiting_tracker;

// ----

		// total time we've been active on this torrent. i.e. either (trying to)
		// download or seed. does not count time when the torrent is stopped or
		// paused. specified in seconds. This only track time _before_ we started
		// the torrent this last time. When the torrent is paused, this counter is
		// incremented to include this current session.
		unsigned int m_active_time:24;

		// the index to the last tracker that worked
		boost::int8_t m_last_working_tracker;

// ----

		// total time we've been finished with this torrent.
		// does not count when the torrent is stopped or paused.
		unsigned int m_finished_time:24;

		// in case the piece picker hasn't been constructed
		// when this settings is set, this variable will keep
		// its value until the piece picker is created
		bool m_sequential_download:1;

		// this is set if the auto_sequential setting is true and this swarm
		// satisfies the criteria to be considered high-availability. i.e. if
		// there's mostly seeds in the swarm, download the files sequentially
		// for improved disk I/O performance.
		bool m_auto_sequential:1;

		// this means we haven't verified the file content
		// of the files we're seeding. the m_verified bitfield
		// indicates which pieces have been verified and which
		// haven't
		bool m_seed_mode:1;

		// if this is true, we're currently super seeding this
		// torrent.
		bool m_super_seeding:1;

		// this is set when we don't want to load seed_mode,
		// paused or auto_managed from the resume data
		const bool m_override_resume_data:1;

#ifndef TORRENT_NO_DEPRECATE
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		// this is true while there is a country
		// resolution in progress. To avoid flodding
		// the DNS request queue, only one ip is resolved
		// at a time.
		mutable bool m_resolving_country:1;

		// this is true if the user has enabled
		// country resolution in this torrent
		bool m_resolve_countries:1;
#endif
#endif

		// set to false when saving resume data. Set to true
		// whenever something is downloaded
		bool m_need_save_resume_data:1;

// ----

		// total time we've been available as a seed on this torrent.
		// does not count when the torrent is stopped or paused. This value only
		// accounts for the time prior to the current start of the torrent. When
		// the torrent is paused, this counter is incremented to account for the
		// additional seeding time.
		unsigned int m_seeding_time:24;

// ----

		// the maximum number of uploads for this torrent
		unsigned int m_max_uploads:24;

		// these are the flags sent in on a call to save_resume_data
		// we need to save them to check them in write_resume_data
		boost::uint8_t m_save_resume_flags;

// ----

		// the number of unchoked peers in this torrent
		unsigned int m_num_uploads:24;

		// when this is set, second_tick will perform the actual
		// work of refreshing the suggest pieces
		bool m_need_suggest_pieces_refresh:1;

		// this is set to true when the torrent starts up
		// The first tracker response, when this is true,
		// will attempt to connect to a bunch of peers immediately
		// and set this to false. We only do this once to get
		// the torrent kick-started
		bool m_need_connect_boost:1;

		// rotating sequence number for LSD announces sent out.
		// used to only use IP broadcast for every 8th lsd announce
		boost::uint8_t m_lsd_seq:3;

		// this is set to true if the torrent was started without
		// metadata. It is used to save metadata in the resume file
		// by default for such torrents. It does not necessarily
		// have to be a magnet link.
		bool m_magnet_link:1;

		// set to true if the session IP filter applies to this
		// torrent or not. Defaults to true.
		bool m_apply_ip_filter:1;

		// if set to true, add tracker URLs loaded from resume
		// data into this torrent instead of replacing them
		bool m_merge_resume_trackers:1;

// ----

		// the number of bytes of padding files
		boost::uint32_t m_padding:24;

		// TODO: gap of 8 bits available here

// ----

		// the scrape data from the tracker response, this
		// is optional and may be 0xffffff
		boost::uint32_t m_incomplete:24;

		// true when the torrent should announce to
		// the DHT
		bool m_announce_to_dht:1;

		// these represent whether or not this torrent is counted
		// in the total counters of active seeds and downloads
		// in the session.
		bool m_is_active_download:1;
		bool m_is_active_finished:1;

		// even if we're not built to support SSL torrents,
		// remember that this is an SSL torrent, so that we don't
		// accidentally start seeding it without any authentication.
		bool m_ssl_torrent:1;

		// this is set to true if we're trying to delete the
		// files belonging to it. When set, don't write any
		// more blocks to disk!
		bool m_deleted:1;

		// pinned torrents are locked in RAM and won't be unloaded
		// in favor of more active torrents. When the torrent is added,
		// the user may choose to initialize this to 1, in which case
		// it will never be unloaded from RAM
		bool m_pinned:1;

		// when this is false, we should unload the torrent as soon
		// as the no other async. job needs the torrent loaded
		bool m_should_be_loaded:1;

// ----

		// the timestamp of the last piece passed for this torrent specified in
		// session_time. This is signed because it must be able to represent time
		// before the session started
		boost::int16_t m_last_download;

		// the number of peer connections to seeds. This should be the same as
		// counting the peer connections that say true for is_seed()
		boost::uint16_t m_num_seeds;

		// this is the number of peers that are seeds, and count against
		// m_num_seeds, but have not yet been connected
		boost::uint16_t m_num_connecting_seeds;

		// the timestamp of the last byte uploaded from this torrent specified in
		// session_time. This is signed because it must be able to represent time
		// before the session started.
		boost::int16_t m_last_upload;

		// this is a second count-down to when we should tick the
		// storage for this torrent. Ticking the storage is used
		// to periodically flush the partfile metadata and possibly
		// other deferred flushing. Any disk operation starts this
		// counter (unless it's already counting down). 0 means no
		// ticking is needed.
		boost::uint8_t m_storage_tick;

// ----

		// if this is true, libtorrent may pause and resume
		// this torrent depending on queuing rules. Torrents
		// started with auto_managed flag set may be added in
		// a paused state in case there are no available
		// slots.
		bool m_auto_managed:1;

		enum { no_gauge_state = 0xf };
		// the current stats gauge this torrent counts against
		boost::uint32_t m_current_gauge_state:4;

		// set to true while moving the storage
		bool m_moving_storage:1;

		// this is true if this torrent is considered inactive from the
		// queuing mechanism's point of view. If a torrent doesn't transfer
		// at high enough rates, it's inactive.
		bool m_inactive:1;

// ----

		// the scrape data from the tracker response, this
		// is optional and may be 0xffffff
		unsigned int m_downloaded:24;

		// the timestamp of the last scrape request to one of the trackers in
		// this torrent specified in session_time. This is signed because it must
		// be able to represent time before the session started
		boost::int16_t m_last_scrape;

// ----

		// progress parts per million (the number of
		// millionths of completeness)
		unsigned int m_progress_ppm:20;

		// this is true when our effective inactive state is different from our
		// actual inactive state. Whenever this state changes, there is a
		// quarantine period until we change the effective state. This is to avoid
		// flapping. If the state changes back during this period, we cancel the
		// quarantine
		bool m_pending_active_change:1;

		// if this is set, accept the save path saved in the resume data, if
		// present
		bool m_use_resume_save_path:1;

		// if set to true, add web seed URLs loaded from resume
		// data into this torrent instead of replacing the ones from the .torrent
		// file
		bool m_merge_resume_http_seeds:1;

		// if this is set, whenever transitioning into a downloading/seeding state
		// from a non-downloading/seeding state, the torrent is paused.
		bool m_stop_when_ready:1;

#if TORRENT_USE_ASSERTS
	public:
		// set to false until we've loaded resume data
		bool m_resume_data_loaded;

		// set to true when torrent is start()ed. It may only be started once
		bool m_was_started;

		// this is set to true while we're looping over m_connections. We may not
		// mutate the list while doing this
		mutable int m_iterating_connections;
#endif
	};

	struct torrent_ref_holder
	{
		torrent_ref_holder(torrent* t, char const* p)
			: m_torrent(t)
			, m_purpose(p)
		{
			if (m_torrent) m_torrent->inc_refcount(m_purpose);
		}

		~torrent_ref_holder()
		{
			if (m_torrent) m_torrent->dec_refcount(m_purpose);
		}
		torrent* m_torrent;
		char const* m_purpose;
	};

}

#endif // TORRENT_TORRENT_HPP_INCLUDED

