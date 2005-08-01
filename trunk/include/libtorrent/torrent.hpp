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
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/tuple/tuple.hpp>

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
#include "libtorrent/resource_request.hpp"

namespace libtorrent
{
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
	struct logger;
#endif

	class piece_manager;

	std::string escape_string(const char* str, int len);
	std::string unescape_string(std::string const& s);


	namespace detail
	{
		struct session_impl;
		struct piece_checker_data;
	}

	int div_round_up(int numerator, int denominator);
	std::pair<int, int> req_to_offset(std::pair<int, int> req, int total_size);
	std::pair<int, int> offset_to_req(std::pair<int, int> offset, int total_size);

	// a torrent is a class that holds information
	// for a specific download. It updates itself against
	// the tracker
	class torrent: public request_callback
	{
	public:

		torrent(
			detail::session_impl& ses
			, entry const& metadata
			, boost::filesystem::path const& save_path
			, address const& net_interface
			, bool compact_mode
			, int block_size);

		// used with metadata-less torrents
		// (the metadata is downloaded from the peers)
		torrent(
			detail::session_impl& ses
			, char const* tracker_url
			, sha1_hash const& info_hash
			, boost::filesystem::path const& save_path
			, address const& net_interface
			, bool compact_mode
			, int block_size);

		~torrent();

		// this is called when the torrent has metadata.
		// it will initialize the storage and the piece-picker
		void init();

		// this will flag the torrent as aborted. The main
		// loop in session_impl will check for this state
		// on all torrents once every second, and take
		// the necessary actions then.
		void abort();
		bool is_aborted() const { return m_abort; }

		// is called every second by session. This will
		// caclulate the upload/download and number
		// of connections this torrent needs. And prepare
		// it for being used by allocate_resources.
		void second_tick(stat& accumulator);

		// debug purpose only
		void print(std::ostream& os) const;

		// this is called from the peer_connection for
		// each piece of metadata it receives
		void metadata_progress(int total_size, int received);
		
		void check_files(
			detail::piece_checker_data& data
			, boost::mutex& mutex, bool lock_session = true);

		stat statistics() const { return m_stat; }
		size_type bytes_left() const;
		boost::tuple<size_type, size_type> bytes_done() const;

		void pause();
		void resume();
		bool is_paused() const { return m_paused; }

		void filter_piece(int index, bool filter);
		void filter_pieces(std::vector<bool> const& bitmask);
		bool is_piece_filtered(int index) const;
		void filtered_pieces(std::vector<bool>& bitmask) const;
	
		//idea from Arvid and MooPolice
		//todo refactoring and improving the function body
		// marks the file with the given index as filtered
		// it will not be downloaded
		void filter_file(int index, bool filter);
		void filter_files(std::vector<bool> const& files);


		torrent_status status() const;

		void use_interface(const char* net_interface);
		peer_connection& connect_to_peer(const address& a);

		void set_ratio(float ratio)
		{ assert(ratio >= 0.0f); m_ratio = ratio; }

		float ratio() const
		{ return m_ratio; }

// --------------------------------------------
		// PEER MANAGEMENT

		// used by peer_connection to attach itself to a torrent
		// since incoming connections don't know what torrent
		// they're a part of until they have received an info_hash.
		void attach_peer(peer_connection* p);

		// this will remove the peer and make sure all
		// the pieces it had have their reference counter
		// decreased in the piece_picker
		// called from the peer_connection destructor
		void remove_peer(peer_connection* p);

		peer_connection* connection_for(const address& a)
		{
			peer_iterator i = m_connections.find(a);
			if (i == m_connections.end()) return 0;
			return i->second;
		}

		// the number of peers that belong to this torrent
		int num_peers() const { return (int)m_connections.size(); }
		int num_seeds() const;

		typedef std::map<address, peer_connection*>::iterator peer_iterator;
		typedef std::map<address, peer_connection*>::const_iterator const_peer_iterator;

