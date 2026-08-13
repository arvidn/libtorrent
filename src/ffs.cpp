/*

Copyright (c) 2016, Alden Torres
Copyright (c) 2017-2021, Arvid Norberg
Copyright (c) 2018, Pavel Pimenov
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/ffs.hpp"
#include "libtorrent/aux_/byteswap.hpp"

#include <bit>

namespace libtorrent {
namespace aux {

int count_leading_zeros(span<std::uint32_t const> buf)
{
	auto const num = int(buf.size());
	std::uint32_t const* ptr = buf.data();

	TORRENT_ASSERT(num >= 0);
	TORRENT_ASSERT(ptr != nullptr);

	for (int i = 0; i < num; i++)
	{
		std::uint32_t const v = aux::network_to_host(ptr[i]);
		if (v == 0)
			continue;
		return i * 32 + std::countl_zero(v);
	}

	return num * 32;
}

int count_trailing_ones(span<std::uint32_t const> buf)
{
	auto const num = int(buf.size());
	std::uint32_t const* ptr = buf.data();

	TORRENT_ASSERT(num >= 0);
	TORRENT_ASSERT(ptr != nullptr);

	for (int i = num - 1; i >= 0; i--)
	{
		std::uint32_t const v = aux::network_to_host(ptr[i]);
		if (v == 0xffffffff)
			continue;
		return (num - i - 1) * 32 + std::countr_one(v);
	}

	return num * 32;
}
}}
