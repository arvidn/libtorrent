/*

Copyright (c) 2003-2016, Arvid Norberg, Daniel Wallin
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

#include <string>
#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.

#include "libtorrent/config.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/stack_allocator.hpp"
#include "libtorrent/piece_block.hpp"
#include "libtorrent/hex.hpp" // to_hex
#include "libtorrent/session_stats.hpp"

#ifndef TORRENT_NO_DEPRECATE
#include "libtorrent/write_resume_data.hpp"
#endif

#include "libtorrent/aux_/escape_string.hpp" // for convert_from_native
#include "libtorrent/aux_/max_path.hpp" // for TORRENT_MAX_PATH

namespace libtorrent {

	constexpr alert_category_t alert::error_notification;
	constexpr alert_category_t alert::peer_notification;
	constexpr alert_category_t alert::port_mapping_notification;
	constexpr alert_category_t alert::storage_notification;
	constexpr alert_category_t alert::tracker_notification;
	constexpr alert_category_t alert::debug_notification;
	constexpr alert_category_t alert::status_notification;
	constexpr alert_category_t alert::progress_notification;
	constexpr alert_category_t alert::ip_block_notification;
	constexpr alert_category_t alert::performance_warning;
	constexpr alert_category_t alert::dht_notification;
	constexpr alert_category_t alert::stats_notification;
	constexpr alert_category_t alert::session_log_notification;
	constexpr alert_category_t alert::torrent_log_notification;
	constexpr alert_category_t alert::peer_log_notification;
	constexpr alert_category_t alert::incoming_request_notification;
	constexpr alert_category_t alert::dht_log_notification;
	constexpr alert_category_t alert::dht_operation_notification;
	constexpr alert_category_t alert::port_mapping_log_notification;
	constexpr alert_category_t alert::picker_log_notification;
	constexpr alert_category_t alert::all_categories;
#ifndef TORRENT_NO_DEPRECATE
	constexpr alert_category_t alert::rss_notification;
#endif

	alert::alert() : m_timestamp(clock_type::now()) {}
	alert::~alert() = default;
	time_point alert::timestamp() const { return m_timestamp; }

	torrent_alert::torrent_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: handle(h)
		, m_alloc(alloc)
	{
		std::shared_ptr<torrent> t = h.native_handle();
		if (t)
		{
			std::string name_str = t->name();
			if (!name_str.empty())
			{
				m_name_idx = alloc.copy_string(name_str);
			}
			else
			{
				m_name_idx = alloc.copy_string(aux::to_hex(t->info_hash()));
			}
		}
		else
		{
			m_name_idx = alloc.copy_string("");
		}

#ifndef TORRENT_NO_DEPRECATE
		name = m_alloc.get().ptr(m_name_idx);
#endif
	}

	char const* torrent_alert::torrent_name() const
	{
		return m_alloc.get().ptr(m_name_idx);
	}

	std::string torrent_alert::message() const
	{
		if (!handle.is_valid()) return " - ";
		return torrent_name();
	}

	peer_alert::peer_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, tcp::endpoint const& i
		, peer_id const& pi)
		: torrent_alert(alloc, h)
		, endpoint(i)
		, pid(pi)
#ifndef TORRENT_NO_DEPRECATE
		, ip(i)
#endif
	{}

	std::string peer_alert::message() const
	{
		return torrent_alert::message() + " peer (" + print_endpoint(endpoint)
			+ ", " + aux::identify_client_impl(pid) + ")";
	}

	tracker_alert::tracker_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep, string_view u)
		: torrent_alert(alloc, h)
		, local_endpoint(ep)
		, m_url_idx(alloc.copy_string(u))
#ifndef TORRENT_NO_DEPRECATE
		, url(u)
#endif
	{}

	char const* tracker_alert::tracker_url() const
	{
		return m_alloc.get().ptr(m_url_idx);
	}

	std::string tracker_alert::message() const
	{
		return torrent_alert::message() + " (" + tracker_url() + ")"
			+ "[" + print_endpoint(local_endpoint) + "]";
	}

	read_piece_alert::read_piece_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, piece_index_t p, boost::shared_array<char> d, int s)
		: torrent_alert(alloc, h)
		, buffer(std::move(d))
		, piece(p)
		, size(s)
	{}

	read_piece_alert::read_piece_alert(aux::stack_allocator& alloc
		, torrent_handle h, piece_index_t p, error_code e)
		: torrent_alert(alloc, h)
		, error(e)
		, piece(p)
		, size(0)
#ifndef TORRENT_NO_DEPRECATE
		, ec(e)
#endif
	{}

	std::string read_piece_alert::message() const
	{
		char msg[200];
		if (error)
		{
			std::snprintf(msg, sizeof(msg), "%s: read_piece %u failed: %s"
				, torrent_alert::message().c_str() , static_cast<int>(piece)
				, convert_from_native(error.message()).c_str());
		}
		else
		{
			std::snprintf(msg, sizeof(msg), "%s: read_piece %u successful"
				, torrent_alert::message().c_str() , static_cast<int>(piece));
		}
		return msg;
	}

	file_completed_alert::file_completed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, file_index_t idx)
		: torrent_alert(alloc, h)
		, index(idx)
	{}

	std::string file_completed_alert::message() const
	{
		std::string ret { torrent_alert::message() };
		char msg[200];
		std::snprintf(msg, sizeof(msg), ": file %d finished downloading"
			, static_cast<int>(index));
		ret.append(msg);
		return ret;
	}

	file_renamed_alert::file_renamed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, string_view n, file_index_t const idx)
		: torrent_alert(alloc, h)
		, index(idx)
		, m_name_idx(alloc.copy_string(n))
#ifndef TORRENT_NO_DEPRECATE
		, name(n)
#endif
	{}

	char const* file_renamed_alert::new_name() const
	{
		return m_alloc.get().ptr(m_name_idx);
	}

	std::string file_renamed_alert::message() const
	{
		std::string ret { torrent_alert::message() };
		char msg[200];
		std::snprintf(msg, sizeof(msg), ": file %d renamed to "
			, static_cast<int>(index));
		ret.append(msg);
		ret.append(new_name());
		return ret;
	}

	file_rename_failed_alert::file_rename_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, file_index_t const idx
		, error_code ec)
		: torrent_alert(alloc, h)
		, index(idx)
		, error(ec)
	{}

	std::string file_rename_failed_alert::message() const
	{
		std::string ret { torrent_alert::message() };
		char msg[200];
		std::snprintf(msg, sizeof(msg), ": failed to rename file %d: "
			, static_cast<int>(index));
		ret.append(msg);
		ret.append(convert_from_native(error.message()));
		return ret;
	}

	performance_alert::performance_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, performance_warning_t w)
		: torrent_alert(alloc, h)
		, warning_code(w)
	{}

	std::string performance_alert::message() const
	{
		static char const* warning_str[] =
		{
			"max outstanding disk writes reached",
			"max outstanding piece requests reached",
			"upload limit too low (download rate will suffer)",
			"download limit too low (upload rate will suffer)",
			"send buffer watermark too low (upload rate will suffer)",
			"too many optimistic unchoke slots",
			"using bittyrant unchoker with no upload rate limit set",
			"the disk queue limit is too high compared to the cache size. The disk queue eats into the cache size",
			"outstanding AIO operations limit reached",
			"too few ports allowed for outgoing connections",
			"too few file descriptors are allowed for this process. connection limit lowered"
		};

		return torrent_alert::message() + ": performance warning: "
			+ warning_str[warning_code];
	}

	state_changed_alert::state_changed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, torrent_status::state_t st
		, torrent_status::state_t prev_st)
		: torrent_alert(alloc, h)
		, state(st)
		, prev_state(prev_st)
	{}

	std::string state_changed_alert::message() const
	{
		static char const* state_str[] =
			{"checking (q)", "checking", "dl metadata"
			, "downloading", "finished", "seeding", "allocating"
			, "checking (r)"};

		return torrent_alert::message() + ": state changed to: "
			+ state_str[state];
	}

	tracker_error_alert::tracker_error_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep, int times
		, int status, string_view u, error_code const& e, string_view m)
		: tracker_alert(alloc, h, ep, u)
		, times_in_row(times)
		, status_code(status)
		, error(e)
		, m_msg_idx(alloc.copy_string(m))
#ifndef TORRENT_NO_DEPRECATE
		, msg(m)
#endif
	{
		TORRENT_ASSERT(!u.empty());
	}

	char const* tracker_error_alert::error_message() const
	{
		return m_alloc.get().ptr(m_msg_idx);
	}

	std::string tracker_error_alert::message() const
	{
		char ret[400];
		std::snprintf(ret, sizeof(ret), "%s (%d) %s \"%s\" (%d)"
			, tracker_alert::message().c_str(), status_code
			, convert_from_native(error.message()).c_str(), error_message()
			, times_in_row);
		return ret;
	}

	tracker_warning_alert::tracker_warning_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, string_view u, string_view m)
		: tracker_alert(alloc, h, ep, u)
		, m_msg_idx(alloc.copy_string(m))
#ifndef TORRENT_NO_DEPRECATE
		, msg(m)
#endif
	{
		TORRENT_ASSERT(!u.empty());
	}

	char const* tracker_warning_alert::warning_message() const
	{
		return m_alloc.get().ptr(m_msg_idx);
	}

	std::string tracker_warning_alert::message() const
	{
		return tracker_alert::message() + " warning: " + warning_message();
	}

	scrape_reply_alert::scrape_reply_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, int incomp, int comp, string_view u)
		: tracker_alert(alloc, h, ep, u)
		, incomplete(incomp)
		, complete(comp)
	{
		TORRENT_ASSERT(!u.empty());
	}

	std::string scrape_reply_alert::message() const
	{
		char ret[400];
		std::snprintf(ret, sizeof(ret), "%s scrape reply: %u %u"
			, tracker_alert::message().c_str(), incomplete, complete);
		return ret;
	}

	scrape_failed_alert::scrape_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, string_view u, error_code const& e)
		: tracker_alert(alloc, h, ep, u)
		, error(e)
		, m_msg_idx()
#ifndef TORRENT_NO_DEPRECATE
		, msg(convert_from_native(e.message()))
#endif
	{
		TORRENT_ASSERT(!u.empty());
	}

	scrape_failed_alert::scrape_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, string_view u, string_view m)
		: tracker_alert(alloc, h, ep, u)
		, error(errors::tracker_failure)
		, m_msg_idx(alloc.copy_string(m))
#ifndef TORRENT_NO_DEPRECATE
		, msg(m)
#endif
	{
		TORRENT_ASSERT(!u.empty());
	}

	char const* scrape_failed_alert::error_message() const
	{
		if (m_msg_idx == aux::allocation_slot()) return "";
		else return m_alloc.get().ptr(m_msg_idx);
	}

	std::string scrape_failed_alert::message() const
	{
		return tracker_alert::message() + " scrape failed: " + error_message();
	}

	tracker_reply_alert::tracker_reply_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, int np, string_view u)
		: tracker_alert(alloc, h, ep, u)
		, num_peers(np)
	{
		TORRENT_ASSERT(!u.empty());
	}

	std::string tracker_reply_alert::message() const
	{
		char ret[400];
		std::snprintf(ret, sizeof(ret), "%s received peers: %u"
			, tracker_alert::message().c_str(), num_peers);
		return ret;
	}

	dht_reply_alert::dht_reply_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, int np)
		: tracker_alert(alloc, h, {}, "")
		, num_peers(np)
	{}

	std::string dht_reply_alert::message() const
	{
		char ret[400];
		std::snprintf(ret, sizeof(ret), "%s received DHT peers: %u"
			, tracker_alert::message().c_str(), num_peers);
		return ret;
	}

	tracker_announce_alert::tracker_announce_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep, string_view u, int e)
		: tracker_alert(alloc, h, ep, u)
		, event(e)
	{
		TORRENT_ASSERT(!u.empty());
	}

	std::string tracker_announce_alert::message() const
	{
		static const char* event_str[] = {"none", "completed", "started", "stopped", "paused"};
		TORRENT_ASSERT_VAL(event < int(sizeof(event_str)/sizeof(event_str[0])), event);
		return tracker_alert::message() + " sending announce (" + event_str[event] + ")";
	}

	hash_failed_alert::hash_failed_alert(
		aux::stack_allocator& alloc
		, torrent_handle const& h
		, piece_index_t index)
		: torrent_alert(alloc, h)
		, piece_index(index)
	{
		TORRENT_ASSERT(index >= piece_index_t(0));
	}

	std::string hash_failed_alert::message() const
	{
		char ret[400];
		std::snprintf(ret, sizeof(ret), "%s hash for piece %u failed"
			, torrent_alert::message().c_str(), static_cast<int>(piece_index));
		return ret;
	}

	peer_ban_alert::peer_ban_alert(aux::stack_allocator& alloc
		, torrent_handle h, tcp::endpoint const& ep
		, peer_id const& peer_id)
		: peer_alert(alloc, h, ep, peer_id)
	{}

	std::string peer_ban_alert::message() const
	{
		return peer_alert::message() + " banned peer";
	}

	peer_unsnubbed_alert::peer_unsnubbed_alert(aux::stack_allocator& alloc
		, torrent_handle h, tcp::endpoint const& ep
		, peer_id const& peer_id)
		: peer_alert(alloc, h, ep, peer_id)
	{}

	std::string peer_unsnubbed_alert::message() const
	{
		return peer_alert::message() + " peer unsnubbed";
	}

	peer_snubbed_alert::peer_snubbed_alert(aux::stack_allocator& alloc
		, torrent_handle h, tcp::endpoint const& ep
		, peer_id const& peer_id)
		: peer_alert(alloc, h, ep, peer_id)
	{}

	std::string peer_snubbed_alert::message() const
	{
		return peer_alert::message() + " peer snubbed";
	}

	invalid_request_alert::invalid_request_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, peer_id const& peer_id, peer_request const& r
		, bool _have, bool _peer_interested, bool _withheld)
		: peer_alert(alloc, h, ep, peer_id)
		, request(r)
		, we_have(_have)
		, peer_interested(_peer_interested)
		, withheld(_withheld)
	{}

	std::string invalid_request_alert::message() const
	{
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s peer sent an invalid piece request "
			"(piece: %u start: %u len: %u)%s"
			, peer_alert::message().c_str()
			, static_cast<int>(request.piece)
			, request.start
			, request.length
			, withheld ? ": super seeding withheld piece"
			: !we_have ? ": we don't have piece"
			: !peer_interested ? ": peer is not interested"
			: "");
		return ret;
	}

	torrent_finished_alert::torrent_finished_alert(aux::stack_allocator& alloc
		, torrent_handle h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_finished_alert::message() const
	{
		return torrent_alert::message() + " torrent finished downloading";
	}

	piece_finished_alert::piece_finished_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, piece_index_t piece_num)
		: torrent_alert(alloc, h)
		, piece_index(piece_num)
	{}

	std::string piece_finished_alert::message() const
	{
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s piece: %u finished downloading"
			, torrent_alert::message().c_str(), static_cast<int>(piece_index));
		return ret;
	}

	request_dropped_alert::request_dropped_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
		, piece_index_t piece_num)
		: peer_alert(alloc, h, ep, peer_id)
		, block_index(block_num)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= piece_index_t(0));
	}

	std::string request_dropped_alert::message() const
	{
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s peer dropped block ( piece: %u block: %u)"
			, torrent_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
	}

	block_timeout_alert::block_timeout_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
		, piece_index_t piece_num)
		: peer_alert(alloc, h, ep, peer_id)
		, block_index(block_num)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= piece_index_t(0));
	}

	std::string block_timeout_alert::message() const
	{
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s peer timed out request ( piece: %u block: %u)"
			, torrent_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
	}

	block_finished_alert::block_finished_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
		, piece_index_t piece_num)
		: peer_alert(alloc, h, ep, peer_id)
		, block_index(block_num)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= piece_index_t(0));
	}

	std::string block_finished_alert::message() const
	{
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s block finished downloading (piece: %u block: %u)"
			, torrent_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
	}

	block_downloading_alert::block_downloading_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep
		, peer_id const& peer_id, int block_num, piece_index_t piece_num)
		: peer_alert(alloc, h, ep, peer_id)
		, block_index(block_num)
		, piece_index(piece_num)
#ifndef TORRENT_NO_DEPRECATE
		, peer_speedmsg("")
#endif
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= piece_index_t(0));
	}

	std::string block_downloading_alert::message() const
	{
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s requested block (piece: %u block: %u)"
			, torrent_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
	}

	unwanted_block_alert::unwanted_block_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep
		, peer_id const& peer_id, int block_num, piece_index_t piece_num)
		: peer_alert(alloc, h, ep, peer_id)
		, block_index(block_num)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= piece_index_t(0));
	}

	std::string unwanted_block_alert::message() const
	{
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s received block not in download queue (piece: %u block: %u)"
			, torrent_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
	}

	storage_moved_alert::storage_moved_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, string_view p)
		: torrent_alert(alloc, h)
		, m_path_idx(alloc.copy_string(p))
#ifndef TORRENT_NO_DEPRECATE
		, path(p)
#endif
	{}

	std::string storage_moved_alert::message() const
	{
		return torrent_alert::message() + " moved storage to: "
			+ storage_path();
	}

	char const* storage_moved_alert::storage_path() const
	{
		return m_alloc.get().ptr(m_path_idx);
	}

	storage_moved_failed_alert::storage_moved_failed_alert(
		aux::stack_allocator& alloc, torrent_handle const& h, error_code const& e
		, string_view f, operation_t const op_)
		: torrent_alert(alloc, h)
		, error(e)
		, op(op_)
		, m_file_idx(alloc.copy_string(f))
#ifndef TORRENT_NO_DEPRECATE
		, operation(operation_name(op_))
		, file(f)
#endif
	{}

	char const* storage_moved_failed_alert::file_path() const
	{
		return m_alloc.get().ptr(m_file_idx);
	}

	std::string storage_moved_failed_alert::message() const
	{
		return torrent_alert::message() + " storage move failed. "
			+ operation_name(op) + " (" + file_path() + "): "
			+ convert_from_native(error.message());
	}

	torrent_deleted_alert::torrent_deleted_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, sha1_hash const& ih)
		: torrent_alert(alloc, h)
		, info_hash(ih)
	{}

	std::string torrent_deleted_alert::message() const
	{
		return torrent_alert::message() + " deleted";
	}

	torrent_delete_failed_alert::torrent_delete_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, error_code const& e, sha1_hash const& ih)
		: torrent_alert(alloc, h)
		, error(e)
		, info_hash(ih)
#ifndef TORRENT_NO_DEPRECATE
		, msg(convert_from_native(error.message()))
#endif
	{
	}

	std::string torrent_delete_failed_alert::message() const
	{
		return torrent_alert::message() + " torrent deletion failed: "
			+ convert_from_native(error.message());
	}

	save_resume_data_alert::save_resume_data_alert(aux::stack_allocator& alloc
		, add_torrent_params p
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
		, params(std::move(p))
#ifndef TORRENT_NO_DEPRECATE
		, resume_data(std::make_shared<entry>(write_resume_data(params)))
#endif
	{
	}

	std::string save_resume_data_alert::message() const
	{
		return torrent_alert::message() + " resume data generated";
	}

	save_resume_data_failed_alert::save_resume_data_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, error_code const& e)
		: torrent_alert(alloc, h)
		, error(e)
#ifndef TORRENT_NO_DEPRECATE
		, msg(convert_from_native(error.message()))
#endif
	{
	}

	std::string save_resume_data_failed_alert::message() const
	{
		return torrent_alert::message() + " resume data was not generated: "
			+ convert_from_native(error.message());
	}

	torrent_paused_alert::torrent_paused_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_paused_alert::message() const
	{
		return torrent_alert::message() + " paused";
	}

	torrent_resumed_alert::torrent_resumed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_resumed_alert::message() const
	{
		return torrent_alert::message() + " resumed";
	}

	torrent_checked_alert::torrent_checked_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_checked_alert::message() const
	{
		return torrent_alert::message() + " checked";
	}

namespace {

	int sock_type_idx(socket_type_t type)
	{
		int idx =
			static_cast<std::underlying_type<socket_type_t>::type>(type);
		TORRENT_ASSERT(0 <= idx && idx < 6);
		return idx;
	}

	char const* sock_type_str(socket_type_t type)
	{
		static char const* type_str[] =
			{ "TCP", "TCP/SSL", "UDP", "I2P", "Socks5", "uTP/SSL" };

		return type_str[sock_type_idx(type)];
	}

	char const* const nat_type_str[] = {"NAT-PMP", "UPnP"};

	char const* const protocol_str[] = {"none", "TCP", "UDP"};

	char const* const socket_type_str[] = {
		"null",
		"TCP",
		"Socks5/TCP",
		"HTTP",
		"uTP",
		"i2p",
		"SSL/TCP",
		"SSL/Socks5",
		"HTTPS",
		"SSL/uTP"
	};

#ifndef TORRENT_NO_DEPRECATE

	int to_op_t(operation_t op)
	{
		using o = operation_t;
		using lfo = listen_failed_alert::op_t;

		// we have to use deprecated enum values here. suppress the warnings
#ifdef _MSC_VER
#pragma warning(push, 1)
// warning C4996: X: was declared deprecated
#pragma warning( disable : 4996 )
#endif
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
		switch (op)
		{
			case o::bittorrent: return -1;
			case o::iocontrol: return -1;
			case o::getpeername: return -1;
			case o::getname: return lfo::get_socket_name;
			case o::alloc_recvbuf: return -1;
			case o::alloc_sndbuf: return -1;
			case o::file_write: return -1;
			case o::file_read: return -1;
			case o::file: return -1;
			case o::sock_write: return -1;
			case o::sock_read: return -1;
			case o::sock_open: return lfo::open;
			case o::sock_bind: return lfo::bind;
			case o::available: return -1;
			case o::encryption: return -1;
			case o::connect: return -1;
			case o::ssl_handshake: return -1;
			case o::get_interface: return -1;
			case o::unknown: return -1;
			case o::sock_listen: return lfo::listen;
			case o::sock_bind_to_device: return lfo::bind_to_device;
			case o::sock_accept: return lfo::accept;
			case o::parse_address: return lfo::parse_addr;
			case o::enum_if: return lfo::enum_if;
			case o::file_stat: return -1;
			case o::file_copy: return -1;
			case o::file_fallocate: return -1;
			case o::file_hard_link: return -1;
			case o::file_remove: return -1;
			case o::file_rename: return -1;
			case o::file_open: return -1;
			case o::mkdir: return -1;
			case o::check_resume: return -1;
			case o::exception: return -1;
			case o::alloc_cache_piece: return -1;
			case o::partfile_move: return -1;
			case o::partfile_read: return -1;
			case o::partfile_write: return -1;
			case o::hostname_lookup: return -1;
			case o::file_seek: return -1;
		};
		return -1;
	}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // TORRENT_NO_DEPRECATE
} // anonymous namespace

	listen_failed_alert::listen_failed_alert(
		aux::stack_allocator& alloc
		, string_view iface
		, libtorrent::address const& listen_addr
		, int listen_port
		, operation_t const op_
		, error_code const& ec
		, libtorrent::socket_type_t t)
		: error(ec)
		, op(op_)
		, socket_type(t)
		, address(listen_addr)
		, port(listen_port)
		, m_alloc(alloc)
		, m_interface_idx(alloc.copy_string(iface))
#ifndef TORRENT_NO_DEPRECATE
		, operation(to_op_t(op_))
		, endpoint(listen_addr, std::uint16_t(listen_port))
		, sock_type(static_cast<socket_type_t>(sock_type_idx(t)))
#endif
	{}

	listen_failed_alert::listen_failed_alert(
		aux::stack_allocator& alloc
		, string_view iface
		, tcp::endpoint const& ep
		, operation_t const op_
		, error_code const& ec
		, libtorrent::socket_type_t t)
		: listen_failed_alert(alloc
			, iface
			, ep.address()
			, ep.port()
			, op_
			, ec
			, t)
	{}

	listen_failed_alert::listen_failed_alert(
		aux::stack_allocator& alloc
		, string_view iface
		, udp::endpoint const& ep
		, operation_t const op_
		, error_code const& ec
		, libtorrent::socket_type_t t)
		: listen_failed_alert(alloc
			, iface
			, ep.address()
			, ep.port()
			, op_
			, ec
			, t)
	{}

	listen_failed_alert::listen_failed_alert(
		aux::stack_allocator& alloc
		, string_view iface
		, operation_t const op_
		, error_code const& ec
		, libtorrent::socket_type_t t)
		: listen_failed_alert(alloc
			, iface
			, libtorrent::address()
			, 0
			, op_
			, ec
			, t)
	{}

	char const* listen_failed_alert::listen_interface() const
	{
		return m_alloc.get().ptr(m_interface_idx);
	}

	std::string listen_failed_alert::message() const
	{
		char ret[300];
		std::snprintf(ret, sizeof(ret), "listening on %s (device: %s) failed: [%s] [%s] %s"
			, print_endpoint(address, port).c_str()
			, listen_interface()
			, operation_name(op)
			, sock_type_str(socket_type)
			, convert_from_native(error.message()).c_str());
		return ret;
	}

	metadata_failed_alert::metadata_failed_alert(aux::stack_allocator& alloc
		, const torrent_handle& h, error_code const& e)
		: torrent_alert(alloc, h)
		, error(e)
	{}

	std::string metadata_failed_alert::message() const
	{
		return torrent_alert::message() + " invalid metadata received";
	}

	metadata_received_alert::metadata_received_alert(aux::stack_allocator& alloc
		, const torrent_handle& h)
		: torrent_alert(alloc, h)
	{}

	std::string metadata_received_alert::message() const
	{
		return torrent_alert::message() + " metadata successfully received";
	}

	udp_error_alert::udp_error_alert(
		aux::stack_allocator&
		, udp::endpoint const& ep
		, error_code const& ec)
		: endpoint(ep)
		, error(ec)
	{}

	std::string udp_error_alert::message() const
	{
		error_code ec;
		return "UDP error: " + convert_from_native(error.message()) + " from: " + endpoint.address().to_string(ec);
	}

	external_ip_alert::external_ip_alert(aux::stack_allocator&
		, address const& ip)
		: external_address(ip)
	{}

	std::string external_ip_alert::message() const
	{
		error_code ec;
		return "external IP received: " + external_address.to_string(ec);
	}

	listen_succeeded_alert::listen_succeeded_alert(aux::stack_allocator&
		, libtorrent::address const& listen_addr
		, int listen_port
		, libtorrent::socket_type_t t)
		: address(listen_addr)
		, port(listen_port)
		, socket_type(t)
#ifndef TORRENT_NO_DEPRECATE
		, endpoint(listen_addr, std::uint16_t(listen_port))
		, sock_type(static_cast<socket_type_t>(sock_type_idx(t)))
#endif
	{}

	listen_succeeded_alert::listen_succeeded_alert(aux::stack_allocator& alloc
		, tcp::endpoint const& ep
		, libtorrent::socket_type_t t)
		: listen_succeeded_alert(alloc
			, ep.address()
			, ep.port()
			, t)
	{}

	listen_succeeded_alert::listen_succeeded_alert(aux::stack_allocator& alloc
		, udp::endpoint const& ep
		, libtorrent::socket_type_t t)
		: listen_succeeded_alert(alloc
			, ep.address()
			, ep.port()
			, t)
	{}

	std::string listen_succeeded_alert::message() const
	{
		char ret[200];
		std::snprintf(ret, sizeof(ret), "successfully listening on [%s] %s"
			, sock_type_str(socket_type), print_endpoint(address, port).c_str());
		return ret;
	}

	portmap_error_alert::portmap_error_alert(aux::stack_allocator&
		, port_mapping_t const i, portmap_transport const t, error_code const& e)
		: mapping(i)
		, map_transport(t)
		, error(e)
#ifndef TORRENT_NO_DEPRECATE
		, map_type(static_cast<int>(t))
		, msg(convert_from_native(error.message()))
#endif
	{}

	std::string portmap_error_alert::message() const
	{
		return std::string("could not map port using ")
			+ nat_type_str[static_cast<int>(map_transport)]
			+ ": " + convert_from_native(error.message());
	}

	portmap_alert::portmap_alert(aux::stack_allocator&, port_mapping_t const i
		, int port
		, portmap_transport const t
		, portmap_protocol const proto)
		: mapping(i)
		, external_port(port)
		, map_protocol(proto)
		, map_transport(t)
#ifndef TORRENT_NO_DEPRECATE
		, protocol(static_cast<int>(proto))
		, map_type(static_cast<int>(t))
#endif
	{}

	std::string portmap_alert::message() const
	{
		char ret[200];
		std::snprintf(ret, sizeof(ret), "successfully mapped port using %s. external port: %s/%u"
			, nat_type_str[static_cast<int>(map_transport)]
			, protocol_str[static_cast<int>(map_protocol)], external_port);
		return ret;
	}

	portmap_log_alert::portmap_log_alert(aux::stack_allocator& alloc
		, portmap_transport const t, const char* m)
		: map_transport(t)
		, m_alloc(alloc)
		, m_log_idx(alloc.copy_string(m))
#ifndef TORRENT_NO_DEPRECATE
		, map_type(static_cast<int>(t))
		, msg(m)
#endif
	{}

	char const* portmap_log_alert::log_message() const
	{
		return m_alloc.get().ptr(m_log_idx);
	}

	std::string portmap_log_alert::message() const
	{
		char ret[600];
		std::snprintf(ret, sizeof(ret), "%s: %s"
			, nat_type_str[static_cast<int>(map_transport)]
			, log_message());
		return ret;
	}

	fastresume_rejected_alert::fastresume_rejected_alert(
		aux::stack_allocator& alloc
		, torrent_handle const& h
		, error_code const& ec
		, string_view f
		, operation_t const op_)
		: torrent_alert(alloc, h)
		, error(ec)
		, op(op_)
		, m_path_idx(alloc.copy_string(f))
#ifndef TORRENT_NO_DEPRECATE
		, operation(operation_name(op_))
		, file(f)
		, msg(convert_from_native(error.message()))
#endif
	{
	}

	std::string fastresume_rejected_alert::message() const
	{
		return torrent_alert::message() + " fast resume rejected. "
			+ operation_name(op) + "(" + file_path() + "): "
			+ convert_from_native(error.message());
	}

	char const* fastresume_rejected_alert::file_path() const
	{
		return m_alloc.get().ptr(m_path_idx);
	}

	peer_blocked_alert::peer_blocked_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep, int r)
		: peer_alert(alloc, h, ep, peer_id(nullptr))
		, reason(r)
	{}

	std::string peer_blocked_alert::message() const
	{
		char ret[600];
		static char const* reason_str[] =
		{
			"ip_filter",
			"port_filter",
			"i2p_mixed",
			"privileged_ports",
			"utp_disabled",
			"tcp_disabled",
			"invalid_local_interface"
		};

		std::snprintf(ret, sizeof(ret), "%s: blocked peer [%s]"
			, peer_alert::message().c_str(), reason_str[reason]);
		return ret;
	}

	dht_announce_alert::dht_announce_alert(aux::stack_allocator&
		, address const& i, int p
		, sha1_hash const& ih)
		: ip(i)
		, port(p)
		, info_hash(ih)
	{}

	std::string dht_announce_alert::message() const
	{
		error_code ec;
		char msg[200];
		std::snprintf(msg, sizeof(msg), "incoming dht announce: %s:%u (%s)"
			, ip.to_string(ec).c_str(), port, aux::to_hex(info_hash).c_str());
		return msg;
	}

	dht_get_peers_alert::dht_get_peers_alert(aux::stack_allocator&
		, sha1_hash const& ih)
		: info_hash(ih)
	{}

	std::string dht_get_peers_alert::message() const
	{
		char msg[200];
		std::snprintf(msg, sizeof(msg), "incoming dht get_peers: %s", aux::to_hex(info_hash).c_str());
		return msg;
	}

namespace {

		std::array<int, stats_alert::num_channels> stat_to_array(stat const& s)
		{
			std::array<int, stats_alert::num_channels> arr;

			arr[stats_alert::upload_payload] = s[stat::upload_payload].counter();
			arr[stats_alert::upload_protocol] = s[stat::upload_protocol].counter();
			arr[stats_alert::download_payload] = s[stat::download_payload].counter();
			arr[stats_alert::download_protocol] = s[stat::download_protocol].counter();
			arr[stats_alert::upload_ip_protocol] = s[stat::upload_ip_protocol].counter();
			arr[stats_alert::download_ip_protocol] = s[stat::download_ip_protocol].counter();

#ifndef TORRENT_NO_DEPRECATE
			arr[stats_alert::upload_dht_protocol] = 0;
			arr[stats_alert::upload_tracker_protocol] = 0;
			arr[stats_alert::download_dht_protocol] = 0;
			arr[stats_alert::download_tracker_protocol] = 0;
#else
			arr[stats_alert::deprecated1] = 0;
			arr[stats_alert::deprecated2] = 0;
			arr[stats_alert::deprecated3] = 0;
			arr[stats_alert::deprecated4] = 0;
#endif
			return arr;
		}
	}

	stats_alert::stats_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, int in, stat const& s)
		: torrent_alert(alloc, h)
		, transferred(stat_to_array(s))
		, interval(in)
	{}

	std::string stats_alert::message() const
	{
		char msg[200];
		std::snprintf(msg, sizeof(msg), "%s: [%d] %d %d %d %d %d %d"
#ifndef TORRENT_NO_DEPRECATE
			" %d %d %d %d"
#endif
			, torrent_alert::message().c_str()
			, interval
			, transferred[0]
			, transferred[1]
			, transferred[2]
			, transferred[3]
			, transferred[4]
			, transferred[5]
#ifndef TORRENT_NO_DEPRECATE
			, transferred[6]
			, transferred[7]
			, transferred[8]
			, transferred[9]
#endif
			);
		return msg;
	}

	cache_flushed_alert::cache_flushed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h) {}

	anonymous_mode_alert::anonymous_mode_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, int k, string_view s)
		: torrent_alert(alloc, h)
		, kind(k)
		, str(s)
	{}

	std::string anonymous_mode_alert::message() const
	{
		char msg[200];
		static char const* msgs[] = {
			"tracker is not anonymous, set a proxy"
		};
		std::snprintf(msg, sizeof(msg), "%s: %s: %s"
			, torrent_alert::message().c_str()
			, msgs[kind], str.c_str());
		return msg;
	}

	lsd_peer_alert::lsd_peer_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, tcp::endpoint const& i)
		: peer_alert(alloc, h, i, peer_id(nullptr))
	{}

	std::string lsd_peer_alert::message() const
	{
		char msg[200];
		std::snprintf(msg, sizeof(msg), "%s: received peer from local service discovery"
			, peer_alert::message().c_str());
		return msg;
	}

	trackerid_alert::trackerid_alert(
		aux::stack_allocator& alloc
		, torrent_handle const& h
		, tcp::endpoint const& ep
		, string_view u
		, const std::string& id)
		: tracker_alert(alloc, h,  ep, u)
		, m_tracker_idx(alloc.copy_string(id))
#ifndef TORRENT_NO_DEPRECATE
		, trackerid(id)
#endif
	{}

	char const* trackerid_alert::tracker_id() const
	{
		return m_alloc.get().ptr(m_tracker_idx);
	}

	std::string trackerid_alert::message() const
	{
		return std::string("trackerid received: ") + tracker_id();
	}

	dht_bootstrap_alert::dht_bootstrap_alert(aux::stack_allocator&)
	{}

	std::string dht_bootstrap_alert::message() const
	{
		return "DHT bootstrap complete";
	}

	torrent_error_alert::torrent_error_alert(
		aux::stack_allocator& alloc
		, torrent_handle const& h
		, error_code const& e, string_view f)
		: torrent_alert(alloc, h)
		, error(e)
		, m_file_idx(alloc.copy_string(f))
#ifndef TORRENT_NO_DEPRECATE
		, error_file(f)
#endif
	{}

	std::string torrent_error_alert::message() const
	{
		char msg[400];
		if (error)
		{
			std::snprintf(msg, sizeof(msg), " ERROR: (%d %s) %s"
				, error.value(), convert_from_native(error.message()).c_str()
				, filename());
		}
		else
		{
			std::snprintf(msg, sizeof(msg), " ERROR: %s", filename());
		}
		return torrent_alert::message() + msg;
	}

	char const* torrent_error_alert::filename() const
	{
		return m_alloc.get().ptr(m_file_idx);
	}

#ifndef TORRENT_NO_DEPRECATE
	torrent_added_alert::torrent_added_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_added_alert::message() const
	{
		return torrent_alert::message() + " added";
	}
#endif

	torrent_removed_alert::torrent_removed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, sha1_hash const& ih)
		: torrent_alert(alloc, h)
		, info_hash(ih)
	{}

	std::string torrent_removed_alert::message() const
	{
		return torrent_alert::message() + " removed";
	}

	torrent_need_cert_alert::torrent_need_cert_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_need_cert_alert::message() const
	{
		return torrent_alert::message() + " needs SSL certificate";
	}

	incoming_connection_alert::incoming_connection_alert(aux::stack_allocator&, int t
		, tcp::endpoint const& i)
		: socket_type(t)
		, endpoint(i)
#ifndef TORRENT_NO_DEPRECATE
		, ip(i)
#endif
	{}

	std::string incoming_connection_alert::message() const
	{
		char msg[600];
		std::snprintf(msg, sizeof(msg), "incoming connection from %s (%s)"
			, print_endpoint(endpoint).c_str(), socket_type_str[socket_type]);
		return msg;
	}

	peer_connect_alert::peer_connect_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep, peer_id const& peer_id, int type)
		: peer_alert(alloc, h, ep, peer_id)
		, socket_type(type)
	{}

	std::string peer_connect_alert::message() const
	{
		char msg[600];
		std::snprintf(msg, sizeof(msg), "%s connecting to peer (%s)"
			, peer_alert::message().c_str(), socket_type_str[socket_type]);
		return msg;
	}

	add_torrent_alert::add_torrent_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, add_torrent_params const& p, error_code const& ec)
		: torrent_alert(alloc, h)
		, params(p)
		, error(ec)
	{}

	std::string add_torrent_alert::message() const
	{
		char msg[600];
		char info_hash[41];
		char const* torrent_name = info_hash;
		if (params.ti) torrent_name = params.ti->name().c_str();
		else if (!params.name.empty()) torrent_name = params.name.c_str();
#ifndef TORRENT_NO_DEPRECATE
		else if (!params.url.empty()) torrent_name = params.url.c_str();
#endif
		else aux::to_hex(params.info_hash, info_hash);

		if (error)
		{
			std::snprintf(msg, sizeof(msg), "failed to add torrent \"%s\": [%s] %s"
				, torrent_name, error.category().name()
				, convert_from_native(error.message()).c_str());
		}
		else
		{
			std::snprintf(msg, sizeof(msg), "added torrent: %s", torrent_name);
		}
		return msg;
	}

	state_update_alert::state_update_alert(aux::stack_allocator&
		, std::vector<torrent_status> st)
		: status(std::move(st))
	{}

	std::string state_update_alert::message() const
	{
		char msg[600];
		std::snprintf(msg, sizeof(msg), "state updates for %d torrents", int(status.size()));
		return msg;
	}

#ifndef TORRENT_NO_DEPRECATE
	mmap_cache_alert::mmap_cache_alert(aux::stack_allocator&
		, error_code const& ec): error(ec)
	{}

	std::string mmap_cache_alert::message() const
	{
		char msg[600];
		std::snprintf(msg, sizeof(msg), "mmap cache failed: (%d) %s", error.value()
			, convert_from_native(error.message()).c_str());
		return msg;
	}
#endif

	char const* operation_name(operation_t const op)
	{
		static char const* names[] = {
			"unknown",
			"bittorrent",
			"iocontrol",
			"getpeername",
			"getname",
			"alloc_recvbuf",
			"alloc_sndbuf",
			"file_write",
			"file_read",
			"file",
			"sock_write",
			"sock_read",
			"sock_open",
			"sock_bind",
			"available",
			"encryption",
			"connect",
			"ssl_handshake",
			"get_interface",
			"sock_listen",
			"sock_bind_to_device",
			"sock_accept",
			"parse_address",
			"enum_if",
			"file_stat",
			"file_copy",
			"file_fallocate",
			"file_hard_link",
			"file_remove",
			"file_rename",
			"file_open",
			"mkdir",
			"check_resume",
			"exception",
			"alloc_cache_piece",
			"partfile_move",
			"partfile_read",
			"partfile_write",
			"hostname_lookup",
			"file_seek"
		};

		int const idx = static_cast<int>(op);
		if (idx < 0 || idx >= int(sizeof(names)/sizeof(names[0])))
			return "unknown operation";

		return names[idx];
	}

#ifndef TORRENT_NO_DEPRECATE
	char const* operation_name(int const op)
	{
		return operation_name(static_cast<operation_t>(op));
	}
#endif

	peer_error_alert::peer_error_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, tcp::endpoint const& ep, peer_id const& peer_id, operation_t const op_
		, error_code const& e)
		: peer_alert(alloc, h, ep, peer_id)
		, op(op_)
		, error(e)
#ifndef TORRENT_NO_DEPRECATE
		, operation(static_cast<int>(op_))
		, msg(convert_from_native(error.message()))
#endif
	{}

	std::string peer_error_alert::message() const
	{
		char buf[200];
		std::snprintf(buf, sizeof(buf), "%s peer error [%s] [%s]: %s"
			, peer_alert::message().c_str()
			, operation_name(op), error.category().name()
			, convert_from_native(error.message()).c_str());
		return buf;
	}

#ifndef TORRENT_NO_DEPRECATE
	torrent_update_alert::torrent_update_alert(aux::stack_allocator& alloc, torrent_handle h
		, sha1_hash const& old_hash, sha1_hash const& new_hash)
		: torrent_alert(alloc, h)
		, old_ih(old_hash)
		, new_ih(new_hash)
	{}

	std::string torrent_update_alert::message() const
	{
		char msg[200];
		std::snprintf(msg, sizeof(msg), " torrent changed info-hash from: %s to %s"
			, aux::to_hex(old_ih).c_str()
			, aux::to_hex(new_ih).c_str());
		return torrent_alert::message() + msg;
	}
#endif

	peer_disconnected_alert::peer_disconnected_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, peer_id const& peer_id, operation_t op_, int type, error_code const& e
		, close_reason_t r)
		: peer_alert(alloc, h, ep, peer_id)
		, socket_type(type)
		, op(op_)
		, error(e)
		, reason(r)
#ifndef TORRENT_NO_DEPRECATE
		, operation(static_cast<int>(op))
		, msg(convert_from_native(error.message()))
#endif
	{}

	std::string peer_disconnected_alert::message() const
	{
		char buf[600];
		std::snprintf(buf, sizeof(buf), "%s disconnecting (%s) [%s] [%s]: %s (reason: %d)"
			, peer_alert::message().c_str()
			, socket_type_str[socket_type]
			, operation_name(op), error.category().name()
			, convert_from_native(error.message()).c_str()
			, int(reason));
		return buf;
	}

	dht_error_alert::dht_error_alert(aux::stack_allocator&
		, operation_t const op_
		, error_code const& ec)
		: error(ec)
		, op(op_)
#ifndef TORRENT_NO_DEPRECATE
		, operation(op_ == operation_t::hostname_lookup
			? op_t::hostname_lookup : op_t::unknown)
#endif
	{}

	std::string dht_error_alert::message() const
	{
		char msg[600];
		std::snprintf(msg, sizeof(msg), "DHT error [%s] (%d) %s"
			, operation_name(op)
			, error.value()
			, convert_from_native(error.message()).c_str());
		return msg;
	}

	dht_immutable_item_alert::dht_immutable_item_alert(aux::stack_allocator&
		, sha1_hash const& t, entry const& i)
		: target(t), item(i)
	{}

	std::string dht_immutable_item_alert::message() const
	{
		char msg[1050];
		std::snprintf(msg, sizeof(msg), "DHT immutable item %s [ %s ]"
			, aux::to_hex(target).c_str()
			, item.to_string().c_str());
		return msg;
	}

	// TODO: 2 the salt here is allocated on the heap. It would be nice to
	// allocate in in the stack_allocator
	dht_mutable_item_alert::dht_mutable_item_alert(aux::stack_allocator&
		, std::array<char, 32> k
		, std::array<char, 64> sig
		, std::int64_t sequence
		, string_view s
		, entry const& i
		, bool a)
		: key(k), signature(sig), seq(sequence), salt(s), item(i), authoritative(a)
	{}

	std::string dht_mutable_item_alert::message() const
	{
		char msg[1050];
		std::snprintf(msg, sizeof(msg), "DHT mutable item (key=%s salt=%s seq=%" PRId64 " %s) [ %s ]"
			, aux::to_hex(key).c_str()
			, salt.c_str()
			, seq
			, authoritative ? "auth" : "non-auth"
			, item.to_string().c_str());
		return msg;
	}

	dht_put_alert::dht_put_alert(aux::stack_allocator&, sha1_hash const& t, int n)
		: target(t)
		, public_key()
		, signature()
		, salt()
		, seq(0)
		, num_success(n)
	{}

	dht_put_alert::dht_put_alert(aux::stack_allocator&
		, std::array<char, 32> key
		, std::array<char, 64> sig
		, std::string s
		, std::int64_t sequence_number
		, int n)
		: target(nullptr)
		, public_key(key)
		, signature(sig)
		, salt(std::move(s))
		, seq(sequence_number)
		, num_success(n)
	{}

	std::string dht_put_alert::message() const
	{
		char msg[1050];
		if (target.is_all_zeros())
		{
			std::snprintf(msg, sizeof(msg), "DHT put complete (success=%d key=%s sig=%s salt=%s seq=%" PRId64 ")"
				, num_success
				, aux::to_hex(public_key).c_str()
				, aux::to_hex(signature).c_str()
				, salt.c_str()
				, seq);
			return msg;
		}

		std::snprintf(msg, sizeof(msg), "DHT put commplete (success=%d hash=%s)"
			, num_success
			, aux::to_hex(target).c_str());
		return msg;
	}

	i2p_alert::i2p_alert(aux::stack_allocator&, error_code const& ec)
		: error(ec)
	{}

	std::string i2p_alert::message() const
	{
		char msg[600];
		std::snprintf(msg, sizeof(msg), "i2p_error: [%s] %s"
			, error.category().name(), convert_from_native(error.message()).c_str());
		return msg;
	}

	dht_outgoing_get_peers_alert::dht_outgoing_get_peers_alert(aux::stack_allocator&
		, sha1_hash const& ih, sha1_hash const& obfih
		, udp::endpoint ep)
		: info_hash(ih)
		, obfuscated_info_hash(obfih)
		, endpoint(std::move(ep))
#ifndef TORRENT_NO_DEPRECATE
		, ip(endpoint)
#endif
	{}

	std::string dht_outgoing_get_peers_alert::message() const
	{
		char msg[600];
		char obf[70];
		obf[0] = '\0';
		if (obfuscated_info_hash != info_hash)
		{
			std::snprintf(obf, sizeof(obf), " [obfuscated: %s]"
			, aux::to_hex(obfuscated_info_hash).c_str());
		}
		std::snprintf(msg, sizeof(msg), "outgoing dht get_peers : %s%s -> %s"
			, aux::to_hex(info_hash).c_str()
			, obf
			, print_endpoint(endpoint).c_str());
		return msg;
	}

	log_alert::log_alert(aux::stack_allocator& alloc, char const* log)
		: m_alloc(alloc)
		, m_str_idx(alloc.copy_string(log))
	{}
	log_alert::log_alert(aux::stack_allocator& alloc, char const* fmt, va_list v)
		: m_alloc(alloc)
		, m_str_idx(alloc.format_string(fmt, v))
	{}

	char const* log_alert::log_message() const
	{
		return m_alloc.get().ptr(m_str_idx);
	}

#ifndef TORRENT_NO_DEPRECATE
	char const* log_alert::msg() const
	{
		return log_message();
	}
#endif

	std::string log_alert::message() const
	{
		return log_message();
	}

	torrent_log_alert::torrent_log_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, char const* fmt, va_list v)
		: torrent_alert(alloc, h)
		, m_str_idx(alloc.format_string(fmt, v))
	{}

	char const* torrent_log_alert::log_message() const
	{
		return m_alloc.get().ptr(m_str_idx);
	}

#ifndef TORRENT_NO_DEPRECATE
	char const* torrent_log_alert::msg() const
	{
		return log_message();
	}
#endif

	std::string torrent_log_alert::message() const
	{
		return torrent_alert::message() + ": " + log_message();
	}

	peer_log_alert::peer_log_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, tcp::endpoint const& i, peer_id const& pi
		, peer_log_alert::direction_t dir
		, char const* event, char const* fmt, va_list v)
		: peer_alert(alloc, h, i, pi)
		, event_type(event)
		, direction(dir)
		, m_str_idx(alloc.format_string(fmt, v))
	{}

	char const* peer_log_alert::log_message() const
	{
		return m_alloc.get().ptr(m_str_idx);
	}

#ifndef TORRENT_NO_DEPRECATE
	char const* peer_log_alert::msg() const
	{
		return log_message();
	}
#endif

	std::string peer_log_alert::message() const
	{
		static char const* mode[] =
		{ "<==", "==>", "<<<", ">>>", "***" };
		return torrent_alert::message() + " [" + print_endpoint(endpoint) + "] "
			+ mode[direction] + " " + event_type + " [ " + log_message() + " ]";
	}

	lsd_error_alert::lsd_error_alert(aux::stack_allocator&, error_code const& ec)
		: alert()
		, error(ec)
	{}

	std::string lsd_error_alert::message() const
	{
		return "Local Service Discovery error: " + convert_from_native(error.message());
	}

namespace {

		aux::array<std::int64_t, counters::num_counters> counters_to_array(counters const& cnt)
		{
			aux::array<std::int64_t, counters::num_counters> arr;

			for (int i = 0; i < counters::num_counters; ++i)
				arr[i] = cnt[i];

			return arr;
		}
	}

	session_stats_alert::session_stats_alert(aux::stack_allocator&, struct counters const& cnt)
		: values(counters_to_array(cnt))
	{}

	std::string session_stats_alert::message() const
	{
		char msg[50];
		std::snprintf(msg, sizeof(msg), "session stats (%d values): " , int(values.size()));
		std::string ret = msg;
		bool first = true;
		for (auto v : values)
		{
			std::snprintf(msg, sizeof(msg), first ? "%" PRId64 : ", %" PRId64, v);
			first = false;
			ret += msg;
		}
		return ret;
	}

	span<std::int64_t const> session_stats_alert::counters() const
	{
		return values;
	}

	dht_stats_alert::dht_stats_alert(aux::stack_allocator&
		, std::vector<dht_routing_bucket> table
		, std::vector<dht_lookup> requests)
		: alert()
		, active_requests(std::move(requests))
		, routing_table(std::move(table))
	{}

	std::string dht_stats_alert::message() const
	{
		char buf[2048];
		std::snprintf(buf, sizeof(buf), "DHT stats: reqs: %d buckets: %d"
			, int(active_requests.size())
			, int(routing_table.size()));
		return buf;
	}

	url_seed_alert::url_seed_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, string_view u, error_code const& e)
		: torrent_alert(alloc, h)
		, error(e)
		, m_url_idx(alloc.copy_string(u))
		, m_msg_idx()
#ifndef TORRENT_NO_DEPRECATE
		, url(u)
		, msg(convert_from_native(e.message()))
#endif
	{}

	url_seed_alert::url_seed_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, string_view u, string_view m)
		: torrent_alert(alloc, h)
		, m_url_idx(alloc.copy_string(u))
		, m_msg_idx(alloc.copy_string(m))
#ifndef TORRENT_NO_DEPRECATE
		, url(u)
		, msg(m)
#endif
	{}

	std::string url_seed_alert::message() const
	{
		return torrent_alert::message() + " url seed ("
			+ server_url() + ") failed: " + convert_from_native(error.message());
	}

	char const* url_seed_alert::server_url() const
	{
		return m_alloc.get().ptr(m_url_idx);
	}

	char const* url_seed_alert::error_message() const
	{
		if (m_msg_idx == aux::allocation_slot()) return "";
		return m_alloc.get().ptr(m_msg_idx);
	}

	file_error_alert::file_error_alert(aux::stack_allocator& alloc
		, error_code const& ec, string_view f, operation_t const op_
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
		, error(ec)
		, op(op_)
		, m_file_idx(alloc.copy_string(f))
#ifndef TORRENT_NO_DEPRECATE
		, operation(operation_name(op_))
		, file(f)
		, msg(convert_from_native(error.message()))
#endif
	{}

	char const* file_error_alert::filename() const
	{
		return m_alloc.get().ptr(m_file_idx);
	}

	std::string file_error_alert::message() const
	{
		return torrent_alert::message() + " "
			+ operation_name(op) + " (" + filename()
			+ ") error: " + convert_from_native(error.message());
	}

	incoming_request_alert::incoming_request_alert(aux::stack_allocator& alloc
		, peer_request r, torrent_handle h
		, tcp::endpoint const& ep, peer_id const& peer_id)
		: peer_alert(alloc, h, ep, peer_id)
		, req(r)
	{}

	std::string incoming_request_alert::message() const
	{
		char msg[1024];
		std::snprintf(msg, sizeof(msg), "%s: incoming request [ piece: %d start: %d length: %d ]"
			, peer_alert::message().c_str(), static_cast<int>(req.piece)
			, req.start, req.length);
		return msg;
	}

	dht_log_alert::dht_log_alert(aux::stack_allocator& alloc
		, dht_log_alert::dht_module_t m, const char* fmt, va_list v)
		: module(m)
		, m_alloc(alloc)
		, m_msg_idx(alloc.format_string(fmt, v))
	{}

	char const* dht_log_alert::log_message() const
	{
		return m_alloc.get().ptr(m_msg_idx);
	}

	std::string dht_log_alert::message() const
	{
		static char const* const dht_modules[] =
		{
			"tracker",
			"node",
			"routing_table",
			"rpc_manager",
			"traversal"
		};

		char ret[900];
		std::snprintf(ret, sizeof(ret), "DHT %s: %s", dht_modules[module]
			, log_message());
		return ret;
	}

	dht_pkt_alert::dht_pkt_alert(aux::stack_allocator& alloc
		, span<char const> buf, dht_pkt_alert::direction_t d
		, udp::endpoint const& ep)
		: direction(d)
		, node(std::move(ep))
		, m_alloc(alloc)
		, m_msg_idx(alloc.copy_buffer(buf))
		, m_size(int(buf.size()))
#ifndef TORRENT_NO_DEPRECATE
		, dir(d)
#endif
	{}

	span<char const> dht_pkt_alert::pkt_buf() const
	{
		return {m_alloc.get().ptr(m_msg_idx), std::size_t(m_size)};
	}

	std::string dht_pkt_alert::message() const
	{
		bdecode_node print;
		error_code ec;

		// ignore errors here. This is best-effort. It may be a broken encoding
		// but at least we'll print the valid parts
		span<char const> pkt = pkt_buf();
		bdecode(pkt.data(), pkt.data() + int(pkt.size()), print, ec, nullptr, 100, 100);

		std::string msg = print_entry(print, true);

		char const* prefix[2] = { "<==", "==>"};
		char buf[1024];
		std::snprintf(buf, sizeof(buf), "%s [%s] %s", prefix[direction]
			, print_endpoint(node).c_str(), msg.c_str());

		return buf;
	}

	dht_get_peers_reply_alert::dht_get_peers_reply_alert(aux::stack_allocator& alloc
		, sha1_hash const& ih
		, std::vector<tcp::endpoint> const& peers)
		: info_hash(ih)
		, m_alloc(alloc)
	{
		for (auto const& endp : peers)
		{
			if (endp.protocol() == tcp::v4())
				m_v4_num_peers++;
#if TORRENT_USE_IPV6
			else
				m_v6_num_peers++;
#endif
		}

		m_v4_peers_idx = alloc.allocate(m_v4_num_peers * 6);
		m_v6_peers_idx = alloc.allocate(m_v6_num_peers * 18);

		char* v4_ptr = alloc.ptr(m_v4_peers_idx);
#if TORRENT_USE_IPV6
		char* v6_ptr = alloc.ptr(m_v6_peers_idx);
#endif
		for (auto const& endp : peers)
		{
			if (endp.protocol() == tcp::v4())
				detail::write_endpoint(endp, v4_ptr);
#if TORRENT_USE_IPV6
			else
				detail::write_endpoint(endp, v6_ptr);
#endif
		}
	}

	std::string dht_get_peers_reply_alert::message() const
	{
		char msg[200];
		std::snprintf(msg, sizeof(msg), "incoming dht get_peers reply: %s, peers %d"
			, aux::to_hex(info_hash).c_str(), num_peers());
		return msg;
	}

	int dht_get_peers_reply_alert::num_peers() const
	{
		return m_v4_num_peers + m_v6_num_peers;
	}

#ifndef TORRENT_NO_DEPRECATE
	void dht_get_peers_reply_alert::peers(std::vector<tcp::endpoint> &v) const
	{
		std::vector<tcp::endpoint> p(peers());
		v.reserve(p.size());
		std::copy(p.begin(), p.end(), std::back_inserter(v));
	}
#endif
	std::vector<tcp::endpoint> dht_get_peers_reply_alert::peers() const
	{
		aux::vector<tcp::endpoint> peers;
		peers.reserve(num_peers());

		char const* v4_ptr = m_alloc.get().ptr(m_v4_peers_idx);
		for (int i = 0; i < m_v4_num_peers; i++)
			peers.push_back(detail::read_v4_endpoint<tcp::endpoint>(v4_ptr));
#if TORRENT_USE_IPV6
		char const* v6_ptr = m_alloc.get().ptr(m_v6_peers_idx);
		for (int i = 0; i < m_v6_num_peers; i++)
			peers.push_back(detail::read_v6_endpoint<tcp::endpoint>(v6_ptr));
#endif

		return peers;
	}

	dht_direct_response_alert::dht_direct_response_alert(
		aux::stack_allocator& alloc, void* userdata_
		, udp::endpoint const& addr_, bdecode_node const& response)
		: userdata(userdata_), endpoint(addr_)
		, m_alloc(alloc)
		, m_response_idx(alloc.copy_buffer(response.data_section()))
		, m_response_size(int(response.data_section().size()))
#ifndef TORRENT_NO_DEPRECATE
		, addr(addr_)
#endif
	{}

	dht_direct_response_alert::dht_direct_response_alert(
		aux::stack_allocator& alloc
		, void* userdata_
		, udp::endpoint const& addr_)
		: userdata(userdata_), endpoint(addr_)
		, m_alloc(alloc)
		, m_response_idx()
		, m_response_size(0)
#ifndef TORRENT_NO_DEPRECATE
		, addr(addr_)
#endif
	{}

	std::string dht_direct_response_alert::message() const
	{
		char msg[1050];
		std::snprintf(msg, sizeof(msg), "DHT direct response (address=%s) [ %s ]"
			, endpoint.address().to_string().c_str()
			, m_response_size ? std::string(m_alloc.get().ptr(m_response_idx)
				, aux::numeric_cast<std::size_t>(m_response_size)).c_str() : "");
		return msg;
	}

	bdecode_node dht_direct_response_alert::response() const
	{
		if (m_response_size == 0) return bdecode_node();
		char const* start = m_alloc.get().ptr(m_response_idx);
		char const* end = start + m_response_size;
		error_code ec;
		bdecode_node ret;
		bdecode(start, end, ret, ec);
		TORRENT_ASSERT(!ec);
		return ret;
	}

	picker_log_alert::picker_log_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, tcp::endpoint const& ep, peer_id const& peer_id, picker_flags_t const flags
		, piece_block const* blocks, int num_blocks)
		: peer_alert(alloc, h, ep, peer_id)
		, picker_flags(flags)
		, m_array_idx(alloc.copy_buffer({reinterpret_cast<char const*>(blocks)
			, aux::numeric_cast<std::size_t>(num_blocks) * sizeof(piece_block)}))
		, m_num_blocks(num_blocks)
	{}

	std::vector<piece_block> picker_log_alert::blocks() const
	{
		// we need to copy this array to make sure the structures are properly
		// aligned, not just to have a nice API
		std::size_t const num_blocks = aux::numeric_cast<std::size_t>(m_num_blocks);
		std::vector<piece_block> ret(num_blocks);

		char const* start = m_alloc.get().ptr(m_array_idx);
		std::memcpy(ret.data(), start, num_blocks * sizeof(piece_block));

		return ret;
	}

	constexpr picker_flags_t picker_log_alert::partial_ratio;
	constexpr picker_flags_t picker_log_alert::prioritize_partials;
	constexpr picker_flags_t picker_log_alert::rarest_first_partials;
	constexpr picker_flags_t picker_log_alert::rarest_first;
	constexpr picker_flags_t picker_log_alert::reverse_rarest_first;
	constexpr picker_flags_t picker_log_alert::suggested_pieces;
	constexpr picker_flags_t picker_log_alert::prio_sequential_pieces;
	constexpr picker_flags_t picker_log_alert::sequential_pieces;
	constexpr picker_flags_t picker_log_alert::reverse_pieces;
	constexpr picker_flags_t picker_log_alert::time_critical;
	constexpr picker_flags_t picker_log_alert::random_pieces;
	constexpr picker_flags_t picker_log_alert::prefer_contiguous;
	constexpr picker_flags_t picker_log_alert::reverse_sequential;
	constexpr picker_flags_t picker_log_alert::backup1;
	constexpr picker_flags_t picker_log_alert::backup2;
	constexpr picker_flags_t picker_log_alert::end_game;

	std::string picker_log_alert::message() const
	{
		static char const* const flag_names[] =
		{
			"partial_ratio ",
			"prioritize_partials ",
			"rarest_first_partials ",
			"rarest_first ",
			"reverse_rarest_first ",
			"suggested_pieces ",
			"prio_sequential_pieces ",
			"sequential_pieces ",
			"reverse_pieces ",
			"time_critical ",
			"random_pieces ",
			"prefer_contiguous ",
			"reverse_sequential ",
			"backup1 ",
			"backup2 ",
			"end_game "
		};

		std::string ret = peer_alert::message();

		auto flags = static_cast<std::uint32_t>(picker_flags);
		int idx = 0;
		ret += " picker_log [ ";
		for (; flags != 0; flags >>= 1, ++idx)
		{
			if ((flags & 1) == 0) continue;
			ret += flag_names[idx];
		}
		ret += "] ";

		std::vector<piece_block> b = blocks();

		for (auto const& p : b)
		{
			char buf[50];
			std::snprintf(buf, sizeof(buf), "(%d,%d) "
				, static_cast<int>(p.piece_index), p.block_index);
			ret += buf;
		}
		return ret;
	}

	session_error_alert::session_error_alert(aux::stack_allocator& alloc
		, error_code e, string_view error_str)
		: error(e)
		, m_alloc(alloc)
		, m_msg_idx(alloc.copy_buffer(error_str))
	{}

	std::string session_error_alert::message() const
	{
		char buf[400];
		if (error)
		{
			std::snprintf(buf, sizeof(buf), "session error: (%d %s) %s"
				, error.value(), convert_from_native(error.message()).c_str()
				, m_alloc.get().ptr(m_msg_idx));
		}
		else
		{
			std::snprintf(buf, sizeof(buf), "session error: %s"
				, m_alloc.get().ptr(m_msg_idx));
		}
		return buf;
	}

namespace {

	using nodes_slot = std::tuple<int, aux::allocation_slot, int, aux::allocation_slot>;

	nodes_slot write_nodes(aux::stack_allocator& alloc
		, std::vector<std::pair<sha1_hash, udp::endpoint>> const& nodes)
	{
		int v4_num_nodes = 0;
		int v6_num_nodes = 0;

		for (auto const& n : nodes)
		{
			if (n.second.protocol() == udp::v4())
				v4_num_nodes++;
#if TORRENT_USE_IPV6
			else
				v6_num_nodes++;
#endif
		}

		aux::allocation_slot const v4_nodes_idx = alloc.allocate(v4_num_nodes * (20 + 6));
		aux::allocation_slot const v6_nodes_idx = alloc.allocate(v6_num_nodes * (20 + 18));

		char* v4_ptr = alloc.ptr(v4_nodes_idx);
#if TORRENT_USE_IPV6
		char* v6_ptr = alloc.ptr(v6_nodes_idx);
#endif
		for (auto const& n : nodes)
		{
			udp::endpoint const& endp = n.second;
			if (endp.protocol() == udp::v4())
			{
				detail::write_string(n.first.to_string(), v4_ptr);
				detail::write_endpoint(endp, v4_ptr);
			}
#if TORRENT_USE_IPV6
			else
			{
				detail::write_string(n.first.to_string(), v6_ptr);
				detail::write_endpoint(endp, v6_ptr);
			}
#endif
		}

		return nodes_slot(v4_num_nodes, v4_nodes_idx, v6_num_nodes, v6_nodes_idx);
	}

	std::vector<std::pair<sha1_hash, udp::endpoint>> read_nodes(
		aux::stack_allocator const& alloc
		, int const v4_num_nodes, aux::allocation_slot const v4_nodes_idx
		, int const v6_num_nodes, aux::allocation_slot const v6_nodes_idx)
	{
		aux::vector<std::pair<sha1_hash, udp::endpoint>> nodes;
		nodes.reserve(v4_num_nodes + v6_num_nodes);

		char const* v4_ptr = alloc.ptr(v4_nodes_idx);
		for (int i = 0; i < v4_num_nodes; i++)
		{
			sha1_hash ih;
			std::memcpy(ih.data(), v4_ptr, 20);
			v4_ptr += 20;
			nodes.emplace_back(ih, detail::read_v4_endpoint<udp::endpoint>(v4_ptr));
		}
#if TORRENT_USE_IPV6
		char const* v6_ptr = alloc.ptr(v6_nodes_idx);
		for (int i = 0; i < v6_num_nodes; i++)
		{
			sha1_hash ih;
			std::memcpy(ih.data(), v6_ptr, 20);
			v6_ptr += 20;
			nodes.emplace_back(ih, detail::read_v6_endpoint<udp::endpoint>(v6_ptr));
		}
#else
		TORRENT_UNUSED(v6_nodes_idx);
#endif

		return nodes;
	}
	}

	dht_live_nodes_alert::dht_live_nodes_alert(aux::stack_allocator& alloc
		, sha1_hash const& nid
		, std::vector<std::pair<sha1_hash, udp::endpoint>> const& nodes)
		: node_id(nid)
		, m_alloc(alloc)
	{
		std::tie(m_v4_num_nodes, m_v4_nodes_idx, m_v6_num_nodes, m_v6_nodes_idx)
			= write_nodes(alloc, nodes);
	}

	std::string dht_live_nodes_alert::message() const
	{
		char msg[200];
		std::snprintf(msg, sizeof(msg), "dht live nodes for id: %s, nodes %d"
			, aux::to_hex(node_id).c_str(), num_nodes());
		return msg;
	}

	int dht_live_nodes_alert::num_nodes() const
	{
		return m_v4_num_nodes + m_v6_num_nodes;
	}

	std::vector<std::pair<sha1_hash, udp::endpoint>> dht_live_nodes_alert::nodes() const
	{
		return read_nodes(m_alloc.get()
			, m_v4_num_nodes, m_v4_nodes_idx
			, m_v6_num_nodes, m_v6_nodes_idx);
	}

	session_stats_header_alert::session_stats_header_alert(aux::stack_allocator&)
	{}

	std::string session_stats_header_alert::message() const
	{
		std::string stats_header = "session stats header: ";
		std::vector<stats_metric> stats = session_stats_metrics();
		std::sort(stats.begin(), stats.end()
			, [] (stats_metric const& lhs, stats_metric const& rhs)
			{ return lhs.value_index < rhs.value_index; });
		bool first = true;
		for (auto const& s : stats)
		{
			if (!first) stats_header += ", ";
			stats_header += s.name;
			first = false;
		}

		return stats_header;
	}

	dht_sample_infohashes_alert::dht_sample_infohashes_alert(aux::stack_allocator& alloc
		, udp::endpoint const& endp
		, time_duration _interval
		, int _num
		, std::vector<sha1_hash> const& samples
		, std::vector<std::pair<sha1_hash, udp::endpoint>> const& nodes)
		: endpoint(endp)
		, interval(_interval)
		, num_infohashes(_num)
		, m_alloc(alloc)
		, m_num_samples(aux::numeric_cast<int>(samples.size()))
	{
		m_samples_idx = alloc.allocate(m_num_samples * 20);

		char *ptr = alloc.ptr(m_samples_idx);
		std::memcpy(ptr, samples.data(), samples.size() * 20);

		std::tie(m_v4_num_nodes, m_v4_nodes_idx, m_v6_num_nodes, m_v6_nodes_idx)
			= write_nodes(alloc, nodes);
	}

	std::string dht_sample_infohashes_alert::message() const
	{
		char msg[200];
		std::snprintf(msg, sizeof(msg)
			, "incoming dht sample_infohashes reply from: %s, samples %d"
			, print_endpoint(endpoint).c_str(), m_num_samples);
		return msg;
	}

	int dht_sample_infohashes_alert::num_samples() const
	{
		return m_num_samples;
	}

	std::vector<sha1_hash> dht_sample_infohashes_alert::samples() const
	{
		aux::vector<sha1_hash> samples;
		samples.resize(m_num_samples);

		const char *ptr = m_alloc.get().ptr(m_samples_idx);
		std::memcpy(samples.data(), ptr, samples.size() * 20);

		return samples;
	}

	int dht_sample_infohashes_alert::num_nodes() const
	{
		return m_v4_num_nodes + m_v6_num_nodes;
	}

	std::vector<std::pair<sha1_hash, udp::endpoint>> dht_sample_infohashes_alert::nodes() const
	{
		return read_nodes(m_alloc.get()
			, m_v4_num_nodes, m_v4_nodes_idx
			, m_v6_num_nodes, m_v6_nodes_idx);
	}

	block_uploaded_alert::block_uploaded_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
		, piece_index_t piece_num)
		: peer_alert(alloc, h, ep, peer_id)
		, block_index(block_num)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= piece_index_t{0});
	}

	std::string block_uploaded_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s block uploaded to a peer (piece: %u block: %u)"
			, torrent_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
	}

	// this will no longer be necessary in C++17
	constexpr alert_category_t torrent_removed_alert::static_category;
	constexpr alert_category_t read_piece_alert::static_category;
	constexpr alert_category_t file_completed_alert::static_category;
	constexpr alert_category_t file_renamed_alert::static_category;
	constexpr alert_category_t file_rename_failed_alert::static_category;
	constexpr alert_category_t performance_alert::static_category;
	constexpr alert_category_t state_changed_alert::static_category;
	constexpr alert_category_t tracker_error_alert::static_category;
	constexpr alert_category_t tracker_warning_alert::static_category;
	constexpr alert_category_t scrape_reply_alert::static_category;
	constexpr alert_category_t scrape_failed_alert::static_category;
	constexpr alert_category_t tracker_reply_alert::static_category;
	constexpr alert_category_t dht_reply_alert::static_category;
	constexpr alert_category_t tracker_announce_alert::static_category;
	constexpr alert_category_t hash_failed_alert::static_category;
	constexpr alert_category_t peer_ban_alert::static_category;
	constexpr alert_category_t peer_unsnubbed_alert::static_category;
	constexpr alert_category_t peer_snubbed_alert::static_category;
	constexpr alert_category_t peer_error_alert::static_category;
	constexpr alert_category_t peer_connect_alert::static_category;
	constexpr alert_category_t peer_disconnected_alert::static_category;
	constexpr alert_category_t invalid_request_alert::static_category;
	constexpr alert_category_t torrent_finished_alert::static_category;
	constexpr alert_category_t piece_finished_alert::static_category;
	constexpr alert_category_t request_dropped_alert::static_category;
	constexpr alert_category_t block_timeout_alert::static_category;
	constexpr alert_category_t block_finished_alert::static_category;
	constexpr alert_category_t block_downloading_alert::static_category;
	constexpr alert_category_t unwanted_block_alert::static_category;
	constexpr alert_category_t storage_moved_alert::static_category;
	constexpr alert_category_t storage_moved_failed_alert::static_category;
	constexpr alert_category_t torrent_deleted_alert::static_category;
	constexpr alert_category_t torrent_delete_failed_alert::static_category;
	constexpr alert_category_t save_resume_data_alert::static_category;
	constexpr alert_category_t save_resume_data_failed_alert::static_category;
	constexpr alert_category_t torrent_paused_alert::static_category;
	constexpr alert_category_t torrent_resumed_alert::static_category;
	constexpr alert_category_t torrent_checked_alert::static_category;
	constexpr alert_category_t url_seed_alert::static_category;
	constexpr alert_category_t file_error_alert::static_category;
	constexpr alert_category_t metadata_failed_alert::static_category;
	constexpr alert_category_t metadata_received_alert::static_category;
	constexpr alert_category_t udp_error_alert::static_category;
	constexpr alert_category_t external_ip_alert::static_category;
	constexpr alert_category_t listen_failed_alert::static_category;
	constexpr alert_category_t listen_succeeded_alert::static_category;
	constexpr alert_category_t portmap_error_alert::static_category;
	constexpr alert_category_t portmap_alert::static_category;
	constexpr alert_category_t portmap_log_alert::static_category;
	constexpr alert_category_t fastresume_rejected_alert::static_category;
	constexpr alert_category_t peer_blocked_alert::static_category;
	constexpr alert_category_t dht_announce_alert::static_category;
	constexpr alert_category_t dht_get_peers_alert::static_category;
	constexpr alert_category_t stats_alert::static_category;
	constexpr alert_category_t cache_flushed_alert::static_category;
	constexpr alert_category_t anonymous_mode_alert::static_category;
	constexpr alert_category_t lsd_peer_alert::static_category;
	constexpr alert_category_t trackerid_alert::static_category;
	constexpr alert_category_t dht_bootstrap_alert::static_category;
	constexpr alert_category_t torrent_error_alert::static_category;
	constexpr alert_category_t torrent_need_cert_alert::static_category;
	constexpr alert_category_t incoming_connection_alert::static_category;
	constexpr alert_category_t add_torrent_alert::static_category;
	constexpr alert_category_t state_update_alert::static_category;
	constexpr alert_category_t session_stats_alert::static_category;
	constexpr alert_category_t dht_error_alert::static_category;
	constexpr alert_category_t dht_immutable_item_alert::static_category;
	constexpr alert_category_t dht_mutable_item_alert::static_category;
	constexpr alert_category_t dht_put_alert::static_category;
	constexpr alert_category_t i2p_alert::static_category;
	constexpr alert_category_t dht_outgoing_get_peers_alert::static_category;
	constexpr alert_category_t log_alert::static_category;
	constexpr alert_category_t torrent_log_alert::static_category;
	constexpr alert_category_t peer_log_alert::static_category;
	constexpr alert_category_t lsd_error_alert::static_category;
	constexpr alert_category_t dht_stats_alert::static_category;
	constexpr alert_category_t incoming_request_alert::static_category;
	constexpr alert_category_t dht_log_alert::static_category;
	constexpr alert_category_t dht_pkt_alert::static_category;
	constexpr alert_category_t dht_get_peers_reply_alert::static_category;
	constexpr alert_category_t dht_direct_response_alert::static_category;
	constexpr alert_category_t picker_log_alert::static_category;
	constexpr alert_category_t session_error_alert::static_category;
	constexpr alert_category_t dht_live_nodes_alert::static_category;
	constexpr alert_category_t session_stats_header_alert::static_category;
	constexpr alert_category_t dht_sample_infohashes_alert::static_category;
	constexpr alert_category_t block_uploaded_alert::static_category;
#ifndef TORRENT_NO_DEPRECATE
	constexpr alert_category_t mmap_cache_alert::static_category;
	constexpr alert_category_t torrent_added_alert::static_category;
	constexpr alert_category_t torrent_update_alert::static_category;
#endif

} // namespace libtorrent
