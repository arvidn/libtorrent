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

#ifndef TORRENT_TORRENT_HANDLE_HPP_INCLUDED
#define TORRENT_TORRENT_HANDLE_HPP_INCLUDED

#include <vector>
#include <set>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/assert.hpp>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/peer_id.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/ptime.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/socket.hpp" // tcp::endpoint

namespace libtorrent
{
	namespace aux
	{
		struct session_impl;
	}

	struct torrent_plugin;
	struct peer_info;
	struct peer_list_entry;
	struct torrent_status;
	class torrent;

	TORRENT_EXPORT std::size_t hash_value(torrent_status const& ts);

#ifndef BOOST_NO_EXCEPTIONS
	// for compatibility with 0.14
	typedef libtorrent_exception duplicate_torrent;
	typedef libtorrent_exception invalid_handle;
	void throw_invalid_handle();
#endif

	struct TORRENT_EXPORT block_info
	{
		enum block_state_t
		{ none, requested, writing, finished };

	private:
		TORRENT_UNION addr_t
		{
			address_v4::bytes_type v4;
#if TORRENT_USE_IPV6
			address_v6::bytes_type v6;
#endif
		} addr;

		boost::uint16_t port;
	public:

		void set_peer(tcp::endpoint const& ep)
		{
#if TORRENT_USE_IPV6
			is_v6_addr = ep.address().is_v6();
			if (is_v6_addr)
				addr.v6 = ep.address().to_v6().to_bytes();
			else
#endif
				addr.v4 = ep.address().to_v4().to_bytes();
			port = ep.port();
		}

		tcp::endpoint peer() const
		{
#if TORRENT_USE_IPV6
			if (is_v6_addr)
				return tcp::endpoint(address_v6(addr.v6), port);
			else
#endif
				return tcp::endpoint(address_v4(addr.v4), port);
		}

		// number of bytes downloaded in this block
		unsigned bytes_progress:15;
		// the total number of bytes in this block
		unsigned block_size:15;
	private:
		// the type of the addr union
		unsigned is_v6_addr:1;
		unsigned unused:1;
	public:
		// the state this block is in (see block_state_t)
		unsigned state:2;
		// the number of peers that has requested this block
		// typically 0 or 1. If > 1, this block is in
		// end game mode
		unsigned num_peers:14;
	};

	struct TORRENT_EXPORT partial_piece_info
	{
		int piece_index;
		int blocks_in_piece;
		// the number of blocks in the finished state
		int finished;
		// the number of blocks in the writing state
		int writing;
		// the number of blocks in the requested state
		int requested;
		block_info* blocks;
		enum state_t { none, slow, medium, fast };
		state_t piece_state;
	};

	struct TORRENT_EXPORT torrent_handle
	{
		friend class invariant_access;
		friend struct aux::session_impl;
		friend struct feed;
		friend class torrent;
		friend std::size_t hash_value(torrent_handle const& th);

		torrent_handle() {}

		enum flags_t { overwrite_existing = 1 };
		void add_piece(int piece, char const* data, int flags = 0) const;
		void read_piece(int piece) const;
		bool have_piece(int piece) const;

		void get_full_peer_list(std::vector<peer_list_entry>& v) const;
		void get_peer_info(std::vector<peer_info>& v) const;

		enum status_flags_t
		{
			query_distributed_copies = 1,
			query_accurate_download_counters = 2,
			query_last_seen_complete = 4,
			query_pieces = 8,
			query_verified_pieces = 16
		};

		// the flags specify which fields are calculated. By default everything
		// is included, you may save CPU by not querying fields you don't need
		torrent_status status(boost::uint32_t flags = 0xffffffff) const;
		void get_download_queue(std::vector<partial_piece_info>& queue) const;

		enum deadline_flags { alert_when_available = 1 };
		void set_piece_deadline(int index, int deadline, int flags = 0) const;
		void reset_piece_deadline(int index) const;

		void set_priority(int prio) const;
		
#ifndef TORRENT_NO_DEPRECATE
#if !TORRENT_NO_FPU
		// fills the specified vector with the download progress [0, 1]
		// of each file in the torrent. The files are ordered as in
		// the torrent_info.
		TORRENT_DEPRECATED_PREFIX
		void file_progress(std::vector<float>& progress) const TORRENT_DEPRECATED;
#endif
#endif
		enum file_progress_flags_t
		{
			piece_granularity = 1
		};

