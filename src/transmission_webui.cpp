/*

Copyright (c) 2012, Arvid Norberg, Magnus Jonsson
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

#include "transmission_webui.hpp"
#include <string.h> // for strcmp() 
#include <stdio.h>
#include <vector>
#include <boost/intrusive_ptr.hpp>
#include <boost/cstdint.hpp>

extern "C" {
#include "mongoose.h"
#include "jsmn.h"
}

#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/torrent_handle.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/socket_io.hpp" // for print_address
#include "libtorrent/io.hpp" // for read_int32
#include "libtorrent/magnet_uri.hpp" // for make_magnet_uri
#include "response_buffer.hpp" // for appendf

namespace libtorrent
{

static const char b64table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char b64_value(char c)
{
	const char *v = strchr(b64table, c);
	if (v == NULL) return 0;
	return v - b64table;
}

std::string base64decode(std::string const& in)
{
	std::string ret;
	if (in.size() < 4) return ret;

	char const* src = in.c_str();
	char const* end = in.c_str() + in.size();
	while (end - src >= 4)
	{
		char a = b64_value(src[0]);
		char b = b64_value(src[1]);
		char c = b64_value(src[2]);
		char d = b64_value(src[3]);
		ret.push_back((a << 2) | (b >> 4));
		if (src[1] == '=') break;
		ret.push_back((b << 4) | (c >> 2));
		if (src[2] == '=') break;
		ret.push_back((c << 6) | d);
		if (src[3] == '=') break;
		src += 4;
	}
	return ret;
}

// skip i. if i points to an object or an array, this function
// needs to make recursive calls to skip its members too
jsmntok_t* skip_item(jsmntok_t* i)
{
	int n = i->size;
	++i;
	// if it's a literal, just skip it, and we're done
	if (n == 0) return i;
	// if it's a container, we need to skip n items
	for (int k = 0; k < n; ++k)
		i = skip_item(i);
	return i;
}

jsmntok_t* find_key(jsmntok_t* tokens, char* buf, char const* key, int type)
{
	if (tokens[0].type != JSMN_OBJECT) return NULL;
	// size is the number of tokens at the object level.
	// half of them are keys, the other half
	int num_keys = tokens[0].size / 2;
	// we skip two items at a time, first the key then the value
	for (jsmntok_t* i = &tokens[1]; num_keys > 0; i = skip_item(skip_item(i)), --num_keys)
	{
		if (i->type != JSMN_STRING) continue;
		buf[i->end] = 0;
		if (strcmp(key, buf + i->start)) continue;
		if (i[1].type != type) continue;
		return i + 1;
	}
	return NULL;
}

char const* find_string(jsmntok_t* tokens, char* buf, char const* key)
{
	jsmntok_t* k = find_key(tokens, buf, key, JSMN_STRING);
	if (k == NULL) return "";
	buf[k->end] = '\0';
	return buf + k->start;
}

boost::int64_t find_int(jsmntok_t* tokens, char* buf, char const* key)
{
	jsmntok_t* k = find_key(tokens, buf, key, JSMN_PRIMITIVE);
	if (k == NULL) return 0;
	buf[k->end] = '\0';
	return strtoll(buf + k->start, NULL, 10);
}

bool find_bool(jsmntok_t* tokens, char* buf, char const* key)
{
	jsmntok_t* k = find_key(tokens, buf, key, JSMN_PRIMITIVE);
	if (k == NULL) return false;
	buf[k->end] = '\0';
	return strcmp(buf + k->start, "true") == 0;
}

void return_error(mg_connection* conn, char const* msg)
{
	mg_printf(conn, "HTTP/1.1 401 Invalid Request\r\n"
		"Content-Type: text/json\r\n"
		"Content-Length: %d\r\n\r\n"
		"{ \"result\": \"%s\" }", int(16 + strlen(msg)), msg);
}

void return_failure(std::vector<char>& buf, char const* msg, boost::int64_t tag)
{
	appendf(buf, "{ \"result\": \"%s\", \"tag\": %" PRId64 "}", msg, tag);
}

struct method_handler
{
	char const* method_name;
	void (transmission_webui::*fun)(std::vector<char>&, jsmntok_t* args, boost::int64_t tag, char* buffer);
};

method_handler handlers[] =
{
	{"torrent-add", &transmission_webui::add_torrent },
	{"torrent-get", &transmission_webui::get_torrent },
	{"torrent-set", &transmission_webui::set_torrent },
	{"torrent-start", &transmission_webui::start_torrent },
	{"torrent-start-now", &transmission_webui::start_torrent_now },
	{"torrent-stop", &transmission_webui::stop_torrent },
	{"torrent-verify", &transmission_webui::verify_torrent },
	{"torrent-reannounce", &transmission_webui::reannounce_torrent },
	{"torrent-remove", &transmission_webui::remove_torrent},
	{"session-stats", &transmission_webui::session_stats},
};

void transmission_webui::handle_json_rpc(std::vector<char>& buf, jsmntok_t* tokens, char* buffer)
{
	// we expect a "method" in the top level
	jsmntok_t* method = find_key(tokens, buffer, "method", JSMN_STRING);
	if (method == NULL)
	{
		return_failure(buf, "missing method in request", -1);
		return;
	}

	bool handled = false;
	buffer[method->end] = 0;
	char const* m = &buffer[method->start];
	jsmntok_t* args = NULL;
	for (int i = 0; i < sizeof(handlers)/sizeof(handlers[0]); ++i)
	{
		if (strcmp(m, handlers[i].method_name)) continue;

		args = find_key(tokens, buffer, "arguments", JSMN_OBJECT);
		boost::int64_t tag = find_int(tokens, buffer, "tag");
		handled = true;

		if (args) buffer[args->end] = 0;
		printf("%s: %s\n", m, args ? buffer + args->start : "{}");

		(this->*handlers[i].fun)(buf, args, tag, buffer);
		break;
	}
	if (!handled)
		printf("Unhandled: %s: %s\n", m, args ? buffer + args->start : "{}");
}

void transmission_webui::add_torrent(std::vector<char>& buf, jsmntok_t* args
	, boost::int64_t tag, char* buffer)
{
	jsmntok_t* cookies = find_key(args, buffer, "cookies", JSMN_STRING);

	add_torrent_params params;
	params.save_path = find_string(args, buffer, "download-dir");
	if (params.save_path.empty()) 
		params.save_path = ".";
	bool paused = find_bool(args, buffer, "paused");
	params.paused = paused;
	params.flags = paused ? 0 : add_torrent_params::flag_auto_managed;

	std::string url = find_string(args, buffer, "filename");
	if (url.substr(0, 7) == "http://"
		|| url.substr(0, 8) == "https://"
		|| url.substr(0, 7) == "magnet:")
	{
		params.url = url;
	}
	else if (!url.empty())
	{
		error_code ec;
		boost::intrusive_ptr<torrent_info> ti(new torrent_info(url, ec));
		if (ec)
		{
			return_failure(buf, ec.message().c_str(), tag);
			return;
		}
		params.ti = ti;
	}
	else
	{
		std::string metainfo = base64decode(find_string(args, buffer, "metainfo"));
		error_code ec;
		boost::intrusive_ptr<torrent_info> ti(new torrent_info(&metainfo[0], metainfo.size(), ec));
		if (ec)
		{
			return_failure(buf, ec.message().c_str(), tag);
			return;
		}
		params.ti = ti;
	}
	
	error_code ec;
	torrent_handle h = m_ses.add_torrent(params);
	if (ec)
	{
		return_failure(buf, ec.message().c_str(), tag);
		return;
	}

	appendf(buf, "{ \"result\": \"success\", \"tag\": %" PRId64 ", "
		"\"arguments\": { \"torrent-added\": { \"hashString\": \"%s\", "
		"\"id\": %u, \"name\": \"%s\"}}}"
		, tag, h.has_metadata() ? to_hex(h.get_torrent_info().info_hash().to_string()).c_str() : ""
		, h.id(), h.name().c_str());
}

char const* to_bool(bool b) { return b ? "true" : "false"; }

bool all_torrents(torrent_status const& s)
{
	return true;
}

void transmission_webui::parse_ids(std::set<boost::uint32_t>& torrent_ids, jsmntok_t* args, char* buffer)
{
	jsmntok_t* ids_ent = find_key(args, buffer, "ids", JSMN_ARRAY);
	if (ids_ent)
	{
		int num_ids = ids_ent->size;
		for (int i = 0; i < num_ids; ++i)
		{
			jsmntok_t* item = &ids_ent[i+1];
			torrent_ids.insert(atoi(buffer + item->start));
		}
	}
	else
	{
		boost::uint32_t id = find_int(args, buffer, "ids");
		if (id == 0) return;
		torrent_ids.insert(torrent_ids.begin(), id);
	}
}

void transmission_webui::get_torrent(std::vector<char>& buf, jsmntok_t* args
	, boost::int64_t tag, char* buffer)
{
	jsmntok_t* field_ent = find_key(args, buffer, "fields", JSMN_ARRAY);
	if (field_ent == NULL)
	{
		return_failure(buf, "missing 'field' argument", tag);
		return;
	}

	std::set<std::string> fields;
	int num_fields = field_ent->size;
	for (int i = 0; i < num_fields; ++i)
	{
		jsmntok_t* item = &field_ent[i+1];
		fields.insert(std::string(buffer + item->start, buffer + item->end));
	}

	std::set<boost::uint32_t> torrent_ids;
	parse_ids(torrent_ids, args, buffer);

	std::vector<torrent_status> t;
	m_ses.get_torrent_status(&t, &all_torrents);

	appendf(buf, "{ \"result\": \"success\", \"arguments\": { \"torrents\": [");

#define TORRENT_PROPERTY(name, format_code, prop) \
	if (fields.count(name)) { \
		appendf(buf, ", \"" name "\": " format_code "" + (count?0:2), prop); \
		++count; \
	}

	int returned_torrents = 0;
	error_code ec;
	torrent_info empty("", ec);
	for (int i = 0; i < t.size(); ++i)
	{
		torrent_info const* ti = &empty;
		if (t[i].has_metadata) ti = &t[i].handle.get_torrent_info();
		torrent_status const& ts = t[i];

		if (!torrent_ids.empty() && torrent_ids.count(ts.handle.id()) == 0)
			continue;

		// skip comma on any item that's not the first one
		appendf(buf, ", {" + (returned_torrents?0:2));
		int count = 0;
		TORRENT_PROPERTY("activityDate", "%" PRId64, time(0) - (std::min)(ts.time_since_download
			, ts.time_since_upload));
		TORRENT_PROPERTY("addedDate", "%" PRId64, ts.added_time);
		TORRENT_PROPERTY("comment", "\"%s\"", ti->comment().c_str());
		TORRENT_PROPERTY("creator", "\"%s\"", ti->creator().c_str());
		TORRENT_PROPERTY("dateCreated", "%" PRId64, ti->creation_date() ? ti->creation_date().get() : 0);
		TORRENT_PROPERTY("doneDate", "%" PRId64, ts.completed_time);
		TORRENT_PROPERTY("downloadDir", "\"%s\"", ts.handle.save_path().c_str());
		TORRENT_PROPERTY("errorString", "\"%s\"", ts.error.c_str());
		TORRENT_PROPERTY("eta", "%d", ts.download_payload_rate <= 0 ? -1
			: (ts.total_wanted - ts.total_wanted_done) / ts.download_payload_rate);
		TORRENT_PROPERTY("hashString", "\"%s\"", to_hex(ts.handle.info_hash().to_string()).c_str());
		TORRENT_PROPERTY("downloadedEver", "%" PRId64, ts.all_time_download);
		TORRENT_PROPERTY("haveValid", "%d", ts.num_pieces);
		TORRENT_PROPERTY("id", "%u", ts.handle.id());
		TORRENT_PROPERTY("isFinished", "%s", to_bool(ts.is_finished));
		TORRENT_PROPERTY("isPrivate", "%s", to_bool(ti->priv()));
		TORRENT_PROPERTY("isStalled", "%s", to_bool(ts.download_payload_rate == 0));
		TORRENT_PROPERTY("leftUntilDone", "%" PRId64, ts.total_wanted - ts.total_wanted_done);
		TORRENT_PROPERTY("magnetLink", "\"%s\"", ti == &empty ? "" : make_magnet_uri(*ti).c_str());
		TORRENT_PROPERTY("metadataPercentComplete", "%f", ts.has_metadata ? 100.f : ts.progress_ppm / 10000.f);
		TORRENT_PROPERTY("name", "\"%s\"", ts.handle.name().c_str());
		TORRENT_PROPERTY("peer-limit", "%d", ts.handle.max_connections());
		TORRENT_PROPERTY("peersConnected", "%d", ts.num_peers);
		TORRENT_PROPERTY("percentDone", "%f", ts.progress_ppm / 10000.f);
		TORRENT_PROPERTY("pieceCount", "%d", ti != &empty ? ti->num_pieces() : 0);
		TORRENT_PROPERTY("pieceSize", "%d", ti != &empty ? ti->piece_length() : 0);
		TORRENT_PROPERTY("queuePosition", "%d", ts.queue_position);
		TORRENT_PROPERTY("rateDownload", "%d", ts.download_rate);
		TORRENT_PROPERTY("rateUpload", "%d", ts.upload_rate);
		TORRENT_PROPERTY("recheckProgress", "%f", ts.progress_ppm / 10000.f);
		TORRENT_PROPERTY("secondsDownloading", "%" PRId64 , ts.active_time);
		TORRENT_PROPERTY("secondsSeeding", "%" PRId64, ts.finished_time);
		TORRENT_PROPERTY("sizeWhenDone", "%" PRId64, ti != &empty ? ti->total_size() : 0);
		TORRENT_PROPERTY("totalSize", "%" PRId64, ts.total_done);
		TORRENT_PROPERTY("uploadedEver", "%" PRId64, ts.all_time_upload);
		TORRENT_PROPERTY("uploadedRatio", "%ld", ts.all_time_download == 0
			? ts.all_time_upload
			: ts.all_time_upload / ts.all_time_download);

		if (fields.count("status"))
		{
#define TR_STATUS_CHECK_WAIT   ( 1 << 0 )  /* Waiting in queue to check files */
#define TR_STATUS_CHECK        ( 1 << 1 )  /* Checking files */
#define TR_STATUS_DOWNLOAD     ( 1 << 2 )  /* Downloading */
#define TR_STATUS_SEED         ( 1 << 3 )  /* Seeding */
#define TR_STATUS_STOPPED      ( 1 << 4 )  /* Torrent is stopped */
			int res = 0;

			switch(ts.state)
			{
				case libtorrent::torrent_status::checking_resume_data:
					res = TR_STATUS_CHECK;
					break;
				case libtorrent::torrent_status::checking_files:
					if (ts.paused)
						res = TR_STATUS_CHECK_WAIT;
					else
						res = TR_STATUS_CHECK;
					break;
				case libtorrent::torrent_status::downloading_metadata:
				case libtorrent::torrent_status::downloading:
				case libtorrent::torrent_status::allocating:
					res = TR_STATUS_DOWNLOAD;
					break;
				case libtorrent::torrent_status::seeding:
				case libtorrent::torrent_status::finished:
					res = TR_STATUS_SEED;
					break;
			}
			if (ts.paused && !ts.auto_managed)
				res |= TR_STATUS_STOPPED;

			appendf(buf, ", \"status\": %d" + (count?0:2), res);
			++count;
		}

		if (fields.count("files"))
		{
			file_storage const& files = ti->files();
			std::vector<libtorrent::size_type> progress;
			ts.handle.file_progress(progress);
			appendf(buf, ", \"files\": [" + (count?0:2));
			for (int i = 0; i < files.num_files(); ++i)
			{
				appendf(buf, ", { \"bytesCompleted\": %" PRId64 ","
					"\"length\": %" PRId64 ","
					"\"name\": \"%s\" }" + (i?0:2)
					, progress[i], files.file_size(i), files.file_path(i).c_str());
			}
			appendf(buf, "]");
			++count;
		}

		if (fields.count("fileStats"))
		{
			file_storage const& files = ti->files();
			std::vector<libtorrent::size_type> progress;
			ts.handle.file_progress(progress);
			appendf(buf, ", \"fileStats\": [" + (count?0:2));
			for (int i = 0; i < files.num_files(); ++i)
			{
				int prio = ts.handle.file_priority(i);
				appendf(buf, ", { \"bytesCompleted\": %" PRId64 ","
					"\"wanted\": %s,"
					"\"priority\": %d }" + (i?0:2)
					, progress[i], to_bool(prio), prio);
			}
			appendf(buf, "]");
			++count;
		}

		if (fields.count("wanted"))
		{
			file_storage const& files = ti->files();
			appendf(buf, ", \"wanted\": [" + (count?0:2));
			for (int i = 0; i < files.num_files(); ++i)
			{
				appendf(buf, ", %s" + (i?0:2)
					, to_bool(ts.handle.file_priority(i)));
			}
			appendf(buf, "]");
			++count;
		}

		if (fields.count("priorities"))
		{
			file_storage const& files = ti->files();
			appendf(buf, ", \"priorities\": [" + (count?0:2));
			for (int i = 0; i < files.num_files(); ++i)
			{
				appendf(buf, ", %d" + (i?0:2)
					, ts.handle.file_priority(i));
			}
			appendf(buf, "]");
			++count;
		}

		if (fields.count("webseeds"))
		{
			std::vector<web_seed_entry> const& webseeds = ti->web_seeds();
			appendf(buf, ", \"webseeds\": [" + (count?0:2));
			for (int i = 0; i < webseeds.size(); ++i)
			{
				appendf(buf, ", \"%s\"" + (i?0:2)
					, webseeds[i].url.c_str());
			}
			appendf(buf, "]");
			++count;
		}

		if (fields.count("pieces"))
		{
			std::string encoded_pieces = base64encode(
				std::string(ts.pieces.bytes(), (ts.pieces.size() + 7) / 8));
			appendf(buf, ", \"pieces\": \"%s\"" + (count?0:2)
				, encoded_pieces.c_str());
			++count;
		}

		if (fields.count("peers"))
		{
			std::vector<peer_info> peers;
			ts.handle.get_peer_info(peers);
			appendf(buf, ", \"peers\": [" + (count?0:2));
			for (int i = 0; i < peers.size(); ++i)
			{
				peer_info const& p = peers[i];
				appendf(buf, ", { \"address\": \"%s\""
					", \"clientName\": \"%s\""
					", \"clientIsChoked\": %s"
					", \"clientIsInterested\": %s"
					", \"flagStr\": \"\""
					", \"isDownloadingFrom\": %s"
					", \"isEncrypted\": %s"
					", \"isIncoming\": %s"
					", \"isUploadingTo\": %s"
					", \"isUTP\": %s"
					", \"peerIsChoked\": %s"
					", \"peerIsInterested\": %s"
					", \"port\": %d"
					", \"progress\": %f"
					", \"rateToClient\": %d"
					", \"rateToPeer\": %d"
					"}"
					+ (i?0:2)
					, print_address(p.ip.address()).c_str()
					, p.client.c_str()
					, to_bool(p.flags & peer_info::choked)
					, to_bool(p.flags & peer_info::interesting)
					, to_bool(p.downloading_piece_index != -1)
					, to_bool(p.flags & (peer_info::rc4_encrypted | peer_info::plaintext_encrypted))
					, to_bool(p.source & peer_info::incoming)
					, to_bool(p.used_send_buffer)
					, to_bool(p.connection_type == peer_info::bittorrent_utp)
					, to_bool(p.flags & peer_info::remote_choked)
					, to_bool(p.flags & peer_info::remote_interested)
					, p.ip.port()
					, p.progress
					, p.down_speed
					, p.up_speed
					);
			}
			appendf(buf, "]");
			++count;
		}

		if (fields.count("trackers"))
		{
			std::vector<announce_entry> trackers = ts.handle.trackers();
			appendf(buf, ", \"trackers\": [" + (count?0:2));
			for (int i = 0; i < trackers.size(); ++i)
			{
				announce_entry const& a = trackers[i];
				appendf(buf, ", { \"announce\": \"%s\""
					", \"id\": %d"
					", \"scrape\": \"%s\""
					", \"tier\": %d"
					"}"
					+ (i?0:2)
					, trackers[i].url.c_str()
					, 0
					, trackers[i].url.c_str()
					, trackers[i].tier
					);
			}
			appendf(buf, "]");
			++count;
		}
		appendf(buf, "}");
		++returned_torrents;
	}

	appendf(buf, "] }, \"tag\": \"%" PRId64 "\" }", tag);
}

