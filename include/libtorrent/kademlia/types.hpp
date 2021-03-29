/*

Copyright (c) 2016-2017, 2019-2020, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef LIBTORRENT_TYPES_HPP
#define LIBTORRENT_TYPES_HPP

#include <cstdint>
#include <algorithm>
#include <array>

namespace lt {
namespace dht {

	struct public_key
	{
		public_key() = default;
		explicit public_key(char const* b)
		{ std::copy(b, b + len, bytes.begin()); }
		bool operator==(public_key const& rhs) const
		{ return bytes == rhs.bytes; }
		static constexpr int len = 32;
		std::array<char, len> bytes;
	};

	struct secret_key
	{
		secret_key() = default;
		explicit secret_key(char const* b)
		{ std::copy(b, b + len, bytes.begin()); }
		bool operator==(secret_key const& rhs) const
		{ return bytes == rhs.bytes; }
		static constexpr int len = 64;
		std::array<char, len> bytes;
	};

	struct signature
	{
		signature() = default;
		explicit signature(char const* b)
		{ std::copy(b, b + len, bytes.begin()); }
		bool operator==(signature const& rhs) const
		{ return bytes == rhs.bytes; }
		static constexpr int len = 64;
		std::array<char, len> bytes;
	};

	struct sequence_number
	{
		sequence_number() : value(0) {}
		explicit sequence_number(std::int64_t v) : value(v) {}
		sequence_number(sequence_number const& sqn) = default;
		bool operator<(sequence_number rhs) const
		{ return value < rhs.value; }
		bool operator>(sequence_number rhs) const
		{ return value > rhs.value; }
		sequence_number& operator=(sequence_number rhs) &
		{ value = rhs.value; return *this; }
		bool operator<=(sequence_number rhs) const
		{ return value <= rhs.value; }
		bool operator==(sequence_number const& rhs) const
		{ return value == rhs.value; }
		sequence_number& operator++()
		{ ++value; return *this; }
		std::int64_t value;
	};

} // namespace dht
} // namespace lt

#endif // LIBTORRENT_TYPES_HPP
