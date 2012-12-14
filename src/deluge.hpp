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

#ifndef TORRENT_DELUGE_HPP
#define TORRENT_DELUGE_HPP

#include <vector>
#include "libtorrent/thread.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/io_service.hpp"

#include <boost/asio/ssl.hpp>

typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket;

namespace libtorrent
{
	class session;
	struct rtok_t;
	struct rencoder;

	struct deluge
	{
		deluge(session& s, std::string pem_path);
		~deluge();

		void start(int port);
		void stop();

		void set_params_model(add_torrent_params const& p)
		{ m_params_model = p; }

		void handle_login(rtok_t const* tokens, char const* buf, rencoder& out);
		void handle_set_event_interest(rtok_t const* tokens, char const* buf, rencoder& out);
		void handle_info(rtok_t const* tokens, char const* buf, rencoder& out);

		void handle_get_config_value(rtok_t const* tokens, char const* buf, rencoder& out);
		void handle_get_config_values(rtok_t const* tokens, char const* buf, rencoder& out);
		void handle_get_session_status(rtok_t const* tokens, char const* buf, rencoder& out);
		void handle_get_enabled_plugins(rtok_t const* tokens, char const* buf, rencoder& out);
		void handle_get_free_space(rtok_t const* tokens, char const* buf, rencoder& out);
		void handle_get_num_connections(rtok_t const* tokens, char const* buf, rencoder& out);
		void handle_get_torrents_status(rtok_t const* tokens, char const* buf, rencoder& out);
		void handle_add_torrent_file(rtok_t const* tokens, char const* buf, rencoder& out);
		void handle_get_filter_tree(rtok_t const* tokens, char const* buf, rencoder& out);

	private:

		void incoming_rpc(rtok_t const* tokens, char const* buf, rencoder& out);
		void output_error(int id, char const* msg, rencoder& out);
		void output_config_value(std::string set_name, aux::session_settings const& sett
			, rencoder& out);

		void write_response(rencoder const& output, ssl_socket* sock, error_code& ec);

		void accept_thread(int port);
		void connection_thread();

		void do_accept();
		void do_stop();
		void on_accept(error_code const& ec, ssl_socket* sock);

		session& m_ses;
		add_torrent_params m_params_model;
		io_service m_ios;
		socket_acceptor* m_listen_socket;
		thread* m_accept_thread;
		std::vector<thread*> m_threads;
		mutex m_mutex;
		condition m_cond;
		boost::asio::ssl::context m_context;

		std::vector<ssl_socket*> m_jobs;
		bool m_shutdown;
	};

}

#endif

