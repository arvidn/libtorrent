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

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/session_status.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/torrent_status.hpp"
#include "libtorrent/session_status.hpp"
#include <boost/bind.hpp>

#include <libtorrent.h>
#include <stdarg.h>

namespace lt = libtorrent;

namespace
{
	// TODO: this should be associated with the session object
	std::vector<lt::torrent_handle> handles;

	int find_handle(lt::torrent_handle h)
	{
		std::vector<lt::torrent_handle>::const_iterator i
			= std::find(handles.begin(), handles.end(), h);
		if (i == handles.end()) return -1;
		return i - handles.begin();
	}

	lt::torrent_handle get_handle(int i)
	{
		if (i < 0 || i >= int(handles.size())) return lt::torrent_handle();
		return handles[i];
	}

	int add_handle(lt::torrent_handle const& h)
	{
		std::vector<lt::torrent_handle>::iterator i = std::find_if(handles.begin()
			, handles.end(), !boost::bind(&lt::torrent_handle::is_valid, _1));
		if (i != handles.end())
		{
			*i = h;
			return i - handles.begin();
		}

		handles.push_back(h);
		return handles.size() - 1;
	}

	int set_int_value(void* dst, int* size, int val)
	{
		if (*size < sizeof(int)) return -2;
		*((int*)dst) = val;
		*size = sizeof(int);
		return 0;
	}

#define STR_TAG(tag, name) \
	case tag: \
		pack.set_str(lt::settings_pack::name, va_arg(lp, char const*)); \
		break;

#define INT_TAG(tag, name) \
	case tag: \
		pack.set_int(lt::settings_pack::name, va_arg(lp, int)); \
		break;

#define BOOL_TAG(tag, name) \
	case tag: \
		pack.set_bool(lt::settings_pack::name, va_arg(lp, bool)); \
		break;

	void tag_list_to_settings_pack(lt::settings_pack& pack, int tag, va_list lp)
	{
		while (tag != TAG_END)
		{
			switch (tag)
			{
				INT_TAG(SES_UPLOAD_RATE_LIMIT, upload_rate_limit)
				INT_TAG(SES_DOWNLOAD_RATE_LIMIT, download_rate_limit)
				INT_TAG(SES_MAX_UPLOAD_SLOTS, unchoke_slots_limit)
				INT_TAG(SES_MAX_CONNECTIONS, connections_limit)
				STR_TAG(SES_PROXY_HOSTNAME, proxy_hostname)
				STR_TAG(SES_PROXY_USERNAME, proxy_username)
				STR_TAG(SES_PROXY_PASSWORD, proxy_password)
				INT_TAG(SES_PROXY_PORT, proxy_port)
				INT_TAG(SES_PROXY_TYPE, proxy_type)
				BOOL_TAG(SES_PROXY_DNS, proxy_hostnames)
				BOOL_TAG(SES_PROXY_PEER_CONNECTIONS, proxy_peer_connections)
				INT_TAG(SES_ALERT_MASK, alert_mask)
				STR_TAG(SES_LISTEN_INTERFACE, listen_interfaces)
				STR_TAG(SES_FINGERPRINT, peer_fingerprint)
				INT_TAG(SES_CACHE_SIZE, cache_size)
				INT_TAG(SES_READ_CACHE_LINE_SIZE, read_cache_line_size)
				INT_TAG(SES_WRITE_CACHE_LINE_SIZE, write_cache_line_size)
				BOOL_TAG(SES_ENABLE_UPNP, enable_upnp)
				BOOL_TAG(SES_ENABLE_NATPMP, enable_natpmp)
				BOOL_TAG(SES_ENABLE_LSD, enable_lsd)
				BOOL_TAG(SES_ENABLE_DHT, enable_dht)
				BOOL_TAG(SES_ENABLE_UTP_OUT, enable_outgoing_utp)
				BOOL_TAG(SES_ENABLE_UTP_IN, enable_incoming_utp)
				BOOL_TAG(SES_ENABLE_TCP_OUT, enable_outgoing_tcp)
				BOOL_TAG(SES_ENABLE_TCP_IN, enable_incoming_tcp)
				BOOL_TAG(SES_NO_ATIME_STORAGE, no_atime_storage)
				default:
					assert(false && "unknown tag in settings tag list");
					return;
			}

			tag = va_arg(lp, int);
		}
	}

#undef BOOL_TAG
#undef INT_TAG
#undef STR_TAG

#define STR_TAG(tag, name) \
	case tag: \
		atp.name = va_arg(lp, char const*); \
		break;

#define INT_TAG(tag, name) \
	case tag: \
		atp.name = va_arg(lp, int); \
		break;

#define STRVEC_TAG(tag, name) \
	case tag: \
		atp.name.push_back(va_arg(lp, char const*)); \
		break;

