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

#include <boost/limits.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/policy.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/url_handler.hpp"
#include "libtorrent/stat.hpp"

namespace libtorrent
{
#ifndef NDEBUG
	struct logger;
#endif

	namespace detail
	{
		struct session_impl;
	}

	// a torrent is a class that holds information
	// for a specific download. It updates itself against
	// the tracker
	class torrent: public request_callback
	{
	public:

		typedef entry::integer_type size_type;

		torrent(
			detail::session_impl& ses
			, const torrent_info& torrent_file
			, const boost::filesystem::path& save_path);

		~torrent();

		void abort() { m_abort = true; m_event = event_stopped; }
		bool is_aborted() const { return m_abort; }

		// is called every second by session.
		void second_tick();

		// returns true if it time for this torrent to make another
		// tracker request
		bool should_request() const throw()
		{
//			boost::posix_time::time_duration d = m_next_request - boost::posix_time::second_clock::local_time();
//			return d.is_negative();
			return m_next_request < boost::posix_time::second_clock::local_time();
		}

		void print(std::ostream& os) const;

		void check_files(detail::piece_checker_data& data,
			boost::mutex& mutex);

		stat statistics() const { return m_stat; }
		size_type bytes_left() const;

		torrent_status status() const;

		peer_connection& connect_to_peer(
			const address& a
			, const peer_id& id);

		const torrent_info& torrent_file() const throw()
		{ return m_torrent_file; }

		policy& get_policy() { return *m_policy; }

		piece_manager& filesystem() { return m_storage; }

// --------------------------------------------
		// PEER MANAGEMENT

		// used by peer_connection to attach itself to a torrent
		// since incoming connections don't know what torrent
		// they're a part of until they have received an info_hash.
		void attach_peer(peer_connection* p);

		// this will remove the peer and make sure all
		// the pieces it had have their reference counter
		// decreased in the piece_picker
		void remove_peer(peer_connection* p);

		// the number of peers that belong to this torrent
		int num_peers() const { return m_connections.size(); }

		// returns true if this torrent has a connection
		// to a peer with the given peer_id
		bool has_peer(const peer_id& id) const;

		typedef std::vector<peer_connection*>::iterator peer_iterator;
		typedef std::vector<peer_connection*>::const_iterator peer_const_iterator;

		peer_const_iterator begin() const { return m_connections.begin(); }
		peer_const_iterator end() const { return m_connections.end(); }

		peer_iterator begin() { return m_connections.begin(); }
		peer_iterator end() { return m_connections.end(); }


// --------------------------------------------
		// TRACKER MANAGEMENT

		// this is a callback called by the tracker_connection class
		// when this torrent got a response from its tracker request
		void tracker_response(const entry& e);

		void tracker_request_timed_out()
		{
#ifndef NDEBUG
			debug_log("*** tracker timed out");
#endif
			// TODO: increase the retry_delay for
			// each failed attempt on the same tracker!
			// maybe we should add a counter that keeps
			// track of how many times a specific tracker
			// has timed out?
			try_next_tracker();
		}

		// TODO: this function should also take the
		// HTTP-response code as an argument
		// with some codes, we should just consider
		// the tracker as a failure and not retry
		// it anymore
		void tracker_request_error(const char* str)
		{
#ifndef NDEBUG
			debug_log(std::string("*** tracker error: ") + str);
#endif
			try_next_tracker();
		}

		// generates a request string for sending
		// to the tracker
		std::string generate_tracker_request(int port);

		boost::posix_time::ptime next_announce() const
		{ return m_next_request; }

// --------------------------------------------
		// PIECE MANAGEMENT

		// returns true if we have downloaded the given piece
		bool have_piece(unsigned int index) const
		{ return m_have_pieces[index]; }

		const std::vector<bool>& pieces() const
		{ return m_have_pieces; }

		// when we get a have- or bitfield- messages, this is called for every
		// piece a peer has gained.
		// returns true if this piece is interesting (i.e. if we would like to download it)
		bool peer_has(int index)
		{
			return m_picker.inc_refcount(index);
		}

		// when peer disconnects, this is called for every piece it had
		void peer_lost(int index)
		{
			m_picker.dec_refcount(index);
		}

		int block_size() const { return m_block_size; }

		// this will tell all peers that we just got his piece
		// and also let the piece picker know that we have this piece
		// so it wont pick it for download
		void announce_piece(int index);

		void close_all_connections();

		piece_picker& picker() { return m_picker; }


		bool verify_piece(int piece_index);

		// this is called from the peer_connection
		// each time a piece has failed the hash
		// test
		void piece_failed(int index);

		float priority() const
		{ return m_priority; }

		void set_priority(float p)
		{
			assert(p >= 0.f && p <= 0.f);
			m_priority = p;
		}

		bool is_seed() const
		{ return m_num_pieces == m_torrent_file.num_pieces(); }

		boost::filesystem::path save_path() const
		{ return m_storage.save_path(); }

		// DEBUG
#ifndef NDEBUG
		logger* spawn_logger(const char* title);
#endif

#ifndef NDEBUG
		virtual void debug_log(const std::string& line);
		void check_invariant();
#endif

	private:

		void try_next_tracker();

		enum event_id
		{
			event_started = 0,
			event_stopped,
			event_completed,
			event_none
		};

		// the size of a request block
		// each piece is divided into these
		// blocks when requested
		int m_block_size;

		// is set to true when the torrent has
		// been aborted.
		bool m_abort;

		event_id m_event;

		void parse_response(const entry& e, std::vector<peer>& peer_list);

		torrent_info m_torrent_file;

		piece_manager m_storage;

		// the time of next tracker request
		boost::posix_time::ptime m_next_request;

		// -----------------------------
		// DATA FROM TRACKER RESPONSE

		// the number number of seconds between requests
		// from the tracker
		int m_duration;

		std::vector<peer_connection*> m_connections;

		// this is the upload and download statistics for the whole torrent.
		// it's updated from all its peers once every second.
		libtorrent::stat m_stat;

		// -----------------------------

		boost::shared_ptr<policy> m_policy;

		// a back reference to the session
		// this torrent belongs to.
		detail::session_impl& m_ses;

		piece_picker m_picker;

		// this is an index into m_torrent_file.trackers()
		int m_last_working_tracker;
		int m_currently_trying_tracker;

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
	};

}

#endif // TORRENT_TORRENT_HPP_INCLUDED
