/*

Copyright (c) 2006-2007, 2009, 2015, 2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_IO_SERVICE_HPP_INCLUDED
#define TORRENT_IO_SERVICE_HPP_INCLUDED

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ts/io_context.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

#error warning "this header is deprecated, use io_context.hpp instead"
namespace lt {

	using io_service = boost::asio::io_context;
}

#endif
