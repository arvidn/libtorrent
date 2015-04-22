/*

Copyright (c) 2008-2014, Arvid Norberg
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

#ifndef TORRENT_BITFIELD_HPP_INCLUDED
#define TORRENT_BITFIELD_HPP_INCLUDED

#include "libtorrent/assert.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/byteswap.hpp"
#include "libtorrent/cpuid.hpp"

#include <cstring> // for memset and memcpy
#include <cstdlib> // for malloc, free and realloc
#include <boost/cstdint.hpp> // uint32_t
#include <algorithm> // for min()

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace libtorrent
{
	// The bitfiled type stores any number of bits as a bitfield 
	// in a heap allocated array.
	struct TORRENT_EXPORT bitfield
	{
		// constructs a new bitfield. The default constructor creates an empty
		// bitfield. ``bits`` is the size of the bitfield (specified in bits).
		// ``val`` is the value to initialize the bits to. If not specified
		// all bits are initialized to 0.
		//
		// The constructor taking a pointer ``b`` and ``bits`` copies a bitfield
		// from the specified buffer, and ``bits`` number of bits (rounded up to
		// the nearest byte boundry).
		bitfield(): m_buf(NULL) {}
		bitfield(int bits): m_buf(NULL)
		{ resize(bits); }
		bitfield(int bits, bool val): m_buf(NULL)
		{ resize(bits, val); }
		bitfield(char const* b, int bits): m_buf(NULL)
		{ assign(b, bits); }
		bitfield(bitfield const& rhs): m_buf(NULL)
		{ assign(rhs.bytes(), rhs.size()); }
#if __cplusplus > 199711L
		bitfield(bitfield&& rhs): m_buf(rhs.m_buf)
		{ rhs.m_buf = NULL; }
#endif

		// hidden
		~bitfield() { dealloc(); }

		// copy bitfield from buffer ``b`` of ``bits`` number of bits, rounded up to
		// the nearest byte boundary.
		void assign(char const* b, int bits)
		{
			resize(bits);
			if (bits > 0)
			{
				std::memcpy(m_buf, b, size_t((bits + 7) / 8));
				clear_trailing_bits();
			}
		}

		// query bit at ``index``. Returns true if bit is 1, otherwise false.
		bool operator[](int index) const
		{ return get_bit(index); }

		bool get_bit(int index) const
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < size());
			return (m_buf[index / 32] & htonl((0x80000000 >> (index & 31)))) != 0;
		}
		
		// set bit at ``index`` to 0 (clear_bit) or 1 (set_bit).
		void clear_bit(int index)
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < size());
			m_buf[index / 32] &= htonl(~(0x80000000 >> (index & 31)));
		}
		void set_bit(int index)
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < size());
			m_buf[index / 32] |= htonl((0x80000000 >> (index & 31)));
		}

		// returns true if all bits in the bitfield are set
		bool all_set() const
		{
			const int words = size() / 32;
			for (int i = 0; i < words; ++i)
			{
				if (m_buf[i] != 0xffffffff) return false;
			}
			int rest = size() & 31;
			boost::uint32_t mask = htonl(0xffffffff << (32-rest));
			if (rest > 0 && (m_buf[words] & mask) != mask)
				return false;
			return true;
		}

		bool none_set() const
		{
			const int words = num_words();
			for (int i = 0; i < words; ++i)
			{
				if (m_buf[i] != 0) return false;
			}
			return true;
		}

		// returns the size of the bitfield in bits.
		int size() const
		{
			return m_buf == NULL ? 0 : int(m_buf[-1]);
		}

		int num_words() const
		{
			return (size() + 31) / 32;
		}

		// returns true if the bitfield has zero size.
		bool empty() const { return m_buf == NULL ? true : m_buf[-1] == 0; }

		// returns a pointer to the internal buffer of the bitfield.
		// TODO: rename to data() ?
		char const* bytes() const { return reinterpret_cast<char const*>(m_buf); }

		// copy operator
		bitfield& operator=(bitfield const& rhs)
		{
			assign(rhs.bytes(), rhs.size());
			return *this;
		}

		// count the number of bits in the bitfield that are set to 1.
		int count() const
		{
			int ret = 0;
			const int words = num_words();
#if TORRENT_HAS_SSE
			unsigned int cpui[4];
			cpuid(cpui, 1);
			if (cpui[2] & (1 << 23))
			{
				for (int i = 0; i < words; ++i)
				{
#ifdef __GNUC__
					ret += __builtin_popcount(m_buf[i]);
#else
					ret += _mm_popcnt_u32(m_buf[i]);
#endif
				}

				return ret;
			}	
#endif // TORRENT_HAS_SSE

			for (int i = 0; i < words; ++i)
			{
				boost::uint32_t v = m_buf[i];
				// from:
				// http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
				static const int S[] = {1, 2, 4, 8, 16}; // Magic Binary Numbers
				static const int B[] = {0x55555555, 0x33333333, 0x0F0F0F0F, 0x00FF00FF, 0x0000FFFF};

				boost::uint32_t c = v - ((v >> 1) & B[0]);
				c = ((c >> S[1]) & B[1]) + (c & B[1]);
				c = ((c >> S[2]) + c) & B[2];
				c = ((c >> S[3]) + c) & B[3];
				c = ((c >> S[4]) + c) & B[4];
				ret += c;
			}

			TORRENT_ASSERT(ret <= size());
			TORRENT_ASSERT(ret >= 0);
			return ret;
		}

		struct const_iterator
		{
		friend struct bitfield;

			typedef bool value_type;
			typedef ptrdiff_t difference_type;
			typedef bool const* pointer;
			typedef bool& reference;
			typedef std::forward_iterator_tag iterator_category;

			bool operator*() { return (*buf & htonl(bit)) != 0; }
			const_iterator& operator++() { inc(); return *this; }
			const_iterator operator++(int)
			{ const_iterator ret(*this); inc(); return ret; }
			const_iterator& operator--() { dec(); return *this; }
			const_iterator operator--(int)
			{ const_iterator ret(*this); dec(); return ret; }

			const_iterator(): buf(0), bit(0x80000000) {}
			bool operator==(const_iterator const& rhs) const
			{ return buf == rhs.buf && bit == rhs.bit; }

			bool operator!=(const_iterator const& rhs) const
			{ return buf != rhs.buf || bit != rhs.bit; }

		private:
			void inc()
			{
				TORRENT_ASSERT(buf);
				if (bit == 0x01)
				{
					bit = 0x80000000;
					++buf;
				}
				else
				{
					bit >>= 1;
				}
			}
			void dec()
			{
				TORRENT_ASSERT(buf);
				if (bit == 0x80000000)
				{
					bit = 0x01;
					--buf;
				}
				else
				{
					bit <<= 1;
				}
			}
			const_iterator(boost::uint32_t const* ptr, int offset)
				: buf(ptr), bit(0x80000000 >> offset) {}
			boost::uint32_t const* buf;
			boost::uint32_t bit;
		};

		const_iterator begin() const { return const_iterator(m_buf, 0); }
		const_iterator end() const { return const_iterator(
			m_buf + num_words() - (((size() & 31) == 0) ? 0 : 1), size() & 31); }

		// set the size of the bitfield to ``bits`` length. If the bitfield is extended,
		// the new bits are initialized to ``val``.
		void resize(int bits, bool val)
		{
			if (bits == size()) return;

			int s = size();
			int b = size() & 31;
			resize(bits);
			if (s >= size()) return;
			int old_size_words = (s + 31) / 32;
			int new_size_words = num_words();
			if (val)
			{
				if (old_size_words && b) m_buf[old_size_words - 1] |= htonl((0xffffffff >> b));
				if (old_size_words < new_size_words)
					std::memset(m_buf + old_size_words, 0xff
						, size_t((new_size_words - old_size_words) * 4));
				clear_trailing_bits();
			}
			else
			{
				if (old_size_words < new_size_words)
					std::memset(m_buf + old_size_words, 0x00
						, size_t((new_size_words - old_size_words) * 4));
			}
			TORRENT_ASSERT(size() == bits);
		}

		void resize(int bits)
		{
			if (bits == size()) return;

			TORRENT_ASSERT(bits >= 0);
			// +1 because the first word is the size (in bits)
			const int b = (bits + 31) / 32;
			if (m_buf)
			{
				m_buf = static_cast<boost::uint32_t*>(std::realloc(m_buf-1, (b+1) * 4));
				m_buf = m_buf + 1;
				m_buf[-1] = bits;
			}
			else if (bits > 0)
			{
				m_buf = static_cast<boost::uint32_t*>(std::malloc((b+1) * 4));
				m_buf = m_buf + 1;
				m_buf[-1] = bits;
			}
			else if (m_buf != NULL)
			{
				std::free(m_buf-1);
				m_buf = NULL;
			}
			clear_trailing_bits();
			TORRENT_ASSERT(size() == bits);
		}

		// set all bits in the bitfield to 1 (set_all) or 0 (clear_all).
		void set_all()
		{
			std::memset(m_buf, 0xff, size_t(num_words() * 4));
			clear_trailing_bits();
		}
		void clear_all()
		{
			std::memset(m_buf, 0x00, size_t(num_words() * 4));
		}
	
		// make the bitfield empty, of zero size.
		void clear() { dealloc(); }

	private:

		void clear_trailing_bits()
		{
			// clear the tail bits in the last byte
			if (size() & 31) m_buf[num_words() - 1] &= htonl(0xffffffff << (32 - (size() & 31)));
		}

		void dealloc()
		{
			if (m_buf) std::free(m_buf-1);
			m_buf = NULL;
		}

		// the first element is not part of the bitfield, it's the
		// number of bits. For this purpose, the m_buf actually points
		// the element 1, not 0. To access the size (in bits), access
		// m_buf[-1]
		boost::uint32_t* m_buf;
	};

}

#endif // TORRENT_BITFIELD_HPP_INCLUDED