void transmission_webui::set_torrent(std::vector<char>& buf, jsmntok_t* args
	, boost::int64_t tag, char* buffer)
{
}

void transmission_webui::start_torrent(std::vector<char>& buf, jsmntok_t* args
	, boost::int64_t tag, char* buffer)
{
	std::vector<torrent_handle> handles;
	get_torrents(handles, args, buffer);
	for (std::vector<torrent_handle>::iterator i = handles.begin()
		, end(handles.end()); i != end; ++i)
	{
		i->auto_managed(true);
		i->resume();
	}
	appendf(buf, "{ \"result\": \"success\", \"tag\": %" PRId64 ", "
		"\"arguments\": {} }", tag);
}

void transmission_webui::start_torrent_now(std::vector<char>& buf, jsmntok_t* args
	, boost::int64_t tag, char* buffer)
{
	std::vector<torrent_handle> handles;
	get_torrents(handles, args, buffer);
	for (std::vector<torrent_handle>::iterator i = handles.begin()
		, end(handles.end()); i != end; ++i)
	{
		i->auto_managed(false);
		i->resume();
	}
	appendf(buf, "{ \"result\": \"success\", \"tag\": %" PRId64 ", "
		"\"arguments\": {} }", tag);
}

void transmission_webui::stop_torrent(std::vector<char>& buf, jsmntok_t* args
	, boost::int64_t tag, char* buffer)
{
	std::vector<torrent_handle> handles;
	get_torrents(handles, args, buffer);
	for (std::vector<torrent_handle>::iterator i = handles.begin()
		, end(handles.end()); i != end; ++i)
	{
		i->auto_managed(false);
		i->pause();
	}
	appendf(buf, "{ \"result\": \"success\", \"tag\": %" PRId64 ", "
		"\"arguments\": {} }", tag);
}

