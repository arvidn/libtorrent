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
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/policy.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/url_handler.hpp"

namespace libtorrent
{
#ifndef NDEBUG
	struct logger;
#endif

	namespace detail
	{
		struct session_impl;
	}

	// TODO: each torrent should have a status value that
	// reflects what's happening to it
	// TODO: There should be a maximum number of peers that
	// is maintained (if someone disconnects, try to connect to
	// anotherone). There should also be a candidate slot where a
	// new peer is tried for one minute, and if it has better ownload
	// speed than one of the peers currently connected, it will be
	// replaced to maximize bandwidth usage. It wil also have to
	// depend on how many and which pieces the peers have.
	// TODO: In debug mode all pieces that are sent should be checked.


	// a torrent is a class that holds information
	// for a specific download. It updates itself against
	// the tracker
	class torrent: public request_callback
	{
	public:

		torrent(detail::session_impl* ses, const torrent_info& torrent_file);
		~torrent() {}

		void abort() { m_abort = true; m_event = event_stopped; }
		bool is_aborted() const { return m_abort; }

		// returns true if it time for this torrent to make another
		// tracker request
		bool should_request() const throw()
		{
			boost::posix_time::time_duration d = m_next_request - boost::posix_time::second_clock::local_time();
			return d.is_negative();
		}

		bool failed() const throw() { return !m_failed.empty(); }
		const char* fail_reason() const throw() { return m_failed.c_str(); }

		void print(std::ostream& os) const;

		void allocate_files(const std::string& save_path)
		{
			m_storage.initialize_pieces(this, save_path);
			m_picker.files_checked(m_storage.pieces());
#ifndef NDEBUG
			m_picker.integrity_check(this);
#endif	
		}

		void uploaded_bytes(int num_bytes) { assert(num_bytes > 0); m_bytes_uploaded += num_bytes; }
		void downloaded_bytes(int num_bytes) { assert(num_bytes > 0); m_bytes_downloaded += num_bytes; }

		int bytes_downloaded() const { return m_bytes_downloaded; }
		int bytes_uploaded() const { return m_bytes_uploaded; }
		int bytes_left() const { return m_storage.bytes_left(); }

		std::pair<torrent_handle::state_t, float> status() const;

		void connect_to_peer(const address& a, const peer_id& id);

		const torrent_info& torrent_file() const throw() { return m_torrent_file; }

		policy& get_policy() { return *m_policy; }
		storage* filesystem() { return &m_storage; }


// --------------------------------------------
		// PEER MANAGEMENT

		// used by peer_connection to attach itself to a torrent
		// since incoming connections don't know what torrent
		// they're a part of until they have received an info_hash.
		void attach_peer(peer_connection* p)
		{
			assert(std::find(m_connections.begin(), m_connections.end(), p) == m_connections.end());
			m_connections.push_back(p);
		}

		// this will remove the peer and make sure all
		// the pieces it had have their reference counter
		// decreased in the piece_picker
		void remove_peer(peer_connection* p);

		// the number of peers that belong to this torrent
		int num_peers() const { return m_connections.size(); }

		// returns the number of connections this torrent has to
		// the given peer_id (should be kept at max 1)
		int num_connections(const peer_id& id) const;

		std::vector<peer_connection*>::const_iterator begin() const { return m_connections.begin(); }
		std::vector<peer_connection*>::const_iterator end() const { return m_connections.end(); }


// --------------------------------------------
		// TRACKER MANAGEMENT

		// this is a callback called by the tracker_connection class
		// when this torrent got a response from its tracker request
		void tracker_response(const entry& e);

		void tracker_request_timed_out()
		{
			std::cout << "TRACKER TIMED OUT\n";
			try_next_tracker();
		}

		void tracker_request_error(const char* str)
		{
			std::cout << "TRACKER ERROR: " << str << "\n";
			try_next_tracker();
		}

		// generates a request string for sending
		// to the tracker
		std::string generate_tracker_request(int port);


// --------------------------------------------
		// PIECE MANAGEMENT

		// returns true if we have downloaded the given piece
		bool have_piece(unsigned int index) const { return m_storage.have_piece(index); }

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

		// DEBUG
#ifndef NDEBUG
		logger* spawn_logger(const char* title);
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

		// total amount of bytes uploaded, downloaded and
		// the number of bytes left to be downloaded
		entry::integer_type m_bytes_uploaded;
		entry::integer_type m_bytes_downloaded;

		torrent_info m_torrent_file;

		storage m_storage;

		// the time of next tracker request
		boost::posix_time::ptime m_next_request;

		// -----------------------------
		// DATA FROM TRACKER RESPONSE

		// the number number of seconds between requests
		// from the tracker
		int m_duration;

		std::string m_failed;
		std::vector<peer_connection*> m_connections;

		// -----------------------------

		boost::shared_ptr<policy> m_policy;

		detail::session_impl* m_ses;

		piece_picker m_picker;

		// this is an index into m_torrent_file.trackers()
		int m_last_working_tracker;
		int m_currently_trying_tracker;

	};

}

#endif // TORRENT_TORRENT_HPP_INCLUDED
