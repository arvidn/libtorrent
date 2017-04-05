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
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/assert.hpp"

#if defined TORRENT_WINDOWS && !defined TORRENT_BUILD_SIMULATOR
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <iphlpapi.h>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

namespace libtorrent
{
	namespace
	{
#if TORRENT_USE_SYSTEMCONFIGURATION && !defined TORRENT_BUILD_SIMULATOR
	// based on code from https://developer.apple.com/library/content/technotes/tn1145/_index.html

	OSStatus MoreSCErrorBoolean(Boolean success)
	{
		OSStatus err;
		int scErr;

		err = noErr;
		if (!success)
		{
			scErr = SCError();
			if (scErr == kSCStatusOK)
			{
				scErr = kSCStatusFailed;
			}
			// Return an SCF error directly as an OSStatus.
			err = scErr;
		}
		return err;
	}

	OSStatus MoreSCError(const void *value)
	{
		return MoreSCErrorBoolean(value != nullptr);
	}

	// Maps Core Foundation error indications (such as they
	// are) to the OSStatus domain.
	OSStatus CFQError(CFTypeRef cf)
	{
		OSStatus err;

		err = noErr;
		if (cf == nullptr)
		{
			err = -1;//coreFoundationUnknownErr
		}
		return err;
	}

	// A version of CFRelease that's tolerant of NULL.
	void CFQRelease(CFTypeRef cf)
	{
		if (cf != nullptr)
		{
			CFRelease(cf);
		}
	}

	// Create a SCF dynamic store reference and a
	// corresponding CFRunLoop source. If you add the
	// run loop source to your run loop then the supplied
	// callback function will be called when local IP
	// address list changes.
	OSStatus CreateIPAddressListChangeCallbackSCF(SCDynamicStoreCallBack callback
		, void *context_info, SCDynamicStoreRef *store, CFRunLoopSourceRef *source)
	{
		OSStatus err;
		SCDynamicStoreContext context = {0, nullptr, nullptr, nullptr, nullptr};
		SCDynamicStoreRef ref = nullptr;
		CFStringRef patterns[2] = {nullptr, nullptr};
		CFArrayRef pattern_list = nullptr;
		CFRunLoopSourceRef rls = nullptr;

		TORRENT_ASSERT(callback != nullptr);
		TORRENT_ASSERT(store != nullptr);
		TORRENT_ASSERT(*store == nullptr);
		TORRENT_ASSERT(source != nullptr);
		TORRENT_ASSERT(*source == nullptr);

		// Create a connection to the dynamic store, then create
		// a search pattern for IPv4 and IPv6.

		context.info = context_info;
		ref = SCDynamicStoreCreate(nullptr, CFSTR("AddIPAddressListChangeCallbackSCF")
			, callback, &context);
		err = MoreSCError(ref);
		if (err == noErr)
		{
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
			// "State:/Network/Service/[^/]+/IPv4".
			patterns[0] = SCDynamicStoreKeyCreateNetworkServiceEntity(nullptr
				, kSCDynamicStoreDomainState, kSCCompAnyRegex, kSCEntNetIPv4);
			err = MoreSCError(patterns[0]);
			if (err == noErr)
			{
				// "State:/Network/Service/[^/]+/IPv6".
				patterns[1] = SCDynamicStoreKeyCreateNetworkServiceEntity(nullptr
					, kSCDynamicStoreDomainState, kSCCompAnyRegex, kSCEntNetIPv6);
				err = MoreSCError(patterns[1]);
			}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
		}

		// Create a pattern list containing the patterns,
		// then tell SCF that we want to watch changes in keys
		// that match that pattern list, then create our run loop
		// source.

		if (err == noErr)
		{
			pattern_list = CFArrayCreate(nullptr
				, reinterpret_cast<const void **>(&patterns), 2, &kCFTypeArrayCallBacks);
			err = CFQError(pattern_list);
		}
		if (err == noErr)
		{
			err = MoreSCErrorBoolean(SCDynamicStoreSetNotificationKeys(ref
				, nullptr, pattern_list));
		}
		if (err == noErr)
		{
			rls = SCDynamicStoreCreateRunLoopSource(nullptr, ref, 0);
			err = MoreSCError(rls);
		}

		// Clean up.

		CFQRelease(patterns[0]);
		CFQRelease(patterns[1]);
		CFQRelease(pattern_list);
		if (err != noErr)
		{
			CFQRelease(ref);
			ref = nullptr;
		}
		*store = ref;
		*source = rls;

		TORRENT_ASSERT((err == noErr) == (*store != nullptr));
		TORRENT_ASSERT((err == noErr) == (*source != nullptr));

		return err;
	}
#endif
	}

