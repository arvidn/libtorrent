/*

Copyright (c) 2008-2018, Arvid Norberg
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
#include "libtorrent/index_range.hpp"
#include "libtorrent/aux_/unique_ptr.hpp"
#include "libtorrent/aux_/byteswap.hpp"
#include "libtorrent/aux_/ffs.hpp"

#include <cstring> // for memset and memcpy
#include <cstdint> // uint32_t

namespace libtorrent {

	// The bitfield type stores any number of bits as a bitfield
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
		// the nearest byte boundary).
		bitfield() noexcept = default;
		explicit bitfield(int bits) { resize(bits); }
		bitfield(int bits, bool val) { resize(bits, val); }
		bitfield(char const* b, int bits) { assign(b, bits); }
		bitfield(bitfield const& rhs) { assign(rhs.data(), rhs.size()); }
		bitfield(bitfield&& rhs) noexcept = default;

		// copy bitfield from buffer ``b`` of ``bits`` number of bits, rounded up to
		// the nearest byte boundary.
		void assign(char const* b, int const bits)
		{
			resize(bits);
			if (bits > 0)
			{
				std::memcpy(buf(), b, std::size_t((bits + 7) / 8));
				clear_trailing_bits();
			}
		}

		// query bit at ``index``. Returns true if bit is 1, otherwise false.
		bool operator[](int index) const noexcept
		{ return get_bit(index); }
		bool get_bit(int index) const noexcept
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < size());
			return (buf()[index / 32] & aux::host_to_network(0x80000000 >> (index & 31))) != 0;
		}

		// set bit at ``index`` to 0 (clear_bit) or 1 (set_bit).
		void clear_bit(int index) noexcept
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < size());
			buf()[index / 32] &= aux::host_to_network(~(0x80000000 >> (index & 31)));
		}
		void set_bit(int index) noexcept
		{
			TORRENT_ASSERT(index >= 0);
			TORRENT_ASSERT(index < size());
			buf()[index / 32] |= aux::host_to_network(0x80000000 >> (index & 31));
		}

		// returns true if all bits in the bitfield are set
		bool all_set() const noexcept;

		// returns true if no bit in the bitfield is set
		bool none_set() const noexcept
		{
			if(size() == 0) return true;

			const int words = num_words();
			std::uint32_t const* b = buf();
			for (int i = 0; i < words; ++i)
			{
				if (b[i] != 0) return false;
			}
			return true;
		}

		// returns the size of the bitfield in bits.
		int size() const noexcept
		{
			int const bits = m_buf == nullptr ? 0 : int(m_buf[0]);
			TORRENT_ASSERT(bits >= 0);
			return bits;
		}

		// returns the number of 32 bit words are needed to represent all bits in
		// this bitfield.
		int num_words() const noexcept
		{
			return (size() + 31) / 32;
		}

		// returns true if the bitfield has zero size.
		bool empty() const noexcept { return size() == 0; }

		// returns a pointer to the internal buffer of the bitfield, or
		// ``nullptr`` if it's empty.
		char const* data() const noexcept { return m_buf ? reinterpret_cast<char const*>(&m_buf[1]) : nullptr; }
		char* data() noexcept { return m_buf ? reinterpret_cast<char*>(&m_buf[1]) : nullptr; }

#if TORRENT_ABI_VERSION == 1
		TORRENT_DEPRECATED
		char const* bytes() const { return data(); }
#endif

		// hidden
		bitfield& operator=(bitfield const& rhs)
		{
			if (&rhs == this) return *this;
			assign(rhs.data(), rhs.size());
			return *this;
		}
		bitfield& operator=(bitfield&& rhs) noexcept = default;

		// swaps the bit-fields two variables refer to
		void swap(bitfield& rhs) noexcept
		{
			std::swap(m_buf, rhs.m_buf);
		}

		// count the number of bits in the bitfield that are set to 1.
		int count() const noexcept;

		// returns the index of the first set bit in the bitfield, i.e. 1 bit.
		int find_first_set() const noexcept;

		// returns the index to the last cleared bit in the bitfield, i.e. 0 bit.
		int find_last_clear() const noexcept;

		// internal
		struct const_iterator
		{
		friend struct bitfield;

			using value_type = bool;
			using difference_type = ptrdiff_t;
			using pointer = bool const*;
			using reference = bool&;
			using iterator_category = std::forward_iterator_tag;

			bool operator*() noexcept { return (*buf & aux::host_to_network(bit)) != 0; }
			const_iterator& operator++() noexcept { inc(); return *this; }
			const_iterator operator++(int) noexcept
			{ const_iterator ret(*this); inc(); return ret; }
			const_iterator& operator--() noexcept { dec(); return *this; }
			const_iterator operator--(int) noexcept
			{ const_iterator ret(*this); dec(); return ret; }

			const_iterator() noexcept {}
			bool operator==(const_iterator const& rhs) const noexcept
			{ return buf == rhs.buf && bit == rhs.bit; }

			bool operator!=(const_iterator const& rhs) const noexcept
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
			const_iterator(std::uint32_t const* ptr, int offset)
				: buf(ptr), bit(0x80000000 >> offset) {}
			std::uint32_t const* buf = nullptr;
			std::uint32_t bit = 0x80000000;
		};

		// internal
		const_iterator begin() const noexcept { return const_iterator(m_buf ? buf() : nullptr, 0); }
		const_iterator end() const noexcept
		{
			if (m_buf)
				return const_iterator(buf() + num_words() - (((size() & 31) == 0) ? 0 : 1), size() & 31);
			else
				return const_iterator(nullptr, size() & 31);
		}

		// set the size of the bitfield to ``bits`` length. If the bitfield is extended,
		// the new bits are initialized to ``val``.
		void resize(int bits, bool val);
		void resize(int bits);

		// set all bits in the bitfield to 1 (set_all) or 0 (clear_all).
		void set_all() noexcept
		{
			if (size() == 0) return;
			std::memset(buf(), 0xff, std::size_t(num_words()) * 4);
			clear_trailing_bits();
		}
		void clear_all() noexcept
		{
			if (size() == 0) return;
			std::memset(buf(), 0x00, std::size_t(num_words()) * 4);
		}

		// make the bitfield empty, of zero size.
		void clear() noexcept { m_buf.reset(); }

	private:

		std::uint32_t const* buf() const noexcept { TORRENT_ASSERT(m_buf); return &m_buf[1]; }
		std::uint32_t* buf() noexcept { TORRENT_ASSERT(m_buf); return &m_buf[1]; }
		void clear_trailing_bits() noexcept
		{
			// clear the tail bits in the last byte
			if (size() & 31) buf()[num_words() - 1] &= aux::host_to_network(0xffffffff << (32 - (size() & 31)));
		}

		// the first element is not part of the bitfield, it's the
		// number of bits.
		aux::unique_ptr<std::uint32_t[]> m_buf;
	};

	template <typename IndexType>
	struct typed_bitfield : bitfield
	{
		typed_bitfield() noexcept {}
		typed_bitfield(typed_bitfield&& rhs) noexcept
			: bitfield(std::forward<bitfield>(rhs))
		{}
		typed_bitfield(typed_bitfield const& rhs)
			: bitfield(static_cast<bitfield const&>(rhs))
		{}
		typed_bitfield(bitfield&& rhs) noexcept : bitfield(std::forward<bitfield>(rhs)) {} // NOLINT
		typed_bitfield(bitfield const& rhs) : bitfield(rhs) {} // NOLINT
		typed_bitfield& operator=(typed_bitfield&& rhs) noexcept
		{
			this->bitfield::operator=(std::forward<bitfield>(rhs));
			return *this;
		}
		typed_bitfield& operator=(typed_bitfield const& rhs)
		{
			this->bitfield::operator=(rhs);
			return *this;
		}
		using bitfield::bitfield;

		// returns an object that can be used in a range-for to iterate over all
		// indices in the bitfield
		index_range<IndexType> range() const noexcept
		{
			return {IndexType{0}, end_index()};
		}

		bool operator[](IndexType const index) const
		{ return this->bitfield::get_bit(static_cast<int>(index)); }

		bool get_bit(IndexType const index) const
		{ return this->bitfield::get_bit(static_cast<int>(index)); }

		void clear_bit(IndexType const index)
		{ this->bitfield::clear_bit(static_cast<int>(index)); }

		void set_bit(IndexType const index)
		{ this->bitfield::set_bit(static_cast<int>(index)); }

		IndexType end_index() const noexcept { return IndexType(this->size()); }
	};
}

#endif // TORRENT_BITFIELD_HPP_INCLUDED
