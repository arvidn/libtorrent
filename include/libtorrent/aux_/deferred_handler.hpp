/*

Copyright (c) 2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DEFERRED_HANDLER_HPP
#define TORRENT_DEFERRED_HANDLER_HPP

#include "libtorrent/assert.hpp"
#include "libtorrent/io_context.hpp"

namespace lt::aux {

template <typename Handler>
struct handler_wrapper
{
	handler_wrapper(bool& in_flight, Handler&& h)
		: m_handler(std::move(h))
		, m_in_flight(in_flight) {}

	template <typename... Args>
	void operator()(Args&&... a)
	{
		TORRENT_ASSERT(m_in_flight);
		m_in_flight = false;
		m_handler(std::forward<Args>(a)...);
	}

	// forward allocator to the underlying handler's
	using allocator_type = typename Handler::allocator_type;

	allocator_type get_allocator() const noexcept
	{ return m_handler.get_allocator(); }

private:
	Handler m_handler;
	bool& m_in_flight;
};

struct deferred_handler
{
	template <typename Handler>
	void post_deferred(lt::io_context& ios, Handler&& h)
	{
		if (m_in_flight) return;
		m_in_flight = true;
		post(ios, handler_wrapper<Handler>(m_in_flight, std::forward<Handler>(h)));
	}
private:
	bool m_in_flight = false;
};

}
#endif
