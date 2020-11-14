/*

Copyright (c) 2017, 2019-2020, Arvid Norberg
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

#ifndef TORRENT_DEFERRED_HANDLER_HPP
#define TORRENT_DEFERRED_HANDLER_HPP

#include "libtorrent/assert.hpp"
#include "libtorrent/io_context.hpp"

namespace libtorrent { namespace aux {

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

}}
#endif
