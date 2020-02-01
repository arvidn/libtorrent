/*

Copyright (c) 2016, Arvid Norberg
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

#ifndef LIBTORRENT_TYPES_HPP
#define LIBTORRENT_TYPES_HPP

#include <cstdint>
#include <algorithm>
#include <array>

namespace libtorrent { namespace dht {

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
		sequence_number& operator=(sequence_number rhs)
		{ value = rhs.value; return *this; }
		bool operator<=(sequence_number rhs) const
		{ return value <= rhs.value; }
		bool operator==(sequence_number const& rhs) const
		{ return value == rhs.value; }
		sequence_number& operator++()
		{ ++value; return *this; }
		std::int64_t value;
	};

}}

#endif // LIBTORRENT_TYPES_HPP