void transmission_webui::verify_torrent(std::vector<char>& buf, jsmntok_t* args
	, boost::int64_t tag, char* buffer)
{
	std::vector<torrent_handle> handles;
	get_torrents(handles, args, buffer);
	for (std::vector<torrent_handle>::iterator i = handles.begin()
		, end(handles.end()); i != end; ++i)
	{
		i->force_recheck();
	}
	appendf(buf, "{ \"result\": \"success\", \"tag\": %" PRId64 ", "
		"\"arguments\": {} }", tag);
}

void transmission_webui::reannounce_torrent(std::vector<char>& buf, jsmntok_t* args
	, boost::int64_t tag, char* buffer)
{
	std::vector<torrent_handle> handles;
	get_torrents(handles, args, buffer);
	for (std::vector<torrent_handle>::iterator i = handles.begin()
		, end(handles.end()); i != end; ++i)
	{
		i->force_reannounce();
	}
	appendf(buf, "{ \"result\": \"success\", \"tag\": %" PRId64 ", "
		"\"arguments\": {} }", tag);
}

void transmission_webui::remove_torrent(std::vector<char>& buf, jsmntok_t* args
	, boost::int64_t tag, char* buffer)
{
	bool delete_data = find_bool(args, buffer, "delete-local-data");

	std::vector<torrent_handle> handles;
	get_torrents(handles, args, buffer);
	for (std::vector<torrent_handle>::iterator i = handles.begin()
		, end(handles.end()); i != end; ++i)
	{
		m_ses.remove_torrent(*i, delete_data ? session::delete_files : 0);
	}
	appendf(buf, "{ \"result\": \"success\", \"tag\": %" PRId64 ", "
		"\"arguments\": {} }", tag);
}

