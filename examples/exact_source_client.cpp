/*

Copyright (c) 2003, 2005, 2009, 2015-2017, 2020, Arvid Norberg
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

// based on simple_client.cpp

#include <cstdlib>
#include "libtorrent/entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/alert_types.hpp"

#include "torrent_view.hpp"
#include "session_view.hpp"
#include "print.hpp"

#include <iostream>

using lt::alert;

FILE* g_log_file = nullptr;

char const* timestamp()
{
	time_t t = std::time(nullptr);
#ifdef TORRENT_WINDOWS
	std::tm const* timeinfo = localtime(&t);
#else
	std::tm buf;
	std::tm const* timeinfo = localtime_r(&t, &buf);
#endif
	static char str[200];
	std::strftime(str, 200, "%b %d %X", timeinfo);
	return str;
}

void print_alert(lt::alert const* a, std::string& str)
{
	using namespace lt;

	if (a->category() & alert_category::error)
	{
		str += esc("31");
	}
	else if (a->category() & (alert_category::peer | alert_category::storage))
	{
		str += esc("33");
	}
	str += "[";
	str += timestamp();
	str += "] ";
	str += a->message();
	str += esc("0");

	static auto const first_ts = a->timestamp();

	if (g_log_file)
		std::fprintf(g_log_file, "[%" PRId64 "] %s\n"
			, std::int64_t(duration_cast<std::chrono::milliseconds>(a->timestamp() - first_ts).count())
			,  a->message().c_str());
	else
		std::printf("[%" PRId64 "] %s\n"
			, std::int64_t(duration_cast<std::chrono::milliseconds>(a->timestamp() - first_ts).count())
			,  a->message().c_str());
}

struct client_state_t
{
	torrent_view& view;
	session_view& ses_view;
	std::deque<std::string> events;
	std::vector<lt::peer_info> peers;
	std::vector<std::int64_t> file_progress;
	std::vector<lt::partial_piece_info> download_queue;
	std::vector<lt::block_info> download_queue_block_info;
	std::vector<int> piece_availability;
	std::vector<lt::announce_entry> trackers;

	void clear()
	{
		peers.clear();
		file_progress.clear();
		download_queue.clear();
		download_queue_block_info.clear();
		piece_availability.clear();
		trackers.clear();
	}
};

/*
// returns true if the alert was handled (and should not be printed to the log)
// returns false if the alert was not handled
bool handle_alert(client_state_t& client_state, lt::alert* a)
{
	using namespace lt;

	if (session_stats_alert* s = alert_cast<session_stats_alert>(a))
	{
		client_state.ses_view.update_counters(s->counters(), s->timestamp());
		return !stats_enabled;
	}

	if (auto* p = alert_cast<peer_info_alert>(a))
	{
		if (client_state.view.get_active_torrent().handle == p->handle)
			client_state.peers = std::move(p->peer_info);
		return true;
	}

	if (auto* p = alert_cast<file_progress_alert>(a))
	{
		if (client_state.view.get_active_torrent().handle == p->handle)
			client_state.file_progress = std::move(p->files);
		return true;
	}

	if (auto* p = alert_cast<piece_info_alert>(a))
	{
		if (client_state.view.get_active_torrent().handle == p->handle)
		{
			client_state.download_queue = std::move(p->piece_info);
			client_state.download_queue_block_info = std::move(p->block_data);
		}
		return true;
	}

	if (auto* p = alert_cast<piece_availability_alert>(a))
	{
		if (client_state.view.get_active_torrent().handle == p->handle)
			client_state.piece_availability = std::move(p->piece_availability);
		return true;
	}

	if (auto* p = alert_cast<tracker_list_alert>(a))
	{
		if (client_state.view.get_active_torrent().handle == p->handle)
			client_state.trackers = std::move(p->trackers);
		return true;
	}

#ifndef TORRENT_DISABLE_DHT
	if (dht_stats_alert* p = alert_cast<dht_stats_alert>(a))
	{
		dht_active_requests = p->active_requests;
		dht_routing_table = p->routing_table;
		return true;
	}
#endif

#ifdef TORRENT_SSL_PEERS
	if (torrent_need_cert_alert* p = alert_cast<torrent_need_cert_alert>(a))
	{
		torrent_handle h = p->handle;
		std::string base_name = path_append("certificates", to_hex(h.info_hash()));
		std::string cert = base_name + ".pem";
		std::string priv = base_name + "_key.pem";

#ifdef TORRENT_WINDOWS
		struct ::_stat st;
		int ret = ::_stat(cert.c_str(), &st);
		if (ret < 0 || (st.st_mode & _S_IFREG) == 0)
#else
		struct ::stat st;
		int ret = ::stat(cert.c_str(), &st);
		if (ret < 0 || (st.st_mode & S_IFREG) == 0)
#endif
		{
			char msg[256];
			std::snprintf(msg, sizeof(msg), "ERROR. could not load certificate %s: %s\n"
				, cert.c_str(), std::strerror(errno));
			if (g_log_file) std::fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);
			return true;
		}

#ifdef TORRENT_WINDOWS
		ret = ::_stat(priv.c_str(), &st);
		if (ret < 0 || (st.st_mode & _S_IFREG) == 0)
#else
		ret = ::stat(priv.c_str(), &st);
		if (ret < 0 || (st.st_mode & S_IFREG) == 0)
#endif
		{
			char msg[256];
			std::snprintf(msg, sizeof(msg), "ERROR. could not load private key %s: %s\n"
				, priv.c_str(), std::strerror(errno));
			if (g_log_file) std::fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);
			return true;
		}

		char msg[256];
		std::snprintf(msg, sizeof(msg), "loaded certificate %s and key %s\n", cert.c_str(), priv.c_str());
		if (g_log_file) std::fprintf(g_log_file, "[%s] %s\n", timestamp(), msg);

		h.set_ssl_certificate(cert, priv, "certificates/dhparams.pem", "1234");
		h.resume();
	}
#endif

	// don't log every peer we try to connect to
	if (alert_cast<peer_connect_alert>(a)) return true;

	if (peer_disconnected_alert* pd = alert_cast<peer_disconnected_alert>(a))
	{
		// ignore failures to connect and peers not responding with a
		// handshake. The peers that we successfully connect to and then
		// disconnect is more interesting.
		if (pd->op == operation_t::connect
			|| pd->error == errors::timed_out_no_handshake)
			return true;
	}

#ifdef _MSC_VER
// it seems msvc makes the definitions of 'p' escape the if-statement here
#pragma warning(push)
#pragma warning(disable: 4456)
#endif

	if (metadata_received_alert* p = alert_cast<metadata_received_alert>(a))
	{
		torrent_handle h = p->handle;
		h.save_resume_data(torrent_handle::save_info_dict);
		++num_outstanding_resume_data;
	}

	if (add_torrent_alert* p = alert_cast<add_torrent_alert>(a))
	{
		if (p->error)
		{
			std::fprintf(stderr, "failed to add torrent: %s %s\n"
				, p->params.ti ? p->params.ti->name().c_str() : p->params.name.c_str()
				, p->error.message().c_str());
		}
		else
		{
			torrent_handle h = p->handle;

			h.save_resume_data(torrent_handle::save_info_dict | torrent_handle::if_metadata_changed);
			++num_outstanding_resume_data;

			// if we have a peer specified, connect to it
			if (!peer.empty())
			{
				auto port = peer.find_last_of(':');
				if (port != std::string::npos)
				{
					peer[port++] = '\0';
					char const* ip = peer.data();
					int const peer_port = atoi(peer.data() + port);
					error_code ec;
					if (peer_port > 0)
						h.connect_peer(tcp::endpoint(make_address(ip, ec), std::uint16_t(peer_port)));
				}
			}
		}
	}

	if (torrent_finished_alert* p = alert_cast<torrent_finished_alert>(a))
	{
		p->handle.set_max_connections(max_connections_per_torrent / 2);

		// write resume data for the finished torrent
		// the alert handler for save_resume_data_alert
		// will save it to disk
		torrent_handle h = p->handle;
		h.save_resume_data(torrent_handle::save_info_dict | torrent_handle::if_download_progress);
		++num_outstanding_resume_data;
		if (exit_on_finish) quit = true;
	}

	if (save_resume_data_alert* p = alert_cast<save_resume_data_alert>(a))
	{
		--num_outstanding_resume_data;
		auto const buf = write_resume_data_buf(p->params);
		save_file(resume_file(p->params.info_hashes), buf);
	}

	if (save_resume_data_failed_alert* p = alert_cast<save_resume_data_failed_alert>(a))
	{
		--num_outstanding_resume_data;
		// don't print the error if it was just that we didn't need to save resume
		// data. Returning true means "handled" and not printed to the log
		return p->error == lt::errors::resume_data_not_modified;
	}

	if (torrent_paused_alert* p = alert_cast<torrent_paused_alert>(a))
	{
		if (!quit)
		{
			// write resume data for the finished torrent
			// the alert handler for save_resume_data_alert
			// will save it to disk
			torrent_handle h = p->handle;
			h.save_resume_data(torrent_handle::save_info_dict);
			++num_outstanding_resume_data;
		}
	}

	if (state_update_alert* p = alert_cast<state_update_alert>(a))
	{
		lt::torrent_handle const prev = client_state.view.get_active_handle();
		client_state.view.update_torrents(std::move(p->status));

		// when the active torrent changes, we need to clear the peers, trackers, files, etc.
		if (client_state.view.get_active_handle() != prev)
			client_state.clear();
		return true;
	}

	if (torrent_removed_alert* p = alert_cast<torrent_removed_alert>(a))
	{
		client_state.view.remove_torrent(std::move(p->handle));
	}
	return false;

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}
*/

