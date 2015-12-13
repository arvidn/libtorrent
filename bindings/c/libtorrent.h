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

	SES_FINGERPRINT, // char const*, 20 character string or shorter
	SES_VERSION_MAJOR, // int
	SES_VERSION_MINOR, // int
	SES_VERSION_TINY, // int
	SES_VERSION_TAG, // int
	SES_LISTEN_INTERFACE, // char const* (ip/device and port)
	SES_UPLOAD_RATE_LIMIT, // int
	SES_DOWNLOAD_RATE_LIMIT, // int
	SES_MAX_UPLOAD_SLOTS, // int
	SES_MAX_CONNECTIONS, // int
	SES_PROXY_HOSTNAME, // char const*
	SES_PROXY_PORT, // int
	SES_PROXY_TYPE, // int (proxy_type_t)
	SES_PROXY_USERNAME, // char const*
	SES_PROXY_PASSWORD, // char const*
	SES_PROXY_DNS, // bool
	SES_PROXY_PEER_CONNECTIONS, // bool
	SES_PROXY_TRACKER_CONNECTIONS, // bool
	SES_ALERT_MASK, // int
	SES_CACHE_SIZE, // int (specified in 16kiB blocks)
	SES_READ_CACHE_LINE_SIZE, // int (specified in 16kiB blocks)
	SES_WRITE_CACHE_LINE_SIZE, // int (specified in 16kiB blocks)
	SES_ENABLE_UPNP, // bool
	SES_ENABLE_NATPMP, // bool
	SES_ENABLE_LSD, // bool
	SES_ENABLE_DHT, // bool
	SES_ENABLE_UTP_OUT, // bool
	SES_ENABLE_UTP_IN, // bool
	SES_ENABLE_TCP_OUT, // bool
	SES_ENABLE_TCP_IN, // bool
	SES_NO_ATIME_STORAGE, // bool

	// === add_torrent tags ===

	// identifying the torrent to add
	TOR_FILENAME = 0x100, // char const*
	TOR_TORRENT, // char const*, specify size of buffer with TOR_TORRENT_SIZE
	TOR_TORRENT_SIZE, // int, the size of the buffer specified by TOR_TORRENT
	TOR_INFOHASH, // char const*, must point to a 20 byte array
	TOR_INFOHASH_HEX, // char const*, must point to a 40 byte string
	TOR_MAGNETLINK, // char const*, url
	TOR_TRACKER_URL, // char const*
	TOR_WEB_SEED, // char const*
	TOR_RESUME_DATA, // char const*
	TOR_RESUME_DATA_SIZE, // int
	TOR_SAVE_PATH, // char const*
	TOR_NAME, // char const*
	TOR_TRACKER_ID, // char const*
	TOR_FLAGS, // int (torrent_flags_t)
	TOR_USER_DATA, //void*
	TOR_STORAGE_MODE, // int (storage_mode_t)
	TOR_MAX_UPLOAD_SLOTS, // int
	TOR_MAX_CONNECTIONS, // int
	TOR_UPLOAD_RATE_LIMIT, // int
	TOR_DOWNLOAD_RATE_LIMIT, // int

};

// used as argument to TOR_FLAGS tag
enum torrent_flags_t
{
	flag_seed_mode = 0x001,
	flag_override_resume_data = 0x002,
	flag_upload_mode = 0x004,
	flag_share_mode = 0x008,
	flag_apply_ip_filter = 0x010,
	flag_paused = 0x020,
	flag_auto_managed = 0x040,
	flag_duplicate_is_error = 0x080,
	flag_merge_resume_trackers = 0x100,
	flag_update_subscribe = 0x200,
	flag_super_seeding = 0x400,
	flag_sequential_download = 0x800,
	flag_use_resume_save_path = 0x1000,
	flag_pinned = 0x2000,
	flag_merge_resume_http_seeds = 0x2000,
	flag_stop_when_ready = 0x4000,
};

// alert categories
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

// used as argument to TOR_PROXY_TYPE
enum proxy_type_t
{
	proxy_none,
	proxy_socks4,
	proxy_socks5,
	proxy_socks5_pw,
	proxy_http,
	proxy_http_pw
};

// used as argument to TOR_STORAGE_MODE
enum storage_mode_t
{
	storage_mode_allocate = 0,
	storage_mode_sparse
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

struct session_t;

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
struct session_t* session_create(int first_tag, ...);
void session_close(struct session_t* ses);

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