	void tag_list_to_torrent_params(lt::add_torrent_params& atp, int tag, va_list lp)
	{
		char const* torrent_data = 0;
		int torrent_size = 0;

		char const* resume_data = 0;
		int resume_size = 0;

		while (tag != TAG_END)
		{
			switch (tag)
			{
				INT_TAG(TOR_MAX_UPLOAD_SLOTS, max_uploads)
				INT_TAG(TOR_MAX_CONNECTIONS, max_connections)
				INT_TAG(TOR_UPLOAD_RATE_LIMIT, upload_limit)
				INT_TAG(TOR_DOWNLOAD_RATE_LIMIT, download_limit)
				INT_TAG(TOR_FLAGS, flags)
				STR_TAG(TOR_NAME, name)
				STR_TAG(TOR_TRACKER_ID, trackerid)
				STR_TAG(TOR_SAVE_PATH, save_path)
				STR_TAG(TOR_MAGNETLINK, url)
				STRVEC_TAG(TOR_TRACKER_URL, trackers)
				STRVEC_TAG(TOR_WEB_SEED, url_seeds)
				case TOR_RESUME_DATA:
					resume_data = va_arg(lp, char const*);
					break;
				case TOR_RESUME_DATA_SIZE:
					resume_size = va_arg(lp, int);
					break;
				case TOR_FILENAME: {
					lt::error_code ec;
					atp.ti.reset(new (std::nothrow) lt::torrent_info(va_arg(lp, char const*), ec));
					break;
				}
				case TOR_TORRENT:
					torrent_data = va_arg(lp, char const*);
					break;
				case TOR_TORRENT_SIZE:
					torrent_size = va_arg(lp, int);
					break;
				case TOR_INFOHASH:
					atp.ti.reset(new (std::nothrow) lt::torrent_info(lt::sha1_hash(va_arg(lp, char const*))));
					break;
				case TOR_INFOHASH_HEX:
				{
					lt::sha1_hash ih;
					lt::from_hex(va_arg(lp, char const*), 40, (char*)&ih[0]);
					atp.ti.reset(new (std::nothrow) lt::torrent_info(ih));
					break;
				}
				case TOR_USER_DATA:
					atp.userdata = va_arg(lp, void*);
					break;
				case TOR_STORAGE_MODE:
					atp.storage_mode = (lt::storage_mode_t)va_arg(lp, int);
					break;

				default:
					assert(false && "unknown tag in torrent tag list");
					return;
			}

			tag = va_arg(lp, int);
		}

		if (!atp.ti && torrent_data && torrent_size > 0)
			atp.ti.reset(new (std::nothrow) lt::torrent_info(torrent_data, torrent_size));

		if (resume_data && resume_size > 0)
			atp.resume_data.assign(resume_data, resume_data + resume_size);
	}

#undef STR_TAG

}