		void file_progress(std::vector<size_type>& progress, int flags = 0) const;

		void clear_error() const;

		std::vector<announce_entry> trackers() const;
		void replace_trackers(std::vector<announce_entry> const&) const;
		void add_tracker(announce_entry const&) const;

		void add_url_seed(std::string const& url) const;
		void remove_url_seed(std::string const& url) const;
		std::set<std::string> url_seeds() const;

		void add_http_seed(std::string const& url) const;
		void remove_http_seed(std::string const& url) const;
		std::set<std::string> http_seeds() const;

#ifndef TORRENT_DISABLE_EXTENSIONS
		void add_extension(boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> const& ext
			, void* userdata = 0);
#endif

		bool set_metadata(char const* metadata, int size) const;
		const torrent_info& get_torrent_info() const;
		bool is_valid() const;

		enum pause_flags_t { graceful_pause = 1 };
		void pause(int flags = 0) const;
		void resume() const;
		void set_upload_mode(bool b) const;
		void set_share_mode(bool b) const;
		void flush_cache() const;

		void apply_ip_filter(bool b) const;

		void force_recheck() const;

		enum save_resume_flags_t { flush_disk_cache = 1, save_info_dict = 2 };
		void save_resume_data(int flags = 0) const;
		bool need_save_resume_data() const;

		void auto_managed(bool m) const;

		int queue_position() const;
		void queue_position_up() const;
		void queue_position_down() const;
		void queue_position_top() const;
		void queue_position_bottom() const;

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES	
		void resolve_countries(bool r);
		bool resolve_countries() const;
#endif

		void set_ssl_certificate(std::string const& certificate
			, std::string const& private_key
			, std::string const& dh_params
			, std::string const& passphrase = "");

		storage_interface* get_storage_impl() const;

		// all these are deprecated, use piece
		// priority functions instead

		// ================ start deprecation ============

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.16, feature will be removed
		TORRENT_DEPRECATED_PREFIX
		int get_peer_upload_limit(tcp::endpoint ip) const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		int get_peer_download_limit(tcp::endpoint ip) const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void set_peer_upload_limit(tcp::endpoint ip, int limit) const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void set_peer_download_limit(tcp::endpoint ip, int limit) const TORRENT_DEPRECATED;

		// deprecated in 0.16, feature will be removed
		TORRENT_DEPRECATED_PREFIX
		void set_ratio(float up_down_ratio) const TORRENT_DEPRECATED;

		// deprecated in 0.16. use status() instead
		TORRENT_DEPRECATED_PREFIX
		bool is_seed() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		bool is_finished() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		bool is_paused() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		bool is_auto_managed() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		bool is_sequential_download() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		bool has_metadata() const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		bool super_seeding() const TORRENT_DEPRECATED;

		// deprecated in 0.13
		// marks the piece with the given index as filtered
		// it will not be downloaded
		TORRENT_DEPRECATED_PREFIX
		void filter_piece(int index, bool filter) const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		void filter_pieces(std::vector<bool> const& pieces) const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		bool is_piece_filtered(int index) const TORRENT_DEPRECATED;
		TORRENT_DEPRECATED_PREFIX
		std::vector<bool> filtered_pieces() const TORRENT_DEPRECATED;
		// marks the file with the given index as filtered
		// it will not be downloaded
		TORRENT_DEPRECATED_PREFIX
		void filter_files(std::vector<bool> const& files) const TORRENT_DEPRECATED;

		// ================ end deprecation ============
#endif

		void piece_availability(std::vector<int>& avail) const;
		
		// priority must be within the range [0, 7]
		void piece_priority(int index, int priority) const;
		int piece_priority(int index) const;

		void prioritize_pieces(std::vector<int> const& pieces) const;
		std::vector<int> piece_priorities() const;

		// priority must be within the range [0, 7]
		void file_priority(int index, int priority) const;
		int file_priority(int index) const;

		void prioritize_files(std::vector<int> const& files) const;
		std::vector<int> file_priorities() const;

