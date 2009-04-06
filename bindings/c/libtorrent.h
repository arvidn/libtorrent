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
	SES_FINGERPRINT,
	SES_LISTENPORT,
	SES_LISTENPORT_END,
	SES_VERSION_MAJOR,
	SES_VERSION_MINOR,
	SES_VERSION_TINY,
	SES_VERSION_TAG,
	SES_FLAGS,
	SES_ALERT_MASK,
	SES_LISTEN_INTERFACE,

	// === add_torrent tags ===

	// identifying the torrent to add
	TOR_FILENAME = 0x100,
	TOR_TORRENT,
	TOR_TORRENT_SIZE,
	TOR_INFOHASH,
	TOR_INFOHASH_HEX,
	TOR_MAGNETLINK,

	TOR_TRACKER_URL,
	TOR_RESUME_DATA,
	TOR_RESUME_DATA_SIZE,
	TOR_SAVE_PATH,
	TOR_NAME,
	TOR_PAUSED,
	TOR_AUTO_MANAGED,
	TOR_DUPLICATE_IS_ERROR,
	TOR_USER_DATA,
	TOR_SEED_MODE,
	TOR_OVERRIDE_RESUME_DATA,
	TOR_STORAGE_MODE,

	SET_UPLOAD_RATE_LIMIT = 0x200,
	SET_DOWNLOAD_RATE_LIMIT,
	SET_MAX_UPLOAD_SLOTS,
	SET_MAX_CONNECTIONS,
	SET_SEQUENTIAL_DOWNLOAD, // torrent only
	SET_SUPER_SEEDING, // torrent only
	SET_HALF_OPEN_LIMIT, // session only



	TAG_END = 0x7fffffff
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

#ifdef __cplusplus
extern "C"
{
#endif

// use SES_* tags in tag list
void* create_session(int first_tag, ...);
void close_session(void* ses);

// use TOR_* tags in tag list
int add_torrent(void* ses, int first_tag, ...);
void remove_torrent(void* ses, int tor, int flags);
// use SET_* tags in tag list
int set_session_settings(void* ses, int first_tag, ...);

int get_torrent_status(int tor, struct torrent_status* s, int struct_size);

// use SET_* tags in tag list
int set_torrent_settings(int tor, int first_tag, ...);

#ifdef __cplusplus
}
#endif

#endif

