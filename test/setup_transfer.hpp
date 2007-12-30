#ifndef SETUP_TRANSFER_HPP
#define SETUP_TRANSFER_HPP

#include "libtorrent/session.hpp"
#include <boost/tuple/tuple.hpp>


void test_sleep(int millisec);

boost::intrusive_ptr<libtorrent::torrent_info> create_torrent(std::ostream* file = 0);

boost::tuple<libtorrent::torrent_handle, libtorrent::torrent_handle
	, libtorrent::torrent_handle>
setup_transfer(libtorrent::session* ses1, libtorrent::session* ses2
	, libtorrent::session* ses3, bool clear_files, bool use_metadata_transfer = true
	, bool connect = true, std::string suffix = "");

void start_web_server(int port);
void stop_web_server(int port);
void start_proxy(int port, int type);
void stop_proxy(int port);

#endif