	ip_change_notifier::ip_change_notifier(io_service& ios)
#if defined TORRENT_BUILD_SIMULATOR
#elif TORRENT_USE_NETLINK
		: m_socket(ios
			, netlink::endpoint(netlink(NETLINK_ROUTE), RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR))
#elif TORRENT_USE_SYSTEMCONFIGURATION
		: m_ios(ios)
#elif defined TORRENT_WINDOWS
		: m_hnd(ios, WSACreateEvent())
#endif
	{
#if defined TORRENT_BUILD_SIMULATOR
		TORRENT_UNUSED(ios);
#elif defined TORRENT_WINDOWS
		if (!m_hnd.is_open()) aux::throw_ex<system_error>(WSAGetLastError(), system_category());
		m_ovl.hEvent = m_hnd.native_handle();
#elif !TORRENT_USE_NETLINK
		TORRENT_UNUSED(ios);
#endif
	}

	ip_change_notifier::~ip_change_notifier()
	{
#if defined TORRENT_BUILD_SIMULATOR
#elif TORRENT_USE_SYSTEMCONFIGURATION
		cancel();
#elif defined TORRENT_WINDOWS
		cancel();
		m_hnd.close();
#endif
	}

	void ip_change_notifier::async_wait(std::function<void(error_code const&)> cb)
	{
#if defined TORRENT_BUILD_SIMULATOR
		// TODO: simulator support
		cb(make_error_code(boost::system::errc::not_supported));
#elif TORRENT_USE_NETLINK
		using namespace std::placeholders;
		m_socket.async_receive(boost::asio::buffer(m_buf)
			, std::bind(&ip_change_notifier::on_notify, this, _1, _2, cb));
#elif TORRENT_USE_SYSTEMCONFIGURATION
		m_cb = std::move(cb);
		if (m_source != nullptr) return; // already setup
		auto on_notify_cb = [](SCDynamicStoreRef /*store*/, CFArrayRef /*changedKeys*/, void *info)
		{
			ip_change_notifier* obj = static_cast<ip_change_notifier*>(info);
			obj->m_ios.post([obj]() { obj->m_cb(error_code()); });
		};
		OSStatus err = CreateIPAddressListChangeCallbackSCF(on_notify_cb, this, &m_store, &m_source);
		if (err == noErr)
		{
			CFRunLoopAddSource(CFRunLoopGetMain(), m_source, kCFRunLoopDefaultMode);
		}
		else
		{
			m_ios.post([this, err]() { m_cb(error_code(err, system_category())); });
		}
#elif defined TORRENT_WINDOWS
		HANDLE hnd;
		DWORD err = NotifyAddrChange(&hnd, &m_ovl);
		if (err == ERROR_IO_PENDING)
		{
			m_hnd.async_wait([this, cb](error_code const& ec) { on_notify(ec, 0, cb); });
		}
		else
		{
			m_hnd.get_io_service().post([cb, err]()
				{ cb(error_code(err, system_category())); });
		}
#else
		cb(make_error_code(boost::system::errc::not_supported));
#endif
	}

	void ip_change_notifier::cancel()
	{
#if defined TORRENT_BUILD_SIMULATOR
#elif TORRENT_USE_NETLINK
		m_socket.cancel();
#elif TORRENT_USE_SYSTEMCONFIGURATION
		m_cb = nullptr;
		if (m_source != nullptr)
		{
			CFRetain(m_source); // to prevent internal memory release
			CFRunLoopRemoveSource(CFRunLoopGetMain(), m_source, kCFRunLoopDefaultMode);
		}
		CFQRelease(m_store);
		CFQRelease(m_source);
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