		// set the interface to bind outgoing connections
		// to.
		void use_interface(const char* net_interface) const;

#ifndef TORRENT_NO_DEPRECATE
		// deprecated in 0.14
		// use save_resume_data() instead. It is async. and
		// will return the resume data in an alert
		TORRENT_DEPRECATED_PREFIX
		entry write_resume_data() const TORRENT_DEPRECATED;
#endif

		// forces this torrent to reannounce
		// (make a rerequest from the tracker)
		void force_reannounce() const;
#ifndef TORRENT_DISABLE_DHT
		// announces this torrent to the DHT immediately
		void force_dht_announce() const;
#endif

		// forces a reannounce in the specified amount of time.
		// This overrides the default announce interval, and no
		// announce will take place until the given time has
		// timed out.
		void force_reannounce(boost::posix_time::time_duration) const;

		// performs a scrape request
		void scrape_tracker() const;

		// returns the name of this torrent, in case it doesn't
		// have metadata it returns the name assigned to it
		// when it was added.
		std::string name() const;

		// TODO: add a feature where the user can tell the torrent
		// to finish all pieces currently in the pipeline, and then
		// abort the torrent.

		void set_upload_limit(int limit) const;
		int upload_limit() const;
		void set_download_limit(int limit) const;
		int download_limit() const;

		void set_sequential_download(bool sd) const;

		// manually connect a peer
		void connect_peer(tcp::endpoint const& adr, int source = 0) const;

		std::string save_path() const;

		// -1 means unlimited unchokes
		void set_max_uploads(int max_uploads) const;
		int max_uploads() const;

		// -1 means unlimited connections
		void set_max_connections(int max_connections) const;
		int max_connections() const;

		void set_tracker_login(std::string const& name
			, std::string const& password) const;

		// post condition: save_path() == save_path if true is returned
		void move_storage(std::string const& save_path) const;
		void rename_file(int index, std::string const& new_name) const;

#if TORRENT_USE_WSTRING
		void move_storage(std::wstring const& save_path) const;
		void rename_file(int index, std::wstring const& new_name) const;
#endif

		void super_seeding(bool on) const;

		sha1_hash info_hash() const;

		bool operator==(const torrent_handle& h) const
		{ return m_torrent.lock() == h.m_torrent.lock(); }

		bool operator!=(const torrent_handle& h) const
		{ return m_torrent.lock() != h.m_torrent.lock(); }

		bool operator<(const torrent_handle& h) const
		{ return m_torrent.lock() < h.m_torrent.lock(); }

		boost::shared_ptr<torrent> native_handle() const;

	private:

		torrent_handle(boost::weak_ptr<torrent> const& t)
			: m_torrent(t)
		{}

#ifdef TORRENT_DEBUG
		void check_invariant() const;
#endif

		boost::weak_ptr<torrent> m_torrent;

	};

	struct TORRENT_EXPORT torrent_status
	{
		torrent_status();
		~torrent_status();

		bool operator==(torrent_status const& st) const
		{ return handle == st.handle; }

		// handle to the torrent
		torrent_handle handle;

		enum state_t
		{
			queued_for_checking,
			checking_files,
			downloading_metadata,
			downloading,
			finished,
			seeding,
			allocating,
			checking_resume_data
		};
		
		state_t state;
		bool paused;
		bool auto_managed;
		bool sequential_download;
		bool is_seeding;
		bool is_finished;
		bool has_metadata;

		float progress;
		// progress parts per million (progress * 1000000)
		// when disabling floating point operations, this is
		// the only option to query progress
		int progress_ppm;
		std::string error;

		boost::posix_time::time_duration next_announce;
		boost::posix_time::time_duration announce_interval;

		std::string current_tracker;

		// transferred this session!
		// total, payload plus protocol
		size_type total_download;
		size_type total_upload;

		// payload only
		size_type total_payload_download;
		size_type total_payload_upload;

		// the amount of payload bytes that
		// has failed their hash test
		size_type total_failed_bytes;

		// the number of payload bytes that
		// has been received redundantly.
		size_type total_redundant_bytes;

		// current transfer rate
		// payload plus protocol
		int download_rate;
		int upload_rate;

