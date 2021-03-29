/*

Copyright (c) 2006-2007, 2009, 2015, 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_IO_CONTEXT_HPP_INCLUDED
#define TORRENT_IO_CONTEXT_HPP_INCLUDED

#if defined TORRENT_BUILD_SIMULATOR
#include "simulator/simulator.hpp"
#else
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/ts/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/defer.hpp>
#include <boost/asio/dispatch.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif // SIMULATOR

namespace lt {

#if defined TORRENT_BUILD_SIMULATOR
	using sim::asio::io_context;
#else
	using boost::asio::io_context;
#endif
	using boost::asio::executor_work_guard;
	using boost::asio::make_work_guard;

	using boost::asio::post;
	using boost::asio::dispatch;
	using boost::asio::defer;
}

#endif
