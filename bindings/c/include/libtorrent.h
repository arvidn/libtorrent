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

#include "libtorrent_settings.h"
#include <stdint.h>

#if defined _MSC_VER || defined __MINGW32__
# define LIBTORRENT_C_EXPORT __declspec(dllexport)
# define LIBTORRENT_C_IMPORT __declspec(dllimport)
#elif __GNU__ >= 4
# define LIBTORRENT_C_EXPORT __attribute__((visibility("default")))
# define LIBTORRENT_C_IMPORT __attribute__((visibility("default")))
#else
# define LIBTORRENT_C_EXPORT
# define LIBTORRENT_C_IMPORT
#endif

#ifdef LIBTORRENT_C_BUILDING_LIBRARY
#define LIBTORRENT_C_DECL LIBTORRENT_C_EXPORT
#else
#define LIBTORRENT_C_DECL LIBTORRENT_C_IMPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum tags
{
	TAG_END = 0,

	// === add_torrent_params tags ===

	// identifying the torrent to add
	TOR_FILENAME = 0x100, // char const*
	TOR_TORRENT, // char const*, specify size of buffer with TOR_TORRENT_SIZE
	TOR_TORRENT_SIZE, // int
	TOR_INFOHASH, // char const*, must point to a 20 byte array
	TOR_MAGNETLINK, // char const*, url

	TOR_TRACKER_URL, // char const*
	TOR_RESUME_DATA, // char const*
	TOR_RESUME_DATA_SIZE, // int
	TOR_SAVE_PATH, // char const*
	TOR_NAME, // char const*
	TOR_FLAGS, // int (torrent_flags enum)
	TOR_USER_DATA, //void*
	TOR_STORAGE_MODE, // int (value from storage_mode_t enum)


	// === torrent settings ===

	TSET_MAX_CONNECTIONS = 0x400, // int
	TSET_UPLOAD_RATE_LIMIT, // int (bytes per second)
	TSET_DOWNLOAD_RATE_LIMIT, // int (bytes per second)
	TSET_MAX_UPLOAD_SLOTS, // int
	TSET_FLAGS, // unsigned int (bits from torrent_flags enum)
	TSET_FLAGS_MASK, // unsigned int (bits from torrent_flags enum). This cannot be queries, only used for setting flags
};

// flags used for TOR_FLAGS
enum torrent_flags
{
	TF_SEED_MODE           = 0x1,
	TF_UPLOAD_MODE         = 0X2,
	TF_SHARE_MODE          = 0x4,
	TF_APPLY_IP_FILTER     = 0x8,
	TF_PAUSED              = 0x10,
	TF_AUTO_MANAGED        = 0x20,
	TF_DUPLICATE_IS_ERROR  = 0x40,
	TF_UPDATE_SUBSCRIBE    = 0x80,
	TF_SUPER_SEEDING       = 0x100,
	TF_SEQUENTIAL_DOWNLOAD = 0x200,
	TF_STOP_WHEN_READY     = 0x400,
	TF_OVERRIDE_TRACKERS   = 0x800,
	TF_OVERRIDE_WEB_SEEDS  = 0x1000,
	TF_NEED_SAVE_RESUME    = 0x2000,

	TF_DISABLE_DHT         = 0x80000,
	TF_DISABLE_LSD         = 0x100000,
	TF_DISABLE_PEX         = 0x200000,
	TF_ALL                 = 0xffffff,
};

enum category_t
{
	CAT_ERROR               = 0x1,
	CAT_PEER                = 0x2,
	CAT_PORT_MAPPING        = 0x4,
	CAT_STORAGE             = 0x8,
	CAT_TRACKER             = 0x10,
	CAT_CONNECT             = 0x20,
	CAT_STATUS              = 0x40,
	CAT_IP_BLOCK            = 0x100,
	CAT_PERFORMANCE_WARNING = 0x200,
	CAT_DHT                 = 0x400,
	CAT_STATS               = 0x800,
	CAT_SESSION_LOG         = 0x2000,
	CAT_TORRENT_LOG         = 0x4000,
	CAT_PEER_LOG            = 0x8000,
	CAT_INCOMING_REQUEST    = 0x10000,
	CAT_DHT_LOG             = 0x20000,
	CAT_DHT_OPERATION       = 0x40000,
	CAT_PORT_MAPPING_LOG    = 0x80000,
	CAT_PICKER_LOG          = 0x100000,
	CAT_FILE_PROGRESS       = 0x200000,
	CAT_PIECE_PROGRESS      = 0x400000,
	CAT_UPLOAD              = 0x800000,
	CAT_BLOCK_PROGRESS      = 0x1000000,
};

