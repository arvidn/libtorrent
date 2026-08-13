/*

Copyright (c) 2016-2018, Alden Torres
Copyright (c) 2016-2022, Arvid Norberg
Copyright (c) 2017, Falcosc
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/bitfield.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/cpuid.hpp"
#include "libtorrent/aux_/unique_ptr.hpp"

#include <bit>

#ifdef __clang__
// disable these warnings until this class is re-worked in a way clang likes
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif

namespace libtorrent {

	bool bitfield::all_set() const noexcept
	{
		if(size() == 0) return false;

		int const words = size() / 32;
		for (int i = 1; i < words + 1; ++i)
		{
			if (m_buf[i] != 0xffffffff) return false;
		}
		int const rest = size() & 31;
		if (rest > 0)
		{
			std::uint32_t const mask = aux::host_to_network(0xffffffff << (32 - rest));
			if ((m_buf[words + 1] & mask) != mask) return false;
		}
		return true;
	}

	bool bitfield::operator==(lt::bitfield const& rhs) const
	{
		if (m_buf == rhs.m_buf)
			return true;

		if (size() != rhs.size())
			return false;

		std::uint32_t const* lb = buf();
		std::uint32_t const* rb = rhs.buf();

		return std::memcmp(lb, rb, std::size_t(num_words()) * 4) == 0;
	}

	int bitfield::count() const noexcept
	{
		int ret = 0;
		int const words = num_words();

		for (int i = 1; i < words + 1; ++i)
			ret += std::popcount(m_buf[i]);

		TORRENT_ASSERT(ret <= size());
		TORRENT_ASSERT(ret >= 0);
		return ret;
	}

	void bitfield::resize(int const bits, bool const val)
	{
		if (bits == size()) return;

		int const s = size();
		int const b = size() & 31;
		resize(bits);
		if (s >= size()) return;
		int const old_size_words = (s + 31) / 32;
		int const new_size_words = num_words();
		if (val)
		{
			if (old_size_words && b) buf()[old_size_words - 1] |= aux::host_to_network(0xffffffff >> b);
			if (old_size_words < new_size_words)
				std::memset(buf() + old_size_words, 0xff
					, static_cast<std::size_t>(new_size_words - old_size_words) * 4);
			clear_trailing_bits();
		}
		else
		{
			if (old_size_words < new_size_words)
				std::memset(buf() + old_size_words, 0x00
					, static_cast<std::size_t>(new_size_words - old_size_words) * 4);
		}
		TORRENT_ASSERT(size() == bits);
	}

	void bitfield::resize(int const bits)
	{
		if (bits == size()) return;

		TORRENT_ASSERT(bits >= 0);
		if (bits == 0)
		{
			m_buf.reset();
			return;
		}
		int const new_size_words = (bits + 31) / 32;
		int const cur_size_words = num_words();
		if (cur_size_words != new_size_words)
		{
			auto b = aux::make_unique<std::uint32_t[], std::ptrdiff_t>(new_size_words + 1);
#ifdef BOOST_NO_EXCEPTIONS
			if (b == nullptr) std::terminate();
#endif
			b[0] = aux::numeric_cast<std::uint32_t>(bits);
			if (m_buf) std::memcpy(&b[1], buf()
				, aux::numeric_cast<std::size_t>(std::min(new_size_words, cur_size_words) * 4));
			if (new_size_words > cur_size_words)
			{
				std::memset(&b[1 + cur_size_words], 0
					, aux::numeric_cast<std::size_t>((new_size_words - cur_size_words) * 4));
			}
			m_buf = std::move(b);
		}
		else
		{
			m_buf[0] = aux::numeric_cast<std::uint32_t>(bits);
		}

		clear_trailing_bits();
		TORRENT_ASSERT(size() == bits);
	}

	int bitfield::find_first_set() const noexcept
	{
		int const num = num_words();
		if (num == 0) return -1;
		int const count = aux::count_leading_zeros({&m_buf[1], num});
		return count != num * 32 ? count : -1;
	}

	int bitfield::find_last_clear() const noexcept
	{
		int const num = num_words();
		if (num == 0) return - 1;
		int const size = this->size();
		std::uint32_t const mask = 0xffffffff << (32 - (size & 31));
		std::uint32_t const last = m_buf[num] ^ aux::host_to_network(mask);
		int const ext = aux::count_trailing_ones(~last) - (31 - (size % 32));
		return last != 0
			? (num - 1) * 32 + ext
			: size - (aux::count_trailing_ones({&m_buf[1], num - 1}) + ext);
	}

	static_assert(std::is_nothrow_move_constructible<bitfield>::value
		, "should be nothrow move constructible");
	static_assert(std::is_nothrow_move_assignable<bitfield>::value
		, "should be nothrow move assignable");
	static_assert(std::is_nothrow_default_constructible<bitfield>::value
		, "should be nothrow default constructible");

	static_assert(std::is_nothrow_move_constructible<typed_bitfield<int>>::value
		, "should be nothrow move constructible");
	static_assert(std::is_nothrow_move_assignable<typed_bitfield<int>>::value
		, "should be nothrow move assignable");
	static_assert(std::is_nothrow_default_constructible<typed_bitfield<int>>::value
		, "should be nothrow default constructible");
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

