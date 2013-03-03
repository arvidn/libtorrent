/*

Copyright (c) 2012, Arvid Norberg
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

#include <deque>
#include <getopt.h> // for getopt_long
#include <stdlib.h> // for daemon()
#include <syslog.h>
#include <boost/bind.hpp>

#include "libtorrent/session.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/io.hpp"
#include "libtorrent/puff.hpp"
#include "disk_space.hpp"
#include "no_auth.hpp"
#include "deluge.hpp"
#include "rencode.hpp"
#include "base64.hpp"
#include <zlib.h>

using namespace libtorrent;

namespace io = libtorrent::detail;

enum rpc_type_t
{
	RPC_RESPONSE = 1,
	RPC_ERROR = 2,
	RPC_EVENT = 3
};

deluge::deluge(session& s, std::string pem_path, auth_interface const* auth)
	: m_ses(s)
	, m_auth(auth)
	, m_listen_socket(NULL)
	, m_accept_thread(NULL)
	, m_context(m_ios, boost::asio::ssl::context::sslv23)
	, m_shutdown(false)
{
	if (m_auth == NULL)
	{
		const static no_auth n;
		m_auth = &n;
	}

	m_params_model.save_path = ".";

	m_context.set_options(
		boost::asio::ssl::context::default_workarounds
		| boost::asio::ssl::context::no_sslv2
		| boost::asio::ssl::context::single_dh_use);
	error_code ec;
//	m_context.set_password_callback(boost::bind(&server::get_password, this));
	m_context.use_certificate_chain_file(pem_path.c_str(), ec);
	if (ec)
	{
		fprintf(stderr, "use cert: %s\n", ec.message().c_str());
		return;
	}
	m_context.use_private_key_file(pem_path.c_str(), boost::asio::ssl::context::pem, ec);
	if (ec)
	{
		fprintf(stderr, "use key: %s\n", ec.message().c_str());
		return;
	}
//	m_context.use_tmp_dh_file("dh512.pem");
}

deluge::~deluge()
{
}

void deluge::accept_thread(int port)
{
	socket_acceptor socket(m_ios);
	m_listen_socket = &socket;

	error_code ec;
	socket.open(tcp::v4(), ec);
	if (ec)
	{
		fprintf(stderr, "open: %s\n", ec.message().c_str());
		return;
	}
	socket.set_option(socket_acceptor::reuse_address(true), ec);
	if (ec)
	{
		fprintf(stderr, "reuse address: %s\n", ec.message().c_str());
		return;
	}
	socket.bind(tcp::endpoint(address_v4::any(), port), ec);
	if (ec)
	{
		fprintf(stderr, "bind: %s\n", ec.message().c_str());
		return;
	}
	socket.listen(5, ec);
	if (ec)
	{
		fprintf(stderr, "listen: %s\n", ec.message().c_str());
		return;
	}

	TORRENT_ASSERT(m_threads.empty());
	for (int i = 0; i < 5; ++i)
		m_threads.push_back(new thread(boost::bind(&deluge::connection_thread, this)));

	do_accept();
	m_ios.run();

	for (std::vector<thread*>::iterator i = m_threads.begin()
		, end(m_threads.end()); i != end; ++i)
	{
		(*i)->join();
		delete *i;
	}

	mutex::scoped_lock l(m_mutex);
	m_threads.clear();

	for (std::vector<ssl_socket*>::iterator i = m_jobs.begin()
		, end(m_jobs.end()); i != end; ++i)
	{
		delete *i;
	}
	m_jobs.clear();
}

void deluge::do_accept()
{
	TORRENT_ASSERT(!m_shutdown);
	ssl_socket* sock = new ssl_socket(m_ios, m_context);
	m_listen_socket->async_accept(sock->lowest_layer(), boost::bind(&deluge::on_accept, this, _1, sock));
}

void deluge::on_accept(error_code const& ec, ssl_socket* sock)
{
	if (ec)
	{
		delete sock;
		do_stop();
		return;
	}

	fprintf(stderr, "accepted connection\n");
	mutex::scoped_lock l(m_mutex);
	m_jobs.push_back(sock);
	m_cond.notify();
	l.unlock();

	do_accept();
}

struct handler_map_t
{
	char const* method;
	char const* args;
	void (deluge::*fun)(deluge::conn_state* st);
};

handler_map_t handlers[] =
{
	{"daemon.login", "[ss]{}", &deluge::handle_login},
	{"daemon.set_event_interest", "[[s]]{}", &deluge::handle_set_event_interest},
	{"daemon.info", "[]{}", &deluge::handle_info},
	{"core.get_config_value", "[s]{}", &deluge::handle_get_config_value},
	{"core.get_config_values", "[[s]]{}", &deluge::handle_get_config_values},
	{"core.get_session_status", "[[s]]{}", &deluge::handle_get_session_status},
	{"core.get_enabled_plugins", "[]{}", &deluge::handle_get_enabled_plugins},
	{"core.get_free_space", "[]{}", &deluge::handle_get_free_space},
	{"core.get_num_connections", "[]{}", &deluge::handle_get_num_connections},
	{"core.get_torrents_status", "[{}[]b]{}", &deluge::handle_get_torrents_status},
	{"core.add_torrent_file", "[ss{}]{}", &deluge::handle_add_torrent_file},
	{"core.get_filter_tree", "[b]{}", &deluge::handle_get_filter_tree},
};

void deluge::incoming_rpc(deluge::conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	printf("<== ");
	print_rtok(tokens, buf);
	printf("\n");

	// RPCs are always 4-tuples, anything else is malformed
	// first the request-ID
	// method name
	// arguments
	// keyword (named) arguments
	if (validate_structure(tokens, "[is[]{}]") == false)
	{
		int id = -1;
		if (tokens[1].type() == type_integer)
			id = tokens[1].integer(buf);

		output_error(id, "invalid RPC format", out);
		return;
	}

	std::string method = tokens[2].string(buf);

	for (int i = 0; i < sizeof(handlers)/sizeof(handlers[0]); ++i)
	{
		if (handlers[i].method != method) continue;

		if (!validate_structure(tokens+3, handlers[i].args))
		{
			output_error(tokens[1].integer(buf), "invalid arguments", out);
			return;
		}

		(this->*handlers[i].fun)(st);
		return;
	}

	output_error(tokens[1].integer(buf), "unknown method", out);
}

struct full_permissions : permissions_interface
{
	full_permissions() {}
	bool allow_start() const { return true; }
	bool allow_stop() const { return true; }
	bool allow_recheck() const { return true; }
	bool allow_list() const { return true; }
	bool allow_add() const { return true; }
	bool allow_remove() const { return true; }
	bool allow_remove_data() const { return true; }
	bool allow_queue_change() const { return true; }
	bool allow_get_settings(int) const { return true; }
	bool allow_set_settings(int) const { return true; }
	bool allow_get_data() const { return true; }
	bool allow_session_status() const { return true; }
};

void deluge::handle_login(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

//#error temp
	//st->perms = m_auth->find_user(...);
	const static full_permissions n;
	st->perms = &n;

	int id = tokens[1].integer(buf);

	// [ RPC_RESPONSE, req-id, [5] ]

	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);
	out.append_list(1);
	out.append_int(5); // auth-level
}

void deluge::handle_set_event_interest(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	if (!st->perms->allow_list())
	{
		output_error(tokens[1].integer(buf), "permission denied", out);
		return;
	}

	int id = st->tokens[1].integer(st->buf);

	// [ RPC_RESPONSE, req-id, [True] ]

	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);
	out.append_list(1);
	out.append_bool(true); // success
}

void deluge::handle_info(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	if (!st->perms->allow_get_settings(settings_pack::user_agent))
	{
		output_error(tokens[1].integer(buf), "permission denied", out);
		return;
	}

	int id = tokens[1].integer(buf);

	// [ RPC_RESPONSE, req-id, ["1.0"] ]

	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);
	out.append_list(1);
	out.append_string(m_ses.get_settings().get_str(settings_pack::user_agent)); // version
}

void deluge::handle_get_enabled_plugins(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	int id = tokens[1].integer(buf);

	// [ RPC_RESPONSE, req-id, [[]] ]

	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);
	out.append_list(1);
	out.append_list(0);
}

char const* map_deluge_setting(std::string const& name)
{
	if (name == "max_download_speed")
		return "download_rate_limit";
	else if (name == "max_upload_speed")
		return "upload_rate_limit";
	else if (name == "max_connections_global")
		return "connections_limit";
	else
		return name.c_str();
}

void deluge::output_config_value(std::string set_name, aux::session_settings const& sett
	, rencoder& out, permissions_interface const* p)
{
	char const* lt_name = map_deluge_setting(set_name);
	int name = setting_by_name(lt_name);
	if (name < 0)
	{
		if (!p->allow_get_settings(-1))
		{
			out.append_none();
			return;
		}

		if (set_name == "dht")
			out.append_bool(m_ses.is_dht_running());
		else if (set_name == "add_paused")
			out.append_bool(m_params_model.flags & add_torrent_params::flag_paused);
		else if (set_name == "max_connections_per_torrent")
			out.append_int(m_params_model.max_connections);
		else if (set_name == "max_upload_slots_per_torrent")
			out.append_int(m_params_model.max_uploads);
		else if (set_name == "max_upload_speed_per_torrent")
			out.append_int(m_params_model.upload_limit);
		else if (set_name == "max_download_speed_per_torrent")
			out.append_int(m_params_model.download_limit);
		else if (set_name == "prioritize_first_last_pieces")
			out.append_bool(false);
		else if (set_name == "compact_allocation")
#ifndef TORRENT_NO_DEPRECATE
			out.append_bool(m_params_model.storage_mode == storage_mode_compact);
#else
			out.append_bool(false);
#endif
		else if (set_name == "download_location")
			out.append_string(m_params_model.save_path);
		else
			out.append_none();
		return;
	}

	if (!p->allow_get_settings(name))
	{
		out.append_none();
		return;
	}

	switch (name & settings_pack::type_mask)
	{
		case settings_pack::string_type_base:
			out.append_string(sett.get_str(name));
			break;
		case settings_pack::int_type_base:
			out.append_int(sett.get_int(name));
			break;
		case settings_pack::bool_type_base:
			out.append_bool(sett.get_bool(name));
			break;
		default:
			out.append_none();
	};
}

void deluge::handle_get_config_value(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	int id = tokens[1].integer(buf);

	aux::session_settings sett = m_ses.get_settings();
	
	// [ RPC_RESPONSE, req-id, [<config value>] ]

	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);
	output_config_value(tokens[4].string(buf), sett, out, st->perms);
}

void deluge::handle_get_free_space(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	if (!st->perms->allow_session_status())
	{
		output_error(tokens[1].integer(buf), "permission denied", out);
		return;
	}

	int id = tokens[1].integer(buf);

	std::string path = tokens[4].type() == type_string
		? tokens[4].string(buf) : m_params_model.save_path;
	
	// [ RPC_RESPONSE, req-id, [free-bytes] ]

	boost::int64_t ret = free_disk_space(path);
	if (ret < 0)
	{
		output_error(id, "InvalidPathError", out);
		return;
	}
	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);
	out.append_int(ret);
}

void deluge::handle_get_num_connections(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	if (!st->perms->allow_session_status())
	{
		output_error(tokens[1].integer(buf), "permission denied", out);
		return;
	}

	int id = tokens[1].integer(buf);

	// [ RPC_RESPONSE, req-id, [num-connections] ]

	session_status sst = m_ses.status();

	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);
	out.append_int(sst.num_peers);
}

char const* deluge_state_str(torrent_status const& st)
{
	if (!st.error.empty())
		return "Error";

	if (st.state == torrent_status::allocating)
		return "Allocating";

	if (st.paused && st.auto_managed)
		return "Queued";

	if (st.paused && !st.auto_managed)
		return "Paused";

	if (st.state == torrent_status::checking_files
		|| st.state == torrent_status::checking_resume_data)
		return "Checking";

	if (st.state == torrent_status::seeding
		|| st.state == torrent_status::finished)
		return "Seeding";

	return "Downloading";
}

char const* torrent_keys[] = {
	"active_time",
	"all_time_download",
	"compact",
	"distributed_copies",
	"download_payload_rate",

	"eta",
	"file_priorities",
	"hash",
	"is_auto_managed",
	"is_finished",

	"max_connections",
	"max_download_speed",
	"max_upload_slots",
	"max_upload_speed",
	"message",

	"move_on_completed_path",
	"move_on_completed",
	"move_completed_path",
	"move_completed",
	"name",

	"next_announce",
	"num_peers",
	"num_seeds",
	"paused",
	"prioritize_first_last",

	"progress",
	"queue",
	"remove_at_ratio",
	"save_path",
	"seeding_time",

	"seeds_peers_ratio",
	"seed_rank",
	"state",
	"stop_at_ratio",
	"stop_ratio",

	"time_added",
	"total_done",
	"total_payload_download",
	"total_payload_upload",
	"total_peers",

	"total_seeds",
	"total_uploaded",
	"total_wanted",
	"tracker",
	"trackers",

	"tracker_status",
	"upload_payload_rate"
};

bool yes(torrent_status const& st) { return true;}

// input [id, method, [ { ... }, [ ... ], bool ] ]
//                   filter_dict  keys    diff
void deluge::handle_get_torrents_status(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	if (!st->perms->allow_list())
	{
		output_error(tokens[1].integer(buf), "permission denied", out);
		return;
	}

	int id = tokens[1].integer(buf);

	rtok_t const* filter_dict = &tokens[4];
	rtok_t const* keys = skip_item(filter_dict);
	rtok_t const* diff = skip_item(keys);

	boost::uint64_t key_mask = 0;
	int num_keys = keys->num_items();
	int num_invalid_keys = 0;

	++keys;

	for (int i = 0; i < num_keys; ++i)
	{
		if (keys[i].type() != type_string)
		{
			output_error(id, "invalid argument", out);
			return;
		}

		std::string k = keys[i].string(buf);
		bool found = false;
		for (int j = 0; j < sizeof(torrent_keys)/sizeof(torrent_keys[0]); ++j)
		{
			if (k != torrent_keys[j]) continue;
			key_mask |= 1LL << j;
			found = true;
		}
		if (!found)
		{
			fprintf(stderr, "invalid torrent key: %s\n", k.c_str());
			++num_invalid_keys;
		}
	}

	num_keys -= num_invalid_keys;
	if (num_keys == 0)
	{
		key_mask = ~0LL;
		num_keys = sizeof(torrent_keys)/sizeof(torrent_keys[0]);
	}

	// TODO: use a predicate function to only return torrents that match
	// the filter dict

	// TODO: pass in a query_mask depending on key_mask

	std::vector<torrent_status> torrents;
	m_ses.get_torrent_status(&torrents, yes, 0xffffffff);

	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);

	out.append_dict();

	for (std::vector<torrent_status>::iterator i = torrents.begin()
		, end(torrents.end()); i != end; ++i)
	{
		// key in the dict
		out.append_string(to_hex(i->info_hash.to_string()));

		// the value, is a dict
		bool need_term = out.append_dict(num_keys);

#define MAYBE_ADD(op) \
		if (key_mask & (1LL << idx)) { \
			out.append_string(torrent_keys[idx]); \
			op; \
		} \
		++idx

		int idx = 0;

		MAYBE_ADD(out.append_int(i->active_time));
		MAYBE_ADD(out.append_int(i->all_time_download));
#ifdef TORRENT_NO_DEPRECATE
		MAYBE_ADD(out.append_bool(i->storage_mode == compact_allocation));
#else
		MAYBE_ADD(out.append_bool(false));
#endif
		MAYBE_ADD(out.append_float(i->distributed_copies));
		MAYBE_ADD(out.append_int(i->download_payload_rate));

		MAYBE_ADD(out.append_int(i->download_payload_rate > 0
			? (i->total_wanted - i->total_wanted_done) / i->download_payload_rate : -1));
		MAYBE_ADD(out.append_list(0)); // TODO: support file_priorities
		MAYBE_ADD(out.append_string(i->info_hash.to_string()));
		MAYBE_ADD(out.append_bool(i->auto_managed));
		MAYBE_ADD(out.append_bool(i->is_finished));

		MAYBE_ADD(out.append_int(i->connections_limit));
		MAYBE_ADD(out.append_int(i->handle.download_limit()));
		MAYBE_ADD(out.append_int(i->uploads_limit));
		MAYBE_ADD(out.append_int(i->handle.upload_limit()));
		MAYBE_ADD(out.append_string(i->error));

		MAYBE_ADD(out.append_string("")); // move on completed path
		MAYBE_ADD(out.append_bool(false)); // move on completed
		MAYBE_ADD(out.append_string("")); // move completed path
		MAYBE_ADD(out.append_bool(false)); // move completed
		MAYBE_ADD(out.append_string(i->handle.name()));

		MAYBE_ADD(out.append_int(i->next_announce.total_seconds()));
		MAYBE_ADD(out.append_int(i->num_peers));
		MAYBE_ADD(out.append_int(i->num_seeds));
		MAYBE_ADD(out.append_bool(i->paused));
		MAYBE_ADD(out.append_bool(false)); // prioritize first+last

		MAYBE_ADD(out.append_float(i->progress));
		MAYBE_ADD(out.append_int(i->queue_position));
		MAYBE_ADD(out.append_bool(false)); // remove at ratio
		MAYBE_ADD(out.append_string(i->handle.save_path()));
		MAYBE_ADD(out.append_int(i->seeding_time));

		MAYBE_ADD(out.append_int(0)); // seeds peers ratio
		MAYBE_ADD(out.append_int(i->seed_rank));
		MAYBE_ADD(out.append_string(deluge_state_str(*i)));
		MAYBE_ADD(out.append_bool(false)); // stop at ratio
		MAYBE_ADD(out.append_int(0)); // stop ratio

		MAYBE_ADD(out.append_int(i->added_time));
		MAYBE_ADD(out.append_int(i->total_done));
		MAYBE_ADD(out.append_int(i->total_payload_download));
		MAYBE_ADD(out.append_int(i->total_payload_upload));
		MAYBE_ADD(out.append_int(i->list_peers));

		MAYBE_ADD(out.append_int(i->list_seeds));
		MAYBE_ADD(out.append_int(i->total_upload));
		MAYBE_ADD(out.append_int(i->total_wanted));
		MAYBE_ADD(out.append_string(i->current_tracker));
		MAYBE_ADD(out.append_list(0)); // trackers

		MAYBE_ADD(out.append_string("")); // tracker status
		MAYBE_ADD(out.append_int(i->upload_payload_rate));
		TORRENT_ASSERT(idx == sizeof(torrent_keys)/sizeof(torrent_keys[0]));

		if (need_term) out.append_term();
	}

	out.append_term();
}

// [id, method, [filename, torrent_file, options-dict], {}]
void deluge::handle_add_torrent_file(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	if (!st->perms->allow_add())
	{
		output_error(tokens[1].integer(buf), "permission denied", out);
		return;
	}

	int id = tokens[1].integer(buf);

	std::string filename = tokens[4].string(buf);
	std::string file = tokens[5].string(buf);
	rtok_t const* options = &tokens[6];

	file = base64decode(file);

	add_torrent_params p = m_params_model;

	error_code ec;
	p.ti = boost::intrusive_ptr<torrent_info>(new torrent_info(&file[0], file.size(), ec));
	if (ec)
	{
		output_error(id, ec.message().c_str(), out);
		return;
	}

	int num_options = options->num_items();
	++options;
	for (int i = 0; i < num_options; ++i, options = skip_item(skip_item(options)))
	{
		if (options->type() != type_string) continue;
		std::string key = options->string(buf);

		if (key == "add_paused")
		{
			if (options[1].type() != type_bool) continue;
			p.paused = options[1].boolean(buf);
		}
		else if (key == "max_download_speed")
		{
			if (options[1].type() != type_float) continue;
			p.download_limit = options[1].floating_point(buf) * 1000;
		}
		else if (key == "max_upload_speed")
		{
			if (options[1].type() != type_float) continue;
			p.upload_limit = options[1].floating_point(buf) * 1000;
		}
		else if (key == "download_location")
		{
			if (options[1].type() != type_string) continue;
			p.save_path = options[1].string(buf);
		}
		else if (key == "max_upload_slots")
		{
			if (options[1].type() != type_integer) continue;
			p.max_uploads = options[1].integer(buf);
		}
		else if (key == "file_priorities")
		{
// TODO: implement this	
		}
		else if (key == "max_connections")
		{
			if (options[1].type() != type_integer) continue;
			p.max_connections = options[1].integer(buf);
		}
		else
		{
			fprintf(stderr, "unknown torrent option: \"%s\"\n", key.c_str());
		}
	}

	torrent_handle h = m_ses.add_torrent(p, ec);
	if (ec)
	{
		output_error(id, ec.message().c_str(), out);
		return;
	}

	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);
	out.append_int(h.id());
}

void deluge::handle_get_filter_tree(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	if (!st->perms->allow_list())
	{
		output_error(tokens[1].integer(buf), "permission denied", out);
		return;
	}

	int id = tokens[1].integer(buf);

	session_status sst = m_ses.status();

	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);
	out.append_dict(1);
	out.append_string("state");
	out.append_list(2);

	out.append_list(2);
	out.append_string("All");
	out.append_int(sst.num_torrents);

	out.append_list(2);
	out.append_string("Paused");
	out.append_int(sst.num_paused_torrents);
}

void deluge::handle_get_config_values(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	int id = tokens[1].integer(buf);

	aux::session_settings sett = m_ses.get_settings();
	
	rtok_t const* keys = &tokens[4];
	int num_keys = keys->num_items();
	++keys;

	// [ RPC_RESPONSE, req-id, <config value> ]

	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);
	bool need_term = out.append_dict(num_keys);
	for (int i = 0; i < num_keys; ++i, keys = skip_item(keys))
	{
		if (keys->type() != type_string)
		{
			out.clear();
			output_error(id, "invalid argument", out);
			return;
		}
		std::string config_name = keys->string(buf);
		out.append_string(config_name);
		output_config_value(config_name, sett, out, st->perms);
	}
}

void deluge::handle_get_session_status(conn_state* st)
{
	rencoder& out = *st->out;
	char const* buf = st->buf;
	rtok_t const*tokens = st->tokens;

	if (!st->perms->allow_session_status())
	{
		output_error(tokens[1].integer(buf), "permission denied", out);
		return;
	}

	int id = tokens[1].integer(buf);
	
	rtok_t const* keys = &tokens[4];
	int num_keys = keys->num_items();
	++keys;

	session_status sst = m_ses.status();

	out.append_list(3);
	out.append_int(RPC_RESPONSE);
	out.append_int(id);
	bool need_term = out.append_dict(num_keys);
	for (int i = 0; i < num_keys; ++i, keys = skip_item(keys))
	{
		if (keys->type() != type_string)
			continue;
		std::string k = keys->string(buf);
		out.append_string(k);

		if (k == "payload_upload_rate")
			out.append_int(sst.payload_upload_rate);
		else if (k == "payload_download_rate")
			out.append_int(sst.payload_download_rate);
		else if (k == "payload_download_rate")
			out.append_int(sst.payload_download_rate);
		else if (k == "download_rate")
			out.append_int(sst.download_rate);
		else if (k == "upload_rate")
			out.append_int(sst.upload_rate);
		else if (k == "has_incoming_connections")
			out.append_bool(sst.has_incoming_connections);
		else if (k == "dht_nodes")
			out.append_int(sst.dht_nodes);
		else
			out.append_none();
	}
	if (need_term) out.append_term();
}

void deluge::output_error(int id, char const* msg, rencoder& out)
{
	// [ RPC_ERROR, req-id, [msg, args, trace] ]

	out.append_list(3);
	out.append_int(RPC_ERROR);
	out.append_int(id);
	out.append_list(3);
	out.append_string(msg); // exception name
	out.append_string(""); // args
	out.append_string(""); // stack-trace
}

struct no_permissions : permissions_interface
{
	no_permissions() {}
	bool allow_start() const { return false; }
	bool allow_stop() const { return false; }
	bool allow_recheck() const { return false; }
	bool allow_list() const { return false; }
	bool allow_add() const { return false; }
	bool allow_remove() const { return false; }
	bool allow_remove_data() const { return false; }
	bool allow_queue_change() const { return false; }
	bool allow_get_settings(int) const { return false; }
	bool allow_set_settings(int) const { return false; }
	bool allow_get_data() const { return false; }
	bool allow_session_status() const { return false; }
};

const static no_permissions no_perms;

void deluge::connection_thread()
{
	conn_state st;
	// initialize to no-permissions. The only way to
	// increase the permission level is to log in
	st.perms = &no_perms;

	mutex::scoped_lock l(m_mutex);
	while (!m_shutdown)
	{
		l.lock();
		while (!m_shutdown && m_jobs.empty())
			m_cond.wait(l);

		if (m_shutdown) return;

		fprintf(stderr, "connection thread woke up: %d\n", int(m_jobs.size()));
		
		ssl_socket* sock = m_jobs.front();
		m_jobs.erase(m_jobs.begin());
		l.unlock();

		error_code ec;
		sock->handshake(boost::asio::ssl::stream_base::server, ec);
		if (ec)
		{
			fprintf(stderr, "ssl handshake: %s\n", ec.message().c_str());
			sock->lowest_layer().close(ec);
			delete sock;
			continue;
		}
		fprintf(stderr, "SSL handshake done\n");

		std::vector<char> buffer;
		std::vector<char> inflated;
		buffer.resize(2048);
		do
		{
			int buffer_use = 0;
			error_code ec;

			int ret;
			z_stream strm;

read_some_more:
			TORRENT_ASSERT(buffer.size() > 0);
			TORRENT_ASSERT(buffer.size() - buffer_use > 0);
			ret = sock->read_some(asio::buffer(&buffer[buffer_use]
				, buffer.size() - buffer_use), ec);
			if (ec)
			{
				fprintf(stderr, "read: %s\n", ec.message().c_str());
				break;
			}
			TORRENT_ASSERT(ret > 0);
//			fprintf(stderr, "read %d bytes (%d/%d)\n", int(ret), buffer_use, int(buffer.size()));

			buffer_use += ret;
	
parse_message:
			// assume no more than a 1:10 compression ratio
			inflated.resize(buffer_use * 10);

			memset(&strm, 0, sizeof(strm));
			ret = inflateInit(&strm);
			if (ret != Z_OK)
			{
				fprintf(stderr, "inflateInit failed: %d\n", ret);
				break;
			}
			strm.next_in = (Bytef*)&buffer[0];
			strm.avail_in = buffer_use;

			strm.next_out = (Bytef*)&inflated[0];
			strm.avail_out = inflated.size();

			ret = inflate(&strm, Z_NO_FLUSH);

			// TODO: in some cases we should just abort as well
			if (ret != Z_STREAM_END)
			{
				if (buffer_use + 512 > buffer.size())
				{
					// don't let the client send infinitely
					// big messages
					if (buffer_use > 1024 * 1024)
					{
						fprintf(stderr, "compressed message size exceeds 1 MB\n");
						break;
					}
					// make sure we have enough space in the
					// incoming buffer.
					buffer.resize(buffer_use + buffer_use / 2 + 512);
				}
				inflateEnd(&strm);
				fprintf(stderr, "inflate: %d\n", ret);
				goto read_some_more;
			}

			// truncate the out buffer to only contain the message
			inflated.resize(inflated.size() - strm.avail_out);

			int consumed_bytes = (char*)strm.next_in - &buffer[0];
			TORRENT_ASSERT(consumed_bytes > 0);

			inflateEnd(&strm);

			rtok_t tokens[200];
			ret = rdecode(tokens, 200, &inflated[0], inflated.size());

//			fprintf(stderr, "rdecode: %d\n", ret);

			rencoder out;

			// an RPC call is at least 5 tokens
			// list, ID, method, args, kwargs
			if (ret < 5) break;

			// each RPC call must be a list of the 4 items
			// it could also be multiple RPC calls wrapped
			// in a list.
			if (tokens[0].type() != type_list) break;

			st.tokens = tokens;
			st.buf = &inflated[0];
			st.out = &out;

			if (tokens[1].type() == type_list)
			{
				int num_items = tokens->num_items();
				for (rtok_t* rpc = &tokens[1]; num_items; --num_items, rpc = skip_item(rpc))
				{
					incoming_rpc(&st);
					write_response(out, sock, ec);
					out.clear();
					if (ec) break;
				}
				if (ec) break;
			}
			else
			{
				incoming_rpc(&st);
				write_response(out, sock, ec);
				if (ec) break;
			}

			// flush anything written to the SSL socket
			BIO* bio = SSL_get_wbio(sock->native_handle());
			TORRENT_ASSERT(bio);
			if (bio) BIO_flush(bio);

			buffer.erase(buffer.begin(), buffer.begin() + consumed_bytes);
			buffer_use -= consumed_bytes;
//			fprintf(stderr, "consumed %d bytes (%d left)\n", consumed_bytes, buffer_use);
	
			// there's still data in the in-buffer that may be a message
			// don't get stuck in read if we have more messages to parse
			if (buffer_use > 0) goto parse_message;
	
			if (buffer.size() < 2048) buffer.resize(2048);

		} while (!m_shutdown);

		fprintf(stderr, "closing connection\n");
		sock->shutdown(ec);
		sock->lowest_layer().close(ec);
		delete sock;
	}

}

void deluge::write_response(rencoder const& out, ssl_socket* sock, error_code& ec)
{
	// ----
	rtok_t tmp[2000];
	int r = rdecode(tmp, 2000, out.data(), out.len());
	TORRENT_ASSERT(r > 0);
	printf("==> ");
	print_rtok(tmp, out.data());
	printf("\n");
	// ----

	z_stream strm;
	memset(&strm, 0, sizeof(strm));
	int ret = deflateInit(&strm, 9);
	if (ret != Z_OK) return;

	std::vector<char> deflated(out.len() * 3);
	strm.next_in = (Bytef*)out.data();
	strm.avail_in = out.len();
	strm.next_out = (Bytef*)&deflated[0];
	strm.avail_out = deflated.size();

	ret = deflate(&strm, Z_FINISH);

	deflated.resize(deflated.size() - strm.avail_out);
	deflateEnd(&strm);
	if (ret != Z_STREAM_END) return;

	ret = asio::write(*sock, asio::buffer(&deflated[0], deflated.size()), ec);
	if (ec)
	{
		fprintf(stderr, "write: %s\n", ec.message().c_str());
		return;
	}
//	fprintf(stderr, "wrote %d bytes\n", ret);
}

void deluge::start(int port)
{
	if (m_accept_thread)
		stop();

	m_accept_thread = new thread(boost::bind(&deluge::accept_thread, this, port));
}

void deluge::do_stop()
{
	mutex::scoped_lock l(m_mutex);
	m_shutdown = true;
	m_cond.notify_all();
	if (m_listen_socket)
	{
		m_listen_socket->close();
		m_listen_socket = NULL;
	}
}

void deluge::stop()
{
	m_ios.post(boost::bind(&deluge::do_stop, this));

	m_accept_thread->join();
	delete m_accept_thread;
	m_accept_thread = NULL;
}

