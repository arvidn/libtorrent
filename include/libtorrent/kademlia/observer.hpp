/*

Copyright (c) 2007-2010, 2013-2020, Arvid Norberg
Copyright (c) 2014, Steven Siloti
Copyright (c) 2015, Thomas Yuan
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef OBSERVER_HPP
#define OBSERVER_HPP

#include <cstdint>
#include <memory>

#include <libtorrent/time.hpp>
#include <libtorrent/address.hpp>
#include <libtorrent/flags.hpp>
#include <libtorrent/socket.hpp> // for udp
#include <libtorrent/kademlia/node_id.hpp>

namespace lt {
namespace dht {

struct dht_observer;
struct observer;
struct msg;
struct traversal_algorithm;

using observer_flags_t = lt::flags::bitfield_flag<std::uint8_t, struct observer_flags_tag>;

struct TORRENT_EXTRA_EXPORT observer
	: std::enable_shared_from_this<observer>
{
	observer(std::shared_ptr<traversal_algorithm> a
		, udp::endpoint const& ep, node_id const& id)
		: m_algorithm(std::move(a))
		, m_id(id)
	{
		TORRENT_ASSERT(m_algorithm);
		set_target(ep);
	}

	observer(observer const&) = delete;
	observer& operator=(observer const&) = delete;

	// defined in rpc_manager.cpp
	virtual ~observer();

	// this is called when a reply is received
	virtual void reply(msg const& m) = 0;

	// this is called if no response has been received after
	// a few seconds, before the request has timed out
	void short_timeout();

	bool has_short_timeout() const { return bool(flags & flag_short_timeout); }

	// this is called when no reply has been received within
	// some timeout, or a reply with incorrect format.
	virtual void timeout();

	// if this is called the destructor should
	// not invoke any new messages, and should
	// only clean up. It means the rpc-manager
	// is being destructed
	void abort();

	dht_observer* get_observer() const;

	traversal_algorithm* algorithm() const { return m_algorithm.get(); }

	time_point sent() const { return m_sent; }

	void set_target(udp::endpoint const& ep);
	address target_addr() const;
	udp::endpoint target_ep() const;

	void set_id(node_id const& id);
	node_id const& id() const { return m_id; }

	static inline constexpr observer_flags_t flag_queried = 0_bit;
	static inline constexpr observer_flags_t flag_initial = 1_bit;
	static inline constexpr observer_flags_t flag_no_id = 2_bit;
	static inline constexpr observer_flags_t flag_short_timeout = 3_bit;
	static inline constexpr observer_flags_t flag_failed = 4_bit;
	static inline constexpr observer_flags_t flag_ipv6_address = 5_bit;
	static inline constexpr observer_flags_t flag_alive = 6_bit;
	static inline constexpr observer_flags_t flag_done = 7_bit;

protected:

	void done();

private:

	std::shared_ptr<observer> self()
	{ return shared_from_this(); }

	time_point m_sent;

	std::shared_ptr<traversal_algorithm> const m_algorithm;

	node_id m_id;

	union addr_t
	{
		address_v6::bytes_type v6;
		address_v4::bytes_type v4;
	} m_addr;

	std::uint16_t m_port = 0;

public:
	observer_flags_t flags{};

#if TORRENT_USE_ASSERTS
	bool m_in_constructor = true;
	bool m_was_sent = false;
	bool m_was_abandoned = false;
	bool m_in_use = true;
#endif
};

using observer_ptr = std::shared_ptr<observer>;

}
}

#endif
