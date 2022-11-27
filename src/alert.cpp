/*

Copyright (c) 2015, Thomas
Copyright (c) 2003, Daniel Wallin
Copyright (c) 2004, Magnus Jonsson
Copyright (c) 2009-2022, Arvid Norberg
Copyright (c) 2014-2018, Steven Siloti
Copyright (c) 2015-2018, Alden Torres
Copyright (c) 2015, Thomas Yuan
Copyright (c) 2016, Pavel Pimenov
Copyright (c) 2017, Andrei Kurushin
Copyright (c) 2017, Antoine Dahan
Copyright (c) 2019, Amir Abrams
Copyright (c) 2020, Fonic
Copyright (c) 2020, Viktor Elofsson
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
#include "libtorrent/socket_type.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/aux_/ip_helpers.hpp" // for is_v4

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/write_resume_data.hpp"
#endif

#include "libtorrent/aux_/escape_string.hpp" // for convert_from_native

namespace libtorrent {

	constexpr alert_category_t alert::error_notification;
	constexpr alert_category_t alert::peer_notification;
	constexpr alert_category_t alert::port_mapping_notification;
	constexpr alert_category_t alert::storage_notification;
	constexpr alert_category_t alert::tracker_notification;
	constexpr alert_category_t alert::connect_notification;
	constexpr alert_category_t alert::status_notification;
#if TORRENT_ABI_VERSION == 1
	constexpr alert_category_t alert::debug_notification;
	constexpr alert_category_t alert::progress_notification;
#endif
	constexpr alert_category_t alert::ip_block_notification;
	constexpr alert_category_t alert::performance_warning;
	constexpr alert_category_t alert::dht_notification;
#if TORRENT_ABI_VERSION <= 2
	constexpr alert_category_t alert::stats_notification;
#endif
	constexpr alert_category_t alert::session_log_notification;
	constexpr alert_category_t alert::torrent_log_notification;
	constexpr alert_category_t alert::peer_log_notification;
	constexpr alert_category_t alert::incoming_request_notification;
	constexpr alert_category_t alert::dht_log_notification;
	constexpr alert_category_t alert::dht_operation_notification;
	constexpr alert_category_t alert::port_mapping_log_notification;
	constexpr alert_category_t alert::picker_log_notification;
	constexpr alert_category_t alert::file_progress_notification;
	constexpr alert_category_t alert::piece_progress_notification;
	constexpr alert_category_t alert::upload_notification;
	constexpr alert_category_t alert::block_progress_notification;

	constexpr alert_category_t alert::all_categories;

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
				if (t->info_hash().has_v2())
					m_name_idx = alloc.copy_string(aux::to_hex(t->info_hash().v2));
				else
					m_name_idx = alloc.copy_string(aux::to_hex(t->info_hash().v1));
			}
		}
		else
		{
			m_name_idx = alloc.copy_string("");
		}

#if TORRENT_ABI_VERSION == 1
		name = m_alloc.get().ptr(m_name_idx);
#endif
	}

	char const* torrent_alert::torrent_name() const
	{
		return m_alloc.get().ptr(m_name_idx);
	}

	std::string torrent_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		if (!handle.is_valid()) return " - ";
		return torrent_name();
#endif
	}

	peer_alert::peer_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, tcp::endpoint const& i
		, peer_id const& pi)
		: torrent_alert(alloc, h)
		, endpoint(i)
		, pid(pi)
#if TORRENT_ABI_VERSION == 1
		, ip(i)
#endif
	{}

	std::string peer_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " peer [ " + print_endpoint(endpoint)
			+ " client: " + aux::identify_client_impl(pid) + " ]";
#endif
	}

	tracker_alert::tracker_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep, string_view u)
		: torrent_alert(alloc, h)
		, local_endpoint(ep)
		, m_url_idx(alloc.copy_string(u))
#if TORRENT_ABI_VERSION == 1
		, url(u)
#endif
	{}

	char const* tracker_alert::tracker_url() const
	{
		return m_alloc.get().ptr(m_url_idx);
	}

	std::string tracker_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " (" + tracker_url() + ")"
			+ "[" + print_endpoint(local_endpoint) + "]";
#endif
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
#if TORRENT_ABI_VERSION == 1
		, ec(e)
#endif
	{}

	std::string read_piece_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[200];
		if (error)
		{
			std::snprintf(msg, sizeof(msg), "%s: read_piece %d failed: %s"
				, torrent_alert::message().c_str() , static_cast<int>(piece)
				, convert_from_native(error.message()).c_str());
		}
		else
		{
			std::snprintf(msg, sizeof(msg), "%s: read_piece %d successful"
				, torrent_alert::message().c_str() , static_cast<int>(piece));
		}
		return msg;
#endif
	}

	file_completed_alert::file_completed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, file_index_t idx)
		: torrent_alert(alloc, h)
		, index(idx)
	{}

	std::string file_completed_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		std::string ret { torrent_alert::message() };
		char msg[200];
		std::snprintf(msg, sizeof(msg), ": file %d finished downloading"
			, static_cast<int>(index));
		ret.append(msg);
		return ret;
#endif
	}

	file_renamed_alert::file_renamed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, string_view n, string_view old, file_index_t const idx)
		: torrent_alert(alloc, h)
		, index(idx)
		, m_name_idx(alloc.copy_string(n))
		, m_old_name_idx(alloc.copy_string(old))
#if TORRENT_ABI_VERSION == 1
		, name(n)
#endif
	{}

	char const* file_renamed_alert::new_name() const
	{
		return m_alloc.get().ptr(m_name_idx);
	}

	char const* file_renamed_alert::old_name() const
	{
		return m_alloc.get().ptr(m_old_name_idx);
	}

	std::string file_renamed_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		std::string ret { torrent_alert::message() };
		char msg[200];
		std::snprintf(msg, sizeof(msg), ": file %d renamed from \""
			, static_cast<int>(index));
		ret.append(msg);
		ret.append(old_name());
		ret.append("\" to \"");
		ret.append(new_name());
		ret.append("\"");
		return ret;
#endif
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		std::string ret { torrent_alert::message() };
		char msg[200];
		std::snprintf(msg, sizeof(msg), ": failed to rename file %d: "
			, static_cast<int>(index));
		ret.append(msg);
		ret.append(convert_from_native(error.message()));
		return ret;
#endif
	}

	performance_alert::performance_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, performance_warning_t w)
		: torrent_alert(alloc, h)
		, warning_code(w)
	{}

	char const* performance_warning_str(performance_alert::performance_warning_t i)
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		TORRENT_UNUSED(i);
		return "";
#else
		static char const* const warning_str[] =
		{
			"max outstanding disk writes reached",
			"max outstanding piece requests reached",
			"upload limit too low (download rate will suffer)",
			"download limit too low (upload rate will suffer)",
			"send buffer watermark too low (upload rate will suffer)",
			"too many optimistic unchoke slots",
			"the disk queue limit is too high compared to the cache size. The disk queue eats into the cache size",
			"outstanding AIO operations limit reached",
			"",
			"too few ports allowed for outgoing connections",
			"too few file descriptors are allowed for this process. connection limit lowered"
		};

		TORRENT_ASSERT(i >= 0);
		TORRENT_ASSERT(i < std::end(warning_str) - std::begin(warning_str));
		return warning_str[i];
#endif
	}

	std::string performance_alert::message() const
	{
		return torrent_alert::message() + ": performance warning: "
			+ performance_warning_str(warning_code);
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		static char const* const state_str[] =
			{"checking (q)", "checking", "dl metadata"
			, "downloading", "finished", "seeding", "allocating"
			, "checking (r)"};

		return torrent_alert::message() + ": state changed to: "
			+ state_str[state];
#endif
	}

	tracker_error_alert::tracker_error_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep, int times
		, protocol_version v, string_view u, operation_t const operation
		, error_code const& e
		, string_view m)
		: tracker_alert(alloc, h, ep, u)
		, times_in_row(times)
		, error(e)
		, op(operation)
		, m_msg_idx(alloc.copy_string(m))
#if TORRENT_ABI_VERSION == 1
		, status_code(e && e.category() == http_category() ? e.value() : -1)
		, msg(m)
#endif
		// TODO: move this field into tracker_alert
		, version(v)
	{
		TORRENT_ASSERT(!u.empty());
	}

	char const* tracker_error_alert::failure_reason() const
	{
		return m_alloc.get().ptr(m_msg_idx);
	}

	std::string tracker_error_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[400];
		std::snprintf(ret, sizeof(ret), "%s %s %s \"%s\" (%d)"
			, tracker_alert::message().c_str()
			, version == protocol_version::V1 ? "v1" : "v2"
			, convert_from_native(error.message()).c_str(), error_message()
			, times_in_row);
		return ret;
#endif
	}

	tracker_warning_alert::tracker_warning_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, string_view u, protocol_version v, string_view m)
		: tracker_alert(alloc, h, ep, u)
		, m_msg_idx(alloc.copy_string(m))
#if TORRENT_ABI_VERSION == 1
		, msg(m)
#endif
		// TODO: move this into tracker_alert
		, version(v)
	{
		TORRENT_ASSERT(!u.empty());
	}

	char const* tracker_warning_alert::warning_message() const
	{
		return m_alloc.get().ptr(m_msg_idx);
	}

	std::string tracker_warning_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return tracker_alert::message() + (version == protocol_version::V1 ? " v1" : " v2") + " warning: " + warning_message();
#endif
	}

	scrape_reply_alert::scrape_reply_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, int incomp, int comp, string_view u, protocol_version const v)
		: tracker_alert(alloc, h, ep, u)
		, incomplete(incomp)
		, complete(comp)
		// TODO: move this into tracker_alert
		, version(v)
	{
		TORRENT_ASSERT(!u.empty());
	}

	std::string scrape_reply_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[400];
		std::snprintf(ret, sizeof(ret), "%s %s scrape reply: %d %d"
			, tracker_alert::message().c_str()
			, version == protocol_version::V1 ? "v1" : "v2"
			, incomplete, complete);
		return ret;
#endif
	}

	scrape_failed_alert::scrape_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, string_view u, protocol_version const v, error_code const& e)
		: tracker_alert(alloc, h, ep, u)
		, error(e)
		, m_msg_idx()
#if TORRENT_ABI_VERSION == 1
		, msg(convert_from_native(e.message()))
#endif
		// TODO: move this into tracker_alert
		, version(v)
	{
		TORRENT_ASSERT(!u.empty());
	}

	scrape_failed_alert::scrape_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, string_view u, string_view m)
		: tracker_alert(alloc, h, ep, u)
		, error(errors::tracker_failure)
		, m_msg_idx(alloc.copy_string(m))
#if TORRENT_ABI_VERSION == 1
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return tracker_alert::message() + " scrape failed: " + error_message();
#endif
	}

	tracker_reply_alert::tracker_reply_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, int np, protocol_version v, string_view u)
		: tracker_alert(alloc, h, ep, u)
		, num_peers(np)
		// TODO: move this field into tracker_alert
		, version(v)
	{
		TORRENT_ASSERT(!u.empty());
	}

	std::string tracker_reply_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[400];
		std::snprintf(ret, sizeof(ret), "%s %s received peers: %d"
			, tracker_alert::message().c_str()
			, version == protocol_version::V1 ? "v1" : "v2"
			, num_peers);
		return ret;
#endif
	}

	dht_reply_alert::dht_reply_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, int np)
		: tracker_alert(alloc, h, {}, "")
		, num_peers(np)
	{}

	std::string dht_reply_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[400];
		std::snprintf(ret, sizeof(ret), "%s received DHT peers: %d"
			, tracker_alert::message().c_str(), num_peers);
		return ret;
#endif
	}

	tracker_announce_alert::tracker_announce_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep, string_view u
		, protocol_version const v, event_t const e)
		: tracker_alert(alloc, h, ep, u)
		, event(e)
		// TODO: move this to tracker_alert
		, version(v)
	{
		TORRENT_ASSERT(!u.empty());
	}

	std::string tracker_announce_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		static const char* const event_str[] = {"none", "completed", "started", "stopped", "paused"};
		return tracker_alert::message()
			+ (version == protocol_version::V1 ? " v1" : " v2")
			+ " sending announce (" + event_str[static_cast<int>(event)] + ")";
#endif
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[400];
		std::snprintf(ret, sizeof(ret), "%s hash for piece %d failed"
			, torrent_alert::message().c_str(), static_cast<int>(piece_index));
		return ret;
#endif
	}

	peer_ban_alert::peer_ban_alert(aux::stack_allocator& alloc
		, torrent_handle h, tcp::endpoint const& ep
		, peer_id const& peer_id)
		: peer_alert(alloc, h, ep, peer_id)
	{}

	std::string peer_ban_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return peer_alert::message() + " banned peer";
#endif
	}

	peer_unsnubbed_alert::peer_unsnubbed_alert(aux::stack_allocator& alloc
		, torrent_handle h, tcp::endpoint const& ep
		, peer_id const& peer_id)
		: peer_alert(alloc, h, ep, peer_id)
	{}

	std::string peer_unsnubbed_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return peer_alert::message() + " peer unsnubbed";
#endif
	}

	peer_snubbed_alert::peer_snubbed_alert(aux::stack_allocator& alloc
		, torrent_handle h, tcp::endpoint const& ep
		, peer_id const& peer_id)
		: peer_alert(alloc, h, ep, peer_id)
	{}

	std::string peer_snubbed_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return peer_alert::message() + " peer snubbed";
#endif
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[400];
		std::snprintf(ret, sizeof(ret), "%s peer sent an invalid piece request "
			"(piece: %d start: %d len: %d)%s"
			, peer_alert::message().c_str()
			, static_cast<int>(request.piece)
			, request.start
			, request.length
			, withheld ? ": super seeding withheld piece"
			: !we_have ? ": we don't have piece"
			: !peer_interested ? ": peer is not interested"
			: "");
		return ret;
#endif
	}

	torrent_finished_alert::torrent_finished_alert(aux::stack_allocator& alloc
		, torrent_handle h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_finished_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " torrent finished downloading";
#endif
	}

	piece_finished_alert::piece_finished_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, piece_index_t piece_num)
		: torrent_alert(alloc, h)
		, piece_index(piece_num)
	{}

	std::string piece_finished_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s piece: %d finished downloading"
			, torrent_alert::message().c_str(), static_cast<int>(piece_index));
		return ret;
#endif
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s peer dropped block ( piece: %d block: %d)"
			, peer_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
#endif
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s peer timed out request ( piece: %d block: %d)"
			, peer_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
#endif
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s block finished downloading (piece: %d block: %d)"
			, peer_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
#endif
	}

	block_downloading_alert::block_downloading_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep
		, peer_id const& peer_id, int block_num, piece_index_t piece_num)
		: peer_alert(alloc, h, ep, peer_id)
		, block_index(block_num)
		, piece_index(piece_num)
#if TORRENT_ABI_VERSION == 1
		, peer_speedmsg("")
#endif
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= piece_index_t(0));
	}

	std::string block_downloading_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s requested block (piece: %d block: %d)"
			, peer_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
#endif
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[200];
		std::snprintf(ret, sizeof(ret), "%s received block not in download queue (piece: %d block: %d)"
			, peer_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
#endif
	}

	storage_moved_alert::storage_moved_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, string_view p, string_view old)
		: torrent_alert(alloc, h)
		, m_path_idx(alloc.copy_string(p))
		, m_old_path_idx(alloc.copy_string(old))
#if TORRENT_ABI_VERSION == 1
		, path(p)
#endif
	{}

	std::string storage_moved_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " moved storage from \""
			+ old_path() + "\" to: \"" + storage_path() + "\"";
#endif
	}

	char const* storage_moved_alert::storage_path() const
	{
		return m_alloc.get().ptr(m_path_idx);
	}

	char const* storage_moved_alert::old_path() const
	{
		return m_alloc.get().ptr(m_old_path_idx);
	}

	storage_moved_failed_alert::storage_moved_failed_alert(
		aux::stack_allocator& alloc, torrent_handle const& h, error_code const& e
		, string_view f, operation_t const op_)
		: torrent_alert(alloc, h)
		, error(e)
		, op(op_)
		, m_file_idx(alloc.copy_string(f))
#if TORRENT_ABI_VERSION == 1
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " storage move failed. "
			+ operation_name(op) + " (" + file_path() + "): "
			+ convert_from_native(error.message());
#endif
	}

	torrent_deleted_alert::torrent_deleted_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, info_hash_t const& ih)
		: torrent_alert(alloc, h)
		, info_hashes(ih)
	{
#if TORRENT_ABI_VERSION < 3
		info_hash = info_hashes.get_best();
#endif
	}

	std::string torrent_deleted_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " deleted";
#endif
	}

	torrent_delete_failed_alert::torrent_delete_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, error_code const& e, info_hash_t const& ih)
		: torrent_alert(alloc, h)
		, error(e)
		, info_hashes(ih)
#if TORRENT_ABI_VERSION == 1
		, msg(convert_from_native(error.message()))
#endif
	{
#if TORRENT_ABI_VERSION < 3
		info_hash = info_hashes.get_best();
#endif
	}

	std::string torrent_delete_failed_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " torrent deletion failed: "
			+ convert_from_native(error.message());
#endif
	}

	save_resume_data_alert::save_resume_data_alert(aux::stack_allocator& alloc
		, add_torrent_params&& p
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
		, params(std::move(p))
#if TORRENT_ABI_VERSION == 1
		, resume_data(std::make_shared<entry>(write_resume_data(params)))
#endif
	{
#if TORRENT_ABI_VERSION < 3
		params.info_hash = params.info_hashes.get_best();
#endif
	}

	std::string save_resume_data_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " resume data generated";
#endif
	}

	save_resume_data_failed_alert::save_resume_data_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, error_code const& e)
		: torrent_alert(alloc, h)
		, error(e)
#if TORRENT_ABI_VERSION == 1
		, msg(convert_from_native(error.message()))
#endif
	{
	}

	std::string save_resume_data_failed_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " resume data was not generated: "
			+ convert_from_native(error.message());
#endif
	}

	torrent_paused_alert::torrent_paused_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_paused_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " paused";
#endif
	}

	torrent_resumed_alert::torrent_resumed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_resumed_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " resumed";
#endif
	}

	torrent_checked_alert::torrent_checked_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_checked_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " checked";
#endif
	}

namespace {

#ifndef TORRENT_DISABLE_ALERT_MSG
	char const* const nat_type_str[] = {"NAT-PMP", "UPnP"};

	char const* const protocol_str[] = {"none", "TCP", "UDP"};
#endif

#if TORRENT_ABI_VERSION == 1
	int sock_type_idx(socket_type_t type)
	{
		// these numbers are the deprecated enum values in
		// listen_succeeded_alert and listen_failed_alert
		static aux::array<int, 9, socket_type_t> const mapping{{
			0, // tcp
			4, // socks5,
			0, // http,
			2, // utp,
			3, // i2p,
			1, // tcp_ssl
			4, // socks5_ssl,
			1, // http_ssl,
			5  // utp_ssl,
		}};
		return mapping[type];
	}

	int to_op_t(operation_t op)
	{
		using o = operation_t;
		using lfo = listen_failed_alert::op_t;

		// we have to use deprecated enum values here. suppress the warnings
#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"
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
			case o::symlink: return -1;
			case o::handshake: return -1;
			case o::sock_option: return -1;
			case o::enum_route: return -1;
			case o::file_seek: return -1;
			case o::timer: return -1;
			case o::file_mmap: return -1;
			case o::file_truncate: return -1;
		}
		return -1;
	}
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#endif // TORRENT_ABI_VERSION

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
#if TORRENT_ABI_VERSION == 1
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[300];
		std::snprintf(ret, sizeof(ret), "listening on %s (device: %s) failed: [%s] [%s] %s"
			, print_endpoint(address, port).c_str()
			, listen_interface()
			, operation_name(op)
			, socket_type_name(socket_type)
			, convert_from_native(error.message()).c_str());
		return ret;
#endif
	}

	metadata_failed_alert::metadata_failed_alert(aux::stack_allocator& alloc
		, const torrent_handle& h, error_code const& e)
		: torrent_alert(alloc, h)
		, error(e)
	{}

	std::string metadata_failed_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " invalid metadata received";
#endif
	}

	metadata_received_alert::metadata_received_alert(aux::stack_allocator& alloc
		, const torrent_handle& h)
		: torrent_alert(alloc, h)
	{}

	std::string metadata_received_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " metadata successfully received";
#endif
	}

	udp_error_alert::udp_error_alert(
		aux::stack_allocator&
		, udp::endpoint const& ep
		, operation_t op
		, error_code const& ec)
		: endpoint(ep)
		, operation(op)
		, error(ec)
	{}

	std::string udp_error_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return "UDP error: " + convert_from_native(error.message())
			+ " from: " + endpoint.address().to_string()
			+ " op: " + operation_name(operation);
#endif
	}

	external_ip_alert::external_ip_alert(aux::stack_allocator&
		, address const& ip)
		: external_address(ip)
	{}

	std::string external_ip_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return "external IP received: " + external_address.to_string();
#endif
	}

	listen_succeeded_alert::listen_succeeded_alert(aux::stack_allocator&
		, libtorrent::address const& listen_addr
		, int listen_port
		, libtorrent::socket_type_t t)
		: address(listen_addr)
		, port(listen_port)
		, socket_type(t)
#if TORRENT_ABI_VERSION == 1
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[200];
		std::snprintf(ret, sizeof(ret), "successfully listening on [%s] %s"
			, socket_type_name(socket_type), print_endpoint(address, port).c_str());
		return ret;
#endif
	}

	portmap_error_alert::portmap_error_alert(aux::stack_allocator&
		, port_mapping_t const i, portmap_transport const t, error_code const& e
		, address const& local)
		: mapping(i)
		, map_transport(t)
		, local_address(local)
		, error(e)
#if TORRENT_ABI_VERSION == 1
		, map_type(static_cast<int>(t))
		, msg(convert_from_native(error.message()))
#endif
	{}

	std::string portmap_error_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return std::string("could not map port using ")
			+ nat_type_str[static_cast<int>(map_transport)]
			+ "[" + local_address.to_string() + "]: "
			+ convert_from_native(error.message());
#endif
	}

	portmap_alert::portmap_alert(aux::stack_allocator&, port_mapping_t const i
		, int const port, portmap_transport const t, portmap_protocol const proto
		, address const& local)
		: mapping(i)
		, external_port(port)
		, map_protocol(proto)
		, map_transport(t)
		, local_address(local)
#if TORRENT_ABI_VERSION == 1
		, protocol(static_cast<int>(proto))
		, map_type(static_cast<int>(t))
#endif
	{}

	std::string portmap_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[200];
		std::snprintf(ret, sizeof(ret), "successfully mapped port using %s. local: %s external port: %s/%d"
			, nat_type_str[static_cast<int>(map_transport)]
			, local_address.to_string().c_str()
			, protocol_str[static_cast<int>(map_protocol)], external_port);
		return ret;
#endif
	}

	portmap_log_alert::portmap_log_alert(aux::stack_allocator& alloc
		, portmap_transport const t, const char* m, address const& local)
		: map_transport(t)
		, local_address(local)
		, m_alloc(alloc)
		, m_log_idx(alloc.copy_string(m))
#if TORRENT_ABI_VERSION == 1
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[1024];
		std::snprintf(ret, sizeof(ret), "%s [%s]: %s"
			, nat_type_str[static_cast<int>(map_transport)]
			, local_address.to_string().c_str()
			, log_message());
		return ret;
#endif
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
#if TORRENT_ABI_VERSION == 1
		, operation(operation_name(op_))
		, file(f)
		, msg(convert_from_native(error.message()))
#endif
	{
	}

	std::string fastresume_rejected_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " fast resume rejected. "
			+ operation_name(op) + "(" + file_path() + "): "
			+ convert_from_native(error.message());
#endif
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[600];
		static char const* const reason_str[] =
		{
			"ip_filter",
			"port_filter",
			"i2p_mixed",
			"privileged_ports",
			"utp_disabled",
			"tcp_disabled",
			"invalid_local_interface",
			"ssrf_mitigation"
		};

		std::snprintf(ret, sizeof(ret), "%s: blocked peer [%s]"
			, peer_alert::message().c_str(), reason_str[reason]);
		return ret;
#endif
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[200];
		std::snprintf(msg, sizeof(msg), "incoming dht announce: %s:%d (%s)"
			, ip.to_string().c_str(), port, aux::to_hex(info_hash).c_str());
		return msg;
#endif
	}

	dht_get_peers_alert::dht_get_peers_alert(aux::stack_allocator&
		, sha1_hash const& ih)
		: info_hash(ih)
	{}

	std::string dht_get_peers_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[200];
		std::snprintf(msg, sizeof(msg), "incoming dht get_peers: %s", aux::to_hex(info_hash).c_str());
		return msg;
#endif
	}

#if TORRENT_ABI_VERSION <= 2
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

#if TORRENT_ABI_VERSION == 1
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[200];
		std::snprintf(msg, sizeof(msg), "%s: [%d] %d %d %d %d %d %d"
#if TORRENT_ABI_VERSION == 1
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
#if TORRENT_ABI_VERSION == 1
			, transferred[6]
			, transferred[7]
			, transferred[8]
			, transferred[9]
#endif
			);
		return msg;
#endif
	}
#endif // TORRENT_ABI_VERSION

	cache_flushed_alert::cache_flushed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h) {}

#if TORRENT_ABI_VERSION == 1
	anonymous_mode_alert::anonymous_mode_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, int k, string_view s)
		: torrent_alert(alloc, h)
		, kind(k)
		, str(s)
	{}

	std::string anonymous_mode_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[200];
		static char const* const msgs[] = {
			"tracker is not anonymous, set a proxy"
		};
		std::snprintf(msg, sizeof(msg), "%s: %s: %s"
			, torrent_alert::message().c_str()
			, msgs[kind], str.c_str());
		return msg;
#endif
	}
#endif // TORRENT_ABI_VERSION

	lsd_peer_alert::lsd_peer_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, tcp::endpoint const& i)
		: peer_alert(alloc, h, i, peer_id(nullptr))
	{}

	std::string lsd_peer_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[200];
		std::snprintf(msg, sizeof(msg), "%s: received peer from local service discovery"
			, peer_alert::message().c_str());
		return msg;
#endif
	}

	trackerid_alert::trackerid_alert(
		aux::stack_allocator& alloc
		, torrent_handle const& h
		, tcp::endpoint const& ep
		, string_view u
		, const std::string& id)
		: tracker_alert(alloc, h,  ep, u)
		, m_tracker_idx(alloc.copy_string(id))
#if TORRENT_ABI_VERSION == 1
		, trackerid(id)
#endif
	{}

	char const* trackerid_alert::tracker_id() const
	{
		return m_alloc.get().ptr(m_tracker_idx);
	}

	std::string trackerid_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return std::string("trackerid received: ") + tracker_id();
#endif
	}

	dht_bootstrap_alert::dht_bootstrap_alert(aux::stack_allocator&)
	{}

	std::string dht_bootstrap_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return "DHT bootstrap complete";
#endif
	}

	torrent_error_alert::torrent_error_alert(
		aux::stack_allocator& alloc
		, torrent_handle const& h
		, error_code const& e, string_view f)
		: torrent_alert(alloc, h)
		, error(e)
		, m_file_idx(alloc.copy_string(f))
#if TORRENT_ABI_VERSION == 1
		, error_file(f)
#endif
	{}

	std::string torrent_error_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
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
#endif
	}

	char const* torrent_error_alert::filename() const
	{
		return m_alloc.get().ptr(m_file_idx);
	}

#if TORRENT_ABI_VERSION == 1
	torrent_added_alert::torrent_added_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_added_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " added";
#endif
	}
#endif

	torrent_removed_alert::torrent_removed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, info_hash_t const& ih, client_data_t u)
		: torrent_alert(alloc, h)
		, info_hashes(ih)
		, userdata(u)
	{
#if TORRENT_ABI_VERSION < 3
		info_hash = info_hashes.get_best();
#endif
	}

	std::string torrent_removed_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " removed";
#endif
	}

	torrent_need_cert_alert::torrent_need_cert_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
	{}

	std::string torrent_need_cert_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " needs SSL certificate";
#endif
	}

	incoming_connection_alert::incoming_connection_alert(aux::stack_allocator&
		, socket_type_t t, tcp::endpoint const& i)
		: socket_type(t)
		, endpoint(i)
#if TORRENT_ABI_VERSION == 1
		, ip(i)
#endif
	{}

	std::string incoming_connection_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[600];
		std::snprintf(msg, sizeof(msg), "incoming connection from %s (%s)"
			, print_endpoint(endpoint).c_str(), socket_type_name(socket_type));
		return msg;
#endif
	}

	peer_connect_alert::peer_connect_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep, peer_id const& peer_id, socket_type_t const type, direction_t const dir)
		: peer_alert(alloc, h, ep, peer_id)
		, direction(dir)
		, socket_type(type)
	{}

	std::string peer_connect_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[600];
		char const* direction_str = direction == direction_t::in ? "incoming" : "outgoing";
		std::snprintf(msg, sizeof(msg), "%s %s connection to peer (%s)"
			, peer_alert::message().c_str(), direction_str, socket_type_name(socket_type));
		return msg;
#endif
	}

	add_torrent_alert::add_torrent_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, add_torrent_params p, error_code const& ec)
		: torrent_alert(alloc, h)
		, params(std::move(p))
		, error(ec)
	{
#if TORRENT_ABI_VERSION < 3
		params.info_hash = params.info_hashes.get_best();
#endif
	}

	std::string add_torrent_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[600];
		char info_hash[41];
		char const* torrent_name = info_hash;
		if (params.ti) torrent_name = params.ti->name().c_str();
		else if (!params.name.empty()) torrent_name = params.name.c_str();
#if TORRENT_ABI_VERSION == 1
		else if (!params.url.empty()) torrent_name = params.url.c_str();
#endif
		else aux::to_hex(params.info_hashes.get_best(), info_hash);

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
#endif
	}

	state_update_alert::state_update_alert(aux::stack_allocator&
		, std::vector<torrent_status> st)
		: status(std::move(st))
	{}

	std::string state_update_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[600];
		std::snprintf(msg, sizeof(msg), "state updates for %d torrents", int(status.size()));
		return msg;
#endif
	}

#if TORRENT_ABI_VERSION == 1
	mmap_cache_alert::mmap_cache_alert(aux::stack_allocator&
		, error_code const& ec): error(ec)
	{}

	std::string mmap_cache_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[600];
		std::snprintf(msg, sizeof(msg), "mmap cache failed: (%d) %s", error.value()
			, convert_from_native(error.message()).c_str());
		return msg;
#endif
	}
#endif

	char const* operation_name(operation_t const op)
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		TORRENT_UNUSED(op);
		return "";
#else
		static char const* const names[] = {
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
			"symlink",
			"handshake",
			"sock_option",
			"enum_route",
			"file_seek",
			"timer",
			"file_mmap",
			"file_truncate",
		};

		int const idx = static_cast<int>(op);
		if (idx < 0 || idx >= int(sizeof(names) / sizeof(names[0])))
			return "unknown operation";

		return names[idx];
#endif
	}

#if TORRENT_ABI_VERSION == 1
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
#if TORRENT_ABI_VERSION == 1
		, operation(static_cast<int>(op_))
		, msg(convert_from_native(error.message()))
#endif
	{}

	std::string peer_error_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char buf[200];
		std::snprintf(buf, sizeof(buf), "%s peer error [%s] [%s]: %s"
			, peer_alert::message().c_str()
			, operation_name(op), error.category().name()
			, convert_from_native(error.message()).c_str());
		return buf;
#endif
	}

	peer_disconnected_alert::peer_disconnected_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, peer_id const& peer_id, operation_t op_, socket_type_t const type, error_code const& e
		, close_reason_t r)
		: peer_alert(alloc, h, ep, peer_id)
		, socket_type(type)
		, op(op_)
		, error(e)
		, reason(r)
#if TORRENT_ABI_VERSION == 1
		, operation(static_cast<int>(op))
		, msg(convert_from_native(error.message()))
#endif
	{}

	std::string peer_disconnected_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char buf[600];
		std::snprintf(buf, sizeof(buf), "%s disconnecting (%s) [%s] [%s]: %s (reason: %d)"
			, peer_alert::message().c_str()
			, socket_type_name(socket_type)
			, operation_name(op), error.category().name()
			, convert_from_native(error.message()).c_str()
			, int(reason));
		return buf;
#endif
	}

	dht_error_alert::dht_error_alert(aux::stack_allocator&
		, operation_t const op_
		, error_code const& ec)
		: error(ec)
		, op(op_)
#if TORRENT_ABI_VERSION == 1
		, operation(op_ == operation_t::hostname_lookup
			? op_t::hostname_lookup : op_t::unknown)
#endif
	{}

	std::string dht_error_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[600];
		std::snprintf(msg, sizeof(msg), "DHT error [%s] (%d) %s"
			, operation_name(op)
			, error.value()
			, convert_from_native(error.message()).c_str());
		return msg;
#endif
	}

	dht_immutable_item_alert::dht_immutable_item_alert(aux::stack_allocator&
		, sha1_hash const& t, entry i)
		: target(t), item(std::move(i))
	{}

	std::string dht_immutable_item_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[1050];
		std::snprintf(msg, sizeof(msg), "DHT immutable item %s [ %s ]"
			, aux::to_hex(target).c_str()
			, item.to_string().c_str());
		return msg;
#endif
	}

	// TODO: 2 the salt here is allocated on the heap. It would be nice to
	// allocate in the stack_allocator
	dht_mutable_item_alert::dht_mutable_item_alert(aux::stack_allocator&
		, std::array<char, 32> const& k
		, std::array<char, 64> const& sig
		, std::int64_t sequence
		, string_view s
		, entry i
		, bool a)
		: key(k), signature(sig), seq(sequence), salt(s), item(std::move(i)), authoritative(a)
	{}

	std::string dht_mutable_item_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[1050];
		std::snprintf(msg, sizeof(msg), "DHT mutable item (key=%s salt=%s seq=%" PRId64 " %s) [ %s ]"
			, aux::to_hex(key).c_str()
			, salt.c_str()
			, seq
			, authoritative ? "auth" : "non-auth"
			, item.to_string().c_str());
		return msg;
#endif
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
		, std::array<char, 32> const& key
		, std::array<char, 64> const& sig
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
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

		std::snprintf(msg, sizeof(msg), "DHT put complete (success=%d hash=%s)"
			, num_success
			, aux::to_hex(target).c_str());
		return msg;
#endif
	}

	i2p_alert::i2p_alert(aux::stack_allocator&, error_code const& ec)
		: error(ec)
	{}

	std::string i2p_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[600];
		std::snprintf(msg, sizeof(msg), "i2p_error: [%s] %s"
			, error.category().name(), convert_from_native(error.message()).c_str());
		return msg;
#endif
	}

	dht_outgoing_get_peers_alert::dht_outgoing_get_peers_alert(aux::stack_allocator&
		, sha1_hash const& ih, sha1_hash const& obfih
		, udp::endpoint ep)
		: info_hash(ih)
		, obfuscated_info_hash(obfih)
		, endpoint(std::move(ep))
#if TORRENT_ABI_VERSION == 1
		, ip(endpoint)
#endif
	{}

	std::string dht_outgoing_get_peers_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
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
#endif
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

#if TORRENT_ABI_VERSION == 1
	char const* log_alert::msg() const
	{
		return log_message();
	}
#endif

	std::string log_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return log_message();
#endif
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

#if TORRENT_ABI_VERSION == 1
	char const* torrent_log_alert::msg() const
	{
		return log_message();
	}
#endif

	std::string torrent_log_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + ": " + log_message();
#endif
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

#if TORRENT_ABI_VERSION == 1
	char const* peer_log_alert::msg() const
	{
		return log_message();
	}
#endif

	std::string peer_log_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		static char const* const mode[] =
		{ "<==", "==>", "<<<", ">>>", "***" };
		return peer_alert::message() + " [" + print_endpoint(endpoint) + "] "
			+ mode[direction] + " " + event_type + " [ " + log_message() + " ]";
#endif
	}

	lsd_error_alert::lsd_error_alert(aux::stack_allocator&, error_code const& ec
		, address const& local)
		: alert()
		, local_address(local)
		, error(ec)
	{}

	std::string lsd_error_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return "Local Service Discovery startup error [" + local_address.to_string() + "]: "
			+ convert_from_native(error.message());
#endif
	}

#if TORRENT_ABI_VERSION == 1
namespace {

	aux::array<std::int64_t, counters::num_counters> counters_to_array(counters const& cnt)
	{
		aux::array<std::int64_t, counters::num_counters> arr;

		for (int i = 0; i < counters::num_counters; ++i)
			arr[i] = cnt[i];

		return arr;
	}
}
#else
namespace {
	template <typename T, typename U>
	T* align_pointer(U* ptr)
	{
		return reinterpret_cast<T*>((reinterpret_cast<std::uintptr_t>(ptr) + alignof(T) - 1)
			& ~(alignof(T) - 1));
	}
}
#endif

#if TORRENT_ABI_VERSION == 1
	session_stats_alert::session_stats_alert(aux::stack_allocator&, struct counters const& cnt)
		: values(counters_to_array(cnt))
	{}
#else
	session_stats_alert::session_stats_alert(aux::stack_allocator& alloc, struct counters const& cnt)
		: m_alloc(alloc)
		, m_counters_idx(alloc.allocate(sizeof(std::int64_t)
			* counters::num_counters + sizeof(std::int64_t) - 1))
	{
		std::int64_t* ptr = align_pointer<std::int64_t>(alloc.ptr(m_counters_idx));
		for (int i = 0; i < counters::num_counters; ++i, ++ptr)
			*ptr = cnt[i];
	}
#endif

	std::string session_stats_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[50];
		auto cnt = counters();
		std::snprintf(msg, sizeof(msg), "session stats (%d values): " , int(cnt.size()));
		std::string ret = msg;
		bool first = true;
		for (auto v : cnt)
		{
			std::snprintf(msg, sizeof(msg), first ? "%" PRId64 : ", %" PRId64, v);
			first = false;
			ret += msg;
		}
		return ret;
#endif
	}

	span<std::int64_t const> session_stats_alert::counters() const
	{
#if TORRENT_ABI_VERSION == 1
		return values;
#else
		return { align_pointer<std::int64_t const>(m_alloc.get().ptr(m_counters_idx))
			, counters::num_counters };
#endif
	}

	dht_stats_alert::dht_stats_alert(aux::stack_allocator&
		, std::vector<dht_routing_bucket> table
		, std::vector<dht_lookup> requests
			, sha1_hash id, udp::endpoint ep)
		: alert()
		, active_requests(std::move(requests))
		, routing_table(std::move(table))
		, nid(id)
		, local_endpoint(ep)
	{}

	std::string dht_stats_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char buf[2048];
		std::snprintf(buf, sizeof(buf), "DHT stats: (%s) reqs: %d buckets: %d"
			, aux::to_hex(nid).c_str()
			, int(active_requests.size())
			, int(routing_table.size()));
		return buf;
#endif
	}

	url_seed_alert::url_seed_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, string_view u, error_code const& e)
		: torrent_alert(alloc, h)
		, error(e)
		, m_url_idx(alloc.copy_string(u))
		, m_msg_idx()
#if TORRENT_ABI_VERSION == 1
		, url(u)
		, msg(convert_from_native(e.message()))
#endif
	{}

	url_seed_alert::url_seed_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, string_view u, string_view m)
		: torrent_alert(alloc, h)
		, m_url_idx(alloc.copy_string(u))
		, m_msg_idx(alloc.copy_string(m))
#if TORRENT_ABI_VERSION == 1
		, url(u)
		, msg(m)
#endif
	{}

	std::string url_seed_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " url seed ("
			+ server_url() + ") failed: " + convert_from_native(error.message());
#endif
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
#if TORRENT_ABI_VERSION == 1
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " "
			+ operation_name(op) + " (" + filename()
			+ ") error: " + convert_from_native(error.message());
#endif
	}

	incoming_request_alert::incoming_request_alert(aux::stack_allocator& alloc
		, peer_request r, torrent_handle h
		, tcp::endpoint const& ep, peer_id const& peer_id)
		: peer_alert(alloc, h, ep, peer_id)
		, req(r)
	{}

	std::string incoming_request_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[1024];
		std::snprintf(msg, sizeof(msg), "%s: incoming request [ piece: %d start: %d length: %d ]"
			, peer_alert::message().c_str(), static_cast<int>(req.piece)
			, req.start, req.length);
		return msg;
#endif
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
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
#endif
	}

	dht_pkt_alert::dht_pkt_alert(aux::stack_allocator& alloc
		, span<char const> buf, dht_pkt_alert::direction_t d
		, udp::endpoint const& ep)
		: direction(d)
		, node(ep)
		, m_alloc(alloc)
		, m_msg_idx(alloc.copy_buffer(buf))
		, m_size(aux::numeric_cast<int>(buf.size()))
#if TORRENT_ABI_VERSION == 1
		, dir(d)
#endif
	{}

	span<char const> dht_pkt_alert::pkt_buf() const
	{
		return {m_alloc.get().ptr(m_msg_idx), m_size};
	}

	std::string dht_pkt_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		bdecode_node print;
		error_code ec;

		// ignore errors here. This is best-effort. It may be a broken encoding
		// but at least we'll print the valid parts
		span<char const> pkt = pkt_buf();
		bdecode(pkt.data(), pkt.data() + int(pkt.size()), print, ec, nullptr, 100, 100);

		std::string msg = print_entry(print, true);

		static char const* const prefix[2] = {"<==", "==>"};
		char buf[1024];
		std::snprintf(buf, sizeof(buf), "%s [%s] %s", prefix[direction]
			, print_endpoint(node).c_str(), msg.c_str());

		return buf;
#endif
	}

	dht_get_peers_reply_alert::dht_get_peers_reply_alert(aux::stack_allocator& alloc
		, sha1_hash const& ih
		, std::vector<tcp::endpoint> const& peers)
		: info_hash(ih)
		, m_alloc(alloc)
	{
		for (auto const& endp : peers)
		{
			if (aux::is_v4(endp))
				m_v4_num_peers++;
			else
				m_v6_num_peers++;
		}

		m_v4_peers_idx = alloc.allocate(m_v4_num_peers * 6);
		m_v6_peers_idx = alloc.allocate(m_v6_num_peers * 18);

		char* v4_ptr = alloc.ptr(m_v4_peers_idx);
		char* v6_ptr = alloc.ptr(m_v6_peers_idx);
		for (auto const& endp : peers)
		{
			if (aux::is_v4(endp))
				aux::write_endpoint(endp, v4_ptr);
			else
				aux::write_endpoint(endp, v6_ptr);
		}
	}

	std::string dht_get_peers_reply_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[200];
		std::snprintf(msg, sizeof(msg), "incoming dht get_peers reply: %s, peers %d"
			, aux::to_hex(info_hash).c_str(), num_peers());
		return msg;
#endif
	}

	int dht_get_peers_reply_alert::num_peers() const
	{
		return m_v4_num_peers + m_v6_num_peers;
	}

#if TORRENT_ABI_VERSION == 1
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
			peers.push_back(aux::read_v4_endpoint<tcp::endpoint>(v4_ptr));
		char const* v6_ptr = m_alloc.get().ptr(m_v6_peers_idx);
		for (int i = 0; i < m_v6_num_peers; i++)
			peers.push_back(aux::read_v6_endpoint<tcp::endpoint>(v6_ptr));

		return std::move(peers);
	}

	dht_direct_response_alert::dht_direct_response_alert(
		aux::stack_allocator& alloc, client_data_t userdata_
		, udp::endpoint const& addr_, bdecode_node const& response)
		: userdata(userdata_), endpoint(addr_)
		, m_alloc(alloc)
		, m_response_idx(alloc.copy_buffer(response.data_section()))
		, m_response_size(int(response.data_section().size()))
#if TORRENT_ABI_VERSION == 1
		, addr(addr_)
#endif
	{}

	dht_direct_response_alert::dht_direct_response_alert(
		aux::stack_allocator& alloc
		, client_data_t userdata_
		, udp::endpoint const& addr_)
		: userdata(userdata_), endpoint(addr_)
		, m_alloc(alloc)
		, m_response_idx()
		, m_response_size(0)
#if TORRENT_ABI_VERSION == 1
		, addr(addr_)
#endif
	{}

	std::string dht_direct_response_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[1050];
		std::snprintf(msg, sizeof(msg), "DHT direct response (address=%s) [ %s ]"
			, endpoint.address().to_string().c_str()
			, m_response_size ? std::string(m_alloc.get().ptr(m_response_idx)
				, aux::numeric_cast<std::size_t>(m_response_size)).c_str() : "");
		return msg;
#endif
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
		, span<piece_block const> blocks)
		: peer_alert(alloc, h, ep, peer_id)
		, picker_flags(flags)
		, m_array_idx(alloc.copy_buffer({reinterpret_cast<char const*>(blocks.data())
			, blocks.size() * int(sizeof(piece_block))}))
		, m_num_blocks(int(blocks.size()))
	{}

	std::vector<piece_block> picker_log_alert::blocks() const
	{
		// we need to copy this array to make sure the structures are properly
		// aligned, not just to have a nice API
		auto const num_blocks = aux::numeric_cast<std::size_t>(m_num_blocks);
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
	constexpr picker_flags_t picker_log_alert::extent_affinity;

	std::string picker_log_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
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
			"end_game ",
			"extent_affinity ",
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
#endif
	}

	session_error_alert::session_error_alert(aux::stack_allocator& alloc
		, error_code e, string_view error_str)
		: error(e)
		, m_alloc(alloc)
		, m_msg_idx(alloc.copy_buffer(error_str))
	{}

	std::string session_error_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
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
#endif
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
			if (aux::is_v4(n.second))
				v4_num_nodes++;
			else
				v6_num_nodes++;
		}

		aux::allocation_slot const v4_nodes_idx = alloc.allocate(v4_num_nodes * (20 + 6));
		aux::allocation_slot const v6_nodes_idx = alloc.allocate(v6_num_nodes * (20 + 18));

		char* v4_ptr = alloc.ptr(v4_nodes_idx);
		char* v6_ptr = alloc.ptr(v6_nodes_idx);
		for (auto const& n : nodes)
		{
			udp::endpoint const& endp = n.second;
			if (aux::is_v4(endp))
			{
				aux::write_string(n.first.to_string(), v4_ptr);
				aux::write_endpoint(endp, v4_ptr);
			}
			else
			{
				aux::write_string(n.first.to_string(), v6_ptr);
				aux::write_endpoint(endp, v6_ptr);
			}
		}

		return nodes_slot{v4_num_nodes, v4_nodes_idx, v6_num_nodes, v6_nodes_idx};
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
			nodes.emplace_back(ih, aux::read_v4_endpoint<udp::endpoint>(v4_ptr));
		}
		char const* v6_ptr = alloc.ptr(v6_nodes_idx);
		for (int i = 0; i < v6_num_nodes; i++)
		{
			sha1_hash ih;
			std::memcpy(ih.data(), v6_ptr, 20);
			v6_ptr += 20;
			nodes.emplace_back(ih, aux::read_v6_endpoint<udp::endpoint>(v6_ptr));
		}

		return std::move(nodes);
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[200];
		std::snprintf(msg, sizeof(msg), "dht live nodes for id: %s, nodes %d"
			, aux::to_hex(node_id).c_str(), num_nodes());
		return msg;
#endif
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
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
#endif
	}

	dht_sample_infohashes_alert::dht_sample_infohashes_alert(aux::stack_allocator& alloc
		, sha1_hash const& nid
		, udp::endpoint const& endp
		, time_duration _interval
		, int _num
		, std::vector<sha1_hash> const& samples
		, std::vector<std::pair<sha1_hash, udp::endpoint>> const& nodes)
		: node_id(nid)
		, endpoint(endp)
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char msg[200];
		std::snprintf(msg, sizeof(msg)
			, "incoming dht sample_infohashes reply from: %s, samples %d"
			, print_endpoint(endpoint).c_str(), m_num_samples);
		return msg;
#endif
	}

	int dht_sample_infohashes_alert::num_samples() const
	{
		return m_num_samples;
	}

	std::vector<sha1_hash> dht_sample_infohashes_alert::samples() const
	{
		aux::vector<sha1_hash> samples;
		samples.resize(m_num_samples);

		char const* ptr = m_alloc.get().ptr(m_samples_idx);
		std::memcpy(samples.data(), ptr, samples.size() * 20);

		return std::move(samples);
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
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char ret[200];
		snprintf(ret, sizeof(ret), "%s block uploaded to a peer (piece: %d block: %d)"
			, peer_alert::message().c_str(), static_cast<int>(piece_index), block_index);
		return ret;
#endif
	}

	alerts_dropped_alert::alerts_dropped_alert(aux::stack_allocator&
		, std::bitset<abi_alert_count> const& dropped)
		: dropped_alerts(dropped)
	{}

	char const* alert_name(int const alert_type)
	{
		static std::array<char const*, num_alert_types> const names = {{
#if TORRENT_ABI_VERSION == 1
		"torrent", "peer", "tracker", "torrent_added",
#else
		"", "", "", "",
#endif
		"torrent_removed", "read_piece", "file_completed",
		"file_renamed", "file_rename_failed", "performance",
		"state_changed", "tracker_error", "tracker_warning",
		"scrape_reply", "scrape_failed", "tracker_reply",
		"dht_reply", "tracker_announce", "hash_failed",
		"peer_ban", "peer_unsnubbed", "peer_snubbed",
		"peer_error", "peer_connect", "peer_disconnected",
		"invalid_request", "torrent_finished", "piece_finished",
		"request_dropped", "block_timeout", "block_finished",
		"block_downloading", "unwanted_block", "storage_moved",
		"storage_moved_failed", "torrent_deleted",
		"torrent_delete_failed", "save_resume_data",
		"save_resume_data_failed", "torrent_paused",
		"torrent_resumed", "torrent_checked", "url_seed",
		"file_error", "metadata_failed", "metadata_received",
		"udp_error", "external_ip", "listen_failed",
		"listen_succeeded", "portmap_error", "portmap",
		"portmap_log", "fastresume_rejected", "peer_blocked",
		"dht_announce", "dht_get_peers", "stats",
		"cache_flushed", "anonymous_mode", "lsd_peer",
		"trackerid", "dht_bootstrap", "", "torrent_error",
		"torrent_need_cert", "incoming_connection",
		"add_torrent", "state_update",
#if TORRENT_ABI_VERSION == 1
		"mmap_cache",
#else
		"",
#endif
		"session_stats",
#if TORRENT_ABI_VERSION == 1
		"torrent_update",
#else
		"",
#endif
		"", "dht_error", "dht_immutable_item", "dht_mutable_item",
		"dht_put", "i2p", "dht_outgoing_get_peers", "log",
		"torrent_log", "peer_log", "lsd_error",
		"dht_stats", "incoming_request", "dht_log",
		"dht_pkt", "dht_get_peers_reply", "dht_direct_response",
		"picker_log", "session_error", "dht_live_nodes",
		"session_stats_header", "dht_sample_infohashes",
		"block_uploaded", "alerts_dropped", "socks5",
		"file_prio", "oversized_file", "torrent_conflict",
		"peer_info", "file_progress", "piece_info"
		}};

		TORRENT_ASSERT(alert_type >= 0);
		TORRENT_ASSERT(alert_type < num_alert_types);
		return names[std::size_t(alert_type)];
	}

	std::string alerts_dropped_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		std::string ret = "dropped alerts: ";

		TORRENT_ASSERT(int(dropped_alerts.size()) >= num_alert_types);
		for (int idx = 0; idx < num_alert_types; ++idx)
		{
			if (!dropped_alerts.test(std::size_t(idx))) continue;
			ret += alert_name(idx);
			ret += ' ';
		}

		return ret;
#endif
	}

	socks5_alert::socks5_alert(aux::stack_allocator&
		, tcp::endpoint const& ep, operation_t operation, error_code const& ec)
		: error(ec)
		, op(operation)
		, ip(ep)
	{}

	std::string socks5_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		char buf[512];
		std::snprintf(buf, sizeof(buf), "SOCKS5 error. op: %s ec: %s ep: %s"
			, operation_name(op), error.message().c_str(), print_endpoint(ip).c_str());
		return buf;
#endif
	}

	file_prio_alert::file_prio_alert(aux::stack_allocator& a, torrent_handle h)
		: torrent_alert(a, std::move(h))
	{}

	std::string file_prio_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " file priorities updated";
#endif
	}

	oversized_file_alert::oversized_file_alert(aux::stack_allocator& a, torrent_handle h)
		: torrent_alert(a, std::move(h))
	{}

	std::string oversized_file_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " has an oversized file";
#endif
	}

	torrent_conflict_alert::torrent_conflict_alert(aux::stack_allocator& alloc, torrent_handle h1
		, torrent_handle h2, std::shared_ptr<torrent_info> ti)
		: torrent_alert(alloc, h1)
		, conflicting_torrent(h2)
		, metadata(std::move(ti))
	{}

	std::string torrent_conflict_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " has a conflict with another torrent";
#endif
	}

	peer_info_alert::peer_info_alert(aux::stack_allocator& alloc, torrent_handle h
		, std::vector<lt::peer_info> p)
		: torrent_alert(alloc, std::move(h))
		, peer_info(std::move(p))
	{}

	std::string peer_info_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " peer_info";
#endif
	}

	file_progress_alert::file_progress_alert(aux::stack_allocator& alloc, torrent_handle h
		, aux::vector<std::int64_t, file_index_t> fp)
		: torrent_alert(alloc, std::move(h))
		, files(std::move(fp))
	{}

	std::string file_progress_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " file_progress";
#endif
	}

	piece_info_alert::piece_info_alert(aux::stack_allocator& alloc, torrent_handle h
			, std::vector<partial_piece_info> pi, std::vector<block_info>&& bd)
		: torrent_alert(alloc, std::move(h))
		, piece_info(std::move(pi))
		, block_data(std::move(bd))
	{}

	std::string piece_info_alert::message() const
	{
#ifdef TORRENT_DISABLE_ALERT_MSG
		return {};
#else
		return torrent_alert::message() + " piece_info";
#endif
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
#if TORRENT_ABI_VERSION <= 2
	constexpr alert_category_t stats_alert::static_category;
#endif
	constexpr alert_category_t cache_flushed_alert::static_category;
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
	constexpr alert_category_t alerts_dropped_alert::static_category;
	constexpr alert_category_t socks5_alert::static_category;
	constexpr alert_category_t file_prio_alert::static_category;
	constexpr alert_category_t oversized_file_alert::static_category;
	constexpr alert_category_t torrent_conflict_alert::static_category;
	constexpr alert_category_t peer_info_alert::static_category;
	constexpr alert_category_t file_progress_alert::static_category;
	constexpr alert_category_t piece_info_alert::static_category;
#if TORRENT_ABI_VERSION == 1
	constexpr alert_category_t anonymous_mode_alert::static_category;
	constexpr alert_category_t mmap_cache_alert::static_category;
	constexpr alert_category_t torrent_added_alert::static_category;
#endif

} // namespace libtorrent
