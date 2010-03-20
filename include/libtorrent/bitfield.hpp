/*

Copyright (c) 2008, Arvid Norberg
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
#include <cstring> // for memset and memcpy

namespace libtorrent
{
	struct TORRENT_EXPORT bitfield
	{
		bitfield(): m_bytes(0), m_size(0), m_own(false) {}
		bitfield(int bits): m_bytes(0), m_size(0)
		{ resize(bits); }
		bitfield(int bits, bool val): m_bytes(0), m_size(0)
		{ resize(bits, val); }
		bitfield(char const* bytes, int bits): m_bytes(0), m_size(0)
		{ assign(bytes, bits); }
		bitfield(bitfield const& rhs): m_bytes(0), m_size(0), m_own(false)
		{ assign(rhs.bytes(), rhs.size()); }

		void borrow_bytes(char* bytes, int bits)
		{
			dealloc();
			m_bytes = (unsigned char*)bytes;
			m_size = bits;
			m_own = false;
		}
		~bitfield() { dealloc(); }

		void assign(char const* bytes, int bits)
		{ resize(bits); std::memcpy(m_bytes, bytes, (bits + 7) / 8); clear_trailing_bits(); }

		bool operator[](int index) const
		{ return get_bit(index); }

		bool get_bit(int index) const
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < m_size);
			return (m_bytes[index / 8] & (0x80 >> (index & 7))) != 0;
		}
		
		void clear_bit(int index)
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < m_size);
			m_bytes[index / 8] &= ~(0x80 >> (index & 7));
		}

		void set_bit(int index)
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < m_size);
			m_bytes[index / 8] |= (0x80 >> (index & 7));
		}

		std::size_t size() const { return m_size; }
		bool empty() const { return m_size == 0; }

		char const* bytes() const { return (char*)m_bytes; }

		bitfield& operator=(bitfield const& rhs)
		{
			assign(rhs.bytes(), rhs.size());
			return *this;
		}

		int count() const
		{
			// 0000, 0001, 0010, 0011, 0100, 0101, 0110, 0111,
			// 1000, 1001, 1010, 1011, 1100, 1101, 1110, 1111
			const static char num_bits[] =
			{
				0, 1, 1, 2, 1, 2, 2, 3,
				1, 2, 2, 3, 2, 3, 3, 4
			};

			int ret = 0;
			const int num_bytes = m_size / 8;
			for (int i = 0; i < num_bytes; ++i)
			{
				ret += num_bits[m_bytes[i] & 0xf] + num_bits[m_bytes[i] >> 4];
			}

			int rest = m_size - num_bytes * 8;
			for (int i = 0; i < rest; ++i)
			{
				ret += (m_bytes[num_bytes] >> (7-i)) & 1;
			}
			TORRENT_ASSERT(ret <= m_size);
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

			bool operator*() { return (*byte & bit) != 0; }
			const_iterator& operator++() { inc(); return *this; }
			const_iterator operator++(int)
			{ const_iterator ret(*this); inc(); return ret; }
			const_iterator& operator--() { dec(); return *this; }
			const_iterator operator--(int)
			{ const_iterator ret(*this); dec(); return ret; }

			const_iterator(): byte(0), bit(0x80) {}
			bool operator==(const_iterator const& rhs) const
			{ return byte == rhs.byte && bit == rhs.bit; }

			bool operator!=(const_iterator const& rhs) const
			{ return byte != rhs.byte || bit != rhs.bit; }

		private:
			void inc()
			{
				TORRENT_ASSERT(byte);
				if (bit == 0x01)
				{
					bit = 0x80;
					++byte;
				}
				else
				{
					bit >>= 1;
				}
			}
			void dec()
			{
				TORRENT_ASSERT(byte);
				if (bit == 0x80)
				{
					bit = 0x01;
					--byte;
				}
				else
				{
					bit <<= 1;
				}
			}
			const_iterator(unsigned char const* ptr, int offset)
				: byte(ptr), bit(0x80 >> offset) {}
			unsigned char const* byte;
			int bit;
		};

		const_iterator begin() const { return const_iterator(m_bytes, 0); }
		const_iterator end() const { return const_iterator(m_bytes + m_size / 8, m_size & 7); }

		void resize(int bits, bool val)
		{
			int s = m_size;
			int b = m_size & 7;
			resize(bits);
			if (s >= m_size) return;
			int old_size_bytes = (s + 7) / 8;
			int new_size_bytes = (m_size + 7) / 8;
			if (val)
			{
				if (old_size_bytes && b) m_bytes[old_size_bytes - 1] |= (0xff >> b);
				if (old_size_bytes < new_size_bytes)
					std::memset(m_bytes + old_size_bytes, 0xff, new_size_bytes - old_size_bytes);
				clear_trailing_bits();
			}
			else
			{
				if (old_size_bytes < new_size_bytes)
					std::memset(m_bytes + old_size_bytes, 0x00, new_size_bytes - old_size_bytes);
			}
		}

		void set_all()
		{
			std::memset(m_bytes, 0xff, (m_size + 7) / 8);
			clear_trailing_bits();
		}

		void clear_all()
		{
			std::memset(m_bytes, 0x00, (m_size + 7) / 8);
		}
	
		void resize(int bits)
		{
			const int bytes = (bits + 7) / 8;
			if (m_bytes)
			{
				if (m_own)
				{
					m_bytes = (unsigned char*)std::realloc(m_bytes, bytes);
					m_own = true;
				}
				else if (bits > m_size)
				{
					unsigned char* tmp = (unsigned char*)std::malloc(bytes);
					std::memcpy(tmp, m_bytes, (std::min)((m_size + 7)/ 8, bytes));
					m_bytes = tmp;
					m_own = true;
				}
			}
			else
			{
				m_bytes = (unsigned char*)std::malloc(bytes);
				m_own = true;
			}
			m_size = bits;
			clear_trailing_bits();
		}

	private:

		void clear_trailing_bits()
		{
			// clear the tail bits in the last byte
			if (m_size & 7) m_bytes[(m_size + 7) / 8 - 1] &= 0xff << (8 - (m_size & 7));
		}

		void dealloc() { if (m_own) std::free(m_bytes); m_bytes = 0; }
		unsigned char* m_bytes;
		int m_size; // in bits
		bool m_own;
	};

}

#endif // TORRENT_BITFIELD_HPP_INCLUDED

