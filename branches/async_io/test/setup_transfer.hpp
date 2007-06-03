#ifndef SETUP_TRANSFER_HPP
#define SETUP_TRANSFER_HPP

#include "libtorrent/session.hpp"
#include <boost/tuple/tuple.hpp>


void test_sleep(int millisec);

boost::tuple<libtorrent::torrent_handle, libtorrent::torrent_handle>
	setup_transfer(libtorrent::session& ses1, libtorrent::session& ses2
	, bool clear_files);

#endif

