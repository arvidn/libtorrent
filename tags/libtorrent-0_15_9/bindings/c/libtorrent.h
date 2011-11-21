/*

Copyright (c) 2009, Arvid Norberg
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

#ifndef LIBTORRENT_H
#define LIBTORRENT_H

enum tags
{
	TAG_END = 0,

	SES_FINGERPRINT, // char const*, 2 character string
	SES_LISTENPORT, // int
	SES_LISTENPORT_END, // int
	SES_VERSION_MAJOR, // int
	SES_VERSION_MINOR, // int
	SES_VERSION_TINY, // int
	SES_VERSION_TAG, // int
	SES_FLAGS, // int
	SES_ALERT_MASK, // int
	SES_LISTEN_INTERFACE, // char const*

	// === add_torrent tags ===

	// identifying the torrent to add
	TOR_FILENAME = 0x100, // char const*
	TOR_TORRENT, // char const*, specify size of buffer with TOR_TORRENT_SIZE
	TOR_TORRENT_SIZE, // int
	TOR_INFOHASH, // char const*, must point to a 20 byte array
	TOR_INFOHASH_HEX, // char const*, must point to a 40 byte string
	TOR_MAGNETLINK, // char const*, url

	TOR_TRACKER_URL, // char const*
	TOR_RESUME_DATA, // char const*
	TOR_RESUME_DATA_SIZE, // int
	TOR_SAVE_PATH, // char const*
	TOR_NAME, // char const*
	TOR_PAUSED, // int
	TOR_AUTO_MANAGED, // int
	TOR_DUPLICATE_IS_ERROR, // int
	TOR_USER_DATA, //void*
	TOR_SEED_MODE, // int
	TOR_OVERRIDE_RESUME_DATA, // int
	TOR_STORAGE_MODE, // int

	SET_UPLOAD_RATE_LIMIT = 0x200, // int
	SET_DOWNLOAD_RATE_LIMIT, // int
	SET_LOCAL_UPLOAD_RATE_LIMIT, // int
	SET_LOCAL_DOWNLOAD_RATE_LIMIT, // int
	SET_MAX_UPLOAD_SLOTS, // int
	SET_MAX_CONNECTIONS, // int
	SET_SEQUENTIAL_DOWNLOAD, // int, torrent only
	SET_SUPER_SEEDING, // int, torrent only
	SET_HALF_OPEN_LIMIT, // int, session only
	SET_PEER_PROXY, // proxy_setting const*, session_only
	SET_WEB_SEED_PROXY, // proxy_setting const*, session_only
	SET_TRACKER_PROXY, // proxy_setting const*, session_only
	SET_DHT_PROXY, // proxy_setting const*, session_only
	SET_PROXY, // proxy_setting const*, session_only
	SET_ALERT_MASK, // int, session_only
};

struct proxy_setting
{
	char hostname[256];
	int port;

	char username[256];
	char password[256];

	int type;
};

enum category_t
{
	cat_error = 0x1,
	cat_peer = 0x2,
	cat_port_mapping = 0x4,
	cat_storage = 0x8,
	cat_tracker = 0x10,
	cat_debug = 0x20,
	cat_status = 0x40,
	cat_progress = 0x80,
	cat_ip_block = 0x100,
	cat_performance_warning = 0x200,
	cat_dht = 0x400,

	cat_all_categories = 0xffffffff
};

enum proxy_type_t
{
	proxy_none,
	proxy_socks4,
	proxy_socks5,
	proxy_socks5_pw,
	proxy_http,
	proxy_http_pw
};

enum storage_mode_t
{
	storage_mode_allocate = 0,
	storage_mode_sparse,
	storage_mode_compact
};

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
	
struct torrent_status
{
	enum state_t state;
	int paused;
	float progress;
	char error[1024];
	int next_announce;
	int announce_interval;
	char current_tracker[512];
	long long total_download;
	long long total_upload;
	long long total_payload_download;
	long long total_payload_upload;
	long long total_failed_bytes;
	long long total_redundant_bytes;
	float download_rate;
	float upload_rate;
	float download_payload_rate;
	float upload_payload_rate;
	int num_seeds;
	int num_peers;
	int num_complete;
	int num_incomplete;
	int list_seeds;
	int list_peers;
	int connect_candidates;

	// what to do?	
//	bitfield pieces;

	int num_pieces;
	long long total_done;
	long long total_wanted_done;
	long long total_wanted;
	float distributed_copies;
	int block_size;
	int num_uploads;
	int num_connections;
	int uploads_limit;
	int connections_limit;
//	enum storage_mode_t storage_mode;
	int up_bandwidth_queue;
	int down_bandwidth_queue;
	long long all_time_upload;
	long long all_time_download;
	int active_time;
	int seeding_time;
	int seed_rank;
	int last_scrape;
	int has_incoming;
	int sparse_regions;
	int seed_mode;
};

struct session_status
{
	int has_incoming_connections;

	float upload_rate;
	float download_rate;
	long long total_download;
	long long total_upload;

	float payload_upload_rate;
	float payload_download_rate;
	long long total_payload_download;
	long long total_payload_upload;

	float ip_overhead_upload_rate;
	float ip_overhead_download_rate;
	long long total_ip_overhead_download;
	long long total_ip_overhead_upload;

	float dht_upload_rate;
	float dht_download_rate;
	long long total_dht_download;
	long long total_dht_upload;

	float tracker_upload_rate;
	float tracker_download_rate;
	long long total_tracker_download;
	long long total_tracker_upload;

	long long total_redundant_bytes;
	long long total_failed_bytes;

	int num_peers;
	int num_unchoked;
	int allowed_upload_slots;

	int up_bandwidth_queue;
	int down_bandwidth_queue;

	int up_bandwidth_bytes_queue;
	int down_bandwidth_bytes_queue;

	int optimistic_unchoke_counter;
	int unchoke_counter;

	int dht_nodes;
	int dht_node_cache;
	int dht_torrents;
	long long dht_global_nodes;
//	std::vector<dht_lookup> active_requests;
};

#ifdef __cplusplus
extern "C"
{
#endif

// the functions whose signature ends with:
// , int first_tag, ...);
// takes a tag list. The tag list is a series
// of tag-value pairs. The tags are constants
// identifying which property the value controls.
// The type of the value varies between tags.
// The enumeration above specifies which type
// it expects. All tag lists must always be
// terminated by TAG_END.

// use SES_* tags in tag list
void* session_create(int first_tag, ...);
void session_close(void* ses);

// use TOR_* tags in tag list
int session_add_torrent(void* ses, int first_tag, ...);
void session_remove_torrent(void* ses, int tor, int flags);

// return < 0 if there are no alerts. Otherwise returns the
// type of alert that was returned
int session_pop_alert(void* ses, char* dest, int len, int* category);

int session_get_status(void* ses, struct session_status* s, int struct_size);

// use SET_* tags in tag list
int session_set_settings(void* ses, int first_tag, ...);
int session_get_setting(void* ses, int tag, void* value, int* value_size);

int torrent_get_status(int tor, struct torrent_status* s, int struct_size);

// use SET_* tags in tag list
int torrent_set_settings(int tor, int first_tag, ...);
int torrent_get_setting(int tor, int tag, void* value, int* value_size);

#ifdef __cplusplus
}
#endif

#endif

