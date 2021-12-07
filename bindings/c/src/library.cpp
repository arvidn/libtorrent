/*

Copyright (c) 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/read_resume_data.hpp"
#include "libtorrent/session_stats.hpp"
#include "libtorrent/alert_types.hpp"

#include <libtorrent.h>
#include <stdarg.h>

namespace {

std::vector<lt::torrent_handle> handles;
std::vector<int> free_handle_slots;

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
		, handles.end()
		, [](lt::torrent_handle const& h) { return !h.is_valid(); });
	if (i != handles.end())
	{
		*i = h;
		return i - handles.begin();
	}

	if (free_handle_slots.empty())
	{
		handles.push_back(h);
		return handles.size() - 1;
	}

	int const ret = free_handle_slots.back();
	free_handle_slots.pop_back();
	handles[ret] = h;
	return ret;
}

void remove_handle(int const h)
{
	handles[h] = lt::torrent_handle{};
	if (h == int(handles.size() - 1))
	{
		handles.pop_back();
	}
	else
	{
		free_handle_slots.push_back(h);
	}
}

int set_int_value(void* dst, int* size, int val)
{
	if (*size < int(sizeof(int))) return -2;
	std::memcpy(dst, &val, sizeof(int));
	*size = sizeof(int);
	return 0;
}

int set_str_value(void* dst, int* size, std::string val)
{
	if (*size <= static_cast<int>(val.size())) return -2;
	std::memcpy(dst, val.c_str(), val.size() + 1);
	*size = val.size() + 1;
	return 0;
}

lt::add_torrent_params make_add_torrent_params(int tag, va_list lp)
{
	lt::add_torrent_params params;

	char const* torrent_data = nullptr;
	int torrent_size = 0;

	char const* resume_data = nullptr;
	int resume_size = 0;

	lt::error_code ec;
	while (tag != TAG_END)
	{
		switch (tag)
		{
			case TOR_FILENAME:
				params.ti = std::make_shared<lt::torrent_info>(va_arg(lp, char const*), ec);
				break;
			case TOR_TORRENT:
				torrent_data = va_arg(lp, char const*);
				break;
			case TOR_TORRENT_SIZE:
				torrent_size = va_arg(lp, int);
				break;
			case TOR_INFOHASH:
				params.info_hashes.v1 = lt::sha1_hash(va_arg(lp, char const*));
				break;
			// TODO: add an info-hash-v2 field too
			case TOR_MAGNETLINK:
				parse_magnet_uri(va_arg(lp, char const*), params, ec);
				break;
			case TOR_TRACKER_URL:
				params.trackers.push_back(va_arg(lp, char const*));
				break;
			case TOR_RESUME_DATA:
				resume_data = va_arg(lp, char const*);
				if (resume_data && resume_size)
					params = lt::read_resume_data({resume_data, resume_size});
				break;
			case TOR_RESUME_DATA_SIZE:
				resume_size = va_arg(lp, int);
				if (resume_data && resume_size)
					params = lt::read_resume_data({resume_data, resume_size});
				break;
			case TOR_SAVE_PATH:
				params.save_path = va_arg(lp, char const*);
				break;
			case TOR_NAME:
				params.name = va_arg(lp, char const*);
				break;
			case TOR_FLAGS:
				params.flags = lt::torrent_flags_t(va_arg(lp, int));
				break;
			case TOR_USER_DATA:
				params.userdata = va_arg(lp, char*);
				break;
			case TOR_STORAGE_MODE:
				params.storage_mode = static_cast<lt::storage_mode_t>(va_arg(lp, int));
				break;
			default:
				// ignore unknown tags
				va_arg(lp, void*);
				break;
		}

		tag = va_arg(lp, int);
	}
	va_end(lp);

	if (!params.ti && torrent_data && torrent_size)
		params.ti = std::make_shared<lt::torrent_info>(lt::span<char const>{torrent_data, torrent_size}, lt::from_span);

	return params;
}
} // unnamed namespace

// defined in src/settings.cpp
extern lt::settings_pack make_settings(int tag, va_list);
extern int settings_key(int tag);

extern "C"
{

TORRENT_EXPORT libtorrent_session* session_create(int tag, ...)
{
	using namespace lt;

	va_list lp;
	va_start(lp, tag);
	lt::settings_pack pack = make_settings(tag, lp);
	va_end(lp);

	return reinterpret_cast<libtorrent_session*>(new (std::nothrow) session(std::move(pack)));
}

TORRENT_EXPORT void session_close(libtorrent_session* ses)
{
	delete reinterpret_cast<lt::session*>(ses);
}

TORRENT_EXPORT int session_add_torrent(libtorrent_session* ses, int tag, ...)
{
	using namespace lt;

	va_list lp;
	va_start(lp, tag);
	lt::add_torrent_params params = make_add_torrent_params(tag, lp);
	va_end(lp);

	auto* s = reinterpret_cast<lt::session*>(ses);
	lt::error_code ec;
	lt::torrent_handle h = s->add_torrent(params, ec);
	if (ec) return -1;

	if (!h.is_valid()) return -1;

	int i = find_handle(h);
	if (i == -1) i = add_handle(h);

	return i;
}

TORRENT_EXPORT void session_remove_torrent(libtorrent_session* ses, int tor, int flags)
{
	lt::torrent_handle h = get_handle(tor);
	if (!h.is_valid()) return;

	remove_handle(tor);

	auto* s = reinterpret_cast<lt::session*>(ses);
	s->remove_torrent(h, lt::remove_flags_t(flags));
}

TORRENT_EXPORT int session_pop_alerts(libtorrent_session* ses, libtorrent_alert const** dest, int* len)
{
	auto* s = reinterpret_cast<lt::session*>(ses);
	if (len == nullptr) return -1;
	if (*len < 0) return -1;
	if (*len == 0) return 0;

	std::vector<lt::alert*> ret;
	s->pop_alerts(&ret);

	// TODO: figure out what to do with the alert we may have lost here. Save
	// them to the next call somehow?
	int const to_copy = std::min(int(ret.size()), *len);
	std::copy(ret.begin(), ret.begin() + to_copy, reinterpret_cast<lt::alert const**>(dest));
	*len = to_copy;
	return 0; // for now
}

TORRENT_EXPORT int session_set_settings(libtorrent_session* ses, int tag, ...)
{
	va_list lp;
	va_start(lp, tag);
	lt::settings_pack pack = make_settings(tag, lp);
	va_end(lp);

	auto* s = reinterpret_cast<lt::session*>(ses);
	s->apply_settings(std::move(pack));

	return 0;
}

TORRENT_EXPORT int session_get_setting(libtorrent_session* ses, int tag, void* value, int* value_size)
{
	auto* s = reinterpret_cast<lt::session*>(ses);
	lt::settings_pack sett = s->get_settings();

	int const key = settings_key(tag);
	if (key < 0) return key;

	using sp = lt::settings_pack;

	switch (key & lt::settings_pack::type_mask)
	{
		case sp::string_type_base:
			return set_str_value(value, value_size, sett.get_str(key));
		case sp::int_type_base:
			return set_int_value(value, value_size, sett.get_int(key));
		case sp::bool_type_base:
			return set_int_value(value, value_size, sett.get_bool(key));
		default:
			return -1;
	}
}

TORRENT_EXPORT int torrent_get_status(int tor, torrent_status* s, int struct_size)
{
	lt::torrent_handle h = get_handle(tor);
	if (!h.is_valid()) return -1;

	lt::torrent_status ts = h.status();

	if (struct_size != sizeof(torrent_status)) return -1;

	s->state = (state_t)ts.state;
	s->progress = ts.progress;
	std::string err_msg = ts.errc.message();
	strncpy(s->error, err_msg.c_str(), sizeof(s->error));
	s->next_announce = lt::total_seconds(ts.next_announce);
	strncpy(s->current_tracker, ts.current_tracker.c_str(), sizeof(s->current_tracker));
	s->total_download = ts.total_download;
	s->total_upload = ts.total_upload;
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
	s->seed_rank = ts.seed_rank;
	s->has_incoming = ts.has_incoming;
	return 0;
}

TORRENT_EXPORT int alert_message(libtorrent_alert const* alert, char* buf, int size)
{
	auto const* a = reinterpret_cast<lt::alert const*>(alert);
	auto const msg = a->message();
	std::strncpy(buf, msg.c_str(), size - 1);
	buf[size - 1] = '\0';
	return 0;
}

TORRENT_EXPORT int64_t const* alert_stats_counters(struct libtorrent_alert const* alert, int* count)
{
	auto const* a = reinterpret_cast<lt::alert const*>(alert);
	auto const* sa = lt::alert_cast<lt::session_stats_alert>(a);
	if (sa == nullptr) return nullptr;

	lt::span<std::int64_t const> counters = sa->counters();
	*count = int(counters.size());
	return counters.data();
}

TORRENT_EXPORT std::int64_t alert_timestamp(struct libtorrent_alert const* alert)
{
	auto const* a = reinterpret_cast<lt::alert const*>(alert);
	return std::chrono::duration_cast<std::chrono::microseconds>(
		a->timestamp().time_since_epoch()).count();
}

TORRENT_EXPORT int alert_type(struct libtorrent_alert const* alert)
{
	auto const* a = reinterpret_cast<lt::alert const*>(alert);
	return a->type();
}

TORRENT_EXPORT uint32_t alert_category(struct libtorrent_alert const* alert)
{
	auto const* a = reinterpret_cast<lt::alert const*>(alert);
	return static_cast<std::uint32_t>(a->category());
}

TORRENT_EXPORT int alert_torrent_handle(struct libtorrent_alert const* alert)
{
	auto const* a = reinterpret_cast<lt::alert const*>(alert);
	int const type = a->type();
	switch (type)
	{
		// torrent_alert
#if TORRENT_ABI_VERSION == 1
		case lt::torrent_added_alert::alert_type:
#endif
		case lt::torrent_removed_alert::alert_type:
		case lt::read_piece_alert::alert_type:
		case lt::file_completed_alert::alert_type:
		case lt::file_renamed_alert::alert_type:
		case lt::file_rename_failed_alert::alert_type:
		case lt::performance_alert::alert_type:
		case lt::state_changed_alert::alert_type:
		case lt::hash_failed_alert::alert_type:
		case lt::torrent_finished_alert::alert_type:
		case lt::piece_finished_alert::alert_type:
		case lt::storage_moved_alert::alert_type:
		case lt::storage_moved_failed_alert::alert_type:
		case lt::torrent_deleted_alert::alert_type:
		case lt::torrent_delete_failed_alert::alert_type:
		case lt::save_resume_data_alert::alert_type:
		case lt::save_resume_data_failed_alert::alert_type:
		case lt::torrent_paused_alert::alert_type:
		case lt::torrent_resumed_alert::alert_type:
		case lt::torrent_checked_alert::alert_type:
		case lt::url_seed_alert::alert_type:
		case lt::file_error_alert::alert_type:
		case lt::metadata_failed_alert::alert_type:
		case lt::metadata_received_alert::alert_type:
		case lt::fastresume_rejected_alert::alert_type:
#if TORRENT_ABI_VERSION <= 2
		case lt::stats_alert::alert_type:
#endif
		case lt::cache_flushed_alert::alert_type:
#if TORRENT_ABI_VERSION == 1
		case lt::anonymous_mode_alert::alert_type:
#endif
		case lt::torrent_error_alert::alert_type:
		case lt::torrent_need_cert_alert::alert_type:
		case lt::add_torrent_alert::alert_type:
		case lt::torrent_log_alert::alert_type:
		// peer_alert
		case lt::peer_ban_alert::alert_type:
		case lt::peer_unsnubbed_alert::alert_type:
		case lt::peer_snubbed_alert::alert_type:
		case lt::peer_error_alert::alert_type:
		case lt::peer_connect_alert::alert_type:
		case lt::peer_disconnected_alert::alert_type:
		case lt::invalid_request_alert::alert_type:
		case lt::request_dropped_alert::alert_type:
		case lt::block_timeout_alert::alert_type:
		case lt::block_finished_alert::alert_type:
		case lt::block_downloading_alert::alert_type:
		case lt::unwanted_block_alert::alert_type:
		case lt::peer_blocked_alert::alert_type:
		case lt::lsd_peer_alert::alert_type:
		case lt::peer_log_alert::alert_type:
		case lt::incoming_request_alert::alert_type:
		case lt::picker_log_alert::alert_type:
		case lt::block_uploaded_alert::alert_type:
		// tracker alert
		case lt::tracker_error_alert::alert_type:
		case lt::tracker_warning_alert::alert_type:
		case lt::scrape_reply_alert::alert_type:
		case lt::scrape_failed_alert::alert_type:
		case lt::tracker_reply_alert::alert_type:
		case lt::dht_reply_alert::alert_type:
		case lt::tracker_announce_alert::alert_type:
		case lt::trackerid_alert::alert_type:
		{
			lt::torrent_handle h = static_cast<lt::torrent_alert const*>(a)->handle;
			return find_handle(h);
		}
		default: return -1;
	};
}

TORRENT_EXPORT int find_metric_idx(char const* name)
{
	return lt::find_metric_idx(name);
}

TORRENT_EXPORT int torrent_set_settings(int tor, int tag, ...)
{
	lt::torrent_handle h = get_handle(tor);
	if (!h.is_valid()) return -1;

	va_list lp;
	va_start(lp, tag);

	bool flags_set = false;
	std::uint64_t flags = 0;
	std::uint64_t mask = UINT64_MAX;

	while (tag != TAG_END)
	{
		switch (tag)
		{
			case TSET_UPLOAD_RATE_LIMIT:
				h.set_upload_limit(va_arg(lp, int));
				break;
			case TSET_DOWNLOAD_RATE_LIMIT:
				h.set_download_limit(va_arg(lp, int));
				break;
			case TSET_MAX_UPLOAD_SLOTS:
				h.set_max_uploads(va_arg(lp, int));
				break;
			case TSET_MAX_CONNECTIONS:
				h.set_max_connections(va_arg(lp, int));
				break;
			case TSET_FLAGS:
				flags = va_arg(lp, int);
				flags_set = true;
				break;
			case TSET_FLAGS_MASK:
				mask = va_arg(lp, int);
				break;
			default:
				// ignore unknown tags
				va_arg(lp, void*);
				break;
		}

		tag = va_arg(lp, int);
	}
	va_end(lp);

	if (flags_set)
		h.set_flags(lt::torrent_flags_t(flags), lt::torrent_flags_t(mask));
	return 0;
}

TORRENT_EXPORT int torrent_get_setting(int const tor, int const tag, void* value, int* value_size)
{
	lt::torrent_handle h = get_handle(tor);
	if (!h.is_valid()) return -1;

	switch (tag)
	{
		case TSET_UPLOAD_RATE_LIMIT:
			return set_int_value(value, value_size, h.upload_limit());
		case TSET_DOWNLOAD_RATE_LIMIT:
			return set_int_value(value, value_size, h.download_limit());
		case TSET_MAX_UPLOAD_SLOTS:
			return set_int_value(value, value_size, h.max_uploads());
		case TSET_MAX_CONNECTIONS:
			return set_int_value(value, value_size, h.max_connections());
		case TSET_FLAGS:
			return set_int_value(value, value_size, static_cast<int>(static_cast<std::uint64_t>(h.flags())));
		default:
			return -2;
	}
}

} // extern "C"