		const_peer_iterator begin() const { return m_connections.begin(); }
		const_peer_iterator end() const { return m_connections.end(); }

		peer_iterator begin() { return m_connections.begin(); }
		peer_iterator end() { return m_connections.end(); }


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
		boost::posix_time::ptime next_announce() const;

		// returns true if it is time for this torrent to make another
		// tracker request
		bool should_request();

		// forcefully sets next_announce to the current time
		void force_tracker_request();
		void force_tracker_request(boost::posix_time::ptime);
		void force_tracker_request_on_loop(boost::posix_time::ptime);
		// sets the username and password that will be sent to
		// the tracker
		void set_tracker_login(std::string const& name, std::string const& pw);

		// the address of the tracker that we managed to
		// announce ourself at the last time we tried to announce
		const address& current_tracker() const;

// --------------------------------------------
		// PIECE MANAGEMENT

		// returns true if we have downloaded the given piece
		bool have_piece(int index) const
		{
			assert(index >= 0 && index < (signed)m_have_pieces.size());
			return m_have_pieces[index];
		}

		const std::vector<bool>& pieces() const
		{ return m_have_pieces; }

		int num_pieces() const { return m_num_pieces; }

		// when we get a have- or bitfield- messages, this is called for every
		// piece a peer has gained.
		void peer_has(int index)
		{
			assert(m_picker.get());
			assert(index >= 0 && index < (signed)m_have_pieces.size());
			m_picker->inc_refcount(index);
		}

		// when peer disconnects, this is called for every piece it had
		void peer_lost(int index)
		{
			assert(m_picker.get());
			assert(index >= 0 && index < (signed)m_have_pieces.size());
			m_picker->dec_refcount(index);
		}

		int block_size() const { return m_block_size; }

		// this will tell all peers that we just got his piece
		// and also let the piece picker know that we have this piece
		// so it wont pick it for download
		void announce_piece(int index);

		void disconnect_all();

		// this is called wheh the torrent has completed
		// the download. It will post an event, disconnect
		// all seeds and let the tracker know we're finished.
		void completed();

		// this is called when the torrent has finished. i.e.
		// all the pieces we have not filtered have been downloaded.
		// If no pieces are filtered, this is called first and then
		// completed() is called immediately after it.
		void finished();

		bool verify_piece(int piece_index);

		// this is called from the peer_connection
		// each time a piece has failed the hash
		// test
		void piece_failed(int index);

		float priority() const
		{ return m_priority; }

		void set_priority(float p)
		{
			assert(p >= 0.f && p <= 1.f);
			m_priority = p;
		}

		bool is_seed() const
		{
			return valid_metadata()
				&& m_num_pieces == m_torrent_file.num_pieces();
		}

		boost::filesystem::path save_path() const;
		alert_manager& alerts() const;
		piece_picker& picker()
		{
			assert(m_picker.get());
			return *m_picker;
		}
		policy& get_policy() { return *m_policy; }
		piece_manager& filesystem();
		torrent_info const& torrent_file() const { return m_torrent_file; }

		std::vector<announce_entry> const& trackers() const
		{ return m_trackers; }

		void replace_trackers(std::vector<announce_entry> const& urls);

		torrent_handle get_handle() const;

		// LOGGING
#if defined(TORRENT_VERBOSE_LOGGING) || defined(TORRENT_LOGGING)
		logger* spawn_logger(const char* title);

		virtual void debug_log(const std::string& line);
#endif

		// DEBUG
#ifndef NDEBUG
		void check_invariant() const;
#endif

// --------------------------------------------
		// RESOURCE MANAGEMENT

		// this will distribute the given upload/download
		// quotas and number of connections, among the peers
		void distribute_resources();

		resource_request m_ul_bandwidth_quota;
		resource_request m_dl_bandwidth_quota;
		resource_request m_uploads_quota;
		resource_request m_connections_quota;