void pop_alerts(client_state_t& client_state, lt::session& ses)
{
	std::vector<lt::alert*> alerts;
	ses.pop_alerts(&alerts);
	for (auto a : alerts)
	{
		//if (::handle_alert(client_state, a)) continue;

		// if we didn't handle the alert, print it to the log
		std::string event_string;
		print_alert(a, event_string);
		client_state.events.push_back(event_string);
		if (client_state.events.size() >= 20) client_state.events.pop_front();
	}
}

int main(int argc, char* argv[]) try
{
	if (argc != 2) {
		std::cerr << "usage: ./exact_source_client torrent-file|magnet-link\n"
			// "to stop the client, press return.\n"
			;
		return 1;
	}

	lt::error_code ec;
	lt::add_torrent_params p;
	lt::session_params params;
	auto& settings = params.settings;

	using lt::settings_pack;
	settings.set_bool(settings_pack::enable_dht, false);
	settings.set_bool(settings_pack::enable_upnp, false);
	settings.set_bool(settings_pack::enable_natpmp, false);
	settings.set_bool(settings_pack::enable_outgoing_utp, false);
	settings.set_bool(settings_pack::enable_incoming_utp, false);
	settings.set_bool(settings_pack::enable_outgoing_tcp, false);
	settings.set_bool(settings_pack::enable_incoming_tcp, false);

	// https://www.libtorrent.org/reference-Alerts.html
	settings.set_int(settings_pack::alert_mask,
		lt::alert_category::error
		| lt::alert_category::peer
		| lt::alert_category::port_mapping
		| lt::alert_category::storage
		| lt::alert_category::tracker
		| lt::alert_category::connect
		| lt::alert_category::status
		| lt::alert_category::ip_block
		| lt::alert_category::performance_warning
		| lt::alert_category::dht
		| lt::alert_category::incoming_request
		| lt::alert_category::dht_operation
		| lt::alert_category::port_mapping_log
		| lt::alert_category::file_progress
		| lt::alert_category::peer_log
		| lt::alert_category::torrent_log // debug_log in torrent.cpp
		// | lt::alert_category::piece_progress
		// | lt::alert_category::block_progress
		// | lt::alert_category::port_mapping_log
		// | lt::alert_category::ip_block
		// | lt::alert_category::stats
	);

	// TODO qbittorrent: set default "cas" parameter values
	// similar to "automatically add these trackers to new downloads"

	lt::session ses(std::move(params));

	// based on client_test.cpp
	lt::string_view torrent = argv[1];
	if (torrent.substr(0, 7) == "magnet:") {
		// add_magnet(s, torrent);
		p = lt::parse_magnet_uri(torrent.to_string(), ec);
		if (ec)
		{
			std::printf("invalid magnet link \"%s\": %s\n"
				, torrent.to_string().c_str(), ec.message().c_str());
			return 1;
		}
		// std::printf("adding magnet: %s\n", torrent.to_string().c_str());
	}
	else {
		p.ti = std::make_shared<lt::torrent_info>(torrent.to_string());
	}
	p.save_path = ".";
	ses.add_torrent(p);

	torrent_view view;
	session_view ses_view;
	client_state_t client_state{
		view, ses_view, {}, {}, {}, {}, {}, {}, {}
	};

	while (true) {
		pop_alerts(client_state, ses);
		std::this_thread::sleep_for(lt::milliseconds(1000));
	}

	/*
	// wait for the user to end
	char a;
	int ret = std::scanf("%c\n", &a);
	(void)ret; // ignore
	*/

	return 0;
}
catch (std::exception const& e) {
	std::cerr << "ERROR: " << e.what() << "\n";
}
