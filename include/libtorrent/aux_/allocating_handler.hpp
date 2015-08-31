/*

Copyright (c) 2015, Arvid Norberg
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

#ifndef TORRENT_ALLOCATING_HANDLER_HPP_INCLUDED
#define TORRENT_ALLOCATING_HANDLER_HPP_INCLUDED

#include <boost/config.hpp>
#include "libtorrent/config.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"

#include <boost/aligned_storage.hpp>

#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent { namespace aux
{
	// this is meant to provide the actual storage for the handler allocator.
	// There's only a single slot, so the allocator is only supposed to be used
	// for handlers where there's only a single outstanding operation at a time,
	// per storage object. For instance, peers only ever have one outstanding
	// read operation at a time, so it can reuse its storage for read handlers.
	template <std::size_t Size>
	struct handler_storage
	{
#ifdef TORRENT_DEBUG
		handler_storage()
			: used(false)
		{}

		bool used;
#else
		handler_storage() {}
#endif
		boost::aligned_storage<Size> bytes;
	private:
		handler_storage(handler_storage const&);
	};

	// this class is a wrapper for an asio handler object. Its main purpose
	// is to pass along additional parameters to the asio handler allocator
	// function, as well as providing a distinct type for the handler
	// allocator function to overload on
	template <class Handler, std::size_t Size>
	struct allocating_handler
	{

		// TODO: 3 make sure the handlers we pass in are potentially movable!
#if !defined BOOST_NO_CXX11_RVALUE_REFERENCES
		allocating_handler(
			Handler&& h, handler_storage<Size>& s)
			: handler(std::move(h))
			, storage(s)
		{}
#endif

		allocating_handler(
			Handler const& h, handler_storage<Size>& s)
			: handler(h)
			, storage(s)
		{}

#if !defined BOOST_NO_CXX11_VARIADIC_TEMPLATES \
		&& !defined BOOST_NO_CXX11_RVALUE_REFERENCES
		template <class... A>
		void operator()(A&&... a) const
		{
			handler(std::forward<A>(a)...);
		}
#else
		template <class A0>
		void operator()(A0 const& a0) const
		{
			handler(a0);
		}

		template <class A0, class A1>
		void operator()(A0 const& a0, A1 const& a1) const
		{
			handler(a0, a1);
		}

		template <class A0, class A1, class A2>
		void operator()(A0 const& a0, A1 const& a1, A2 const& a2) const
		{
			handler(a0, a1, a2);
		}
#endif

		friend void* asio_handler_allocate(
			std::size_t size, allocating_handler<Handler, Size>* ctx)
		{
			TORRENT_UNUSED(size);
			TORRENT_ASSERT(size <= Size);
#ifdef TORRENT_DEBUG
			TORRENT_ASSERT(!ctx->storage.used);
			ctx->storage.used = true;
#endif
			return &ctx->storage.bytes;
		}

		friend void asio_handler_deallocate(
			void* ptr, std::size_t size, allocating_handler<Handler, Size>* ctx)
		{
			TORRENT_UNUSED(ptr);
			TORRENT_UNUSED(size);
			TORRENT_UNUSED(ctx);

			TORRENT_ASSERT(size <= Size);
			TORRENT_ASSERT(ptr == &ctx->storage.bytes);
#ifdef TORRENT_DEBUG
			ctx->storage.used = false;
#endif
		}

		Handler handler;
		handler_storage<Size>& storage;
	};

}
}

#endif