		// the rate of payload that is
		// sent and received
		int download_payload_rate;
		int upload_payload_rate;

		// the number of peers this torrent is connected to
		// that are seeding.
		int num_seeds;

		// the number of peers this torrent
		// is connected to (including seeds).
		int num_peers;

		// if the tracker sends scrape info in its
		// announce reply, these fields will be
		// set to the total number of peers that
		// have the whole file and the total number
		// of peers that are still downloading
		int num_complete;
		int num_incomplete;

		// this is the number of seeds whose IP we know
		// but are not necessarily connected to
		int list_seeds;

		// this is the number of peers whose IP we know
		// (including seeds), but are not necessarily
		// connected to
		int list_peers;

		// the number of peers in our peerlist that
		// we potentially could connect to
		int connect_candidates;
		
		bitfield pieces;
		bitfield verified_pieces;
		
		// this is the number of pieces the client has
		// downloaded. it is equal to:
		// std::accumulate(pieces->begin(), pieces->end());
		int num_pieces;

		// the number of bytes of the file we have
		// including pieces that may have been filtered
		// after we downloaded them
		size_type total_done;

		// the number of bytes we have of those that we
		// want. i.e. not counting bytes from pieces that
		// are filtered as not wanted.
		size_type total_wanted_done;

		// the total number of bytes we want to download
		// this may be smaller than the total torrent size
		// in case any pieces are filtered as not wanted
		size_type total_wanted;

		// the number of distributed copies of the file.
		// note that one copy may be spread out among many peers.
		//
		// the integer part tells how many copies
		//   there are of the rarest piece(s)
		//
		// the fractional part tells the fraction of pieces that
		//   have more copies than the rarest piece(s).

		// the number of full distributed copies (i.e. the number
		// of peers that have the rarest piece)
		int distributed_full_copies;

		// the fraction of pieces that more peers has than the
		// rarest pieces. This indicates how close the swarm is
		// to have one more full distributed copy
		int distributed_fraction;

		float distributed_copies;

		// the block size that is used in this torrent. i.e.
		// the number of bytes each piece request asks for
		// and each bit in the download queue bitfield represents
		int block_size;

		int num_uploads;
		int num_connections;
		int uploads_limit;
		int connections_limit;

		// true if the torrent is saved in compact mode
		// false if it is saved in full allocation mode
		storage_mode_t storage_mode;

		int up_bandwidth_queue;
		int down_bandwidth_queue;

		// number of bytes downloaded since torrent was started
		// saved and restored from resume data
		size_type all_time_upload;
		size_type all_time_download;

		// the number of seconds of being active
		// and as being a seed, saved and restored
		// from resume data
		int active_time;
		int finished_time;
		int seeding_time;

		// higher value means more important to seed
		int seed_rank;

		// number of seconds since last scrape, or -1 if
		// there hasn't been a scrape
		int last_scrape;

		// true if there are incoming connections to this
		// torrent
		bool has_incoming;

		// the number of "holes" in the torrent
		int sparse_regions;

		// is true if this torrent is (still) in seed_mode
		bool seed_mode;

		// this is set to true when the torrent is blocked
		// from downloading, typically caused by a file
		// write operation failing
		bool upload_mode;

		// this is true if the torrent is in share-mode
		bool share_mode;

		// true if the torrent is in super seeding mode
		bool super_seeding;

		// the priority of this torrent
		int priority;

		// the time this torrent was added and completed
		time_t added_time;
		time_t completed_time;
		time_t last_seen_complete;

		// number of seconds since last upload or download activity
		int time_since_upload;
		int time_since_download;

		// the position in the download queue where this torrent is
		// this is -1 for seeds and finished torrents
		int queue_position;

		// true if this torrent has had changes since the last
		// time resume data was saved
		bool need_save_resume;

		// defaults to true. Determines whether the session
		// IP filter applies to this torrent or not
		bool ip_filter_applies;

		// the info-hash for this torrent
		sha1_hash info_hash;
		
		// if this torrent has its own listen socket, this is
		// the port it's listening on. Otherwise it's 0
		int listen_port;
	};

}

#endif // TORRENT_TORRENT_HANDLE_HPP_INCLUDED

