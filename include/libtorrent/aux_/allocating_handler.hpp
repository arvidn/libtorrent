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

#include "libtorrent/config.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/aligned_storage.hpp"

#include "libtorrent/debug.hpp" // for TORRENT_ASSERT

#include <type_traits>

namespace libtorrent { namespace aux {

	// this is meant to provide the actual storage for the handler allocator.
	// There's only a single slot, so the allocator is only supposed to be used
	// for handlers where there's only a single outstanding operation at a time,
	// per storage object. For instance, peers only ever have one outstanding
	// read operation at a time, so it can reuse its storage for read handlers.
	template <std::size_t Size>
	struct handler_storage
	{
#if TORRENT_USE_ASSERTS
		handler_storage()
			: used(false)
		{}

		bool used;
#else
		handler_storage() = default;
#endif
		typename aux::aligned_storage<Size>::type bytes;
	private:
		handler_storage(handler_storage const&);
	};

	struct TORRENT_EXTRA_EXPORT error_handler_interface
	{
		virtual void on_exception(std::exception const&) = 0;
		virtual void on_error(error_code const&) = 0;

	protected:
		~error_handler_interface() {}
	};

	// this class is a wrapper for an asio handler object. Its main purpose
	// is to pass along additional parameters to the asio handler allocator
	// function, as well as providing a distinct type for the handler
	// allocator function to overload on
	template <class Handler, std::size_t Size>
	struct allocating_handler
	{
		allocating_handler(
			Handler h, handler_storage<Size>& s, error_handler_interface& eh)
			: handler(std::move(h))
			, storage(s)
#ifndef BOOST_NO_EXCEPTIONS
			, error_handler(eh)
#endif
		{}

		template <class... A>
		void operator()(A&&... a) const
		{
#ifdef BOOST_NO_EXCEPTIONS
			handler(std::forward<A>(a)...);
#else
			try
			{
				handler(std::forward<A>(a)...);
			}
			catch (system_error const& e)
			{
				error_handler.on_error(e.code());
			}
			catch (std::exception const& e)
			{
				error_handler.on_exception(e);
			}
			catch (...)
			{
				// this is pretty bad
				TORRENT_ASSERT(false);
				std::runtime_error e("unknown exception");
				error_handler.on_exception(e);
			}
#endif
		}

		friend void* asio_handler_allocate(
			std::size_t size, allocating_handler<Handler, Size>* ctx)
		{
			TORRENT_UNUSED(size);
			TORRENT_ASSERT_VAL(size <= Size, size);
#if TORRENT_USE_ASSERTS
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

			TORRENT_ASSERT_VAL(size <= Size, size);
			TORRENT_ASSERT(ptr == &ctx->storage.bytes);
#if TORRENT_USE_ASSERTS
			ctx->storage.used = false;
#endif
		}

		Handler handler;
		handler_storage<Size>& storage;
#ifndef BOOST_NO_EXCEPTIONS
		error_handler_interface& error_handler;
#endif
	};

	template <class Handler, size_t Size>
	aux::allocating_handler<Handler, Size>
	make_handler(Handler handler
		, handler_storage<Size>& storage
		, error_handler_interface& err_handler)
	{
		return aux::allocating_handler<Handler, Size>(
			std::forward<Handler>(handler), storage, err_handler);
	}
}
}

#endif
