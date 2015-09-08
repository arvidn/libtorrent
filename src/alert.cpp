/*

Copyright (c) 2003-2014, Arvid Norberg, Daniel Wallin
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
#include "libtorrent/escape_string.hpp"
#include "libtorrent/extensions.hpp"
#include <boost/bind.hpp>

namespace libtorrent {

	alert::alert() : m_timestamp(time_now()) {}
	alert::~alert() {}
	ptime alert::timestamp() const { return m_timestamp; }

	torrent_alert::torrent_alert(torrent_handle const& h)
		: handle(h)
	{}

	std::string torrent_alert::message() const
	{
		if (!handle.is_valid()) return " - ";
		torrent_status st = handle.status(torrent_handle::query_name);
		if (st.name.empty())
		{
			char msg[41];
			to_hex((char const*)&st.info_hash[0], 20, msg);
			return msg;
		}
		return st.name;
	}

	peer_alert::peer_alert(torrent_handle const& h, tcp::endpoint const& i
		, peer_id const& pi)
		: torrent_alert(h)
		, ip(i)
		, pid(pi)
	{}

	std::string peer_alert::message() const
	{
		error_code ec;
		return torrent_alert::message() + " peer (" + print_endpoint(ip)
			+ ", " + identify_client(pid) + ")";
	}

	tracker_alert::tracker_alert(torrent_handle const& h
		, std::string const& u)
		: torrent_alert(h)
		, url(u)
	{}

	std::string tracker_alert::message() const
	{
		return torrent_alert::message() + " (" + url + ")";
	}

	read_piece_alert::read_piece_alert(torrent_handle const& h
		, int p, boost::shared_array<char> d, int s)
		: torrent_alert(h)
		, buffer(d)
		, piece(p)
		, size(s)
	{}

	read_piece_alert::read_piece_alert(torrent_handle h, int p, error_code e)
		: torrent_alert(h)
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

	file_completed_alert::file_completed_alert(torrent_handle const& h
		, int idx)
		: torrent_alert(h)
		, index(idx)
	{}

	std::string file_completed_alert::message() const
	{
		char msg[200 + TORRENT_MAX_PATH];
		snprintf(msg, sizeof(msg), "%s: file %d finished downloading"
			, torrent_alert::message().c_str(), index);
		return msg;
	}

	file_renamed_alert::file_renamed_alert(torrent_handle const& h
		, std::string const& n
		, int idx)
		: torrent_alert(h)
		, name(n)
		, index(idx)
	{}

	std::string file_renamed_alert::message() const
	{
		char msg[200 + TORRENT_MAX_PATH * 2];
		snprintf(msg, sizeof(msg), "%s: file %d renamed to %s", torrent_alert::message().c_str()
			, index, name.c_str());
		return msg;
	}

	file_rename_failed_alert::file_rename_failed_alert(torrent_handle const& h
		, int idx
		, error_code ec)
		: torrent_alert(h)
		, index(idx)
		, error(ec)
	{}

	std::string file_rename_failed_alert::message() const
	{
		char ret[200 + TORRENT_MAX_PATH * 2];
		snprintf(ret, sizeof(ret), "%s: failed to rename file %d: %s"
			, torrent_alert::message().c_str(), index
			, convert_from_native(error.message()).c_str());
		return ret;
	}

	performance_alert::performance_alert(torrent_handle const& h
		, performance_warning_t w)
		: torrent_alert(h)
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
			"too few ports allowed for outgoing connections",
			"too few file descriptors are allowed for this process. connection limit lowered"
		};

		return torrent_alert::message() + ": performance warning: "
			+ warning_str[warning_code];
	}

	state_changed_alert::state_changed_alert(torrent_handle const& h
		, torrent_status::state_t st
		, torrent_status::state_t prev_st)
		: torrent_alert(h)
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

	tracker_error_alert::tracker_error_alert(torrent_handle const& h
		, int times
		, int status
		, std::string const& u
		, error_code const& e
		, std::string const& m)
		: tracker_alert(h, u)
		, times_in_row(times)
		, status_code(status)
		, error(e)
		, msg(m)
	{
		TORRENT_ASSERT(!url.empty());
	}

	std::string tracker_error_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s (%d) %s \"%s\" (%d)"
			, tracker_alert::message().c_str(), status_code
			, convert_from_native(error.message()).c_str(), msg.c_str(), times_in_row);
		return ret;
	}

	tracker_warning_alert::tracker_warning_alert(torrent_handle const& h
		, std::string const& u
		, std::string const& m)
		: tracker_alert(h, u)
		, msg(m)
	{ TORRENT_ASSERT(!url.empty()); }

	std::string tracker_warning_alert::message() const
	{
		return tracker_alert::message() + " warning: " + msg;
	}

	scrape_reply_alert::scrape_reply_alert(torrent_handle const& h
		, int incomp
		, int comp
		, std::string const& u)
		: tracker_alert(h, u)
		, incomplete(incomp)
		, complete(comp)
	{
		TORRENT_ASSERT(!url.empty());
	}

	std::string scrape_reply_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s scrape reply: %u %u"
			, tracker_alert::message().c_str(), incomplete, complete);
		return ret;
	}

	scrape_failed_alert::scrape_failed_alert(torrent_handle const& h
		, std::string const& u
		, error_code const& e)
		: tracker_alert(h, u)
		, msg(convert_from_native(e.message()))
	{
		TORRENT_ASSERT(!url.empty());
	}

	scrape_failed_alert::scrape_failed_alert(torrent_handle const& h
		, std::string const& u
		, std::string const& m)
		: tracker_alert(h, u)
		, msg(m)
	{
		TORRENT_ASSERT(!url.empty());
	}

	std::string scrape_failed_alert::message() const
	{
		return tracker_alert::message() + " scrape failed: " + msg;
	}

	tracker_reply_alert::tracker_reply_alert(torrent_handle const& h
		, int np
		, std::string const& u)
		: tracker_alert(h, u)
		, num_peers(np)
	{
		TORRENT_ASSERT(!url.empty());
	}

	std::string tracker_reply_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s received peers: %u"
			, tracker_alert::message().c_str(), num_peers);
		return ret;
	}

	dht_reply_alert::dht_reply_alert(torrent_handle const& h
		, int np)
		: tracker_alert(h, "")
		, num_peers(np)
	{}

	std::string dht_reply_alert::message() const
	{
		char ret[400];
		snprintf(ret, sizeof(ret), "%s received DHT peers: %u"
			, tracker_alert::message().c_str(), num_peers);
		return ret;
	}

	tracker_announce_alert::tracker_announce_alert(torrent_handle const& h
		, std::string const& u, int e)
		: tracker_alert(h, u)
		, event(e)
	{
		TORRENT_ASSERT(!url.empty());
	}

	std::string tracker_announce_alert::message() const
	{
		const static char* event_str[] = {"none", "completed", "started", "stopped", "paused"};
		TORRENT_ASSERT_VAL(event < int(sizeof(event_str)/sizeof(event_str[0])), event);
		return tracker_alert::message() + " sending announce (" + event_str[event] + ")";
	}

	hash_failed_alert::hash_failed_alert(
		torrent_handle const& h
		, int index)
		: torrent_alert(h)
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

	peer_ban_alert::peer_ban_alert(torrent_handle h, tcp::endpoint const& ep
		, peer_id const& peer_id)
		: peer_alert(h, ep, peer_id)
	{}

	std::string peer_ban_alert::message() const
	{
		return peer_alert::message() + " banned peer";
	}

	peer_unsnubbed_alert::peer_unsnubbed_alert(torrent_handle h, tcp::endpoint const& ep
		, peer_id const& peer_id)
		: peer_alert(h, ep, peer_id)
	{}

	std::string peer_unsnubbed_alert::message() const
	{
		return peer_alert::message() + " peer unsnubbed";
	}

	peer_snubbed_alert::peer_snubbed_alert(torrent_handle h, tcp::endpoint const& ep
		, peer_id const& peer_id)
		: peer_alert(h, ep, peer_id)
	{}

	std::string peer_snubbed_alert::message() const
	{
		return peer_alert::message() + " peer snubbed";
	}

	peer_error_alert::peer_error_alert(torrent_handle const& h, tcp::endpoint const& ep
		, peer_id const& peer_id, error_code const& e)
		: peer_alert(h, ep, peer_id)
		, error(e)
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
	}

	std::string peer_error_alert::message() const
	{
		return peer_alert::message() + " peer error: " + convert_from_native(error.message());
	}

	invalid_request_alert::invalid_request_alert(torrent_handle const& h, tcp::endpoint const& ep
		, peer_id const& peer_id, peer_request const& r)
		: peer_alert(h, ep, peer_id)
		, request(r)
	{}

	std::string invalid_request_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s peer sent an invalid piece request (piece: %u start: %u len: %u)"
			, peer_alert::message().c_str(), request.piece, request.start, request.length);
		return ret;
	}

	torrent_finished_alert::torrent_finished_alert(
		const torrent_handle& h)
		: torrent_alert(h)
	{}

	std::string torrent_finished_alert::message() const
	{
		return torrent_alert::message() + " torrent finished downloading";
	}

	piece_finished_alert::piece_finished_alert(
		const torrent_handle& h
		, int piece_num)
		: torrent_alert(h)
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

	request_dropped_alert::request_dropped_alert(const torrent_handle& h, tcp::endpoint const& ep
		, peer_id const& peer_id, int block_num, int piece_num)
		: peer_alert(h, ep, peer_id)
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

	block_timeout_alert::block_timeout_alert(const torrent_handle& h, tcp::endpoint const& ep
		, peer_id const& peer_id, int block_num, int piece_num)
		: peer_alert(h, ep, peer_id)
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

	block_finished_alert::block_finished_alert(const torrent_handle& h, tcp::endpoint const& ep
		, peer_id const& peer_id, int block_num, int piece_num)
		: peer_alert(h, ep, peer_id)
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

	block_downloading_alert::block_downloading_alert(const torrent_handle& h, tcp::endpoint const& ep
		, peer_id const& peer_id, char const* speedmsg, int block_num, int piece_num)
		: peer_alert(h, ep, peer_id)
		, peer_speedmsg(speedmsg)
		, block_index(block_num)
		, piece_index(piece_num)
	{
		TORRENT_ASSERT(block_index >= 0 && piece_index >= 0);
	}

	std::string block_downloading_alert::message() const
	{
		char ret[200];
		snprintf(ret, sizeof(ret), "%s requested block (piece: %u block: %u) %s"
			, torrent_alert::message().c_str(), piece_index, block_index, peer_speedmsg);
		return ret;
	}

	unwanted_block_alert::unwanted_block_alert(const torrent_handle& h, tcp::endpoint const& ep
		, peer_id const& peer_id, int block_num, int piece_num)
		: peer_alert(h, ep, peer_id)
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

	storage_moved_alert::storage_moved_alert(torrent_handle const& h, std::string const& p)
		: torrent_alert(h)
		, path(p)
	{}

	std::string storage_moved_alert::message() const
	{
		return torrent_alert::message() + " moved storage to: "
			+ path;
	}

	storage_moved_failed_alert::storage_moved_failed_alert(torrent_handle const& h
		, error_code const& e)
		: torrent_alert(h)
		, error(e)
	{}

	std::string storage_moved_failed_alert::message() const
	{
		return torrent_alert::message() + " storage move failed: "
			+ convert_from_native(error.message());
	}

	torrent_deleted_alert::torrent_deleted_alert(torrent_handle const& h, sha1_hash const& ih)
		: torrent_alert(h)
	{
		info_hash = ih;
	}

	std::string torrent_deleted_alert::message() const
	{
		return torrent_alert::message() + " deleted";
	}

	torrent_delete_failed_alert::torrent_delete_failed_alert(torrent_handle const& h, error_code const& e, sha1_hash const& ih)
		: torrent_alert(h)
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
			+ convert_from_native(error.message());
	}

	save_resume_data_failed_alert::save_resume_data_failed_alert(torrent_handle const& h
		, error_code const& e)
		: torrent_alert(h)
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

	torrent_paused_alert::torrent_paused_alert(torrent_handle const& h)
		: torrent_alert(h)
	{}

	std::string torrent_paused_alert::message() const
	{
		return torrent_alert::message() + " paused";
	}

	torrent_resumed_alert::torrent_resumed_alert(torrent_handle const& h)
		: torrent_alert(h)
	{}

	std::string torrent_resumed_alert::message() const
	{
		return torrent_alert::message() + " resumed";
	}

	torrent_checked_alert::torrent_checked_alert(torrent_handle const& h)
		: torrent_alert(h)
	{}

	std::string torrent_checked_alert::message() const
	{
		return torrent_alert::message() + " checked";
	}

	url_seed_alert::url_seed_alert(
		torrent_handle const& h
		, std::string const& u
		, error_code const& e)
		: torrent_alert(h)
		, url(u)
		, msg(convert_from_native(e.message()))
	{}

	url_seed_alert::url_seed_alert(
		torrent_handle const& h
		, std::string const& u
		, std::string const& m)
		: torrent_alert(h)
		, url(u)
		, msg(m)
	{}

	std::string url_seed_alert::message() const
	{
		return torrent_alert::message() + " url seed ("
			+ url + ") failed: " + msg;
	}

	file_error_alert::file_error_alert(
		std::string const& f
		, torrent_handle const& h
		, error_code const& e)
		: torrent_alert(h)
		, file(f)
		, error(e)
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
	}

	std::string file_error_alert::message() const
	{
		return torrent_alert::message() + " file (" + file + ") error: "
			+ convert_from_native(error.message());
	}

	metadata_failed_alert::metadata_failed_alert(const torrent_handle& h, error_code e)
		: torrent_alert(h)
		, error(e)
	{}

	std::string metadata_failed_alert::message() const
	{
		return torrent_alert::message() + " invalid metadata received";
	}

	metadata_received_alert::metadata_received_alert(
		const torrent_handle& h)
		: torrent_alert(h)
	{}

	std::string metadata_received_alert::message() const
	{
		return torrent_alert::message() + " metadata successfully received";
	}

	udp_error_alert::udp_error_alert(
		udp::endpoint const& ep
		, error_code const& ec)
		: endpoint(ep)
		, error(ec)
	{}

	std::string udp_error_alert::message() const
	{
		error_code ec;
		return "UDP error: " + convert_from_native(error.message()) + " from: " + endpoint.address().to_string(ec);
	}

	external_ip_alert::external_ip_alert(address const& ip)
		: external_address(ip)
	{}

	std::string external_ip_alert::message() const
	{
		error_code ec;
		return "external IP received: " + external_address.to_string(ec);
	}

	save_resume_data_alert::save_resume_data_alert(boost::shared_ptr<entry> const& rd
		, torrent_handle const& h)
		: torrent_alert(h)
		, resume_data(rd)
	{}

	std::string save_resume_data_alert::message() const
	{
		return torrent_alert::message() + " resume data generated";
	}

	listen_failed_alert::listen_failed_alert(
		tcp::endpoint const& ep
		, int op
		, error_code const& ec
		, socket_type_t t)
		: endpoint(ep)
		, error(ec)
		, operation(op)
		, sock_type(t)
	{}

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
		static char const* type_str[] =
		{
			"TCP", "TCP/SSL", "UDP", "I2P", "Socks5"
		};
		char ret[250];
		snprintf(ret, sizeof(ret), "listening on %s failed: [%s] [%s] %s"
			, print_endpoint(endpoint).c_str()
			, op_str[operation]
			, type_str[sock_type]
			, convert_from_native(error.message()).c_str());
		return ret;
	}

	listen_succeeded_alert::listen_succeeded_alert(tcp::endpoint const& ep, socket_type_t t)
		: endpoint(ep)
		, sock_type(t)
	{}

	std::string listen_succeeded_alert::message() const
	{
		static char const* type_str[] =
		{
			"TCP", "TCP/SSL", "UDP"
		};
		char ret[200];
		snprintf(ret, sizeof(ret), "successfully listening on [%s] %s"
			, type_str[sock_type], print_endpoint(endpoint).c_str());
		return ret;
	}

	portmap_error_alert::portmap_error_alert(int i, int t, error_code const& e)
		:  mapping(i), map_type(t), error(e)
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
	}

	std::string portmap_error_alert::message() const
	{
		static char const* type_str[] = {"NAT-PMP", "UPnP"};
		return std::string("could not map port using ") + type_str[map_type]
			+ ": " + convert_from_native(error.message());
	}

	portmap_alert::portmap_alert(int i, int port, int t)
		: mapping(i), external_port(port), map_type(t)
	{}

	std::string portmap_alert::message() const
	{
		static char const* type_str[] = {"NAT-PMP", "UPnP"};
		char ret[200];
		snprintf(ret, sizeof(ret), "successfully mapped port using %s. external port: %u"
			, type_str[map_type], external_port);
		return ret;
	}

	portmap_log_alert::portmap_log_alert(int t, std::string const& m)
		: map_type(t), msg(m)
	{}

	std::string portmap_log_alert::message() const
	{
		static char const* type_str[] = {"NAT-PMP", "UPnP"};
		char ret[600];
		snprintf(ret, sizeof(ret), "%s: %s", type_str[map_type], msg.c_str());
		return ret;
	}

	fastresume_rejected_alert::fastresume_rejected_alert(torrent_handle const& h
		, error_code const& e)
		: torrent_alert(h)
		, error(e)
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
	}

	std::string fastresume_rejected_alert::message() const
	{
		return torrent_alert::message() + " fast resume rejected: "
			+ convert_from_native(error.message());
	}

	peer_blocked_alert::peer_blocked_alert(torrent_handle const& h, address const& i
		, int r)
		: torrent_alert(h)
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
			"tcp_disabled"
		};

		snprintf(ret, sizeof(ret), "%s: blocked peer: %s [%s]"
			, torrent_alert::message().c_str(), ip.to_string(ec).c_str()
			, reason_str[reason]);
		return ret;
	}

	dht_announce_alert::dht_announce_alert(address const& i, int p
		, sha1_hash const& ih)
		: ip(i)
		, port(p)
		, info_hash(ih)
	{}

	std::string dht_announce_alert::message() const
	{
		error_code ec;
		char ih_hex[41];
		to_hex((const char*)&info_hash[0], 20, ih_hex);
		char msg[200];
		snprintf(msg, sizeof(msg), "incoming dht announce: %s:%u (%s)"
			, ip.to_string(ec).c_str(), port, ih_hex);
		return msg;
	}

	dht_get_peers_alert::dht_get_peers_alert(sha1_hash const& ih)
		: info_hash(ih)
	{}

	std::string dht_get_peers_alert::message() const
	{
		char ih_hex[41];
		to_hex((const char*)&info_hash[0], 20, ih_hex);
		char msg[200];
		snprintf(msg, sizeof(msg), "incoming dht get_peers: %s", ih_hex);
		return msg;
	}

	stats_alert::stats_alert(torrent_handle const& h, int in
		, stat const& s)
		: torrent_alert(h)
		, interval(in)
	{
		for (int i = 0; i < num_channels; ++i)
			transferred[i] = s[i].counter();
	}

	std::string stats_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "%s: [%d] %d %d %d %d %d %d %d %d %d %d"
			, torrent_alert::message().c_str()
			, interval
			, transferred[0]
			, transferred[1]
			, transferred[2]
			, transferred[3]
#ifndef TORRENT_DISABLE_FULL_STATS
			, transferred[4]
			, transferred[5]
			, transferred[6]
			, transferred[7]
			, transferred[8]
			, transferred[9]
#endif
			);
		return msg;
	}

	cache_flushed_alert::cache_flushed_alert(torrent_handle const& h): torrent_alert(h) {}

	anonymous_mode_alert::anonymous_mode_alert(torrent_handle const& h
		, int k, std::string const& s)
		: torrent_alert(h)
		, kind(k)
		, str(s)
	{}

	std::string anonymous_mode_alert::message() const
	{
		char msg[200];
		char const* msgs[] = {
			"tracker is not anonymous, set a proxy"
		};
		snprintf(msg, sizeof(msg), "%s: %s: %s"
			, torrent_alert::message().c_str()
			, msgs[kind], str.c_str());
		return msg;
	}

	lsd_peer_alert::lsd_peer_alert(torrent_handle const& h
		, tcp::endpoint const& i)
		: peer_alert(h, i, peer_id(0))
	{}

	std::string lsd_peer_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), "%s: received peer from local service discovery"
			, peer_alert::message().c_str());
		return msg;
	}

	trackerid_alert::trackerid_alert(torrent_handle const& h
		, std::string const& u
		, const std::string& id)
		: tracker_alert(h, u)
		, trackerid(id)
	{}

	std::string trackerid_alert::message() const
	{
		return "trackerid received: " + trackerid;
	}

	dht_bootstrap_alert::dht_bootstrap_alert()
	{}

	std::string dht_bootstrap_alert::message() const
	{
		return "DHT bootstrap complete";
	}

	rss_alert::rss_alert(feed_handle h, std::string const& u, int s, error_code const& ec)
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

	torrent_error_alert::torrent_error_alert(torrent_handle const& h
		, error_code const& e)
		: torrent_alert(h)
		, error(e)
	{}

	std::string torrent_error_alert::message() const
	{
		char msg[200];
		snprintf(msg, sizeof(msg), " ERROR: %s", convert_from_native(error.message()).c_str());
		return torrent_alert::message() + msg;
	}
	
	torrent_added_alert::torrent_added_alert(torrent_handle const& h)
		: torrent_alert(h)
	{}

	std::string torrent_added_alert::message() const
	{
		return torrent_alert::message() + " added";
	}

	torrent_removed_alert::torrent_removed_alert(torrent_handle const& h, sha1_hash const& ih)
		: torrent_alert(h)
		, info_hash(ih)
	{}

	std::string torrent_removed_alert::message() const
	{
		return torrent_alert::message() + " removed";
	}

	torrent_need_cert_alert::torrent_need_cert_alert(torrent_handle const& h)
		: torrent_alert(h)
	{}

	std::string torrent_need_cert_alert::message() const
	{
		return torrent_alert::message() + " needs SSL certificate";
	}

	static char const* type_str[] = {
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

	incoming_connection_alert::incoming_connection_alert(int t, tcp::endpoint const& i)
		: socket_type(t)
		, ip(i)
	{}

	std::string incoming_connection_alert::message() const
	{
		char msg[600];
		error_code ec;
		snprintf(msg, sizeof(msg), "incoming connection from %s (%s)"
			, print_endpoint(ip).c_str(), type_str[socket_type]);
		return msg;
	}

	peer_connect_alert::peer_connect_alert(torrent_handle h, tcp::endpoint const& ep
		, peer_id const& peer_id, int type)
		: peer_alert(h, ep, peer_id)
		, socket_type(type)
	{}

	std::string peer_connect_alert::message() const
	{
		char msg[600];
		error_code ec;
		snprintf(msg, sizeof(msg), "%s connecting to peer (%s)"
			, peer_alert::message().c_str(), type_str[socket_type]);
		return msg;
	}

	add_torrent_alert::add_torrent_alert(torrent_handle h, add_torrent_params const& p, error_code ec)
		: torrent_alert(h)
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
		else to_hex((const char*)&params.info_hash[0], 20, info_hash);

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

	std::string state_update_alert::message() const
	{
		char msg[600];
		snprintf(msg, sizeof(msg), "state updates for %d torrents", int(status.size()));
		return msg;
	}

	torrent_update_alert::torrent_update_alert(torrent_handle h, sha1_hash const& old_hash, sha1_hash const& new_hash)
		: torrent_alert(h)
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

	rss_item_alert::rss_item_alert(feed_handle h, feed_item const& item)
		: handle(h)
		, item(item)
	{}

	std::string rss_item_alert::message() const
	{
		char msg[500];
		snprintf(msg, sizeof(msg), "feed [%s] has new RSS item %s"
			, handle.get_feed_status().title.c_str()
			, item.title.empty() ? item.url.c_str() : item.title.c_str());
		return msg;
	}

	peer_disconnected_alert::peer_disconnected_alert(torrent_handle const& h, tcp::endpoint const& ep
		, peer_id const& peer_id, error_code const& e)
		: peer_alert(h, ep, peer_id)
		, error(e)
	{
#ifndef TORRENT_NO_DEPRECATE
		msg = convert_from_native(error.message());
#endif
	}

	std::string peer_disconnected_alert::message() const
	{
		char msg[600];
		snprintf(msg, sizeof(msg), "%s disconnecting: [%s] %s", peer_alert::message().c_str()
			, error.category().name(), convert_from_native(error.message()).c_str());
		return msg;
	}

	dht_error_alert::dht_error_alert(int op, error_code const& ec)
		: error(ec), operation(op_t(op))
	{}

	std::string dht_error_alert::message() const
	{
		const static char* const operation_names[] =
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

	dht_immutable_item_alert::dht_immutable_item_alert(sha1_hash const& t, entry const& i)
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

	dht_mutable_item_alert::dht_mutable_item_alert(boost::array<char, 32> k
		, boost::array<char, 64> sig
		, boost::uint64_t sequence
		, std::string const& s
		, entry const& i)
		: key(k), signature(sig), seq(sequence), salt(s), item(i)
	{}

	std::string dht_mutable_item_alert::message() const
	{
		char msg[1050];
		snprintf(msg, sizeof(msg), "DHT mutable item (key=%s salt=%s seq=%" PRId64 ") [ %s ]"
			, to_hex(std::string(&key[0], 32)).c_str()
			, salt.c_str()
			, seq
			, item.to_string().c_str());
		return msg;
	}

	dht_put_alert::dht_put_alert(sha1_hash const& t)
		: target(t)
		, seq(0)
	{}

	dht_put_alert::dht_put_alert(boost::array<char, 32> key
		, boost::array<char, 64> sig
		, std::string s
		, boost::uint64_t sequence_number)
		: target(0)
		, public_key(key)
		, signature(sig)
		, salt(s)
		, seq(sequence_number)
	{}

	std::string dht_put_alert::message() const
	{
		char msg[1050];
		snprintf(msg, sizeof(msg), "DHT put complete (key=%s sig=%s salt=%s seq=%" PRId64 ")"
			, to_hex(std::string(&public_key[0], 32)).c_str()
			, to_hex(std::string(&signature[0], 64)).c_str()
			, salt.c_str()
			, seq);
		return msg;
	}

	i2p_alert::i2p_alert(error_code const& ec)
		: error(ec)
	{}

	std::string i2p_alert::message() const
	{
		char msg[600];
		snprintf(msg, sizeof(msg), "i2p_error: [%s] %s"
			, error.category().name(), convert_from_native(error.message()).c_str());
		return msg;
	}

} // namespace libtorrent

