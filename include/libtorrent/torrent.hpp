/*

Copyright (c) 2003, Arvid Norberg
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
#include <iostream>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/limits.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/intrusive_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/policy.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/stat.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/bandwidth_limit.hpp"
#include "libtorrent/bandwidth_queue_entry.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/assert.hpp"

namespace libtorrent
{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
	struct logger;
#endif

	class piece_manager;
	struct torrent_plugin;

	namespace aux
	{
		struct session_impl;
		struct piece_checker_data;
	}

	namespace fs = boost::filesystem;

	// a torrent is a class that holds information
	// for a specific download. It updates itself against
	// the tracker
	class TORRENT_EXPORT torrent: public request_callback
		, public boost::enable_shared_from_this<torrent>
	{
	public:

		torrent(
			aux::session_impl& ses
			, aux::checker_impl& checker
			, boost::intrusive_ptr<torrent_info> tf
			, fs::path const& save_path
			, tcp::endpoint const& net_interface
			, storage_mode_t m_storage_mode
			, int block_size
			, storage_constructor_type sc
			, bool paused);

		// used with metadata-less torrents
		// (the metadata is downloaded from the peers)
		torrent(
			aux::session_impl& ses
			, aux::checker_impl& checker
			, char const* tracker_url
			, sha1_hash const& info_hash
			, char const* name
			, fs::path const& save_path
			, tcp::endpoint const& net_interface
			, storage_mode_t m_storage_mode
			, int block_size
			, storage_constructor_type sc
			, bool paused);

		~torrent();

		// starts the announce timer
		void start();

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(boost::shared_ptr<torrent_plugin>);
		void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> const& ext
			, void* userdata);
#endif

		// this is called when the torrent has metadata.
		// it will initialize the storage and the piece-picker
		void init();

		// this will flag the torrent as aborted. The main
		// loop in session_impl will check for this state
		// on all torrents once every second, and take
		// the necessary actions then.
		void abort();
		bool is_aborted() const { return m_abort; }

		// returns true if this torrent is being allocated
		// by the checker thread.
		bool is_allocating() const;
		
		session_settings const& settings() const;
		
		aux::session_impl& session() { return m_ses; }
		
		void set_sequenced_download_threshold(int threshold);
	
		bool verify_resume_data(entry& rd, std::string& error)
		{ TORRENT_ASSERT(m_storage); return m_storage->verify_resume_data(rd, error); }

		void second_tick(stat& accumulator, float tick_interval);

		// debug purpose only
		void print(std::ostream& os) const;

		std::string name() const;

		bool check_fastresume(aux::piece_checker_data&);
		std::pair<bool, float> check_files();
		void files_checked(std::vector<piece_picker::downloading_piece> const&
			unfinished_pieces);

		stat statistics() const { return m_stat; }
		size_type bytes_left() const;
		boost::tuples::tuple<size_type, size_type> bytes_done() const;
		size_type quantized_bytes_done() const;

		void ip_filter_updated() { m_policy.ip_filter_updated(); }

		void pause();
		void resume();
		bool is_paused() const { return m_paused; }

		void delete_files();

		// ============ start deprecation =============
		void filter_piece(int index, bool filter);
		void filter_pieces(std::vector<bool> const& bitmask);
		bool is_piece_filtered(int index) const;
		void filtered_pieces(std::vector<bool>& bitmask) const;
		void filter_files(std::vector<bool> const& files);
		// ============ end deprecation =============

		void piece_availability(std::vector<int>& avail) const;
		
		void set_piece_priority(int index, int priority);
		int piece_priority(int index) const;

		void prioritize_pieces(std::vector<int> const& pieces);
		void piece_priorities(std::vector<int>&) const;

		void prioritize_files(std::vector<int> const& files);

		torrent_status status() const;
		void file_progress(std::vector<float>& fp) const;

		void use_interface(const char* net_interface);
		tcp::endpoint const& get_interface() const { return m_net_interface; }
		
		void connect_to_url_seed(std::string const& url);
		bool connect_to_peer(policy::peer* peerinfo);

		void set_ratio(float ratio)
		{ TORRENT_ASSERT(ratio >= 0.0f); m_ratio = ratio; }

		float ratio() const
		{ return m_ratio; }

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		void resolve_countries(bool r)
		{ m_resolve_countries = r; }

		bool resolving_countries() const { return m_resolve_countries; }
#endif

// --------------------------------------------
		// BANDWIDTH MANAGEMENT

		bandwidth_limit m_bandwidth_limit[2];

		void request_bandwidth(int channel
			, boost::intrusive_ptr<peer_connection> const& p
			, int priority);

		void perform_bandwidth_request(int channel
			, boost::intrusive_ptr<peer_connection> const& p
			, int block_size, int priority);

		void expire_bandwidth(int channel, int amount);
		void assign_bandwidth(int channel, int amount, int blk);
		
		int bandwidth_throttle(int channel) const;

		int max_assignable_bandwidth(int channel) const
		{ return m_bandwidth_limit[channel].max_assignable(); }

// --------------------------------------------
		// PEER MANAGEMENT
		
		// add or remove a url that will be attempted for
		// finding the file(s) in this torrent.
		void add_url_seed(std::string const& url)
		{ m_web_seeds.insert(url); }
	
		void remove_url_seed(std::string const& url)
		{ m_web_seeds.erase(url); }

		void retry_url_seed(std::string const& url);

		std::set<std::string> url_seeds() const
		{ return m_web_seeds; }

		bool free_upload_slots() const
		{ return m_num_uploads < m_max_uploads; }

		void choke_peer(peer_connection& c);
		bool unchoke_peer(peer_connection& c);

		// used by peer_connection to attach itself to a torrent
		// since incoming connections don't know what torrent
		// they're a part of until they have received an info_hash.
		void attach_peer(peer_connection* p);

		// this will remove the peer and make sure all
		// the pieces it had have their reference counter
		// decreased in the piece_picker
		void remove_peer(peer_connection* p);

		void cancel_block(piece_block block);

		bool want_more_peers() const;
		bool try_connect_peer();

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

		void get_peer_info(std::vector<peer_info>& v);
		void get_download_queue(std::vector<partial_piece_info>& queue);

// --------------------------------------------
		// TRACKER MANAGEMENT

		// these are callbacks called by the tracker_connection instance
		// (either http_tracker_connection or udp_tracker_connection)
		// when this torrent got a response from its tracker request
		// or when a failure occured
		virtual void tracker_response(
			tracker_request const& r
			, std::vector<peer_entry>& e, int interval
			, int complete, int incomplete);
		virtual void tracker_request_timed_out(
			tracker_request const& r);
		virtual void tracker_request_error(tracker_request const& r
			, int response_code, const std::string& str);
		virtual void tracker_warning(std::string const& msg);
		virtual void tracker_scrape_response(tracker_request const& req
			, int complete, int incomplete, int downloaded);

		// generates a request string for sending
		// to the tracker
		tracker_request generate_tracker_request();

		// if no password and username is set
		// this will return an empty string, otherwise
		// it will concatenate the login and password
		// ready to be sent over http (but without
		// base64 encoding).
		std::string tracker_login() const;

		// returns the absolute time when the next tracker
		// announce will take place.
		ptime next_announce() const;

		// returns true if it is time for this torrent to make another
		// tracker request
		bool should_request();

		// forcefully sets next_announce to the current time
		void force_tracker_request();
		void force_tracker_request(ptime);
		void scrape_tracker();

		// sets the username and password that will be sent to
		// the tracker
		void set_tracker_login(std::string const& name, std::string const& pw);

		// the tcp::endpoint of the tracker that we managed to
		// announce ourself at the last time we tried to announce
		const tcp::endpoint& current_tracker() const;

// --------------------------------------------
		// PIECE MANAGEMENT

		// returns true if we have downloaded the given piece
		bool have_piece(int index) const
		{
			TORRENT_ASSERT(index >= 0 && index < (signed)m_have_pieces.size());
			return m_have_pieces[index];
		}

		const std::vector<bool>& pieces() const
		{ return m_have_pieces; }

		int num_pieces() const { return m_num_pieces; }

		// when we get a have- or bitfield- messages, this is called for every
		// piece a peer has gained.
		void peer_has(int index)
		{
			if (m_picker.get())
			{
				TORRENT_ASSERT(!is_seed());
				TORRENT_ASSERT(index >= 0 && index < (signed)m_have_pieces.size());
				m_picker->inc_refcount(index);
			}
#ifndef NDEBUG
			else
			{
				TORRENT_ASSERT(is_seed());
			}
#endif
		}
		
		void peer_has_all()
		{
			if (m_picker.get())
			{
				TORRENT_ASSERT(!is_seed());
				m_picker->inc_refcount_all();
			}
#ifndef NDEBUG
			else
			{
				TORRENT_ASSERT(is_seed());
			}
#endif
		}

		// when peer disconnects, this is called for every piece it had
		void peer_lost(int index)
		{
			if (m_picker.get())
			{
				TORRENT_ASSERT(!is_seed());
				TORRENT_ASSERT(index >= 0 && index < (signed)m_have_pieces.size());
				m_picker->dec_refcount(index);
			}
#ifndef NDEBUG
			else
			{
				TORRENT_ASSERT(is_seed());
			}
#endif
		}

		int block_size() const { TORRENT_ASSERT(m_block_size > 0); return m_block_size; }
		peer_request to_req(piece_block const& p);

		// this will tell all peers that we just got his piece
		// and also let the piece picker know that we have this piece
		// so it wont pick it for download
		void announce_piece(int index);

		void disconnect_all();

		// this is called wheh the torrent has completed
		// the download. It will post an event, disconnect
		// all seeds and let the tracker know we're finished.
		void completed();

		// this is the asio callback that is called when a name
		// lookup for a PEER is completed.
		void on_peer_name_lookup(asio::error_code const& e, tcp::resolver::iterator i
			, peer_id pid);

		// this is the asio callback that is called when a name
		// lookup for a WEB SEED is completed.
		void on_name_lookup(asio::error_code const& e, tcp::resolver::iterator i
			, std::string url, tcp::endpoint proxy);

		// this is the asio callback that is called when a name
		// lookup for a proxy for a web seed is completed.
		void on_proxy_name_lookup(asio::error_code const& e, tcp::resolver::iterator i
			, std::string url);

		// this is called when the torrent has finished. i.e.
		// all the pieces we have not filtered have been downloaded.
		// If no pieces are filtered, this is called first and then
		// completed() is called immediately after it.
		void finished();

		void async_verify_piece(int piece_index, boost::function<void(bool)> const&);

		// this is called from the peer_connection
		// each time a piece has failed the hash
		// test
		void piece_finished(int index, bool passed_hash_check);
		void piece_failed(int index);
		void received_redundant_data(int num_bytes)
		{ TORRENT_ASSERT(num_bytes > 0); m_total_redundant_bytes += num_bytes; }

		// this is true if we have all the pieces
		bool is_seed() const
		{
			return valid_metadata()
				&& m_num_pieces == m_torrent_file->num_pieces();
		}

		// this is true if we have all the pieces that we want
		bool is_finished() const
		{
			if (is_seed()) return true;
			return valid_metadata() && m_torrent_file->num_pieces()
				- m_num_pieces - m_picker->num_filtered() == 0;
		}

		fs::path save_path() const;
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

		std::vector<announce_entry> const& trackers() const
		{ return m_trackers; }

		void replace_trackers(std::vector<announce_entry> const& urls);

		torrent_handle get_handle() const;

		// LOGGING
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		virtual void debug_log(const std::string& line);
#endif

		// DEBUG
#ifndef NDEBUG
		void check_invariant() const;
#endif

// --------------------------------------------
		// RESOURCE MANAGEMENT

		void set_peer_upload_limit(tcp::endpoint ip, int limit);
		void set_peer_download_limit(tcp::endpoint ip, int limit);

		void set_upload_limit(int limit);
		int upload_limit() const;
		void set_download_limit(int limit);
		int download_limit() const;

		void set_max_uploads(int limit);
		int max_uploads() const { return m_max_uploads; }
		void set_max_connections(int limit);
		int max_connections() const { return m_max_connections; }
		void move_storage(fs::path const& save_path);

		// unless this returns true, new connections must wait
		// with their initialization.
		bool ready_for_connections() const
		{ return m_connections_initialized; }
		bool valid_metadata() const
		{ return m_torrent_file->is_valid(); }

		// parses the info section from the given
		// bencoded tree and moves the torrent
		// to the checker thread for initial checking
		// of the storage.
		void set_metadata(entry const&);

	private:

		void on_files_deleted(int ret, disk_io_job const& j);
		void on_files_released(int ret, disk_io_job const& j);
		void on_torrent_paused(int ret, disk_io_job const& j);
		void on_storage_moved(int ret, disk_io_job const& j);

		void on_piece_verified(int ret, disk_io_job const& j
			, boost::function<void(bool)> f);
	
		void try_next_tracker();
		int prioritize_tracker(int tracker_index);
		void on_country_lookup(asio::error_code const& error, tcp::resolver::iterator i
			, boost::intrusive_ptr<peer_connection> p) const;
		bool request_bandwidth_from_session(int channel) const;

		void update_peer_interest();

		boost::intrusive_ptr<torrent_info> m_torrent_file;

		// is set to true when the torrent has
		// been aborted.
		bool m_abort;

		// is true if this torrent has been paused
		bool m_paused;
		// this is true from the time when the torrent was
		// paused to the time should_request() is called
		bool m_just_paused;

		tracker_request::event_t m_event;

		void parse_response(const entry& e, std::vector<peer_entry>& peer_list);

		// the size of a request block
		// each piece is divided into these
		// blocks when requested
		int m_block_size;

		// if this pointer is 0, the torrent is in
		// a state where the metadata hasn't been
		// received yet.
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

		// the time of next tracker request
		ptime m_next_request;

		// -----------------------------
		// DATA FROM TRACKER RESPONSE

		// the number number of seconds between requests
		// from the tracker
		int m_duration;

		// the scrape data from the tracker response, this
		// is optional and may be -1.
		int m_complete;
		int m_incomplete;

#ifndef NDEBUG
	public:
#endif
		std::set<peer_connection*> m_connections;
#ifndef NDEBUG
	private:
#endif

		// The list of web seeds in this torrent. Seeds
		// with fatal errors are removed from the set
		std::set<std::string> m_web_seeds;

		// a list of web seeds that have failed and are
		// waiting to be retried
		std::map<std::string, ptime> m_web_seeds_next_retry;
		
		// urls of the web seeds that we are currently
		// resolving the address for
		std::set<std::string> m_resolving_web_seeds;

		// used to resolve the names of web seeds
		mutable tcp::resolver m_host_resolver;
		
#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
		// this is true while there is a country
		// resolution in progress. To avoid flodding
		// the DNS request queue, only one ip is resolved
		// at a time.
		mutable bool m_resolving_country;
		
		// this is true if the user has enabled
		// country resolution in this torrent
		bool m_resolve_countries;
#endif

		// this announce timer is used both
		// by Local service discovery and
		// by the DHT.
		deadline_timer m_announce_timer;

		static void on_announce_disp(boost::weak_ptr<torrent> p
			, asio::error_code const& e);

		// this is called once per announce interval
		void on_announce();

#ifndef TORRENT_DISABLE_DHT
		static void on_dht_announce_response_disp(boost::weak_ptr<torrent> t
			, std::vector<tcp::endpoint> const& peers);
		void on_dht_announce_response(std::vector<tcp::endpoint> const& peers);
		bool should_announce_dht() const;

		// the time when the DHT was last announced of our
		// presence on this torrent
		ptime m_last_dht_announce;
#endif

		// this is the upload and download statistics for the whole torrent.
		// it's updated from all its peers once every second.
		libtorrent::stat m_stat;

		// -----------------------------

		// a back reference to the session
		// this torrent belongs to.
		aux::session_impl& m_ses;
		aux::checker_impl& m_checker;

		boost::scoped_ptr<piece_picker> m_picker;

		// the queue of peer_connections that want more bandwidth
		typedef std::deque<bw_queue_entry<peer_connection, torrent> > queue_t;
		queue_t m_bandwidth_queue[2];

		std::vector<announce_entry> m_trackers;
		// this is an index into m_trackers

		int m_last_working_tracker;
		int m_currently_trying_tracker;
		// the number of connection attempts that has
		// failed in a row, this is currently used to
		// determine the timeout until next try.
		int m_failed_trackers;

		// this is a counter that is decreased every
		// second, and when it reaches 0, the policy::pulse()
		// is called and the time scaler is reset to 10.
		int m_time_scaler;

		// the bitmask that says which pieces we have
		std::vector<bool> m_have_pieces;

		// the number of pieces we have. The same as
		// std::accumulate(m_have_pieces.begin(),
		// m_have_pieces.end(), 0)
		int m_num_pieces;

		// in case the piece picker hasn't been constructed
		// when this settings is set, this variable will keep
		// its value until the piece picker is created
		int m_sequenced_download_threshold;

		// is false by default and set to
		// true when the first tracker reponse
		// is received
		bool m_got_tracker_response;

		// the upload/download ratio that each peer
		// tries to maintain.
		// 0 is infinite
		float m_ratio;

		// the number of bytes that has been
		// downloaded that failed the hash-test
		size_type m_total_failed_bytes;
		size_type m_total_redundant_bytes;

		std::string m_username;
		std::string m_password;

		// the network interface all outgoing connections
		// are opened through
		tcp::endpoint m_net_interface;

		fs::path m_save_path;

		// determines the storage state for this torrent.
		storage_mode_t m_storage_mode;

		// defaults to 16 kiB, but can be set by the user
		// when creating the torrent
		const int m_default_block_size;

		// this is set to false as long as the connections
		// of this torrent hasn't been initialized. If we
		// have metadata from the start, connections are
		// initialized immediately, if we didn't have metadata,
		// they are initialized right after files_checked().
		// valid_resume_data() will return false as long as
		// the connections aren't initialized, to avoid
		// them from altering the piece-picker before it
		// has been initialized with files_checked().
		bool m_connections_initialized;

		// if the torrent is started without metadata, it may
		// still be given a name until the metadata is received
		// once the metadata is received this field will no
		// longer be used and will be reset
		boost::scoped_ptr<std::string> m_name;

		session_settings const& m_settings;

		storage_constructor_type m_storage_constructor;

		// the maximum number of uploads for this torrent
		int m_max_uploads;

		// the number of unchoked peers in this torrent
		int m_num_uploads;

		// the maximum number of connections for this torrent
		int m_max_connections;

#ifndef NDEBUG
		bool m_files_checked;
#endif
		
#ifndef TORRENT_DISABLE_EXTENSIONS
		typedef std::list<boost::shared_ptr<torrent_plugin> > extension_list_t;
		extension_list_t m_extensions;
#endif

#ifndef NDEBUG
		// this is the amount downloaded when this torrent
		// is started. i.e.
		// total_done - m_initial_done <= total_payload_download
		size_type m_initial_done;
#endif

		policy m_policy;
	};

	inline ptime torrent::next_announce() const
	{
		return m_next_request;
	}

	inline void torrent::force_tracker_request()
	{
		m_next_request = time_now();
	}

	inline void torrent::force_tracker_request(ptime t)
	{
		m_next_request = t;
	}

	inline void torrent::set_tracker_login(
		std::string const& name
		, std::string const& pw)
	{
		m_username = name;
		m_password = pw;
	}

}

#endif // TORRENT_TORRENT_HPP_INCLUDED

