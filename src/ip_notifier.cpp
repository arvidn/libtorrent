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

#include "libtorrent/aux_/ip_notifier.hpp"
#include "libtorrent/assert.hpp"

#if defined TORRENT_BUILD_SIMULATOR
// TODO: simulator support
#elif TORRENT_USE_NETLINK
#include "libtorrent/netlink.hpp"
#include <array>
#elif TORRENT_USE_SYSTEMCONFIGURATION
#include <SystemConfiguration/SystemConfiguration.h>
#elif defined TORRENT_WINDOWS
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/asio/windows/object_handle.hpp>
#include <iphlpapi.h>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

namespace libtorrent { namespace aux {

namespace {

#if defined TORRENT_BUILD_SIMULATOR
struct ip_change_notifier_impl final : ip_change_notifier
{
	explicit ip_change_notifier_impl(io_service& ios)
		: m_ios(ios) {}

	void async_wait(std::function<void(error_code const&)> cb) override
	{
		m_ios.post([cb]()
		{ cb(make_error_code(boost::system::errc::not_supported)); });
	}

	void cancel() override {}

private:
	io_service& m_ios;
};
#elif TORRENT_USE_NETLINK
struct ip_change_notifier_impl final : ip_change_notifier
{
	explicit ip_change_notifier_impl(io_service& ios)
		: m_socket(ios
			, netlink::endpoint(netlink(NETLINK_ROUTE), RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR))
	{}

	// noncopyable
	ip_change_notifier_impl(ip_change_notifier_impl const&) = delete;
	ip_change_notifier_impl& operator=(ip_change_notifier_impl const&) = delete;

	void async_wait(std::function<void(error_code const&)> cb) override
	{
		using namespace std::placeholders;
		m_socket.async_receive(boost::asio::buffer(m_buf)
			, std::bind(&ip_change_notifier_impl::on_notify, this, _1, _2, std::move(cb)));
	}

	void cancel() override
	{ m_socket.cancel();}

private:
	netlink::socket m_socket;
	std::array<char, 4096> m_buf;

	void on_notify(error_code const& ec, std::size_t bytes_transferred
		, std::function<void(error_code const&)> const& cb)
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
};
#elif TORRENT_USE_SYSTEMCONFIGURATION

template <typename T> void CFRefRetain(T h) { CFRetain(h); }
template <typename T> void CFRefRelease(T h) { CFRelease(h); }

template <typename T
	, void (*Retain)(T) = CFRefRetain<T>, void (*Release)(T) = CFRefRelease<T>>
struct CFRef
{
	CFRef() {}
	explicit CFRef(T h) : m_h(h) {} // take ownership
	~CFRef() { release(); }

	CFRef(CFRef const& rhs) : m_h(rhs.m_h) { retain(); }
	CFRef& operator=(CFRef const& rhs)
	{
		if (m_h == rhs.m_h) return *this;
		release();
		m_h = rhs.m_h;
		retain();
		return *this;
	}

	CFRef& operator=(T h) { m_h = h; return *this;}
	CFRef& operator=(std::nullptr_t) { release(); return *this;}

	T get() const { return m_h; }
	explicit operator bool() const { return m_h != nullptr; }

private:
	T m_h = nullptr; // handle

