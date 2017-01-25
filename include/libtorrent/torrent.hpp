/*

Copyright (c) 2003-2016, Arvid Norberg
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

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/limits.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/version.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/policy.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/bandwidth_limit.hpp"
#include "libtorrent/bandwidth_queue_entry.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/deadline_timer.hpp"
#include "libtorrent/union_endpoint.hpp"

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

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
	struct logger;
#endif

	class piece_manager;
	struct torrent_plugin;
	struct bitfield;
	struct announce_entry;
	struct tracker_request;
	struct add_torrent_params;
	struct storage_interface;
	class bt_peer_connection;
	struct listen_socket_t;

	namespace aux
	{
		struct session_impl;
		struct piece_checker_data;
	}

	struct time_critical_piece
	{
		// when this piece was first requested
		ptime first_requested;
		// when this piece was last requested
		ptime last_requested;
		// by what time we want this piece
		ptime deadline;
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

	// a torrent is a class that holds information
	// for a specific download. It updates itself against
	// the tracker
	class TORRENT_EXTRA_EXPORT torrent: public request_callback
		, public boost::enable_shared_from_this<torrent>
	{
	public:

		torrent(aux::session_impl& ses, tcp::endpoint const& net_interface
			, int block_size, int seq, add_torrent_params const& p
			, sha1_hash const& info_hash);
		~torrent();

#ifndef TORRENT_DISABLE_ENCRYPTION
		sha1_hash const& obfuscated_hash() const
		{ return m_obfuscated_hash; }
#endif

		// This may be called from multiple threads
		sha1_hash const& info_hash() const { return m_info_hash; }
	
		bool is_deleted() const { return m_deleted; }

		// starts the announce timer
		void start();

		void start_download_url();

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(boost::shared_ptr<torrent_plugin>);
		void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> const& ext
			, void* userdata);
		void notify_extension_add_peer(tcp::endpoint const& ip, int src, int flags);
#endif

		peer_connection* find_lowest_ranking_peer() const;

#if TORRENT_USE_ASSERTS
		bool has_peer(peer_connection* p) const
		{ return m_connections.find(p) != m_connections.end(); }
#endif

		// this is called when the torrent has metadata.
		// it will initialize the storage and the piece-picker
		void init();

		// find the peer that introduced us to the given endpoint. This is
		// used when trying to holepunch. We need the introducer so that we
		// can send a rendezvous connect message
		bt_peer_connection* find_introducer(tcp::endpoint const& ep) const;

		// if we're connected to a peer at ep, return its peer connection
		// only count BitTorrent peers
		bt_peer_connection* find_peer(tcp::endpoint const& ep) const;

		void on_resume_data_checked(int ret, disk_io_job const& j);
		void on_force_recheck(int ret, disk_io_job const& j);
		void on_piece_checked(int ret, disk_io_job const& j);
		void files_checked();
		void start_checking();

		void start_announcing();
		void stop_announcing();

		void send_share_mode();
		void send_upload_only();

		void set_share_mode(bool s);
		bool share_mode() const { return m_share_mode; }

		bool graceful_pause() const { return m_graceful_pause_mode; }

		void set_upload_mode(bool b);
		bool upload_mode() const { return m_upload_mode || m_graceful_pause_mode; }
		bool is_upload_only() const { return is_finished() || upload_mode(); }

		int seed_rank(session_settings const& s) const;

		enum flags_t { overwrite_existing = 1 };
		void add_piece(int piece, char const* data, int flags = 0);
		void on_disk_write_complete(int ret, disk_io_job const& j
			, peer_request p);
		void on_disk_cache_complete(int ret, disk_io_job const& j);

		void set_progress_ppm(int p) { m_progress_ppm = p; }
		struct read_piece_struct
		{
			boost::shared_array<char> piece_data;
			int blocks_left;
			bool fail;
			error_code error;
		};
		void read_piece(int piece);
		void on_disk_read_complete(int ret, disk_io_job const& j, peer_request r, read_piece_struct* rp);

		storage_mode_t storage_mode() const { return (storage_mode_t)m_storage_mode; }
		storage_interface* get_storage()
		{
			if (!m_owning_storage) return 0;
			return m_owning_storage->get_storage_impl();
		}

		// this will flag the torrent as aborted. The main
		// loop in session_impl will check for this state
		// on all torrents once every second, and take
		// the necessary actions then.
		void abort();
		bool is_aborted() const { return m_abort; }

		void new_external_ip();

		torrent_status::state_t state() const { return (torrent_status::state_t)m_state; }
		void set_state(torrent_status::state_t s);

		session_settings const& settings() const;
		
		aux::session_impl& session() { return m_ses; }
		
		void set_sequential_download(bool sd);
		bool is_sequential_download() const
		{ return m_sequential_download; }
	
		void queue_up();
		void queue_down();
		void set_queue_position(int p);
		int queue_position() const { return m_sequence_number; }

		void second_tick(stat& accumulator, int tick_interval_ms);

		// see if we need to connect to web seeds, and if so,
		// connect to them
		void maybe_connect_web_seeds();

		std::string name() const;

		stat statistics() const { return m_stat; }
		void add_stats(stat const& s);
		size_type bytes_left() const;
		int block_bytes_wanted(piece_block const& p) const;
		void bytes_done(torrent_status& st, bool accurate) const;
		size_type quantized_bytes_done() const;

		void ip_filter_updated() { m_policy.ip_filter_updated(); }

		void handle_disk_error(disk_io_job const& j, peer_connection* c = 0);
		void clear_error();
		void set_error(error_code const& ec, std::string const& file);
		bool has_error() const { return !!m_error; }
		error_code error() const { return m_error; }

		void flush_cache();
		void pause(bool graceful = false);
		void resume();
		void set_allow_peers(bool b, bool graceful_pause = false);
		void set_announce_to_dht(bool b) { m_announce_to_dht = b; }
		void set_announce_to_trackers(bool b) { m_announce_to_trackers = b; }
		void set_announce_to_lsd(bool b) { m_announce_to_lsd = b; }

		ptime started() const { return m_started; }
		void do_pause();
		void do_resume();

		bool is_paused() const;
		bool allows_peers() const { return m_allow_peers; }
		bool is_torrent_paused() const { return !m_allow_peers || m_graceful_pause_mode; }
		void force_recheck();
		void save_resume_data(int flags);

		bool is_active_download() const;
		bool is_active_finished() const;
		void update_guage();

		bool need_save_resume_data() const
		{
			// save resume data every 15 minutes regardless, just to
			// keep stats up to date
			return m_need_save_resume_data || time(0) - m_last_saved_resume > 15 * 60;
		}

		bool is_auto_managed() const { return m_auto_managed; }
		void auto_managed(bool a);

		bool should_check_files() const;

		bool delete_files();

		// ============ start deprecation =============
		void filter_piece(int index, bool filter);
		void filter_pieces(std::vector<bool> const& bitmask);
		bool is_piece_filtered(int index) const;
		void filtered_pieces(std::vector<bool>& bitmask) const;
		void filter_files(std::vector<bool> const& files);
#if !TORRENT_NO_FPU
		void file_progress(std::vector<float>& fp) const;
#endif
		// ============ end deprecation =============

		void piece_availability(std::vector<int>& avail) const;
		
		void set_piece_priority(int index, int priority);
		int piece_priority(int index) const;

		void prioritize_pieces(std::vector<int> const& pieces);
		void piece_priorities(std::vector<int>*) const;

		void set_file_priority(int index, int priority);
		int file_priority(int index) const;

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

		void file_progress(std::vector<size_type>& fp, int flags = 0) const;

		void use_interface(std::string net_interface);
		tcp::endpoint get_interface() const;
		
		void connect_to_url_seed(std::list<web_seed_entry>::iterator url);
		bool connect_to_peer(policy::peer* peerinfo, bool ignore_limit = false);

		int priority() const { return m_priority; }
		void set_priority(int prio)
		{
			TORRENT_ASSERT(prio <= 255 && prio >= 0);
			if (prio > 255) prio = 255;
			else if (prio < 0) prio = 0;
			m_priority = prio;
			state_updated();
		}

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		void resolve_countries(bool r)
		{ m_resolve_countries = r; }

		bool resolving_countries() const
		{
			return m_resolve_countries && !m_ses.settings().force_proxy;
		}
#endif

// --------------------------------------------
		// BANDWIDTH MANAGEMENT

		bandwidth_channel m_bandwidth_channel[2];

		int bandwidth_throttle(int channel) const;

// --------------------------------------------
		// PEER MANAGEMENT
		
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_ERROR_LOGGING || defined TORRENT_LOGGING
		void log_to_all_peers(char const* message);
#endif

		// add or remove a url that will be attempted for
		// finding the file(s) in this torrent.
		void add_web_seed(std::string const& url, web_seed_entry::type_t type);
		void add_web_seed(std::string const& url, web_seed_entry::type_t type
			, std::string const& auth, web_seed_entry::headers_t const& extra_headers);
	
		void remove_web_seed(std::string const& url, web_seed_entry::type_t type);
		void disconnect_web_seed(peer_connection* p);

		void retry_web_seed(peer_connection* p, int retry = 0);

		void remove_web_seed(peer_connection* p);

		std::list<web_seed_entry> web_seeds() const
		{ return m_web_seeds; }

		std::set<std::string> web_seeds(web_seed_entry::type_t type) const;

		bool free_upload_slots() const
		{ return m_num_uploads < m_max_uploads; }

		bool choke_peer(peer_connection& c);
		bool unchoke_peer(peer_connection& c, bool optimistic = false);

		// used by peer_connection to attach itself to a torrent
		// since incoming connections don't know what torrent
		// they're a part of until they have received an info_hash.
		// false means attach failed
		bool attach_peer(peer_connection* p);

		// this will remove the peer and make sure all
		// the pieces it had have their reference counter
		// decreased in the piece_picker
		void remove_peer(peer_connection* p);

		void cancel_block(piece_block block);

		bool want_more_peers() const;
		bool try_connect_peer();
		void add_peer(tcp::endpoint const& adr, int source);

		// the number of peers that belong to this torrent
		int num_peers() const { return (int)m_connections.size(); }
		int num_seeds() const;

		typedef std::set<peer_connection*>::iterator peer_iterator;
		typedef std::set<peer_connection*>::const_iterator const_peer_iterator;

		const_peer_iterator begin() const { return m_connections.begin(); }
		const_peer_iterator end() const { return m_connections.end(); }

		peer_iterator begin() { return m_connections.begin(); }
		peer_iterator end() { return m_connections.end(); }

		void resolve_peer_country(boost::intrusive_ptr<peer_connection> const& p) const;

		void get_full_peer_list(std::vector<peer_list_entry>& v) const;
		void get_peer_info(std::vector<peer_info>& v);
		void get_download_queue(std::vector<partial_piece_info>* queue);

		void refresh_explicit_cache(int cache_size);

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
			, std::vector<peer_entry>& e, int interval, int min_interval
			, int complete, int incomplete, int downloaded
			, address const& external_ip, std::string const& trackerid);
		virtual void tracker_request_error(tracker_request const& r
			, int response_code, error_code const& ec, const std::string& msg
			, int retry_interval);
		virtual void tracker_warning(tracker_request const& req
			, std::string const& msg);
		virtual void tracker_scrape_response(tracker_request const& req
			, int complete, int incomplete, int downloaded, int downloaders);

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

		// returns the absolute time when the next tracker
		// announce will take place.
		ptime next_announce() const;

		// forcefully sets next_announce to the current time
		void force_tracker_request(ptime, int tracker_idx);
		void scrape_tracker();
		void announce_with_tracker(tracker_request::event_t e
			= tracker_request::none
			, address const& bind_interface = address_v4::any());
		int seconds_since_last_scrape() const { return m_last_scrape; }

#ifndef TORRENT_DISABLE_DHT
		void dht_announce();
#endif

		// sets the username and password that will be sent to
		// the tracker
		void set_tracker_login(std::string const& name, std::string const& pw);

		// the tcp::endpoint of the tracker that we managed to
		// announce ourself at the last time we tried to announce
		tcp::endpoint current_tracker() const;

		announce_entry* find_tracker(tracker_request const& r);

// --------------------------------------------
		// PIECE MANAGEMENT

		void recalc_share_mode();

		void update_sparse_piece_prio(int piece, int cursor, int reverse_cursor);

		void get_suggested_pieces(std::vector<int>& s) const;

		bool super_seeding() const
		{
			// we're not super seeding if we're not a seed
			return m_super_seeding && is_finished();
		}
		
		void super_seeding(bool on);
		int get_piece_to_super_seed(bitfield const&);

		// returns true if we have downloaded the given piece
		bool have_piece(int index) const
		{
			if (!valid_metadata()) return false;
			if (!has_picker()) return is_seed();
			return m_picker->have_piece(index);
		}

		// called when we learn that we have a piece
		// only once per piece
		void we_have(int index);

		int num_have() const
		{
			// pretend we have every piece when in seed mode
			if (m_seed_mode) {
				return m_torrent_file->num_pieces();
			}

			return has_picker()
				? m_picker->num_have()
				: m_torrent_file->num_pieces();
		}

		// when we get a have message, this is called for that piece
		void peer_has(int index, peer_connection const* peer)
		{
			if (has_picker())
			{
				m_picker->inc_refcount(index, peer);
			}
#ifdef TORRENT_DEBUG
			else
			{
				TORRENT_ASSERT(is_seed());
			}
#endif
		}
		
		// when we get a bitfield message, this is called for that piece
		void peer_has(bitfield const& bits, peer_connection const* peer)
		{
			if (has_picker())
			{
				if (bits.all_set() && bits.size() > 0)
					m_picker->inc_refcount_all(peer);
				else
					m_picker->inc_refcount(bits, peer);
			}
#ifdef TORRENT_DEBUG
			else
			{
				TORRENT_ASSERT(is_seed());
			}
#endif
		}

		void peer_has_all(peer_connection const* peer)
		{
			if (has_picker())
			{
				m_picker->inc_refcount_all(peer);
			}
#ifdef TORRENT_DEBUG
			else
			{
				TORRENT_ASSERT(is_seed());
			}
#endif
		}

		void peer_lost(bitfield const& bits, peer_connection const* peer)
		{
			if (has_picker())
			{
				if (bits.all_set() && bits.size() > 0)
					m_picker->dec_refcount_all(peer);
				else
					m_picker->dec_refcount(bits, peer);
			}
#ifdef TORRENT_DEBUG
			else
			{
				TORRENT_ASSERT(is_seed());
			}
#endif
		}

		void peer_lost(int index, peer_connection const* peer)
		{
			if (has_picker())
			{
				m_picker->dec_refcount(index, peer);
			}
#ifdef TORRENT_DEBUG
			else
			{
				TORRENT_ASSERT(is_seed());
			}
#endif
		}

		int block_size() const { TORRENT_ASSERT(m_block_size_shift > 0); return 1 << m_block_size_shift; }
		peer_request to_req(piece_block const& p) const;

		void disconnect_all(error_code const& ec);
		int disconnect_peers(int num, error_code const& ec);

		// this is called wheh the torrent has completed
		// the download. It will post an event, disconnect
		// all seeds and let the tracker know we're finished.
		void completed();

#if TORRENT_USE_I2P
		void on_i2p_resolve(error_code const& ec, char const* dest);
#endif

		// this is the asio callback that is called when a name
		// lookup for a PEER is completed.
		void on_peer_name_lookup(error_code const& e, tcp::resolver::iterator i
			, peer_id pid);

		// this is the asio callback that is called when a name
		// lookup for a WEB SEED is completed.
		void on_name_lookup(error_code const& e, tcp::resolver::iterator i
			, std::list<web_seed_entry>::iterator url, tcp::endpoint proxy);

		void connect_web_seed(std::list<web_seed_entry>::iterator web, tcp::endpoint a);

		// this is the asio callback that is called when a name
		// lookup for a proxy for a web seed is completed.
		void on_proxy_name_lookup(error_code const& e, tcp::resolver::iterator i
			, std::list<web_seed_entry>::iterator url);

		// remove a web seed, or schedule it for removal in case there
		// are outstanding operations on it
		void remove_web_seed(std::list<web_seed_entry>::iterator web);

		// this is called when the torrent has finished. i.e.
		// all the pieces we have not filtered have been downloaded.
		// If no pieces are filtered, this is called first and then
		// completed() is called immediately after it.
		void finished();

		// This is the opposite of finished. It is called if we used
		// to be finished but enabled some files for download so that
		// we wasn't finished anymore.
		void resume_download();

		void async_verify_piece(int piece_index, boost::function<void(int)> const&);

		// this is called from the peer_connection
		// each time a piece has failed the hash
		// test
		void piece_finished(int index, int passed_hash_check);

		// piece_passed is called when a piece passes the hash check
		// this will tell all peers that we just got his piece
		// and also let the piece picker know that we have this piece
		// so it wont pick it for download
		void piece_passed(int index);

		// piece_failed is called when a piece fails the hash check
		void piece_failed(int index);

		// this will restore the piece picker state for a piece
		// by re marking all the requests to blocks in this piece
		// that are still outstanding in peers' download queues.
		// this is done when a piece fails
		void restore_piece_state(int index);

		enum wasted_reason_t
		{
			piece_timed_out, piece_cancelled, piece_unknown, piece_seed, piece_end_game, piece_closing
			, waste_reason_max
		};
		void add_redundant_bytes(int b, wasted_reason_t reason);
		void add_failed_bytes(int b);

		// this is true if we have all the pieces
		bool is_seed() const
		{
			return valid_metadata()
				&& (!m_picker
				|| m_seed_mode
				|| m_state == torrent_status::seeding
				|| m_picker->num_have() == m_picker->num_pieces());
		}

		// this is true if we have all the pieces that we want
		bool is_finished() const
		{
			if (is_seed()) return true;
			return valid_metadata() && m_torrent_file->num_pieces()
				- m_picker->num_have() - m_picker->num_filtered() == 0;
		}

		bool is_inactive() const
		{
			if (!settings().dont_count_slow_torrents) return false;
			return m_inactive;
		}

		std::string save_path() const;
		alert_manager& alerts() const;
		piece_picker& picker()
		{
			TORRENT_ASSERT(m_picker.get());
			return *m_picker;
		}
		bool has_picker() const
		{
			return m_picker.get() != 0;
		}
		policy& get_policy() { return m_policy; }
		piece_manager& filesystem();
		torrent_info const& torrent_file() const
		{ return *m_torrent_file; }

		boost::intrusive_ptr<torrent_info const> get_torrent_copy();

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
		void read_resume_data(lazy_entry const& rd);

		void seen_complete() { m_last_seen_complete = time(0); }
		int time_since_complete() const { return int(time(0) - m_last_seen_complete); }
		time_t last_seen_complete() const { return m_last_seen_complete; }

		// LOGGING
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		virtual void debug_log(const char* fmt, ...) const;
#endif

		// DEBUG
#if TORRENT_USE_INVARIANT_CHECKS
		void check_invariant() const;
#endif

// --------------------------------------------
		// RESOURCE MANAGEMENT

		int get_peer_upload_limit(tcp::endpoint ip) const;
		int get_peer_download_limit(tcp::endpoint ip) const;
		void set_peer_upload_limit(tcp::endpoint ip, int limit);
		void set_peer_download_limit(tcp::endpoint ip, int limit);

		void set_upload_limit(int limit, bool state_update = true);
		int upload_limit() const;
		void set_download_limit(int limit, bool state_update = true);
		int download_limit() const;

		void set_max_uploads(int limit, bool state_update = true);
		int max_uploads() const { return m_max_uploads; }
		void set_max_connections(int limit, bool state_update = true);
		int max_connections() const { return m_max_connections; }

		// flags are defined in storage.hpp
		void move_storage(std::string const& save_path, int flags);

		// renames the file with the given index to the new name
		// the name may include a directory path
		// returns false on failure
		bool rename_file(int index, std::string const& name);

		// unless this returns true, new connections must wait
		// with their initialization.
		bool ready_for_connections() const
		{ return m_connections_initialized; }
		bool valid_metadata() const
		{ return m_torrent_file->is_valid(); }
		bool are_files_checked() const
		{ return m_files_checked; }

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

		void update_last_upload() { m_last_upload = 0; }

		void set_apply_ip_filter(bool b);
		bool apply_ip_filter() const { return m_apply_ip_filter; }

		void queue_torrent_check();
		void dequeue_torrent_check();

		void clear_in_state_update()
		{ m_in_state_updates = false; }

		void inc_num_connecting()
		{ ++m_num_connecting; }
		void dec_num_connecting()
		{
			TORRENT_ASSERT(m_num_connecting > 0);
			--m_num_connecting;
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
		{ return m_time_critical_pieces.size(); }

	private:

		void on_files_deleted(int ret, disk_io_job const& j);
		void on_files_released(int ret, disk_io_job const& j);
		void on_torrent_paused(int ret, disk_io_job const& j);
		void on_storage_moved(int ret, disk_io_job const& j);
		void on_save_resume_data(int ret, disk_io_job const& j);
		void on_file_renamed(int ret, disk_io_job const& j);
		void on_cache_flushed(int ret, disk_io_job const& j);

		void on_piece_verified(int ret, disk_io_job const& j
			, boost::function<void(int)> f);
	
		int prioritize_tracker(int tracker_index);
		int deprioritize_tracker(int tracker_index);

		void on_country_lookup(error_code const& error, tcp::resolver::iterator i
			, boost::intrusive_ptr<peer_connection> p) const;
		bool request_bandwidth_from_session(int channel) const;

		void update_peer_interest(bool was_finished);
		void prioritize_udp_trackers();

		void parse_response(const entry& e, std::vector<peer_entry>& peer_list);

		void update_tracker_timer(ptime now);

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

		policy m_policy;

		// all time totals of uploaded and downloaded payload
		// stored in resume data
		size_type m_total_uploaded;
		size_type m_total_downloaded;

		// if this torrent is running, this was the time
		// when it was started. This is used to have a
		// bias towards keeping seeding torrents that
		// recently was started, to avoid oscillation
		ptime m_started;

		boost::intrusive_ptr<torrent_info> m_torrent_file;

		// if this pointer is 0, the torrent is in
		// a state where the metadata hasn't been
		// received yet, or during shutdown.
		// the piece_manager keeps the torrent object
		// alive by holding a shared_ptr to it and
		// the torrent keeps the piece manager alive
		// with this intrusive_ptr. This cycle is
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
		boost::intrusive_ptr<piece_manager> m_owning_storage;

		// this is a weak (non owninig) pointer to
		// the piece_manager. This is used after the torrent
		// has been aborted, and it can no longer own
		// the object.
		piece_manager* m_storage;

#ifdef TORRENT_USE_OPENSSL
		boost::shared_ptr<asio::ssl::context> m_ssl_ctx;

#if BOOST_VERSION >= 104700
		bool verify_peer_cert(bool preverified, boost::asio::ssl::verify_context& ctx);
#endif

		void init_ssl(std::string const& cert);
#endif

		std::set<peer_connection*> m_connections;

		// of all peers in m_connections, this is the number
		// of peers that are outgoing and still waiting to
		// complete the connection. This is used to possibly
		// kick out these connections when we get incoming
		// connections (if we've reached the connection limit)
		int m_num_connecting;

		// The list of web seeds in this torrent. Seeds
		// with fatal errors are removed from the set
		std::list<web_seed_entry> m_web_seeds;

#ifndef TORRENT_DISABLE_EXTENSIONS
		typedef std::list<boost::shared_ptr<torrent_plugin> > extension_list_t;
		extension_list_t m_extensions;
#endif

		// used for tracker announces
		deadline_timer m_tracker_timer;

		// this is the upload and download statistics for the whole torrent.
		// it's updated from all its peers once every second.
		libtorrent::stat m_stat;

		// -----------------------------

		// a back reference to the session
		// this torrent belongs to.
		aux::session_impl& m_ses;

		// used to resolve hostnames for web seeds
		mutable tcp::resolver m_host_resolver;

		std::vector<boost::uint8_t> m_file_priority;

		// this vector contains the number of bytes completely
		// downloaded (as in passed-hash-check) in each file.
		// this lets us trigger on individual files completing
		std::vector<size_type> m_file_progress;

		boost::scoped_ptr<piece_picker> m_picker;

		std::vector<announce_entry> m_trackers;
		// this is an index into m_trackers

		// this list is sorted by time_critical_piece::deadline
		std::deque<time_critical_piece> m_time_critical_pieces;

		std::string m_trackerid;
		std::string m_username;
		std::string m_password;

		// the network interfaces outgoing connections
		// are opened through. If there is more then one,
		// they are used in a round-robin fasion
		std::vector<union_endpoint> m_net_interfaces;

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
		std::vector<char> m_torrent_file_buf;

		// each bit represents a piece. a set bit means
		// the piece has had its hash verified. This
		// is only used in seed mode (when m_seed_mode
		// is true)
		bitfield m_verified;

		// set if there's an error on this torrent
		error_code m_error;
		// if the error ocurred on a file, this is the file
		std::string m_error_file;

		// used if there is any resume data
		std::vector<char> m_resume_data;
		lazy_entry m_resume_entry;

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
		time_t m_last_saved_resume;

		// this was the last time _we_ saw a seed in this swarm
		time_t m_last_seen_complete;

		// this is the time last any of our peers saw a seed
		// in this swarm
		time_t m_swarm_last_seen_complete;

		// m_num_verified = m_verified.count()
		boost::uint32_t m_num_verified;

#ifndef TORRENT_DISABLE_ENCRYPTION
		// this is SHA1("req2" + info-hash), used for
		// encrypted hand shakes
		sha1_hash m_obfuscated_hash;
#endif

		// keep a copy if the info-hash here, so it can be accessed from multiple
		// threads, and be cheap to access from the client
		sha1_hash m_info_hash;

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

		// ==============================
		// The following members are specifically
		// ordered to make the 24 bit members
		// properly 32 bit aligned by inserting
		// 8 bits after each one
		// ==============================

		// the number of seconds we've been in upload mode
		unsigned int m_upload_mode_time:24;

		// the state of this torrent (queued, checking, downloading, etc.)
		unsigned int m_state:3;

		// determines the storage state for this torrent.
		unsigned int m_storage_mode:2;

		// this is true while tracker announcing is enabled
		// is is disabled while paused and checking files
		bool m_announcing:1;

		// this is true while the tracker deadline timer
		// is in use. i.e. one or more trackers are waiting
		// for a reannounce
		bool m_waiting_tracker:1;

		// this means we haven't verified the file content
		// of the files we're seeding. the m_verified bitfield
		// indicates which pieces have been verified and which
		// haven't
		bool m_seed_mode:1;

// ----

		// total time we've been available on this torrent
		// does not count when the torrent is stopped or paused
		// in seconds
		unsigned int m_active_time:24;

		// the index to the last tracker that worked
		boost::int8_t m_last_working_tracker;

// ----

		// total time we've been finished with this torrent
		// does not count when the torrent is stopped or paused
		unsigned int m_finished_time:24;

		// in case the piece picker hasn't been constructed
		// when this settings is set, this variable will keep
		// its value until the piece picker is created
		bool m_sequential_download:1;

		// is false by default and set to
		// true when the first tracker reponse
		// is received
		bool m_got_tracker_response:1;

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

		// if this is true, we're currently super seeding this
		// torrent.
		bool m_super_seeding:1;

		// this is set when we don't want to load seed_mode,
		// paused or auto_managed from the resume data
		const bool m_override_resume_data:1;

		// this is true while there is a country
		// resolution in progress. To avoid flodding
		// the DNS request queue, only one ip is resolved
		// at a time.
		mutable bool m_resolving_country:1;
		
		// this is true if the user has enabled
		// country resolution in this torrent
		bool m_resolve_countries:1;

		// set to false when saving resume data. Set to true
		// whenever something is downloaded
		bool m_need_save_resume_data:1;

// ----

		// total time we've been available as a seed on this torrent
		// does not count when the torrent is stopped or paused
		unsigned int m_seeding_time:24;

		// this is a counter that is decreased every
		// second, and when it reaches 0, the policy::pulse()
		// is called and the time scaler is reset to 10.
		boost::int8_t m_time_scaler;

// ----

		// the maximum number of uploads for this torrent
		unsigned int m_max_uploads:24;

		// these are the flags sent in on a call to save_resume_data
		// we need to save them to check them in write_resume_data
		boost::uint8_t m_save_resume_flags;

// ----

		// the number of unchoked peers in this torrent
		unsigned int m_num_uploads:24;

		// the size of a request block
		// each piece is divided into these
		// blocks when requested. The block size is
		// 1 << m_block_size_shift
		unsigned int m_block_size_shift:5;

		// is set to true every time there is an incoming
		// connection to this torrent
		bool m_has_incoming:1;

		// this is set to true when the files are checked
		// before the files are checked, we don't try to
		// connect to peers
		bool m_files_checked:1;

		// this is true if the torrent has been added to
		// checking queue in the session
		bool m_queued_for_checking:1;

// ----

		// the maximum number of connections for this torrent
		unsigned int m_max_connections:24;

		// set to true when this torrent has been paused but
		// is waiting to finish all current download requests
		// before actually closing all connections
		bool m_graceful_pause_mode:1;

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

		// this is the priority of the torrent. The higher
		// the value is, the more bandwidth is assigned to
		// the torrent's peers
		boost::uint32_t m_priority:8;

// ----

		// the scrape data from the tracker response, this
		// is optional and may be 0xffffff
		boost::uint32_t m_complete:24;

		// state subscription. If set, a pointer to this torrent
		// will be added to the m_state_updates set in session_impl
		// whenever this torrent's state changes (any state).
		bool m_state_subscription:1;

		// in state_updates list. When adding a torrent to the
		// session_impl's m_state_update list, this bit is set
		// to never add the same torrent twice
		bool m_in_state_updates:1;

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

		// set to true while moving the storage
		bool m_moving_storage:1;

		// this is true if this torrent is considered inactive from the
		// queuing mechanism's point of view. If a torrent doesn't transfer
		// at high enough rates, it's inactive.
		bool m_inactive:1;

// ----

		// the scrape data from the tracker response, this
		// is optional and may be 0xffffff
		boost::uint32_t m_incomplete:24;

		// is set to true when the torrent has
		// been aborted.
		bool m_abort:1;

		// true when the torrent should announce to
		// the DHT
		bool m_announce_to_dht:1;

		// true when this torrent should anncounce to
		// trackers
		bool m_announce_to_trackers:1;

		// true when this torrent should anncounce to
		// the local network
		bool m_announce_to_lsd:1;

		// is true if this torrent has allows having peers
		bool m_allow_peers:1;

		// set to true when this torrent may not download anything
		bool m_upload_mode:1;

		// if this is true, libtorrent may pause and resume
		// this torrent depending on queuing rules. Torrents
		// started with auto_managed flag set may be added in
		// a paused state in case there are no available
		// slots.
		bool m_auto_managed:1;

		// this is set when the torrent is in share-mode
		bool m_share_mode:1;

// ----

		// the number of seconds since the last piece passed for
		// this torrent
		boost::uint64_t m_last_download:24;

		// the number of seconds since the last scrape request to
		// one of the trackers in this torrent
		boost::uint64_t m_last_scrape:16;

		// the number of seconds since the last byte was uploaded
		// from this torrent
		boost::uint64_t m_last_upload:24;

// ----

		// the scrape data from the tracker response, this
		// is optional and may be 0xffffff
		unsigned int m_downloaded:24;

		// round-robin index into m_interfaces
		mutable boost::uint8_t m_interface_index;

// ----

		// progress parts per million (the number of
		// millionths of completeness)
		unsigned int m_progress_ppm:20;

		// the number of seconds this torrent has been under the inactive
		// threshold in terms of sending and receiving data. When this counter
		// reaches the settings.inactive_torrent_timeout it will be considered
		// inactive and possibly open up another queue slot, to start another,
		// queued, torrent. Every second it's above the threshold
		boost::int16_t m_inactive_counter;

		// if this is set, accept the save path saved in the resume data, if
		// present
		bool m_use_resume_save_path:1;

		// if set to true, add web seed URLs loaded from resume
		// data into this torrent instead of replacing the ones from the .torrent
		// file
		bool m_merge_resume_http_seeds:1;

#if TORRENT_USE_ASSERTS
	public:
		// set to false until we've loaded resume data
		bool m_resume_data_loaded;
#endif
	};
}

#endif // TORRENT_TORRENT_HPP_INCLUDED

