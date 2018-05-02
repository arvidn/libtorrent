/*

Copyright (c) 2003-2018, Arvid Norberg, Daniel Wallin
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

#include "libtorrent/config.hpp"
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/aux_/time.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/stack_allocator.hpp"
#include "libtorrent/piece_picker.hpp" // for piece_block

#include "libtorrent/aux_/escape_string.hpp" // for convert_from_native

#include <boost/bind.hpp>

namespace libtorrent {

	alert::alert() : m_timestamp(clock_type::now()) {}
	alert::~alert() {}
	time_point alert::timestamp() const { return m_timestamp; }

	torrent_alert::torrent_alert(aux::stack_allocator& alloc
		, torrent_handle const& h)
		: handle(h)
		, m_alloc(alloc)
	{
		boost::shared_ptr<torrent> t = h.native_handle();
		if (t)
		{
			std::string name_str = t->name();
			if (!name_str.empty()) {
				m_name_idx = alloc.copy_string(name_str);
			}
			else
			{
				char msg[41];
				to_hex(t->info_hash().data(), 20, msg);
				m_name_idx = alloc.copy_string(msg);
			}
		}
		else
		{
			m_name_idx = alloc.copy_string("");
		}

#ifndef TORRENT_NO_DEPRECATE
		name = m_alloc.ptr(m_name_idx);
#endif
	}

	char const* torrent_alert::torrent_name() const
	{
		return m_alloc.ptr(m_name_idx);
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
		, ip(i)
		, pid(pi)
	{}

	std::string peer_alert::message() const
	{
		error_code ec;
		return torrent_alert::message() + " peer (" + print_endpoint(ip)
			+ ", " + identify_client(pid) + ")";
	}

	tracker_alert::tracker_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, std::string const& u)
		: torrent_alert(alloc, h)
#ifndef TORRENT_NO_DEPRECATE
		, url(u)
#endif
		, m_url_idx(alloc.copy_string(u))
	{}

	char const* tracker_alert::tracker_url() const
	{
		return m_alloc.ptr(m_url_idx);
	}

	std::string tracker_alert::message() const
	{
		return torrent_alert::message() + " (" + tracker_url() + ")";
	}

	read_piece_alert::read_piece_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, int p, boost::shared_array<char> d, int s)
		: torrent_alert(alloc, h)
		, buffer(d)
		, piece(p)
		, size(s)
	{}

	read_piece_alert::read_piece_alert(aux::stack_allocator& alloc
		, torrent_handle h, int p, error_code e)
		: torrent_alert(alloc, h)
		, ec(e)
		, piece(p)
		, size(0)
	{}

	std::string read_piece_alert::message() const
	{
		char msg[200];
		if (ec)
		{
			snprintf(msg, sizeof(msg), "%s: read_piece %u failed: %s"
				, torrent_alert::message().c_str() , piece
				, convert_from_native(ec.message()).c_str());
		}
		else
		{
			snprintf(msg, sizeof(msg), "%s: read_piece %u successful"
				, torrent_alert::message().c_str() , piece);
		}
		return msg;
	}

	file_completed_alert::file_completed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, int idx)
		: torrent_alert(alloc, h)
		, index(idx)
	{}

	std::string file_completed_alert::message() const
	{
		char msg[200 + TORRENT_MAX_PATH];
		snprintf(msg, sizeof(msg), "%s: file %d finished downloading"
			, torrent_alert::message().c_str(), index);
		return msg;
	}

	file_renamed_alert::file_renamed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, std::string const& n
		, int idx)
		: torrent_alert(alloc, h)
#ifndef TORRENT_NO_DEPRECATE
		, name(n)
#endif
		, index(idx)
		, m_name_idx(alloc.copy_string(n))
	{}

	char const* file_renamed_alert::new_name() const
	{
		return m_alloc.ptr(m_name_idx);
	}

	std::string file_renamed_alert::message() const
	{
		char msg[200 + TORRENT_MAX_PATH * 2];
		snprintf(msg, sizeof(msg), "%s: file %d renamed to %s"
			, torrent_alert::message().c_str(), index, new_name());
		return msg;
	}

	file_rename_failed_alert::file_rename_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, int idx
		, error_code ec)
		: torrent_alert(alloc, h)
		, index(idx)
		, error(ec)
	{}

	std::string file_rename_failed_alert::message() const
	{
		char ret[200 + TORRENT_MAX_PATH * 2];
		snprintf(ret, sizeof(ret), "%s: failed to rename file %d: %s"
			, torrent_alert::message().c_str(), index, convert_from_native(error.message()).c_str());
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
		, torrent_handle const& h
		, int times
		, int status
		, std::string const& u
		, error_code const& e
		, std::string const& m)
		: tracker_alert(alloc, h, u)
		, times_in_row(times)
		, status_code(status)
		, error(e)
#ifndef TORRENT_NO_DEPRECATE
		, msg(m)
#endif
		, m_msg_idx(alloc.copy_string(m))
	{
		TORRENT_ASSERT(!u.empty());
	}

	char const* tracker_error_alert::error_message() const
	{
		return m_alloc.ptr(m_msg_idx);
	}

	std::string tracker_error_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s (%d) %s \"%s\" (%d)"
			, tracker_alert::message().c_str(), status_code
			, convert_from_native(error.message()).c_str(), error_message()
			, times_in_row);
		return ret;
	}

	tracker_warning_alert::tracker_warning_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, std::string const& u
		, std::string const& m)
		: tracker_alert(alloc, h, u)
#ifndef TORRENT_NO_DEPRECATE
		, msg(m)
#endif
		, m_msg_idx(alloc.copy_string(m))
	{
		TORRENT_ASSERT(!u.empty());
	}

	char const* tracker_warning_alert::warning_message() const
	{
		return m_alloc.ptr(m_msg_idx);
	}

	std::string tracker_warning_alert::message() const
	{
		return tracker_alert::message() + " warning: " + warning_message();
	}

	scrape_reply_alert::scrape_reply_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, int incomp
		, int comp
		, std::string const& u)
		: tracker_alert(alloc, h, u)
		, incomplete(incomp)
		, complete(comp)
	{
		TORRENT_ASSERT(!u.empty());
	}

	std::string scrape_reply_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s scrape reply: %u %u"
			, tracker_alert::message().c_str(), incomplete, complete);
		return ret;
	}

	scrape_failed_alert::scrape_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, std::string const& u
		, error_code const& e)
		: tracker_alert(alloc, h, u)
#ifndef TORRENT_NO_DEPRECATE
		, msg(convert_from_native(e.message()))
#endif
		, error(e)
		, m_msg_idx(-1)
	{
		TORRENT_ASSERT(!u.empty());
	}

	scrape_failed_alert::scrape_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, std::string const& u
		, std::string const& m)
		: tracker_alert(alloc, h, u)
#ifndef TORRENT_NO_DEPRECATE
		, msg(m)
#endif
		, error(errors::tracker_failure)
		, m_msg_idx(alloc.copy_string(m))
	{
		TORRENT_ASSERT(!u.empty());
	}

	char const* scrape_failed_alert::error_message() const
	{
		if (m_msg_idx == -1) return "";
		else return m_alloc.ptr(m_msg_idx);
	}

	std::string scrape_failed_alert::message() const
	{
		return tracker_alert::message() + " scrape failed: " + error_message();
	}

	tracker_reply_alert::tracker_reply_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, int np
		, std::string const& u)
		: tracker_alert(alloc, h, u)
		, num_peers(np)
	{
		TORRENT_ASSERT(!u.empty());
	}

	std::string tracker_reply_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s received peers: %u"
			, tracker_alert::message().c_str(), num_peers);
		return ret;
	}

	dht_reply_alert::dht_reply_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, int np)
		: tracker_alert(alloc, h, "")
		, num_peers(np)
	{}

	std::string dht_reply_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s received DHT peers: %u"
			, tracker_alert::message().c_str(), num_peers);
		return ret;
	}

	tracker_announce_alert::tracker_announce_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, std::string const& u, int e)
		: tracker_alert(alloc, h, u)
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
		, int index)
		: torrent_alert(alloc, h)
		, piece_index(index)

	{
		TORRENT_ASSERT(index >= 0);
	}

	std::string hash_failed_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s hash for piece %u failed"
			, torrent_alert::message().c_str(), piece_index);
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
		snprintf(ret, sizeof(ret), "%s peer sent an invalid piece request "
			"(piece: %u start: %u len: %u)%s"
			, peer_alert::message().c_str(), request.piece, request.start
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
		, torrent_handle const& h, int piece_num)
		: torrent_alert(alloc, h)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(piece_index >= 0);
	}

	std::string piece_finished_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s piece: %u finished downloading"
			, torrent_alert::message().c_str(), piece_index);
		return ret;
	}

	request_dropped_alert::request_dropped_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
		, int piece_num)
		: peer_alert(alloc, h, ep, peer_id)
		, block_index(block_num)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);
	}

	std::string request_dropped_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s peer dropped block ( piece: %u block: %u)"
			, torrent_alert::message().c_str(), piece_index, block_index);
		return ret;
	}

	block_timeout_alert::block_timeout_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
		, int piece_num)
		: peer_alert(alloc, h, ep, peer_id)
		, block_index(block_num)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);
	}

	std::string block_timeout_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s peer timed out request ( piece: %u block: %u)"
			, torrent_alert::message().c_str(), piece_index, block_index);
		return ret;
	}

	block_finished_alert::block_finished_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep, peer_id const& peer_id, int block_num
		, int piece_num)
		: peer_alert(alloc, h, ep, peer_id)
		, block_index(block_num)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);
	}

	std::string block_finished_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s block finished downloading (piece: %u block: %u)"
			, torrent_alert::message().c_str(), piece_index, block_index);
		return ret;
	}

	block_downloading_alert::block_downloading_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep
		, peer_id const& peer_id, int block_num, int piece_num)
		: peer_alert(alloc, h, ep, peer_id)
#ifndef TORRENT_NO_DEPRECATE
		, peer_speedmsg("")
#endif
		, block_index(block_num)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);
	}

	std::string block_downloading_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s requested block (piece: %u block: %u)"
			, torrent_alert::message().c_str(), piece_index, block_index);
		return ret;
	}

	unwanted_block_alert::unwanted_block_alert(aux::stack_allocator& alloc, torrent_handle h
		, tcp::endpoint const& ep
		, peer_id const& peer_id, int block_num, int piece_num)
		: peer_alert(alloc, h, ep, peer_id)
		, block_index(block_num)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);
	}

	std::string unwanted_block_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s received block not in download queue (piece: %u block: %u)"
			, torrent_alert::message().c_str(), piece_index, block_index);
		return ret;
	}

	storage_moved_alert::storage_moved_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, std::string const& p)
		: torrent_alert(alloc, h)
#ifndef TORRENT_NO_DEPRECATE
		, path(p)
#endif
		, m_path_idx(alloc.copy_string(p))
	{}

	std::string storage_moved_alert::message() const
	{
		return torrent_alert::message() + " moved storage to: "
			+ storage_path();
	}

	char const* storage_moved_alert::storage_path() const
	{
		return m_alloc.ptr(m_path_idx);
	}

	storage_moved_failed_alert::storage_moved_failed_alert(
		aux::stack_allocator& alloc
		, torrent_handle const& h
		, error_code const& e
		, std::string const& f
		, char const* op)
		: torrent_alert(alloc, h)
		, error(e)
#ifndef TORRENT_NO_DEPRECATE
		, file(f)
#endif
		, operation(op)
		, m_file_idx(alloc.copy_string(f))
	{}

	char const* storage_moved_failed_alert::file_path() const
	{
		return m_alloc.ptr(m_file_idx);
	}

	std::string storage_moved_failed_alert::message() const
	{
		return torrent_alert::message() + " storage move failed. "
			+ (operation?operation:"") + " (" + file_path() + "): "
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
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
	}

	std::string torrent_delete_failed_alert::message() const
	{
		return torrent_alert::message() + " torrent deletion failed: "
			+convert_from_native(error.message());
	}

	save_resume_data_alert::save_resume_data_alert(aux::stack_allocator& alloc
		, boost::shared_ptr<entry> const& rd
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
		, resume_data(rd)
	{}

	std::string save_resume_data_alert::message() const
	{
		return torrent_alert::message() + " resume data generated";
	}

	save_resume_data_failed_alert::save_resume_data_failed_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, error_code const& e)
		: torrent_alert(alloc, h)
		, error(e)
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
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

	namespace
	{
		static char const* const sock_type_str[] =
		{
			"TCP", "TCP/SSL", "UDP", "I2P", "Socks5", "uTP/SSL"
		};

		static char const* const nat_type_str[] = {"NAT-PMP", "UPnP"};

		static char const* const protocol_str[] = {"TCP", "UDP"};

		static char const* const socket_type_str[] = {
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

		tcp::endpoint parse_interface(std::string const& iface, int port)
		{
			// ignore errors
			error_code ec;
			return tcp::endpoint(address::from_string(iface, ec), port);
		}
	}

	listen_failed_alert::listen_failed_alert(
		aux::stack_allocator& alloc
		, std::string const& iface
		, int prt
		, int op
		, error_code const& ec
		, socket_type_t t)
		: error(ec)
		, operation(op)
		, sock_type(t)
		, endpoint(parse_interface(iface, prt))
		, m_alloc(alloc)
		, m_interface_idx(alloc.copy_string(iface))
	{}

	char const* listen_failed_alert::listen_interface() const
	{
		return m_alloc.ptr(m_interface_idx);
	}

	std::string listen_failed_alert::message() const
	{
		static char const* op_str[] =
		{
			"parse_addr",
			"open",
			"bind",
			"listen",
			"get_peer_name",
			"accept"
		};
		char ret[300];
		snprintf(ret, sizeof(ret), "listening on %s : %s failed: [%s] [%s] %s"
			, listen_interface()
			, print_endpoint(endpoint).c_str()
			, op_str[operation]
			, sock_type_str[sock_type]
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
		, tcp::endpoint const& ep, socket_type_t t)
		: endpoint(ep)
		, sock_type(t)
	{}

	std::string listen_succeeded_alert::message() const
	{
		char const* type_str[] = { "TCP", "SSL/TCP", "UDP", "i2p", "socks5", "SSL/uTP" };
		char ret[200];
		snprintf(ret, sizeof(ret), "successfully listening on [%s] %s"
			, type_str[sock_type], print_endpoint(endpoint).c_str());
		return ret;
	}

	portmap_error_alert::portmap_error_alert(aux::stack_allocator&
		, int i, int t, error_code const& e)
		:  mapping(i), map_type(t), error(e)
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
	}

	std::string portmap_error_alert::message() const
	{
		return std::string("could not map port using ") + nat_type_str[map_type]
			+ ": " + convert_from_native(error.message());
	}

	portmap_alert::portmap_alert(aux::stack_allocator&, int i, int port, int t
		, int proto)
		: mapping(i), external_port(port), map_type(t), protocol(proto)
	{}

	std::string portmap_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "successfully mapped port using %s. external port: %s/%u"
			, nat_type_str[map_type], protocol_str[protocol], external_port);
		return ret;
	}

#ifndef TORRENT_DISABLE_LOGGING

	portmap_log_alert::portmap_log_alert(aux::stack_allocator& alloc, int t, const char* m)
		: map_type(t)
#ifndef TORRENT_NO_DEPRECATE
		, msg(m)
#endif
		, m_alloc(alloc)
		, m_log_idx(alloc.copy_string(m))
	{}

	char const* portmap_log_alert::log_message() const
	{
		return m_alloc.ptr(m_log_idx);
	}

	std::string portmap_log_alert::message() const
	{
		char ret[600];
		snprintf(ret, sizeof(ret), "%s: %s", nat_type_str[map_type]
			, log_message());
		return ret;
	}

#endif

	fastresume_rejected_alert::fastresume_rejected_alert(
		aux::stack_allocator& alloc
		, torrent_handle const& h
		, error_code const& ec
		, std::string const& f
		, char const* op)
		: torrent_alert(alloc, h)
		, error(ec)
#ifndef TORRENT_NO_DEPRECATE
		, file(f)
#endif
		, operation(op)
		, m_path_idx(alloc.copy_string(f))
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
	}

	std::string fastresume_rejected_alert::message() const
	{
		return torrent_alert::message() + " fast resume rejected. "
			+ (operation?operation:"") + "(" + file_path() + "): "
			+ convert_from_native(error.message());
	}

	char const* fastresume_rejected_alert::file_path() const
	{
		return m_alloc.ptr(m_path_idx);
	}

	peer_blocked_alert::peer_blocked_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, address const& i
		, int r)
		: torrent_alert(alloc, h)
		, ip(i)
		, reason(r)
	{}

	std::string peer_blocked_alert::message() const
	{
		error_code ec;
		char ret[600];
		char const* reason_str[] =
		{
			"ip_filter",
			"port_filter",
			"i2p_mixed",
			"privileged_ports",
			"utp_disabled",
			"tcp_disabled",
			"invalid_local_interface"
		};

		snprintf(ret, sizeof(ret), "%s: blocked peer: %s [%s]"
			, torrent_alert::message().c_str(), ip.to_string(ec).c_str()
			, reason_str[reason]);
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
		char ih_hex[41];
		to_hex(info_hash.data(), 20, ih_hex);
		char msg[200];
		snprintf(msg, sizeof(msg), "incoming dht announce: %s:%u (%s)"
			, ip.to_string(ec).c_str(), port, ih_hex);
		return msg;
	}

	dht_get_peers_alert::dht_get_peers_alert(aux::stack_allocator&
		, sha1_hash const& ih)
		: info_hash(ih)
	{}

	std::string dht_get_peers_alert::message() const
	{
		char ih_hex[41];
		to_hex(info_hash.data(), 20, ih_hex);
		char msg[200];
		snprintf(msg, sizeof(msg), "incoming dht get_peers: %s", ih_hex);
		return msg;
	}

	stats_alert::stats_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, int in, stat const& s)
		: torrent_alert(alloc, h)
		, interval(in)
	{
		transferred[upload_payload] = s[stat::upload_payload].counter();
		transferred[upload_protocol] = s[stat::upload_protocol].counter();
		transferred[download_payload] = s[stat::download_payload].counter();
		transferred[download_protocol] = s[stat::download_protocol].counter();
		transferred[upload_ip_protocol] = s[stat::upload_ip_protocol].counter();
		transferred[download_ip_protocol] = s[stat::download_ip_protocol].counter();

#ifndef TORRENT_NO_DEPRECATE
		transferred[upload_dht_protocol] = 0;
		transferred[upload_tracker_protocol] = 0;
		transferred[download_dht_protocol] = 0;
		transferred[download_tracker_protocol] = 0;
#else
		transferred[deprecated1] = 0;
		transferred[deprecated2] = 0;
		transferred[deprecated3] = 0;
		transferred[deprecated4] = 0;
#endif
	}

	std::string stats_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "%s: [%d] %d %d %d %d %d %d"
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
		, torrent_handle const& h, int k, std::string const& s)
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
		snprintf(msg, sizeof(msg), "%s: %s: %s"
			, torrent_alert::message().c_str()
			, msgs[kind], str.c_str());
		return msg;
	}

	lsd_peer_alert::lsd_peer_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, tcp::endpoint const& i)
		: peer_alert(alloc, h, i, peer_id(0))
	{}

	std::string lsd_peer_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "%s: received peer from local service discovery"
			, peer_alert::message().c_str());
		return msg;
	}

	trackerid_alert::trackerid_alert(
		aux::stack_allocator& alloc
		, torrent_handle const& h
		, std::string const& u
		, const std::string& id)
		: tracker_alert(alloc, h, u)
#ifndef TORRENT_NO_DEPRECATE
		, trackerid(id)
#endif
		, m_tracker_idx(alloc.copy_string(id))
	{}

	char const* trackerid_alert::tracker_id() const
	{
		return m_alloc.ptr(m_tracker_idx);
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

#ifndef TORRENT_NO_DEPRECATE
	rss_alert::rss_alert(aux::stack_allocator&, feed_handle h
		, std::string const& u, int s, error_code const& ec)
		: handle(h), url(u), state(s), error(ec)
	{}

	std::string rss_alert::message() const
	{
		char msg[600];
		char const* state_msg[] = {"updating", "updated", "error"};
		snprintf(msg, sizeof(msg), "RSS feed %s: %s (%s)"
			, url.c_str(), state_msg[state], convert_from_native(error.message()).c_str());
		return msg;
	}
#endif

	torrent_error_alert::torrent_error_alert(
		aux::stack_allocator& alloc
		, torrent_handle const& h
		, error_code const& e, std::string const& f)
		: torrent_alert(alloc, h)
		, error(e)
#ifndef TORRENT_NO_DEPRECATE
		, error_file(f)
#endif
		, m_file_idx(alloc.copy_string(f))
	{}

	std::string torrent_error_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), " ERROR: %s", convert_from_native(error.message()).c_str());
		return torrent_alert::message() + msg;
	}

	char const* torrent_error_alert::filename() const
	{
		return m_alloc.ptr(m_file_idx);
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
		, ip(i)
	{}

	std::string incoming_connection_alert::message() const
	{
		char msg[600];
		error_code ec;
		snprintf(msg, sizeof(msg), "incoming connection from %s (%s)"
			, print_endpoint(ip).c_str(), socket_type_str[socket_type]);
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
		error_code ec;
		snprintf(msg, sizeof(msg), "%s connecting to peer (%s)"
			, peer_alert::message().c_str(), socket_type_str[socket_type]);
		return msg;
	}

	add_torrent_alert::add_torrent_alert(aux::stack_allocator& alloc, torrent_handle h
		, add_torrent_params const& p, error_code ec)
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
		else if (!params.url.empty()) torrent_name = params.url.c_str();
		else to_hex(params.info_hash.data(), 20, info_hash);

		if (error)
		{
			snprintf(msg, sizeof(msg), "failed to add torrent \"%s\": [%s] %s"
				, torrent_name, error.category().name()
				, convert_from_native(error.message()).c_str());
		}
		else
		{
			snprintf(msg, sizeof(msg), "added torrent: %s", torrent_name);
		}
		return msg;
	}

	state_update_alert::state_update_alert(aux::stack_allocator&
		, std::vector<torrent_status> st)
		: status(st)
	{}

	std::string state_update_alert::message() const
	{
		char msg[600];
		snprintf(msg, sizeof(msg), "state updates for %d torrents", int(status.size()));
		return msg;
	}

#ifndef TORRENT_NO_DEPRECATE
	mmap_cache_alert::mmap_cache_alert(aux::stack_allocator&
		, error_code const& ec): error(ec)
	{}

	std::string mmap_cache_alert::message() const
	{
		char msg[600];
		snprintf(msg, sizeof(msg), "mmap cache failed: (%d) %s", error.value()
			, convert_from_native(error.message()).c_str());
		return msg;
	}
#endif

	peer_error_alert::peer_error_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, tcp::endpoint const& ep, peer_id const& peer_id, int op
		, error_code const& e)
		: peer_alert(alloc, h, ep, peer_id)
		, operation(op)
		, error(e)
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
	}

	std::string peer_error_alert::message() const
	{
		char buf[200];
		snprintf(buf, sizeof(buf), "%s peer error [%s] [%s]: %s"
			, peer_alert::message().c_str()
			, operation_name(operation), error.category().name()
			, convert_from_native(error.message()).c_str());
		return buf;
	}

	char const* operation_name(int op)
	{
		static char const* names[] = {
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
		};

		if (op < 0 || op >= int(sizeof(names)/sizeof(names[0])))
			return "unknown operation";

		return names[op];
	}

	torrent_update_alert::torrent_update_alert(aux::stack_allocator& alloc, torrent_handle h
		, sha1_hash const& old_hash, sha1_hash const& new_hash)
		: torrent_alert(alloc, h)
		, old_ih(old_hash)
		, new_ih(new_hash)
	{}

	std::string torrent_update_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), " torrent changed info-hash from: %s to %s"
			, to_hex(old_ih.to_string()).c_str()
			, to_hex(new_ih.to_string()).c_str());
		return torrent_alert::message() + msg;
	}

#ifndef TORRENT_NO_DEPRECATE
	rss_item_alert::rss_item_alert(aux::stack_allocator&, feed_handle h
		, feed_item const& i)
		: handle(h)
		, item(i)
	{}

	std::string rss_item_alert::message() const
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "feed [%s] has new RSS item %s"
			, handle.get_feed_status().title.c_str()
			, item.title.empty() ? item.url.c_str() : item.title.c_str());
		return msg;
	}
#endif

	peer_disconnected_alert::peer_disconnected_alert(aux::stack_allocator& alloc
		, torrent_handle const& h, tcp::endpoint const& ep
		, peer_id const& peer_id, operation_t op, int type, error_code const& e
		, close_reason_t r)
		: peer_alert(alloc, h, ep, peer_id)
		, socket_type(type)
		, operation(op)
		, error(e)
		, reason(r)
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
	}

	std::string peer_disconnected_alert::message() const
	{
		char buf[600];
		snprintf(buf, sizeof(buf), "%s disconnecting (%s) [%s] [%s]: %s (reason: %d)"
			, peer_alert::message().c_str()
			, socket_type_str[socket_type]
			, operation_name(operation), error.category().name()
			, convert_from_native(error.message()).c_str()
			, int(reason));
		return buf;
	}

	dht_error_alert::dht_error_alert(aux::stack_allocator&, int op
		, error_code const& ec)
		: error(ec), operation(op_t(op))
	{}

	std::string dht_error_alert::message() const
	{
		static const char* const operation_names[] =
		{
			"unknown",
			"hostname lookup"
		};

		int op = operation;
		if (op < 0 || op >= int(sizeof(operation_names)/sizeof(operation_names[0])))
			op = 0;

		char msg[600];
		snprintf(msg, sizeof(msg), "DHT error [%s] (%d) %s"
			, operation_names[op]
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
		snprintf(msg, sizeof(msg), "DHT immutable item %s [ %s ]"
			, to_hex(target.to_string()).c_str()
			, item.to_string().c_str());
		return msg;
	}

	// TODO: 2 the salt here is allocated on the heap. It would be nice to
	// allocate in in the stack_allocator
	dht_mutable_item_alert::dht_mutable_item_alert(aux::stack_allocator&
		, boost::array<char, 32> k
		, boost::array<char, 64> sig
		, boost::uint64_t sequence
		, std::string const& s
		, entry const& i
		, bool a)
		: key(k), signature(sig), seq(sequence), salt(s), item(i), authoritative(a)
	{}

	std::string dht_mutable_item_alert::message() const
	{
		char msg[1050];
		snprintf(msg, sizeof(msg), "DHT mutable item (key=%s salt=%s seq=%" PRId64 " %s) [ %s ]"
			, to_hex(std::string(&key[0], 32)).c_str()
			, salt.c_str()
			, seq
			, authoritative ? "auth" : "non-auth"
			, item.to_string().c_str());
		return msg;
	}

	dht_put_alert::dht_put_alert(aux::stack_allocator&, sha1_hash const& t, int n)
		: target(t)
		, seq(0)
		, num_success(n)
	{}

	dht_put_alert::dht_put_alert(aux::stack_allocator&
		, boost::array<char, 32> key
		, boost::array<char, 64> sig
		, std::string s
		, boost::uint64_t sequence_number
		, int n)
		: target(0)
		, public_key(key)
		, signature(sig)
		, salt(s)
		, seq(sequence_number)
		, num_success(n)
	{}

	std::string dht_put_alert::message() const
	{
		char msg[1050];
		if (target.is_all_zeros())
		{
			snprintf(msg, sizeof(msg), "DHT put complete (success=%d key=%s sig=%s salt=%s seq=%" PRId64 ")"
				, num_success
				, to_hex(std::string(&public_key[0], 32)).c_str()
				, to_hex(std::string(&signature[0], 64)).c_str()
				, salt.c_str()
				, seq);
			return msg;
		}

		snprintf(msg, sizeof(msg), "DHT put commplete (success=%d hash=%s)"
			, num_success
			, to_hex(target.to_string()).c_str());
		return msg;
	}

	i2p_alert::i2p_alert(aux::stack_allocator&, error_code const& ec)
		: error(ec)
	{}

	std::string i2p_alert::message() const
	{
		char msg[600];
		snprintf(msg, sizeof(msg), "i2p_error: [%s] %s"
			, error.category().name(), convert_from_native(error.message()).c_str());
		return msg;
	}

	dht_outgoing_get_peers_alert::dht_outgoing_get_peers_alert(aux::stack_allocator&
		, sha1_hash const& ih, sha1_hash const& obfih
		, udp::endpoint ep)
		: info_hash(ih)
		, obfuscated_info_hash(obfih)
		, ip(ep)
	{}

	std::string dht_outgoing_get_peers_alert::message() const
	{
		char msg[600];
		char obf[70];
		obf[0] = '\0';
		if (obfuscated_info_hash != info_hash)
		{
			snprintf(obf, sizeof(obf), " [obfuscated: %s]"
			, to_hex(obfuscated_info_hash.to_string()).c_str());
		}
		snprintf(msg, sizeof(msg), "outgoing dht get_peers : %s%s -> %s"
			, to_hex(info_hash.to_string()).c_str()
			, obf
			, print_endpoint(ip).c_str());
		return msg;
	}

#ifndef TORRENT_DISABLE_LOGGING

	log_alert::log_alert(aux::stack_allocator& alloc, char const* log)
		: m_alloc(alloc)
		, m_str_idx(alloc.copy_string(log))
	{}

	char const* log_alert::msg() const
	{
		return m_alloc.ptr(m_str_idx);
	}

	std::string log_alert::message() const
	{
		return msg();
	}

	torrent_log_alert::torrent_log_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, char const* log)
		: torrent_alert(alloc, h)
		, m_str_idx(alloc.copy_string(log))
	{}

	char const* torrent_log_alert::msg() const
	{
		return m_alloc.ptr(m_str_idx);
	}

	std::string torrent_log_alert::message() const
	{
		return torrent_alert::message() + ": " + msg();
	}

	peer_log_alert::peer_log_alert(aux::stack_allocator& alloc
		, torrent_handle const& h
		, tcp::endpoint const& i
		, peer_id const& pi
		, direction_t dir
		, char const* event
		, char const* log)
		: peer_alert(alloc, h, i, pi)
		, event_type(event)
		, direction(dir)
		, m_str_idx(alloc.copy_string(log))
	{}

	char const* peer_log_alert::msg() const
	{
		return m_alloc.ptr(m_str_idx);
	}

	std::string peer_log_alert::message() const
	{
		static char const* mode[] =
		{ "<==", "==>", "<<<", ">>>", "***" };
		return torrent_alert::message() + " [" + print_endpoint(ip) + "] "
			+ mode[direction] + " " + event_type + " [ " + msg() + " ]";
	}

#endif

	lsd_error_alert::lsd_error_alert(aux::stack_allocator&, error_code const& ec)
		: alert()
		, error(ec)
	{}

	std::string lsd_error_alert::message() const
	{
		return "Local Service Discovery error: " + error.message();
	}

	session_stats_alert::session_stats_alert(aux::stack_allocator&, counters const& cnt)
	{
		for (int i = 0; i < counters::num_counters; ++i)
			values[i] = cnt[i];
	}

	std::string session_stats_alert::message() const
	{
		// this specific output is parsed by tools/parse_session_stats.py
		// if this is changed, that parser should also be changed
		char msg[100];
		snprintf(msg, sizeof(msg), "session stats (%d values): "
			, int(sizeof(values)/sizeof(values[0])));
		std::string ret = msg;
		bool first = true;
		for (int i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
		{
			snprintf(msg, sizeof(msg), first ? "%" PRIu64 : ", %" PRIu64, values[i]);
			first = false;
			ret += msg;
		}
		return ret;
	}

	dht_stats_alert::dht_stats_alert(aux::stack_allocator&
		, std::vector<dht_routing_bucket> const& table
		, std::vector<dht_lookup> const& requests)
		: alert()
		, active_requests(requests)
		, routing_table(table)
	{}

	std::string dht_stats_alert::message() const
	{
		char buf[2048];
		snprintf(buf, sizeof(buf), "DHT stats: reqs: %d buckets: %d"
			, int(active_requests.size())
			, int(routing_table.size()));
		return buf;
	}

	url_seed_alert::url_seed_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, std::string const& u, error_code const& e)
		: torrent_alert(alloc, h)
#ifndef TORRENT_NO_DEPRECATE
		, url(u)
		, msg(convert_from_native(e.message()))
#endif
		, error(e)
		, m_url_idx(alloc.copy_string(u))
		, m_msg_idx(-1)
	{}

	url_seed_alert::url_seed_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, std::string const& u, std::string const& m)
		: torrent_alert(alloc, h)
#ifndef TORRENT_NO_DEPRECATE
		, url(u)
		, msg(m)
#endif
		, m_url_idx(alloc.copy_string(u))
		, m_msg_idx(alloc.copy_string(m))
	{}

	std::string url_seed_alert::message() const
	{
		return torrent_alert::message() + " url seed ("
			+ server_url() + ") failed: " + convert_from_native(error.message());
	}

	char const* url_seed_alert::server_url() const
	{
		return m_alloc.ptr(m_url_idx);
	}

	char const* url_seed_alert::error_message() const
	{
		if (m_msg_idx == -1) return "";
		return m_alloc.ptr(m_msg_idx);
	}

	file_error_alert::file_error_alert(aux::stack_allocator& alloc
		, error_code const& ec
		, std::string const& f
		, char const* op
		, torrent_handle const& h)
		: torrent_alert(alloc, h)
#ifndef TORRENT_NO_DEPRECATE
		, file(f)
#endif
		, error(ec)
		, operation(op)
		, m_file_idx(alloc.copy_string(f))
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
	}

	char const* file_error_alert::filename() const
	{
		return m_alloc.ptr(m_file_idx);
	}

	std::string file_error_alert::message() const
	{
		return torrent_alert::message() + " "
			+ (operation?operation:"") + " (" + filename()
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
		snprintf(msg, sizeof(msg), "%s: incoming request [ piece: %d start: %d length: %d ]"
			, peer_alert::message().c_str(), req.piece, req.start, req.length);
		return msg;
	}

	dht_log_alert::dht_log_alert(aux::stack_allocator& alloc
		, dht_log_alert::dht_module_t m, const char* msg)
		: module(m)
		, m_alloc(alloc)
		, m_msg_idx(alloc.copy_string(msg))
	{}

	char const* dht_log_alert::log_message() const
	{
		return m_alloc.ptr(m_msg_idx);
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
		snprintf(ret, sizeof(ret), "DHT %s: %s", dht_modules[module]
			, log_message());
		return ret;
	}

	dht_pkt_alert::dht_pkt_alert(aux::stack_allocator& alloc
		, char const* buf, int size, dht_pkt_alert::direction_t d, udp::endpoint ep)
		: dir(d)
		, node(ep)
		, m_alloc(alloc)
		, m_msg_idx(alloc.copy_buffer(buf, size))
		, m_size(size)
	{}

	char const* dht_pkt_alert::pkt_buf() const
	{
		return m_alloc.ptr(m_msg_idx);
	}

	int dht_pkt_alert::pkt_size() const
	{
		return m_size;
	}

	std::string dht_pkt_alert::message() const
	{
		bdecode_node print;
		error_code ec;

		// ignore errors here. This is best-effort. It may be a broken encoding
		// but at least we'll print the valid parts
		bdecode(pkt_buf(), pkt_buf() + pkt_size(), print, ec, NULL, 100, 100);

		std::string msg = print_entry(print, true);

		char const* prefix[2] = { "<==", "==>"};
		char buf[1024];
		snprintf(buf, sizeof(buf), "%s [%s] %s", prefix[dir]
			, print_endpoint(node).c_str(), msg.c_str());

		return buf;
	}

	dht_get_peers_reply_alert::dht_get_peers_reply_alert(aux::stack_allocator& alloc
		, sha1_hash const& ih
		, std::vector<tcp::endpoint> const& peers)
		: info_hash(ih)
		, m_alloc(alloc)
		, m_num_peers(peers.size())
	{
		std::size_t total_size = m_num_peers; // num bytes for sizes
		for (int i = 0; i < m_num_peers; i++) {
			total_size += peers[i].size();
		}

		m_peers_idx = alloc.allocate(total_size);

		char *ptr = alloc.ptr(m_peers_idx);
		for (int i = 0; i < m_num_peers; i++) {
			tcp::endpoint endp = peers[i];
			std::size_t size = endp.size();

			detail::write_uint8(size, ptr);
			memcpy(ptr, endp.data(), size);
			ptr += size;
		}
	}

	std::string dht_get_peers_reply_alert::message() const
	{
		char ih_hex[41];
		to_hex(info_hash.data(), 20, ih_hex);
		char msg[200];
		snprintf(msg, sizeof(msg), "incoming dht get_peers reply: %s, peers %d", ih_hex, m_num_peers);
		return msg;
	}

	int dht_get_peers_reply_alert::num_peers() const
	{
		return m_num_peers;
	}

#ifndef TORRENT_NO_DEPRECATE
	void dht_get_peers_reply_alert::peers(std::vector<tcp::endpoint> &v) const {
		std::vector<tcp::endpoint> p(peers());
		v.reserve(p.size());
		std::copy(p.begin(), p.end(), std::back_inserter(v));
    }
#endif
	std::vector<tcp::endpoint> dht_get_peers_reply_alert::peers() const {
		std::vector<tcp::endpoint> peers(m_num_peers);

		const char *ptr = m_alloc.ptr(m_peers_idx);
		for (int i = 0; i < m_num_peers; i++) {
			std::size_t size = detail::read_uint8(ptr);
			memcpy(peers[i].data(), ptr, size);
			ptr += size;
		}

		return peers;
	}

	dht_direct_response_alert::dht_direct_response_alert(
		aux::stack_allocator& alloc, void* userdata_
		, udp::endpoint const& addr_, bdecode_node const& response)
		: userdata(userdata_), addr(addr_), m_alloc(alloc)
		, m_response_idx(alloc.copy_buffer(response.data_section().first, response.data_section().second))
		, m_response_size(response.data_section().second)
	{}

	dht_direct_response_alert::dht_direct_response_alert(
		aux::stack_allocator& alloc
		, void* userdata_
		, udp::endpoint const& addr_)
		: userdata(userdata_), addr(addr_), m_alloc(alloc)
		, m_response_idx(-1), m_response_size(0)
	{}

	std::string dht_direct_response_alert::message() const
	{
		char msg[1050];
		snprintf(msg, sizeof(msg), "DHT direct response (address=%s) [ %s ]"
			, addr.address().to_string().c_str()
			, m_response_size ? std::string(m_alloc.ptr(m_response_idx), m_response_size).c_str() : "");
		return msg;
	}

	bdecode_node dht_direct_response_alert::response() const
	{
		if (m_response_size == 0) return bdecode_node();
		char const* start = m_alloc.ptr(m_response_idx);
		char const* end = start + m_response_size;
		error_code ec;
		bdecode_node ret;
		bdecode(start, end, ret, ec);
		TORRENT_ASSERT(!ec);
		return ret;
	}

#ifndef TORRENT_DISABLE_LOGGING

	picker_log_alert::picker_log_alert(aux::stack_allocator& alloc, torrent_handle const& h
		, tcp::endpoint const& ep, peer_id const& peer_id, boost::uint32_t flags
		, piece_block const* blocks, int num_blocks)
		: peer_alert(alloc, h, ep, peer_id)
		, picker_flags(flags)
		, m_array_idx(alloc.copy_buffer(reinterpret_cast<char const*>(blocks)
			, num_blocks * sizeof(piece_block)))
		, m_num_blocks(num_blocks)
	{}

	std::vector<piece_block> picker_log_alert::blocks() const
	{
		// we need to copy this array to make sure the structures are properly
		// aigned, not just to have a nice API
		std::vector<piece_block> ret;
		ret.resize(m_num_blocks);

		char const* start = m_alloc.ptr(m_array_idx);
		memcpy(&ret[0], start, m_num_blocks * sizeof(piece_block));

		return ret;
	}

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

		boost::uint32_t flags = picker_flags;
		int idx = 0;
		ret += " picker_log [ ";
		for (; flags != 0; flags >>= 1, ++idx)
		{
			if ((flags & 1) == 0) continue;
			ret += flag_names[idx];
		}
		ret += "] ";

		std::vector<piece_block> b = blocks();

		for (int i = 0; i < int(b.size()); ++i)
		{
			char buf[50];
			snprintf(buf, sizeof(buf), "(%d,%d) "
				, b[i].piece_index, b[i].block_index);
			ret += buf;
		}
		return ret;
	}

#endif // TORRENT_DISABLE_LOGGING

} // namespace libtorrent

