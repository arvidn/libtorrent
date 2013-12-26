/*

Copyright (c) 2012-2013, Arvid Norberg
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

#ifndef TORRENT_ATOMIC_HPP_INCLUDED
#define TORRENT_ATOMIC_HPP_INCLUDED

#include "libtorrent/config.hpp"

#if TORRENT_USE_OSATOMIC
#include <libkern/OSAtomic.h>
#endif

#if TORRENT_USE_INTERLOCKED_ATOMIC
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if TORRENT_USE_BEOS_ATOMIC
#include <be/support/SupportDefs.h>
#endif

#if TORRENT_USE_SOLARIS_ATOMIC
#include <atomic.h>
#endif

namespace libtorrent
{
	struct atomic_count
	{
		atomic_count() : m_value(0) {}
		atomic_count(int v) : m_value(v) {}

#if TORRENT_USE_INTERLOCKED_ATOMIC
		typedef LONG value_type;
#elif TORRENT_USE_SOLARIS_ATOMIC
		typedef unsigned int value_type;
#else
		typedef int value_type;
#endif

#if TORRENT_USE_OSATOMIC
		operator value_type() const { return OSAtomicAdd32(0, const_cast<value_type*>(&m_value)); }
		value_type operator-=(int v) { return OSAtomicAdd32Barrier(-v, &m_value); }
		value_type operator+=(int v) { return OSAtomicAdd32Barrier(v, &m_value); }
		// pre inc/dec operators
		value_type operator++() { return OSAtomicAdd32Barrier(1, &m_value); }
		value_type operator--() { return OSAtomicAdd32Barrier(-1, &m_value); }
		// post inc/dec operators
		value_type operator++(int) { return OSAtomicAdd32Barrier(1, &m_value)-1; }
		value_type operator--(int) { return OSAtomicAdd32Barrier(-1, &m_value)+1; }
#elif TORRENT_USE_GCC_ATOMIC
		operator value_type() const { return __sync_fetch_and_add(const_cast<value_type*>(&m_value), 0); }
		value_type operator-=(value_type v) { return __sync_sub_and_fetch(&m_value, v); }
		value_type operator+=(value_type v) { return __sync_add_and_fetch(&m_value, v); }
		// pre inc/dec operators
		value_type operator++() { return __sync_add_and_fetch(&m_value, 1); }
		value_type operator--() { return __sync_add_and_fetch(&m_value, -1); }
		// post inc/dec operators
		value_type operator++(int) { return __sync_fetch_and_add(&m_value, 1); }
		value_type operator--(int) { return __sync_fetch_and_add(&m_value, -1); }
#elif TORRENT_USE_INTERLOCKED_ATOMIC
		operator value_type() const { return InterlockedExchangeAdd(const_cast<value_type*>(&m_value), 0); }
		value_type operator-=(value_type v) { return InterlockedExchangeAdd(&m_value, -v); }
		value_type operator+=(value_type v) { return InterlockedExchangeAdd(&m_value, v); }
		// pre inc/dec operators
		value_type operator++() { return InterlockedIncrement(&m_value); }
		value_type operator--() { return InterlockedDecrement(&m_value); }
		// post inc/dec operators
		value_type operator++(int) { return InterlockedIncrement(&m_value) - 1; }
		value_type operator--(int) { return InterlockedDecrement(&m_value) + 1; }
#elif TORRENT_USE_SOLARIS_ATOMIC
		operator value_type() const { return atomic_add_32_nv(const_cast<value_type*>(&m_value), 0); }
		value_type operator-=(value_type v) { return atomic_add_32(&m_value, -v); }
		value_type operator+=(value_type v) { return atomic_add_32(&m_value, v); }
		// pre inc/dec operators
		value_type operator++() { return atomic_add_32_nv(&m_value, 1); }
		value_type operator--() { return atomic_add_32_nv(&m_value, -1); }
		// post inc/dec operators
		value_type operator++(int) { return atomic_add_32_nv(&m_value, 1) - 1; }
		value_type operator--(int) { return atomic_add_32_nv(&m_value, -1) + 1; }
#elif TORRENT_USE_BEOS_ATOMIC
		operator value_type() const { return atomic_add(const_cast<value_type*>(&m_value), 0); }
		value_type operator-=(value_type v) { return atomic_add(&m_value, -v) - v; }
		value_type operator+=(value_type v) { return atomic_add(&m_value, v) + v; }
		// pre inc/dec operators
		value_type operator++() { return atomic_add(&m_value, 1) + 1; }
		value_type operator--() { return atomic_add(&m_value, -1) - 1; }
		// post inc/dec operators
		value_type operator++(int) { return atomic_add(&m_value, 1); }
		value_type operator--(int) { return atomic_add(&m_value, -1); }
#else
#error "don't know which atomic operations to use"
#endif
	private:
		volatile value_type m_value;
	};
}

#endif