extern "C"
{

TORRENT_EXPORT session_t* session_create(int tag, ...)
{
	using namespace libtorrent;

	va_list lp;
	va_start(lp, tag);

	settings_pack pack;
	tag_list_to_settings_pack(pack, tag, lp);

	return reinterpret_cast<session_t*>(new (std::nothrow) session(pack));
}

TORRENT_EXPORT void session_close(session_t* ses)
{
	delete reinterpret_cast<lt::session*>(ses);
}

TORRENT_EXPORT int session_add_torrent(void* ses, int tag, ...)
{
	using namespace libtorrent;

	va_list lp;
	va_start(lp, tag);
	session* s = (session*)ses;
	add_torrent_params params;

	tag_list_to_torrent_params(params, tag, lp);

	error_code ec;
	torrent_handle h = s->add_torrent(params, ec);
	if (ec) return -1;
	if (!h.is_valid()) return -1;

	int i = find_handle(h);
	if (i == -1) i = add_handle(h);

	return i;
}

TORRENT_EXPORT void session_remove_torrent(void* ses, int tor, int flags)
{
	using namespace libtorrent;
	torrent_handle h = get_handle(tor);
	if (!h.is_valid()) return;

	session* s = (session*)ses;
	s->remove_torrent(h, flags);
}

TORRENT_EXPORT int session_pop_alert(void* ses, char* dest, int len, int* category)
{
	using namespace libtorrent;

	// TODO: this should be associated with the session object
	// and this could be a lot more efficient. The C API should probably pop
	// multiple alerts at a time as well
	static std::vector<lt::alert*> alerts;
	if (alerts.empty())
	{
		session* s = (session*)ses;
		s->pop_alerts(&alerts);
	}

	if (alerts.empty()) return -1;

	alert* a = alerts.front();
	alerts.erase(alerts.begin());

	if (category) *category = a->category();
	strncpy(dest, a->message().c_str(), len - 1);
	dest[len - 1] = 0;

	return 0; // for now
}

TORRENT_EXPORT int session_set_settings(void* ses, int tag, ...)
{
	using namespace libtorrent;

	session* s = (session*)ses;

	settings_pack sett;

	va_list lp;
	va_start(lp, tag);

	settings_pack pack;
	tag_list_to_settings_pack(pack, tag, lp);

	s->apply_settings(pack);

	return 0;
}

TORRENT_EXPORT int session_get_setting(void* ses, int tag, void* value, int* value_size)
{
	using namespace libtorrent;
	session* s = (session*)ses;
	settings_pack pack = s->get_settings();


#define INT_TAG(tag, name) \
	case tag: \
		return set_int_value(value, value_size, pack.get_int(lt::settings_pack::name));

#define BOOL_TAG(tag, name) \
	case tag: \
		return set_int_value(value, value_size, pack.get_bool(lt::settings_pack::name));

	switch (tag)
	{
		INT_TAG(SES_UPLOAD_RATE_LIMIT, upload_rate_limit)
		INT_TAG(SES_DOWNLOAD_RATE_LIMIT, download_rate_limit)
		INT_TAG(SES_MAX_UPLOAD_SLOTS, unchoke_slots_limit)
		INT_TAG(SES_MAX_CONNECTIONS, connections_limit)
//		STR_TAG(SES_PROXY_HOSTNAME, proxy_hostname)
//		STR_TAG(SES_PROXY_USERNAME, proxy_username)
//		STR_TAG(SES_PROXY_PASSWORD, proxy_password)
		INT_TAG(SES_PROXY_PORT, proxy_port)
		INT_TAG(SES_PROXY_TYPE, proxy_type)
		BOOL_TAG(SES_PROXY_DNS, proxy_hostnames)
		BOOL_TAG(SES_PROXY_PEER_CONNECTIONS, proxy_peer_connections)
		INT_TAG(SES_ALERT_MASK, alert_mask)
//		STR_TAG(SES_LISTEN_INTERFACE, listen_interfaces)
//		STR_TAG(SES_FINGERPRINT, peer_fingerprint)
		INT_TAG(SES_CACHE_SIZE, cache_size)
		INT_TAG(SES_READ_CACHE_LINE_SIZE, read_cache_line_size)
		INT_TAG(SES_WRITE_CACHE_LINE_SIZE, write_cache_line_size)
		BOOL_TAG(SES_ENABLE_UPNP, enable_upnp)
		BOOL_TAG(SES_ENABLE_NATPMP, enable_natpmp)
		BOOL_TAG(SES_ENABLE_LSD, enable_lsd)
		BOOL_TAG(SES_ENABLE_DHT, enable_dht)
		BOOL_TAG(SES_ENABLE_UTP_OUT, enable_outgoing_utp)
		BOOL_TAG(SES_ENABLE_UTP_IN, enable_incoming_utp)
		BOOL_TAG(SES_ENABLE_TCP_OUT, enable_outgoing_tcp)
		BOOL_TAG(SES_ENABLE_TCP_IN, enable_incoming_tcp)
		BOOL_TAG(SES_NO_ATIME_STORAGE, no_atime_storage)
		default:
			return -2;
	}

#undef BOOL_TAG
#undef INT_TAG

}

TORRENT_EXPORT int session_get_status(void* sesptr, struct session_status* s, int struct_size)
{
	lt::session* ses = (libtorrent::session*)sesptr;

	// TODO: use performance counters here instead

	lt::session_status ss = ses->status();
	if (struct_size != sizeof(session_status)) return -1;

	s->has_incoming_connections = ss.has_incoming_connections;

	s->upload_rate = ss.upload_rate;
	s->download_rate = ss.download_rate;
	s->total_download = ss.total_download;
	s->total_upload = ss.total_upload;

	s->payload_upload_rate = ss.payload_upload_rate;
	s->payload_download_rate = ss.payload_download_rate;
	s->total_payload_download = ss.total_payload_download;
	s->total_payload_upload = ss.total_payload_upload;

	s->ip_overhead_upload_rate = ss.ip_overhead_upload_rate;
	s->ip_overhead_download_rate = ss.ip_overhead_download_rate;
	s->total_ip_overhead_download = ss.total_ip_overhead_download;
	s->total_ip_overhead_upload = ss.total_ip_overhead_upload;

	s->dht_upload_rate = ss.dht_upload_rate;
	s->dht_download_rate = ss.dht_download_rate;
	s->total_dht_download = ss.total_dht_download;
	s->total_dht_upload = ss.total_dht_upload;

	s->tracker_upload_rate = ss.tracker_upload_rate;
	s->tracker_download_rate = ss.tracker_download_rate;
	s->total_tracker_download = ss.total_tracker_download;
	s->total_tracker_upload = ss.total_tracker_upload;

	s->total_redundant_bytes = ss.total_redundant_bytes;
	s->total_failed_bytes = ss.total_failed_bytes;

	s->num_peers = ss.num_peers;
	s->num_unchoked = ss.num_unchoked;
	s->allowed_upload_slots = ss.allowed_upload_slots;

	s->up_bandwidth_queue = ss.up_bandwidth_queue;
	s->down_bandwidth_queue = ss.down_bandwidth_queue;

	s->up_bandwidth_bytes_queue = ss.up_bandwidth_bytes_queue;
	s->down_bandwidth_bytes_queue = ss.down_bandwidth_bytes_queue;

	s->optimistic_unchoke_counter = ss.optimistic_unchoke_counter;
	s->unchoke_counter = ss.unchoke_counter;

	s->dht_nodes = ss.dht_nodes;
	s->dht_node_cache = ss.dht_node_cache;
	s->dht_torrents = ss.dht_torrents;
	s->dht_global_nodes = ss.dht_global_nodes;
	return 0;
}

TORRENT_EXPORT int torrent_get_status(int tor, torrent_status* s, int struct_size)
{
	lt::torrent_handle h = get_handle(tor);
	if (!h.is_valid()) return -1;

	lt::torrent_status ts = h.status();

	if (struct_size != sizeof(torrent_status)) return -1;

	s->state = (state_t)ts.state;
	s->paused = ts.paused;
	s->progress = ts.progress;
	strncpy(s->error, ts.error.c_str(), 1025);
	s->next_announce = lt::total_seconds(ts.next_announce);
	s->announce_interval = lt::total_seconds(ts.announce_interval);
	strncpy(s->current_tracker, ts.current_tracker.c_str(), 512);
	s->total_download = ts.total_download = ts.total_download = ts.total_download;
	s->total_upload = ts.total_upload = ts.total_upload = ts.total_upload;
	s->total_payload_download = ts.total_payload_download;
	s->total_payload_upload = ts.total_payload_upload;
	s->total_failed_bytes = ts.total_failed_bytes;
	s->total_redundant_bytes = ts.total_redundant_bytes;
	s->download_rate = ts.download_rate;
	s->upload_rate = ts.upload_rate;
	s->download_payload_rate = ts.download_payload_rate;
	s->upload_payload_rate = ts.upload_payload_rate;
	s->num_seeds = ts.num_seeds;
	s->num_peers = ts.num_peers;
	s->num_complete = ts.num_complete;
	s->num_incomplete = ts.num_incomplete;
	s->list_seeds = ts.list_seeds;
	s->list_peers = ts.list_peers;
	s->connect_candidates = ts.connect_candidates;
	s->num_pieces = ts.num_pieces;
	s->total_done = ts.total_done;
	s->total_wanted_done = ts.total_wanted_done;
	s->total_wanted = ts.total_wanted;
	s->distributed_copies = ts.distributed_copies;
	s->block_size = ts.block_size;
	s->num_uploads = ts.num_uploads;
	s->num_connections = ts.num_connections;
	s->uploads_limit = ts.uploads_limit;
	s->connections_limit = ts.connections_limit;
//	s->storage_mode = (storage_mode_t)ts.storage_mode;
	s->up_bandwidth_queue = ts.up_bandwidth_queue;
	s->down_bandwidth_queue = ts.down_bandwidth_queue;
	s->all_time_upload = ts.all_time_upload;
	s->all_time_download = ts.all_time_download;
	s->active_time = ts.active_time;
	s->seeding_time = ts.seeding_time;
	s->seed_rank = ts.seed_rank;
	s->last_scrape = ts.last_scrape;
	s->has_incoming = ts.has_incoming;
	s->seed_mode = ts.seed_mode;
	return 0;
}

TORRENT_EXPORT int torrent_set_settings(int tor, int tag, ...)
{
	using namespace libtorrent;
	torrent_handle h = get_handle(tor);
	if (!h.is_valid()) return -1;

	va_list lp;
	va_start(lp, tag);

	while (tag != TAG_END)
	{
		switch (tag)
		{
			case TOR_UPLOAD_RATE_LIMIT:
				h.set_upload_limit(va_arg(lp, int));
				break;
			case TOR_DOWNLOAD_RATE_LIMIT:
				h.set_download_limit(va_arg(lp, int));
				break;
			case TOR_MAX_UPLOAD_SLOTS:
				h.set_max_uploads(va_arg(lp, int));
				break;
			case TOR_MAX_CONNECTIONS:
				h.set_max_connections(va_arg(lp, int));
				break;
			default:
				// ignore unknown tags
				va_arg(lp, void*);
				break;
		}

		tag = va_arg(lp, int);
	}
	return 0;
}

TORRENT_EXPORT int torrent_get_setting(int tor, int tag, void* value, int* value_size)
{
	using namespace libtorrent;
	torrent_handle h = get_handle(tor);
	if (!h.is_valid()) return -1;

	switch (tag)
	{
		case SES_UPLOAD_RATE_LIMIT:
			return set_int_value(value, value_size, h.upload_limit());
		case SES_DOWNLOAD_RATE_LIMIT:
			return set_int_value(value, value_size, h.download_limit());
		case SES_MAX_UPLOAD_SLOTS:
			return set_int_value(value, value_size, h.max_uploads());
		case SES_MAX_CONNECTIONS:
			return set_int_value(value, value_size, h.max_connections());
		default:
			return -2;
	}
}

} // extern "C"