	void retain() { if (m_h != nullptr) Retain(m_h); }
	void release() { if (m_h != nullptr) Release(m_h); m_h = nullptr; }
};

void CFDispatchRetain(dispatch_queue_t q) { dispatch_retain(q); }
void CFDispatchRelease(dispatch_queue_t q) { dispatch_release(q); }
using CFDispatchRef = CFRef<dispatch_queue_t, CFDispatchRetain, CFDispatchRelease>;

#if TORRENT_USE_SC_NETWORK_REACHABILITY
CFRef<SCNetworkReachabilityRef> create_reachability(SCNetworkReachabilityCallBack callback
	, void* context_info)
{
	TORRENT_ASSERT(callback != nullptr);

	sockaddr_in addr = {};
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;

	CFRef<SCNetworkReachabilityRef> reach{SCNetworkReachabilityCreateWithAddress(nullptr
		, reinterpret_cast<sockaddr const*>(&addr))};
	if (!reach)
		return CFRef<SCNetworkReachabilityRef>();

	SCNetworkReachabilityContext context = {0, nullptr, nullptr, nullptr, nullptr};
	context.info = context_info;

	return SCNetworkReachabilitySetCallback(reach.get(), callback, &context)
		? reach : CFRef<SCNetworkReachabilityRef>();
}

struct ip_change_notifier_impl final : ip_change_notifier
{
	explicit ip_change_notifier_impl(io_service& ios)
		: m_ios(ios)
	{
		m_queue = dispatch_queue_create("libtorrent.IPChangeNotifierQueue", nullptr);
		m_reach = create_reachability(
			[](SCNetworkReachabilityRef /*target*/, SCNetworkReachabilityFlags /*flags*/, void *info)
			{
				auto obj = static_cast<ip_change_notifier_impl*>(info);
				obj->m_ios.post([obj]()
				{
					if (!obj->m_cb) return;
					auto cb = std::move(obj->m_cb);
					obj->m_cb = nullptr;
					cb(error_code());
				});
			}, this);

		if (!m_queue || !m_reach
			|| !SCNetworkReachabilitySetDispatchQueue(m_reach.get(), m_queue.get()))
			cancel();
	}

	// noncopyable
	ip_change_notifier_impl(ip_change_notifier_impl const&) = delete;
	ip_change_notifier_impl& operator=(ip_change_notifier_impl const&) = delete;

	~ip_change_notifier_impl() override
	{ cancel(); }

	void async_wait(std::function<void(error_code const&)> cb) override
	{
		if (m_queue)
			m_cb = std::move(cb);
		else
			m_ios.post([cb]()
			{ cb(make_error_code(boost::system::errc::not_supported)); });
	}

	void cancel() override
	{
		if (m_reach)
			SCNetworkReachabilitySetDispatchQueue(m_reach.get(), nullptr);

		m_cb = nullptr;
		m_reach = nullptr;
		m_queue = nullptr;
	}

private:
	io_service& m_ios;
	CFDispatchRef m_queue;
	CFRef<SCNetworkReachabilityRef> m_reach;
	std::function<void(error_code const&)> m_cb = nullptr;
};
#else
// see https://developer.apple.com/library/content/technotes/tn1145/_index.html
CFRef<CFMutableArrayRef> create_keys_array()
{
	CFRef<CFMutableArrayRef> keys{CFArrayCreateMutable(nullptr
		, 0, &kCFTypeArrayCallBacks)};

	// "State:/Network/Interface/[^/]+/IPv4"
	CFRef<CFStringRef> key{SCDynamicStoreKeyCreateNetworkInterfaceEntity(nullptr
		, kSCDynamicStoreDomainState, kSCCompAnyRegex, kSCEntNetIPv4)};
	CFArrayAppendValue(keys.get(), key.get());

	// NOTE: for IPv6, you can replicate the above setup with kSCEntNetIPv6
	// but due to the current state of most common configurations, where
	// IPv4 is used alongside with IPv6, you will end up with twice the
	// notifications for the same change

	return keys;
}

CFRef<SCDynamicStoreRef> create_dynamic_store(SCDynamicStoreCallBack callback, void* context_info)
{
	TORRENT_ASSERT(callback != nullptr);

	SCDynamicStoreContext context = {0, nullptr, nullptr, nullptr, nullptr};
	context.info = context_info;
	CFRef<SCDynamicStoreRef> store{SCDynamicStoreCreate(nullptr
		, CFSTR("libtorrent.IPChangeNotifierStore"), callback, &context)};
	if (!store)
		return CFRef<SCDynamicStoreRef>();

	CFRef<CFMutableArrayRef> keys = create_keys_array();
	return SCDynamicStoreSetNotificationKeys(store.get(), nullptr, keys.get())
		? store : CFRef<SCDynamicStoreRef>();
}

struct ip_change_notifier_impl final : ip_change_notifier
{
	explicit ip_change_notifier_impl(io_service& ios)
		: m_ios(ios)
	{
		m_queue = dispatch_queue_create("libtorrent.IPChangeNotifierQueue", nullptr);
		m_store = create_dynamic_store(
			[](SCDynamicStoreRef /*store*/, CFArrayRef /*changedKeys*/, void *info)
			{
				auto obj = static_cast<ip_change_notifier_impl*>(info);
				obj->m_ios.post([obj]()
				{
					if (!obj->m_cb) return;
					auto cb = std::move(obj->m_cb);
					obj->m_cb = nullptr;
					cb(error_code());
				});
			}, this);

		if (!m_queue || !m_store
			|| !SCDynamicStoreSetDispatchQueue(m_store.get(), m_queue.get()))
			cancel();
	}