enum remove_flags
{
	RF_DELETE_FILES = 0x1,
	RF_DELETE_PARTFILE = 0x2,
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
	SM_ALLOCATE = 0,
	SM_SPARSE
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
	float progress;
	char error[1024];
	int next_announce;
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
	int seed_rank;
	int has_incoming;
};

struct libtorrent_alert;
struct libtorrent_session;

// the functions whose signature ends with::
//
// 	int first_tag, ...);
//
// takes a tag list. The tag list is a series of tag-value pairs. The tags are
// constants identifying which property the value controls. The type of the
// value varies between tags. The enumeration above specifies which type it
// expects. All tag lists must always be terminated by TAG_END.

// creates a session object and returns it as an opaque ``void*``. Returns
// ``NULL`` on error. Any non-NULL return value must be freed by passing it to
// ``session_close()``.

// use SET_* tags in tag list to configure the session.
LIBTORRENT_C_DECL struct libtorrent_session* session_create(int first_tag, ...);
LIBTORRENT_C_DECL void session_close(struct libtorrent_session* ses);

// use TOR_* tags in tag list
// returns a torrent handle, or a negative value if an error prevented the
// torrent from being added.
LIBTORRENT_C_DECL int session_add_torrent(struct libtorrent_session* ses, int first_tag, ...);

// remove specitied torrent from specified session. flags from remove_flags enum
LIBTORRENT_C_DECL void session_remove_torrent(struct libtorrent_session* ses, int tor, int flags);

// pass in an array of (possibly uninitialized) pointers to opaque type
// libtorrent_alert. Pass in the number of elements the array can hold, in
// in-out parameter ``array_size``. The number of valid alert pointers stored in
// the array is returned back via the value ``array_size`` points to.
LIBTORRENT_C_DECL int session_pop_alerts(struct libtorrent_session* ses, struct libtorrent_alert const** dest, int* array_size);

// updates session settings. use ``SET_*`` tags.
LIBTORRENT_C_DECL int session_set_settings(struct libtorrent_session* ses, int first_tag, ...);

// reads one session setting. use ``SET_*`` tag. For string, the value buffer
// must be large enough to hold the full string, or it will be truncated.
// the size of the returned value will be written to the ``value_size`` out
// parameter. Returns non-zero on failure.
LIBTORRENT_C_DECL int session_get_setting(struct libtorrent_session* ses, int tag, void* value, int* value_size);

// TODO: remove this in favor of post_torrent_updates()
LIBTORRENT_C_DECL int torrent_get_status(int tor, struct torrent_status* s, int struct_size);

// prints the alert's human readable message into buf, truncating it at ``size``
LIBTORRENT_C_DECL int alert_message(struct libtorrent_alert const* alert, char* buf, int size);

// returns the timestamp of when the alert was posted. The timestamp is the
// number of microseconds since epoch.
LIBTORRENT_C_DECL int64_t alert_timestamp(struct libtorrent_alert const* alert);

// returns the type of the alert. defined in libtorrent_alerts.h
LIBTORRENT_C_DECL int alert_type(struct libtorrent_alert const* alert);

// returns the category of the alert. This is a bitmask with one or more bits
// set from the category_t enum.
LIBTORRENT_C_DECL uint32_t alert_category(struct libtorrent_alert const* alert);

// if this is an alert with an associated torrent handle, return that handle.
// Otherwise, return -1.
LIBTORRENT_C_DECL int alert_torrent_handle(struct libtorrent_alert const* alert);

// if ``alert`` refers to a session_stats_alert, returns a pointer to session
// counters and NULL otherwise. The ``count`` out parameter is set to the number
// of counters in the array. The returned array is valid until the next call to
// session_pop_alerts(). To find a value in the array, use
// ``find_metric_idx()``.
LIBTORRENT_C_DECL int64_t const* alert_stats_counters(struct libtorrent_alert const* alert, int* count);

LIBTORRENT_C_DECL int find_metric_idx(char const* name);

// set a torrent specific setting. ``tor`` is the torrent handle whose setting
// to change. This call can change multiple settings in a single call. The
// variadic parameters is tag list of pairs of ``TSET_*`` enum values and the
// corresponding ``int`` value to set.
LIBTORRENT_C_DECL int torrent_set_settings(int tor, int first_tag, ...);

// get a torrent specific setting. ``tor`` is the torrent handle, ``tag``
// is a value from the ``TSET_*`` enum, indicating which setting to read.
// ``value`` should point to a buffer large enough to receive the value
// the size of the value written to the buffer is
// returned in the in-out-parameter ``value_size``, which need to be initialized
// to the buffer size.
LIBTORRENT_C_DECL int torrent_get_setting(int tor, int tag, void* value, int* value_size);

#ifdef __cplusplus
}
#endif

#undef LIBTORRENT_C_DECL
#undef LIBTORRENT_C_IMPORT
#undef LIBTORRENT_C_EXPORT

#endif