void transmission_webui::session_stats(std::vector<char>& buf, jsmntok_t* args
	, boost::int64_t tag, char* buffer)
{
	session_status st = m_ses.status();

	appendf(buf, "{ \"result\": \"success\", \"tag\": %" PRId64 ", "
		"\"arguments\": { "
		"\"activeTorrentCount\": %d,"
		"\"downloadSpeed\": %d,"
		"\"pausedTorrentCount\": %d,"
		"\"torrentCount\": %d,"
		"\"uploadSpeed\": %d,"
		"\"culumative-stats\": {"
			"\"uploadedBytes\": %"PRId64","
			"\"downloadedBytes\": %"PRId64","
			"\"filesAdded\": %d,"
			"\"sessionCount\": %d,"
			"\"secondsActive\": %d"
			"},"
		"\"current-stats\": {"
			"\"uploadedBytes\": %"PRId64","
			"\"downloadedBytes\": %"PRId64","
			"\"filesAdded\": %d,"
			"\"sessionCount\": %d,"
			"\"secondsActive\": %d"
			"}"
		"}}", tag
		, st.num_torrents - st.num_paused_torrents
		, st.payload_download_rate
		, st.num_paused_torrents
		, st.num_torrents
		, st.payload_upload_rate
		// cumulative-stats (not supported)
		, st.total_payload_download
		, st.total_payload_upload
		, st.num_torrents
		, 1
		, time(NULL) - m_start_time
		, st.total_payload_download
		, st.total_payload_upload
		, st.num_torrents
		, 1
		, time(NULL) - m_start_time
		// current-stats
		, st.total_payload_download
		, st.total_payload_upload
		, st.num_torrents
		, 1
		, time(NULL) - m_start_time);
}

