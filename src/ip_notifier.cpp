/*

Copyright (c) 2016-2017, Alden Torres
Copyright (c) 2016, 2020, Steven Siloti
Copyright (c) 2017-2020, Arvid Norberg
Copyright (c) 2017, Tim Niederhausen
Copyright (c) 2020, Tiger Wang
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
#include "libtorrent/socket.hpp"
#include <array>
#include <unordered_map>
#elif TORRENT_USE_SYSTEMCONFIGURATION || TORRENT_USE_SC_NETWORK_REACHABILITY
#include <SystemConfiguration/SystemConfiguration.h>
#elif defined TORRENT_WINDOWS
#include "libtorrent/aux_/throw.hpp"
#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <iphlpapi.h>
#ifdef TORRENT_WINRT
#include <netioapi.h>
#endif
#include <mutex>
#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif

#include "libtorrent/aux_/netlink_utils.hpp"

namespace libtorrent { namespace aux {

namespace {

#if (TORRENT_USE_SYSTEMCONFIGURATION || TORRENT_USE_SC_NETWORK_REACHABILITY) && \
	!defined TORRENT_BUILD_SIMULATOR

// common utilities for Mac and iOS
template <typename T> void CFRefRetain(T h) { CFRetain(h); }
template <typename T> void CFRefRelease(T h) { CFRelease(h); }

template <typename T
	, void (*Retain)(T) = CFRefRetain<T>, void (*Release)(T) = CFRefRelease<T>>
struct CFRef
{
	CFRef() = default;
	explicit CFRef(T h) : m_h(h) {} // take ownership
	~CFRef() { release(); }

	CFRef(CFRef&& rhs) : m_h(rhs.m_h) { rhs.m_h = nullptr; }
	CFRef& operator=(CFRef&& rhs) &
	{
		if (m_h == rhs.m_h) return *this;
		release();
		m_h = rhs.m_h;
		rhs.m_h = nullptr;
		return *this;
	}

	CFRef(CFRef const& rhs) : m_h(rhs.m_h) { retain(); }
	CFRef& operator=(CFRef const& rhs) &
	{
		if (m_h == rhs.m_h) return *this;
		release();
		m_h = rhs.m_h;
		retain();
		return *this;
	}

	CFRef& operator=(T h) & { m_h = h; return *this;}
	CFRef& operator=(std::nullptr_t) & { release(); return *this;}

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
#endif

#if defined TORRENT_BUILD_SIMULATOR
struct ip_change_notifier_impl final : ip_change_notifier
{
	explicit ip_change_notifier_impl(io_context& ios)
		: m_ios(ios) {}

	void async_wait(std::function<void(error_code const&)> cb) override
	{
		post(m_ios, [cb1=std::move(cb)]()
		{ cb1(make_error_code(boost::system::errc::not_supported)); });
	}

	void cancel() override {}

private:
	io_context& m_ios;
};
#elif TORRENT_USE_NETLINK
struct ip_change_notifier_impl final : ip_change_notifier
{
	explicit ip_change_notifier_impl(io_context& ios)
		: m_socket(ios
			, netlink::endpoint(netlink(NETLINK_ROUTE), RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR))
	{
		// Linux can generate ENOBUFS if the socket's buffers are full
		// don't treat it as an error
		error_code ec;
		m_socket.set_option(libtorrent::no_enobufs(true), ec);
	}

	// non-copyable
	ip_change_notifier_impl(ip_change_notifier_impl const&) = delete;
	ip_change_notifier_impl& operator=(ip_change_notifier_impl const&) = delete;

	void async_wait(std::function<void(error_code const&)> cb) override
	{
		m_socket.async_receive(boost::asio::buffer(m_buf)
			, [cb1=std::move(cb), this] (error_code const& ec, std::size_t const bytes_transferred)
			{
				if (ec) cb1(ec);
				else this->on_notify(int(bytes_transferred), std::move(cb1));
			});
	}

	void cancel() override
	{ m_socket.cancel();}

private:
	netlink::socket m_socket;
	std::array<char, 4096> m_buf;

	struct local_address
	{
		int family;
		std::array<char, 16> data;
	};
	// maps if_index to the most recently advertised local address
	// this is used to filter duplicate updates
	std::unordered_map<std::uint32_t, local_address> m_state;

	void on_notify(int len, std::function<void(error_code const& ec)> cb)
	{
		bool pertinent = false;

		for (auto const* nh = reinterpret_cast<nlmsghdr const*>(this->m_buf.data());
			nlmsg_ok (nh, len);
			nh = nlmsg_next(nh, len))
		{
			if (nh->nlmsg_type != RTM_NEWADDR)
				continue;

			auto const* addr_msg = static_cast<ifaddrmsg const*>(nlmsg_data(nh));
			std::uint32_t const index = addr_msg->ifa_index;
			int const family = addr_msg->ifa_family;
			std::size_t attr_len = ifa_payload(nh);
			auto const* rta_ptr = ifa_rta(addr_msg);

			for (; rta_ok(rta_ptr, attr_len); rta_ptr = rta_next(rta_ptr, attr_len))
			{
				auto* const ptr = rta_data(rta_ptr);
				if (rta_ptr->rta_type != IFA_LOCAL)
					continue;

				auto& existing = m_state[index];
				std::size_t const address_len = family == AF_INET ? 4 : 16;
				if (existing.family == family
					&& std::memcmp(&existing.data, ptr, address_len) == 0)
				{
					break;
				}

				existing.family = family;
				std::memcpy(existing.data.data(), ptr, address_len);
				pertinent = true;
			}
		}

		if (!pertinent)
		{
			m_socket.async_receive(boost::asio::buffer(m_buf)
				, [cb1=std::move(cb), this] (error_code const& ec, std::size_t const bytes_transferred)
				{
					if (ec) cb1(ec);
					else this->on_notify(int(bytes_transferred), std::move(cb1));
				});
		}
		else
		{
			// on linux we could parse the message to get information about the
			// change but Windows requires the application to enumerate the
			// interfaces after a notification so do that for Linux as well to
			// minimize the difference between platforms
			cb(error_code());
		}
	}
};

#elif TORRENT_USE_SC_NETWORK_REACHABILITY

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
	explicit ip_change_notifier_impl(io_context& ios)
		: m_ios(ios)
	{
		m_queue = dispatch_queue_create("libtorrent.IPChangeNotifierQueue", nullptr);
		m_reach = create_reachability(
			[](SCNetworkReachabilityRef /*target*/, SCNetworkReachabilityFlags /*flags*/, void *info)
			{
				auto obj = static_cast<ip_change_notifier_impl*>(info);
				post(obj->m_ios, [obj]()
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

	// non-copyable
	ip_change_notifier_impl(ip_change_notifier_impl const&) = delete;
	ip_change_notifier_impl& operator=(ip_change_notifier_impl const&) = delete;

	~ip_change_notifier_impl() override
	{ cancel(); }

	void async_wait(std::function<void(error_code const&)> cb) override
	{
		if (m_queue)
			m_cb = std::move(cb);
		else
			post(m_ios, [cb]()
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
	io_context& m_ios;
	CFDispatchRef m_queue;
	CFRef<SCNetworkReachabilityRef> m_reach;
	std::function<void(error_code const&)> m_cb = nullptr;
};
#elif TORRENT_USE_SYSTEMCONFIGURATION
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

#if defined __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif
	CFRef<SCDynamicStoreRef> store{SCDynamicStoreCreate(nullptr
		, CFSTR("libtorrent.IPChangeNotifierStore"), callback, &context)};
#if defined __clang__
#pragma clang diagnostic pop
#endif
	if (!store)
		return CFRef<SCDynamicStoreRef>();

	CFRef<CFMutableArrayRef> keys = create_keys_array();
	return SCDynamicStoreSetNotificationKeys(store.get(), nullptr, keys.get())
		? store : CFRef<SCDynamicStoreRef>();
}

struct ip_change_notifier_impl final : ip_change_notifier
{
	explicit ip_change_notifier_impl(io_context& ios)
		: m_ios(ios)
	{
		m_queue = dispatch_queue_create("libtorrent.IPChangeNotifierQueue", nullptr);
		m_store = create_dynamic_store(
			[](SCDynamicStoreRef /*store*/, CFArrayRef /*changedKeys*/, void *info)
			{
				auto obj = static_cast<ip_change_notifier_impl*>(info);
				post(obj->m_ios, [obj]()
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

	// non-copyable
	ip_change_notifier_impl(ip_change_notifier_impl const&) = delete;
	ip_change_notifier_impl& operator=(ip_change_notifier_impl const&) = delete;

	~ip_change_notifier_impl() override
	{ cancel(); }

	void async_wait(std::function<void(error_code const&)> cb) override
	{
		if (m_queue)
			m_cb = std::move(cb);
		else
			post(m_ios, [cb]()
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
	io_context& m_ios;
	CFDispatchRef m_queue;
	CFRef<SCDynamicStoreRef> m_store;
	std::function<void(error_code const&)> m_cb = nullptr;
};

#elif defined TORRENT_WINDOWS
struct ip_change_notifier_impl final : ip_change_notifier
{
	explicit ip_change_notifier_impl(io_context& ios)
		: m_ios(ios)
	{
		NotifyUnicastIpAddressChange(AF_UNSPEC, address_change_cb, this, false, &m_hnd);
	}

	// non-copyable
	ip_change_notifier_impl(ip_change_notifier_impl const&) = delete;
	ip_change_notifier_impl& operator=(ip_change_notifier_impl const&) = delete;

	// non-moveable
	ip_change_notifier_impl(ip_change_notifier_impl&&) = delete;
	ip_change_notifier_impl& operator=(ip_change_notifier_impl&&) = delete;

	~ip_change_notifier_impl() override
	{
		if (m_hnd != nullptr)
		{
			CancelMibChangeNotify2(m_hnd);
			m_hnd = nullptr;
		}
	}

	void async_wait(std::function<void(error_code const&)> cb) override
	{
		if (m_hnd == nullptr)
		{
			cb(make_error_code(boost::system::errc::not_supported));
			return;
		}

		std::lock_guard<std::mutex> l(m_cb_mutex);
		m_cb.emplace_back(std::move(cb));
	}

	void cancel() override
	{
		std::vector<std::function<void(error_code const&)>> cbs;
		{
			std::lock_guard<std::mutex> l(m_cb_mutex);
			cbs = std::move(m_cb);
		}
		for (auto& cb : cbs) cb(make_error_code(boost::asio::error::operation_aborted));
	}

private:
	static void WINAPI address_change_cb(void* ctx, MIB_UNICASTIPADDRESS_ROW*, MIB_NOTIFICATION_TYPE)
	{
		ip_change_notifier_impl* impl = static_cast<ip_change_notifier_impl*>(ctx);
		std::vector<std::function<void(error_code const&)>> cbs;
		{
			std::lock_guard<std::mutex> l(impl->m_cb_mutex);
			cbs = std::move(impl->m_cb);
		}
		post(impl->m_ios, [c = std::move(cbs)]()
		{
			for (auto& cb : c) cb(error_code());
		});
	}

	io_context& m_ios;
	HANDLE m_hnd = nullptr;
	// address_change_cb gets invoked from a separate worker thread so the callbacks
	// vector must be protected by a mutex
	std::mutex m_cb_mutex;
	std::vector<std::function<void(error_code const&)>> m_cb;
};
#else
struct ip_change_notifier_impl final : ip_change_notifier
{
	explicit ip_change_notifier_impl(io_context& ios)
		: m_ios(ios) {}

	void async_wait(std::function<void(error_code const&)> cb) override
	{
		post(m_ios, [cb]()
		{ cb(make_error_code(boost::system::errc::not_supported)); });
	}

	void cancel() override {}

private:
	io_context& m_ios;
};
#endif

} // anonymous namespace

	std::unique_ptr<ip_change_notifier> create_ip_notifier(io_context& ios)
	{
		return std::make_unique<ip_change_notifier_impl>(ios);
	}
}}
