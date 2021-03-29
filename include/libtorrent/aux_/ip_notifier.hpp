/*

Copyright (c) 2016, Steven Siloti
Copyright (c) 2017, 2019-2020, Arvid Norberg
Copyright (c) 2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_IP_NOTIFIER_HPP_INCLUDED
#define TORRENT_IP_NOTIFIER_HPP_INCLUDED

#include <functional>
#include <memory>

#include "libtorrent/error_code.hpp"
#include "libtorrent/io_context.hpp"

namespace lt::aux {

	struct TORRENT_EXTRA_EXPORT ip_change_notifier
	{
		// cb will be invoked  when a change is detected in the
		// system's IP addresses
		virtual void async_wait(std::function<void(error_code const&)> cb) = 0;
		virtual void cancel() = 0;

		virtual ~ip_change_notifier() {}
	};

	TORRENT_EXTRA_EXPORT std::unique_ptr<ip_change_notifier> create_ip_notifier(io_context& ios);
}

#endif