void transmission_webui::get_torrents(std::vector<torrent_handle>& handles, jsmntok_t* args
	, char* buffer)
{
	std::vector<torrent_handle> h = m_ses.get_torrents();

	std::set<boost::uint32_t> torrent_ids;
	parse_ids(torrent_ids, args, buffer);

	if (torrent_ids.empty())
	{
		// if ids is omitted, return all torrents
		handles.swap(h);
		return;
	}
	for (std::vector<torrent_handle>::iterator i = h.begin()
		, end(h.end()); i != end; ++i)
	{
		if (torrent_ids.count(i->id()))
			handles.insert(handles.begin(), *i);
	}
}

transmission_webui::transmission_webui(session& s)
	: webui_base(s)
{
	m_start_time = time(NULL);
}

transmission_webui::~transmission_webui() {}

bool transmission_webui::handle_http(mg_connection* conn, mg_request_info const* request_info)
{
	char const* cl = mg_get_header(conn, "content-length");
	std::vector<char> post_body;
	if (cl != NULL)
	{
		int content_length = atoi(cl);
		if (content_length > 0 && content_length < 10 * 1024 * 1024)
		{
			post_body.resize(content_length + 1);
			mg_read(conn, &post_body[0], post_body.size());
			post_body[content_length] = 0;
			// null terminate
		}
	}

	printf("REQUEST: %s\n", request_info->uri);

	std::vector<char> response;
	if (!strcmp(request_info->uri, "/transmission/rpc")
		|| !strcmp(request_info->uri, "/rpc"))
	{
		if (post_body.empty())
		{
			return_error(conn, "request with no POST body");
			return true;
		}
		jsmntok_t tokens[256];
		jsmn_parser p;
		jsmn_init(&p);

		int r = jsmn_parse(&p, &post_body[0], tokens, sizeof(tokens)/sizeof(tokens[0]));
		if (r == JSMN_ERROR_INVAL)
		{
			return_error(conn, "request not JSON");
			return true;
		}
		else if (r == JSMN_ERROR_NOMEM)
		{
			return_error(conn, "request too big");
			return true;
		}
		else if (r == JSMN_ERROR_PART)
		{
			return_error(conn, "request truncated");
			return true;
		}
		else if (r != JSMN_SUCCESS)
		{
			return_error(conn, "invalid request");
			return true;
		}

		handle_json_rpc(response, tokens, &post_body[0]);

		// we need a null terminator
		response.push_back('\0');
		// subtract one from content-length
		// to not count null terminator
		mg_printf(conn, "HTTP/1.1 200 OK\r\n"
			"Content-Type: text/json\r\n"
			"Content-Length: %d\r\n\r\n", int(response.size()) - 1);
		mg_write(conn, &response[0], response.size());
		printf("%s\n", &response[0]);
		return true;
	}

	// TODO: handle other urls here

	return false;
}


}

