/*

Copyright (c) 2016, Steven Siloti
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

#include "libtorrent/ip_notifier.hpp"

#if defined TORRENT_WINDOWS && !defined TORRENT_BUILD_SIMULATOR
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <iphlpapi.h>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

using namespace std::placeholders;

namespace libtorrent
{
	ip_change_notifier::ip_change_notifier(io_service& ios)
#if defined TORRENT_BUILD_SIMULATOR
#elif TORRENT_USE_NETLINK
		: m_socket(ios
			, netlink::endpoint(netlink(NETLINK_ROUTE), RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR))
#elif defined TORRENT_WINDOWS
		: m_hnd(ios, WSACreateEvent())
#endif
	{
#if defined TORRENT_BUILD_SIMULATOR
		TORRENT_UNUSED(ios);
#elif defined TORRENT_WINDOWS
		if (!m_hnd.is_open())
#ifndef BOOST_NO_EXCEPTIONS
			throw system_error(WSAGetLastError(), system_category());
#else
			std::terminate();
#endif // BOOST_NO_EXCEPTIONS
		m_ovl.hEvent = m_hnd.native_handle();
#elif !TORRENT_USE_NETLINK
		TORRENT_UNUSED(ios);
#endif
	}

	ip_change_notifier::~ip_change_notifier()
	{
#if defined TORRENT_WINDOWS && !defined TORRENT_BUILD_SIMULATOR
		cancel();
		m_hnd.close();
#endif
	}

	void ip_change_notifier::async_wait(std::function<void(error_code const&)> cb)
	{
#if defined TORRENT_BUILD_SIMULATOR
		TORRENT_UNUSED(cb);
#elif TORRENT_USE_NETLINK
		m_socket.async_receive(boost::asio::buffer(m_buf)
			, std::bind(&ip_change_notifier::on_notify, this, _1, _2, cb));
#elif defined TORRENT_WINDOWS
		HANDLE hnd;
		DWORD err = NotifyAddrChange(&hnd, &m_ovl);
		if (err == ERROR_IO_PENDING)
		{
			m_hnd.async_wait([this, cb](error_code const& ec) { on_notify(ec, 0, cb); });
		}
		else
		{
			m_hnd.get_io_service().post([this, cb, err]()
				{ cb(error_code(err, system_category())); });
		}
#else
		TORRENT_UNUSED(cb);
#endif
	}

	void ip_change_notifier::cancel()
	{
#if defined TORRENT_BUILD_SIMULATOR
#elif TORRENT_USE_NETLINK
		m_socket.cancel();
#elif defined TORRENT_WINDOWS
		CancelIPChangeNotify(&m_ovl);
		m_hnd.cancel();
#endif
	}

	void ip_change_notifier::on_notify(error_code const& ec
		, std::size_t bytes_transferred
		, std::function<void(error_code const&)> cb)
	{
		TORRENT_UNUSED(bytes_transferred);

		// on linux we could parse the message to get information about the
		// change but Windows requires the application to enumerate the
		// interfaces after a notification so do that for Linux as well to
		// minimize the difference between platforms

		// Linux can generate ENOBUFS if the socket's buffers are full
		// don't treat it as an error
		if (ec.value() == boost::system::errc::no_buffer_space)
			cb(error_code());
		else
			cb(ec);
	}
}