		void set_upload_limit(int limit);
		void set_download_limit(int limit);
		void set_max_uploads(int limit);
		void set_max_connections(int limit);
		bool move_storage(boost::filesystem::path const& save_path);

		bool valid_metadata() const { return m_storage.get() != 0; }
		std::vector<char> const& metadata() const { return m_metadata; }

		bool received_metadata(
			char const* buf
			, int size
			, int offset
			, int total_size);

		// returns a range of the metadata that
		// we should request.
		std::pair<int, int> metadata_request();
		void cancel_metadata_request(std::pair<int, int> req);

	private:

		void try_next_tracker();
		int prioritize_tracker(int tracker_index);

		torrent_info m_torrent_file;

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
		std::auto_ptr<piece_manager> m_storage;

		// the time of next tracker request
		boost::posix_time::ptime m_next_request;

		// -----------------------------
		// DATA FROM TRACKER RESPONSE

		// the number number of seconds between requests
		// from the tracker
		int m_duration;

		// the scrape data from the tracker response, this
		// is optional and may be -1.
		int m_complete;
		int m_incomplete;
		
		std::map<address, peer_connection*> m_connections;

		// this is the upload and download statistics for the whole torrent.
		// it's updated from all its peers once every second.
		libtorrent::stat m_stat;

		// -----------------------------

		boost::shared_ptr<policy> m_policy;

		// a back reference to the session
		// this torrent belongs to.
		detail::session_impl& m_ses;

		std::auto_ptr<piece_picker> m_picker;

		std::vector<announce_entry> m_trackers;
		// this is an index into m_torrent_file.trackers()
		int m_last_working_tracker;
		int m_currently_trying_tracker;
		// the number of connection attempts that has
		// failed in a row, this is currently used to
		// determine the timeout until next try.
		int m_failed_trackers;

		// this is a counter that is increased every
		// second, and when it reaches 10, the policy::pulse()
		// is called and the time scaler is reset to 0.
		int m_time_scaler;

		// this is the priority of this torrent. It is used
		// to weight the assigned upload bandwidth between peers
		// it should be within the range [0, 1]
		float m_priority;

		// the bitmask that says which pieces we have
		std::vector<bool> m_have_pieces;

		// the number of pieces we have. The same as
		// std::accumulate(m_have_pieces.begin(),
		// m_have_pieces.end(), 0)
		int m_num_pieces;

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

		std::string m_username;
		std::string m_password;

		// the network interface all outgoing connections
		// are opened through
		address m_net_interface;

		// the max number of bytes this torrent
		// can upload per second
		int m_upload_bandwidth_limit;
		int m_download_bandwidth_limit;

		// this buffer is filled with the info-section of
		// the metadata file while downloading it from
		// peers, and while sending it.
		std::vector<char> m_metadata;

		// this is a bitfield of size 256, each bit represents
		// a piece of the metadata. It is set to one if we
		// have that piece. This vector may be empty
		// (size 0) if we haven't received any metadata
		// or if we already have all metadata
		std::vector<bool> m_have_metadata;
		// this vector keeps track of how many times each meatdata
		// block has been requested
		std::vector<int> m_requested_metadata;

		boost::filesystem::path m_save_path;

		// determines the storage state for this torrent.
		const bool m_compact_mode;

		int m_metadata_progress;
		int m_metadata_size;

		// defaults to 16 kiB, but can be set by the user
		// when creating the torrent
		const int m_default_block_size;
	};

	inline boost::posix_time::ptime torrent::next_announce() const
	{
		return m_next_request;
	}

	inline void torrent::force_tracker_request()
	{
		using boost::posix_time::second_clock;
		m_next_request = second_clock::universal_time();
	}

	inline void torrent::force_tracker_request(boost::posix_time::ptime t)
	{
		namespace time = boost::posix_time;
		m_next_request = t;
	}

	inline void torrent::force_tracker_request_on_loop(boost::posix_time::ptime t)
	{
		namespace time = boost::posix_time;
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

