/*

Copyright (c) 2015-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
Copyright (c) 2019, Steven Siloti
Copyright (c) 2020, Paul-Louis Ageneau
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
#include <memory> // for shared_ptr

#ifdef TORRENT_ASIO_DEBUGGING
#include "libtorrent/debug.hpp"
#endif

namespace libtorrent { namespace aux {

#ifdef BOOST_ASIO_ENABLE_HANDLER_TRACKING
	constexpr std::size_t tracking = 8;
#else
	constexpr std::size_t tracking = 0;
#endif

#if defined _MSC_VER || defined __MINGW64__

	// windows
#if _HAS_ITERATOR_DEBUGGING > 0
	constexpr std::size_t debug_read_iter = 34 * sizeof(void*);
	constexpr std::size_t debug_write_iter = 34 * sizeof(void*);
	constexpr std::size_t debug_tick = 4 * sizeof(void*);
#else
	constexpr std::size_t debug_read_iter = 0;
	constexpr std::size_t debug_write_iter = 0;
	constexpr std::size_t debug_tick = 0;
#endif
#if TORRENT_USE_SSL
	constexpr std::size_t openssl_read_cost = 26 + 14 * sizeof(void*);
	constexpr std::size_t openssl_write_cost = 26 + 14 * sizeof(void*);
#else
	constexpr std::size_t openssl_read_cost = 0;
	constexpr std::size_t openssl_write_cost = 0;
#endif

	constexpr std::size_t read_handler_max_size = tracking + debug_read_iter + openssl_read_cost + 102 + 8 * sizeof(void*);
	constexpr std::size_t write_handler_max_size = tracking + debug_write_iter + openssl_write_cost + 102 + 8 * sizeof(void*);
	constexpr std::size_t udp_handler_max_size = tracking + debug_tick + 128 + 8 * sizeof(void*);
	constexpr std::size_t utp_handler_max_size = tracking + debug_tick + 152 + 8 * sizeof(void*);
	constexpr std::size_t tick_handler_max_size = tracking + debug_tick + 144;
	constexpr std::size_t abort_handler_max_size = tracking + debug_tick + 104;
	constexpr std::size_t submit_handler_max_size = tracking + debug_tick + 104;
	constexpr std::size_t deferred_handler_max_size = tracking + debug_tick + 112;
#else

	// non-windows

#ifdef _GLIBCXX_DEBUG
#if defined __clang__
	constexpr std::size_t debug_read_iter = 12 * sizeof(void*);
	constexpr std::size_t debug_write_iter = 12 * sizeof(void*);
#else
	constexpr std::size_t debug_write_iter = 8 * sizeof(void*);
	constexpr std::size_t debug_read_iter = 12 * sizeof(void*);
#endif
#else
	constexpr std::size_t debug_read_iter = 0;
	constexpr std::size_t debug_write_iter = 0;
#endif

#if TORRENT_USE_SSL
#ifdef __clang__
	constexpr std::size_t openssl_read_cost = 264;
	constexpr std::size_t openssl_write_cost = 216;
#else
	constexpr std::size_t openssl_read_cost = 152;
	constexpr std::size_t openssl_write_cost = 152;
#endif
#else
	constexpr std::size_t openssl_read_cost = 0;
	constexpr std::size_t openssl_write_cost = 0;
#endif

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
	constexpr std::size_t fuzzer_write_cost = 32;
	constexpr std::size_t fuzzer_read_cost = 80;
#else
	constexpr std::size_t fuzzer_write_cost = 0;
	constexpr std::size_t fuzzer_read_cost = 0;
#endif
	constexpr std::size_t write_handler_max_size = tracking + debug_write_iter + openssl_write_cost + fuzzer_write_cost + 152;
	constexpr std::size_t read_handler_max_size = tracking + debug_read_iter + openssl_read_cost + fuzzer_read_cost + 152;
	constexpr std::size_t udp_handler_max_size = tracking + 144;
	constexpr std::size_t utp_handler_max_size = tracking + 168;
	constexpr std::size_t abort_handler_max_size = tracking + 72;
	constexpr std::size_t submit_handler_max_size = tracking + 72;
	constexpr std::size_t deferred_handler_max_size = tracking + 80;
	constexpr std::size_t tick_handler_max_size = tracking + 112;
#endif

	enum HandlerName
	{
		// when adding a handler here, be sure to update handler_names in
		// debug.hpp as well
		write_handler, read_handler, udp_handler, tick_handler, abort_handler,
		defer_handler, utp_handler, submit_handler
	};

	// this is meant to provide the actual storage for the handler allocator.
	// There's only a single slot, so the allocator is only supposed to be used
	// for handlers where there's only a single outstanding operation at a time,
	// per storage object. For instance, peers only ever have one outstanding
	// read operation at a time, so it can reuse its storage for read handlers.
	template <std::size_t Size, HandlerName Name>
	struct handler_storage
	{
		handler_storage() = default;
		static constexpr std::size_t size = Size;
		static constexpr HandlerName name = Name;

		typename aux::aligned_storage<Size, alignof(std::max_align_t)>::type bytes;
#if TORRENT_USE_ASSERTS
		bool used = false;
#endif
		handler_storage(handler_storage const&) = delete;
	};

	struct TORRENT_EXTRA_EXPORT error_handler_interface
	{
		virtual void on_exception(std::exception const&) = 0;
		virtual void on_error(error_code const&) = 0;

	protected:
		~error_handler_interface() {}
	};

	template <std::size_t V>
	struct required_size { static std::size_t const value = V; };

	template <std::size_t V>
	struct available_size { static std::size_t const value = V; };

	template <typename Required, typename Available, HandlerName Name>
	struct assert_message
	{
		static_assert(Required::value <= Available::value
			, "Handler buffer not large enough, please increase it");
		static std::size_t const value = Available::value;
	};

	template <typename T, std::size_t Size, HandlerName Name>
	struct handler_allocator
	{
		template <typename U, std::size_t S, HandlerName N>
		friend struct handler_allocator;

		using value_type = T;
		using size_type = std::size_t;

		friend bool operator==(handler_allocator lhs, handler_allocator rhs)
		{ return lhs.m_storage == rhs.m_storage; }
		friend bool operator!=(handler_allocator lhs, handler_allocator rhs)
		{ return lhs.m_storage != rhs.m_storage; }

		template <class U>
		struct rebind {
			using other = handler_allocator<U
				, assert_message<required_size<sizeof(U)>
				, available_size<Size>, Name>::value, Name>;
			static_assert(alignof(U) <= alignof(std::max_align_t), "handler storage is not correctly aligned");
		};

		explicit handler_allocator(handler_storage<Size, Name>* s) : m_storage(s) {}
		template <typename U>
		handler_allocator(handler_allocator<U, Size, Name> const& other) : m_storage(other.m_storage) {}

		T* allocate(std::size_t size)
		{
			TORRENT_UNUSED(size);
			TORRENT_ASSERT_VAL(size == 1, size);
			TORRENT_ASSERT_VAL(sizeof(T) <= Size, sizeof(T));
			TORRENT_ASSERT(!m_storage->used);
#if TORRENT_USE_ASSERTS
			m_storage->used = true;
#endif
#ifdef TORRENT_ASIO_DEBUGGING
			record_handler_allocation<T>(static_cast<int>(Name), Size);
#endif
			return reinterpret_cast<T*>(&m_storage->bytes);
		}

		void deallocate(T* ptr, std::size_t size)
		{
			TORRENT_UNUSED(ptr);
			TORRENT_UNUSED(size);

			TORRENT_ASSERT_VAL(size == 1, size);
			TORRENT_ASSERT_VAL(sizeof(T) <= Size, sizeof(T));
			TORRENT_ASSERT(ptr == reinterpret_cast<T*>(&m_storage->bytes));
			TORRENT_ASSERT(m_storage->used);
#if TORRENT_USE_ASSERTS
			m_storage->used = false;
#endif
		}

	private:
		handler_storage<Size, Name>* m_storage;
	};

	// this class is a wrapper for an asio handler object. Its main purpose
	// is to pass along additional parameters to the asio handler allocator
	// function, as well as providing a distinct type for the handler
	// allocator function to overload on
	template <class Handler, std::size_t Size, HandlerName Name>
	struct allocating_handler
	{
		allocating_handler(
			Handler h, handler_storage<Size, Name>* s, error_handler_interface* eh)
			: handler(std::move(h))
			, storage(s)
#ifndef BOOST_NO_EXCEPTIONS
			, error_handler(eh)
#endif
		{}

		template <class... A>
		void operator()(A&&... a)
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
				error_handler->on_error(e.code());
			}
			catch (std::exception const& e)
			{
				error_handler->on_exception(e);
			}
			catch (...)
			{
				// this is pretty bad
				TORRENT_ASSERT(false);
				std::runtime_error e("unknown exception");
				error_handler->on_exception(e);
			}
#endif
		}

		using allocator_type = handler_allocator<int, Size, Name>;

		allocator_type get_allocator() const noexcept
		{ return allocator_type{storage}; }

	private:

		Handler handler;
		handler_storage<Size, Name>* storage;
#ifndef BOOST_NO_EXCEPTIONS
		error_handler_interface* error_handler;
#endif
	};

	template <class Handler, size_t Size, HandlerName Name>
	aux::allocating_handler<Handler, Size, Name>
	make_handler(Handler handler
		, handler_storage<Size, Name>& storage
		, error_handler_interface& err_handler)
	{
		return aux::allocating_handler<Handler, Size, Name>(
			std::forward<Handler>(handler), &storage, &err_handler);
	}

	// TODO: in C++17, Handler and Storage could just use "auto"
	template <typename T
		, typename HandlerType
		, HandlerType Handler
		, void (T::*ErrorHandler)(error_code const&)
		, void (T::*ExceptHandler)(std::exception const&)
		, typename StorageType
		, StorageType T::* Storage>
	struct handler
	{
		explicit handler(std::shared_ptr<T> p) : ptr_(std::move(p)) {}

		std::shared_ptr<T> ptr_;

		template <class... A>
		void operator()(A&&... a)
		{
#ifdef BOOST_NO_EXCEPTIONS
			(ptr_.get()->*Handler)(std::forward<A>(a)...);
#else
			try
			{
				(ptr_.get()->*Handler)(std::forward<A>(a)...);
			}
			catch (system_error const& e)
			{
				(ptr_.get()->*ErrorHandler)(e.code());
			}
			catch (std::exception const& e)
			{
				(ptr_.get()->*ExceptHandler)(e);
			}
			catch (...)
			{
				// this is pretty bad
				TORRENT_ASSERT(false);
				std::runtime_error e("unknown exception");
				(ptr_.get()->*ExceptHandler)(e);
			}
#endif
		}

		using allocator_type = handler_allocator<handler, StorageType::size, StorageType::name>;

		allocator_type get_allocator() const noexcept
		{ return allocator_type{&(ptr_.get()->*Storage)}; }
	};
}
}

#endif