	// noncopyable
	ip_change_notifier_impl(ip_change_notifier_impl const&) = delete;
	ip_change_notifier_impl& operator=(ip_change_notifier_impl const&) = delete;

	~ip_change_notifier_impl() override
	{ cancel(); }

	void async_wait(std::function<void(error_code const&)> cb) override
	{
		if (m_queue)
			m_cb = std::move(cb);
		else
			m_ios.post([cb]()
			{ cb(make_error_code(boost::system::errc::not_supported)); });
	}

	void cancel() override
	{
		if (m_store)
			SCDynamicStoreSetDispatchQueue(m_store.get(), nullptr);

		m_cb = nullptr;
		m_store = nullptr;
		m_queue = nullptr;
	}

private:
	io_service& m_ios;
	CFDispatchRef m_queue;
	CFRef<SCDynamicStoreRef> m_store;
	std::function<void(error_code const&)> m_cb = nullptr;
};
#endif // TORRENT_USE_SC_NETWORK_REACHABILITY

#elif defined TORRENT_WINDOWS
struct ip_change_notifier_impl final : ip_change_notifier
{
	explicit ip_change_notifier_impl(io_service& ios)
		: m_hnd(ios, WSACreateEvent())
	{
		if (!m_hnd.is_open()) aux::throw_ex<system_error>(WSAGetLastError(), system_category());
		m_ovl.hEvent = m_hnd.native_handle();
	}

	// noncopyable
	ip_change_notifier_impl(ip_change_notifier_impl const&) = delete;
	ip_change_notifier_impl& operator=(ip_change_notifier_impl const&) = delete;

	~ip_change_notifier_impl() override
	{
		cancel();
		// the reason to call close() here is because the
		// object_handle destructor doesn't close the handle
		m_hnd.close();
	}

	void async_wait(std::function<void(error_code const&)> cb) override
	{
		HANDLE hnd;
		DWORD err = NotifyAddrChange(&hnd, &m_ovl);
		if (err == ERROR_IO_PENDING)
		{
			m_hnd.async_wait([cb](error_code const& ec)
			{
				// call CancelIPChangeNotify here?
				cb(ec);
			});
		}
		else
		{
			m_hnd.get_io_service().post([cb, err]()
			{ cb(error_code(err, system_category())); });
		}
	}

	void cancel() override
	{
		CancelIPChangeNotify(&m_ovl);
		m_hnd.cancel();
	}

private:
	OVERLAPPED m_ovl = {};
	boost::asio::windows::object_handle m_hnd;
};
#endif

} // anonymous namespace

	std::unique_ptr<ip_change_notifier> create_ip_notifier(io_service& ios)
	{
		return std::unique_ptr<ip_change_notifier>(new ip_change_notifier_impl(ios));
	}
}}
