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

#include "libtorrent/session.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/torrent_handle.hpp"
#include <boost/bind.hpp>

#include <libtorrent.h>

namespace
{
	std::vector<libtorrent::torrent_handle> handles;

	int find_handle(libtorrent::torrent_handle h)
	{
		std::vector<libtorrent::torrent_handle>::const_iterator i
			= std::find(handles.begin(), handles.end(), h);
		if (i == handles.end()) return -1;
		return i - handles.begin();
	}

	libtorrent::torrent_handle get_handle(int i)
	{
		if (i < 0 || i >= int(handles.size())) return libtorrent::torrent_handle();
		return handles[i];
	}

	int add_handle(libtorrent::torrent_handle const& h)
	{
		std::vector<libtorrent::torrent_handle>::iterator i = std::find_if(handles.begin()
			, handles.end(), !boost::bind(&libtorrent::torrent_handle::is_valid, _1));
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

	void copy_proxy_setting(libtorrent::proxy_settings* s, proxy_setting const* ps)
	{
		s->hostname.assign(ps->hostname);
		s->port = ps->port;
		s->username.assign(ps->username);
		s->password.assign(ps->password);
		s->type = (libtorrent::proxy_settings::proxy_type)ps->type;
	}
}

extern "C"
{

TORRENT_EXPORT void* session_create(int tag, ...)
{
	using namespace libtorrent;

	va_list lp;
	va_start(lp, tag);

	fingerprint fing("LT", LIBTORRENT_VERSION_MAJOR, LIBTORRENT_VERSION_MINOR, 0, 0);
	std::pair<int, int> listen_range(-1, -1);
	char const* listen_interface = "0.0.0.0";
	int flags = session::start_default_features | session::add_default_plugins;
	int alert_mask = alert::error_notification;

	while (tag != TAG_END)
	{
		switch (tag)
		{
			case SES_FINGERPRINT:
			{
				char const* f = va_arg(lp, char const*);
				fing.name[0] = f[0];
				fing.name[1] = f[1];
				break;
			}
			case SES_LISTENPORT:
				listen_range.first = va_arg(lp, int);
				break;
			case SES_LISTENPORT_END:
				listen_range.second = va_arg(lp, int);
				break;
			case SES_VERSION_MAJOR:
				fing.major_version = va_arg(lp, int);
				break;
			case SES_VERSION_MINOR:
				fing.minor_version = va_arg(lp, int);
				break;
			case SES_VERSION_TINY:
				fing.revision_version = va_arg(lp, int);
				break;
			case SES_VERSION_TAG:
				fing.tag_version = va_arg(lp, int);
				break;
			case SES_FLAGS:
				flags = va_arg(lp, int);
				break;
			case SES_ALERT_MASK:
				alert_mask = va_arg(lp, int);
				break;
			case SES_LISTEN_INTERFACE:
				listen_interface = va_arg(lp, char const*);
				break;
			default:
				// skip unknown tags
				va_arg(lp, void*);
				break;
		}

		tag = va_arg(lp, int);
	}

	if (listen_range.first != -1 && (listen_range.second == -1
		|| listen_range.second < listen_range.first))
		listen_range.second = listen_range.first;

	return new (std::nothrow) session(fing, listen_range, listen_interface, flags, alert_mask);
}

TORRENT_EXPORT void session_close(void* ses)
{
	delete (libtorrent::session*)ses;
}

TORRENT_EXPORT int session_add_torrent(void* ses, int tag, ...)
{
	using namespace libtorrent;

	va_list lp;
	va_start(lp, tag);
	session* s = (session*)ses;
	add_torrent_params params;

	char const* torrent_data = 0;
	int torrent_size = 0;

	char const* resume_data = 0;
	int resume_size = 0;

	char const* magnet_url = 0;

	error_code ec;

	while (tag != TAG_END)
	{
		switch (tag)
		{
			case TOR_FILENAME:
				params.ti = new (std::nothrow) torrent_info(va_arg(lp, char const*), ec);
				break;
			case TOR_TORRENT:
				torrent_data = va_arg(lp, char const*);
				break;
			case TOR_TORRENT_SIZE:
				torrent_size = va_arg(lp, int);
				break;
			case TOR_INFOHASH:
				params.ti = new (std::nothrow) torrent_info(sha1_hash(va_arg(lp, char const*)));
				break;
			case TOR_INFOHASH_HEX:
			{
				sha1_hash ih;
				from_hex(va_arg(lp, char const*), 40, (char*)&ih[0]);
				params.ti = new (std::nothrow) torrent_info(ih);
				break;
			}
			case TOR_MAGNETLINK:
				magnet_url = va_arg(lp, char const*);
				break;
			case TOR_TRACKER_URL:
				params.tracker_url = va_arg(lp, char const*);
				break;
			case TOR_RESUME_DATA:
				resume_data = va_arg(lp, char const*);
				break;
			case TOR_RESUME_DATA_SIZE:
				resume_size = va_arg(lp, int);
				break;
			case TOR_SAVE_PATH:
				params.save_path = va_arg(lp, char const*);
				break;
			case TOR_NAME:
				params.name = va_arg(lp, char const*);
				break;
			case TOR_PAUSED:
				params.paused = va_arg(lp, int) != 0;
				break;
			case TOR_AUTO_MANAGED:
				params.auto_managed = va_arg(lp, int) != 0;
				break;
			case TOR_DUPLICATE_IS_ERROR:
				params.duplicate_is_error = va_arg(lp, int) != 0;
				break;
			case TOR_USER_DATA:
				params.userdata = va_arg(lp, void*);
				break;
			case TOR_SEED_MODE:
				params.seed_mode = va_arg(lp, int) != 0;
				break;
			case TOR_OVERRIDE_RESUME_DATA:
				params.override_resume_data = va_arg(lp, int) != 0;
				break;
			case TOR_STORAGE_MODE:
				params.storage_mode = (libtorrent::storage_mode_t)va_arg(lp, int);
				break;
			default:
				// ignore unknown tags
				va_arg(lp, void*);
				break;
		}

		tag = va_arg(lp, int);
	}

	if (!params.ti && torrent_data && torrent_size)
		params.ti = new (std::nothrow) torrent_info(torrent_data, torrent_size);

	std::vector<char> rd;
	if (resume_data && resume_size)
	{
		rd.assign(resume_data, resume_data + resume_size);
		params.resume_data = &rd;
	}
	torrent_handle h;
	if (!params.ti && magnet_url)
	{
		h = add_magnet_uri(*s, magnet_url, params, ec);
	}
	else
	{
		h = s->add_torrent(params, ec);
	}

	if (!h.is_valid())
	{
		return -1;
	}

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

	session* s = (session*)ses;

	std::auto_ptr<alert> a = s->pop_alert();
	if (!a.get()) return -1;

	if (category) *category = a->category();
	strncpy(dest, a->message().c_str(), len - 1);
	dest[len - 1] = 0;

	return 0; // for now
}

TORRENT_EXPORT int session_set_settings(void* ses, int tag, ...)
{
	using namespace libtorrent;

	session* s = (session*)ses;

	va_list lp;
	va_start(lp, tag);

	while (tag != TAG_END)
	{
		switch (tag)
		{
			case SET_UPLOAD_RATE_LIMIT:
				s->set_upload_rate_limit(va_arg(lp, int));
				break;
			case SET_DOWNLOAD_RATE_LIMIT:
				s->set_download_rate_limit(va_arg(lp, int));
				break;
			case SET_LOCAL_UPLOAD_RATE_LIMIT:
				s->set_local_upload_rate_limit(va_arg(lp, int));
				break;
			case SET_LOCAL_DOWNLOAD_RATE_LIMIT:
				s->set_local_download_rate_limit(va_arg(lp, int));
				break;
			case SET_MAX_UPLOAD_SLOTS:
				s->set_max_uploads(va_arg(lp, int));
				break;
			case SET_MAX_CONNECTIONS:
				s->set_max_connections(va_arg(lp, int));
				break;
			case SET_HALF_OPEN_LIMIT:
				s->set_max_half_open_connections(va_arg(lp, int));
				break;
			case SET_PEER_PROXY:
			{
				libtorrent::proxy_settings ps;
				copy_proxy_setting(&ps, va_arg(lp, struct proxy_setting const*));
				s->set_peer_proxy(ps);
			}
			case SET_WEB_SEED_PROXY:
			{
				libtorrent::proxy_settings ps;
				copy_proxy_setting(&ps, va_arg(lp, struct proxy_setting const*));
				s->set_web_seed_proxy(ps);
			}
			case SET_TRACKER_PROXY:
			{
				libtorrent::proxy_settings ps;
				copy_proxy_setting(&ps, va_arg(lp, struct proxy_setting const*));
				s->set_tracker_proxy(ps);
			}
			case SET_ALERT_MASK:
			{
				s->set_alert_mask(va_arg(lp, int));
			}
#ifndef TORRENT_DISABLE_DHT
			case SET_DHT_PROXY:
			{
				libtorrent::proxy_settings ps;
				copy_proxy_setting(&ps, va_arg(lp, struct proxy_setting const*));
				s->set_dht_proxy(ps);
			}
#endif
			case SET_PROXY:
			{
				libtorrent::proxy_settings ps;
				copy_proxy_setting(&ps, va_arg(lp, struct proxy_setting const*));
				s->set_peer_proxy(ps);
				s->set_web_seed_proxy(ps);
				s->set_tracker_proxy(ps);
#ifndef TORRENT_DISABLE_DHT
				s->set_dht_proxy(ps);
#endif
			}
			default:
				// ignore unknown tags
				va_arg(lp, void*);
				break;
		}

		tag = va_arg(lp, int);
	}
	return 0;
}

TORRENT_EXPORT int session_get_setting(void* ses, int tag, void* value, int* value_size)
{
	using namespace libtorrent;
	session* s = (session*)ses;

	switch (tag)
	{
		case SET_UPLOAD_RATE_LIMIT:
			return set_int_value(value, value_size, s->upload_rate_limit());
		case SET_DOWNLOAD_RATE_LIMIT:
			return set_int_value(value, value_size, s->download_rate_limit());
		case SET_LOCAL_UPLOAD_RATE_LIMIT:
			return set_int_value(value, value_size, s->local_upload_rate_limit());
		case SET_LOCAL_DOWNLOAD_RATE_LIMIT:
			return set_int_value(value, value_size, s->local_download_rate_limit());
		case SET_MAX_UPLOAD_SLOTS:
			return set_int_value(value, value_size, s->max_uploads());
		case SET_MAX_CONNECTIONS:
			return set_int_value(value, value_size, s->max_connections());
		case SET_HALF_OPEN_LIMIT:
			return set_int_value(value, value_size, s->max_half_open_connections());
		default:
			return -2;
	}
}

TORRENT_EXPORT int session_get_status(void* sesptr, struct session_status* s, int struct_size)
{
	libtorrent::session* ses = (libtorrent::session*)sesptr;

	libtorrent::session_status ss = ses->status();
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
	libtorrent::torrent_handle h = get_handle(tor);
	if (!h.is_valid()) return -1;

	libtorrent::torrent_status ts = h.status();

	if (struct_size != sizeof(torrent_status)) return -1;

	s->state = (state_t)ts.state;
	s->paused = ts.paused;
	s->progress = ts.progress;
	strncpy(s->error, ts.error.c_str(), 1025);
	s->next_announce = ts.next_announce.total_seconds();
	s->announce_interval = ts.announce_interval.total_seconds();
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
	s->sparse_regions = ts.sparse_regions;
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
			case SET_UPLOAD_RATE_LIMIT:
				h.set_upload_limit(va_arg(lp, int));
				break;
			case SET_DOWNLOAD_RATE_LIMIT:
				h.set_download_limit(va_arg(lp, int));
				break;
			case SET_MAX_UPLOAD_SLOTS:
				h.set_max_uploads(va_arg(lp, int));
				break;
			case SET_MAX_CONNECTIONS:
				h.set_max_connections(va_arg(lp, int));
				break;
			case SET_SEQUENTIAL_DOWNLOAD:
				h.set_sequential_download(va_arg(lp, int) != 0);
				break;
			case SET_SUPER_SEEDING:
				h.super_seeding(va_arg(lp, int) != 0);
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
		case SET_UPLOAD_RATE_LIMIT:
			return set_int_value(value, value_size, h.upload_limit());
		case SET_DOWNLOAD_RATE_LIMIT:
			return set_int_value(value, value_size, h.download_limit());
		case SET_MAX_UPLOAD_SLOTS:
			return set_int_value(value, value_size, h.max_uploads());
		case SET_MAX_CONNECTIONS:
			return set_int_value(value, value_size, h.max_connections());
		case SET_SEQUENTIAL_DOWNLOAD:
			return set_int_value(value, value_size, h.is_sequential_download());
		case SET_SUPER_SEEDING:
			return set_int_value(value, value_size, h.super_seeding());
		default:
			return -2;
	}
}

} // extern "C"

