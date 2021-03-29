/*

Copyright (c) 2003, 2008-2010, 2019-2020, Arvid Norberg
Copyright (c) 2016, 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/stat.hpp"

namespace lt::aux {

void stat_channel::second_tick(int tick_interval_ms)
{
	std::int64_t sample = std::int64_t(m_counter) * 1000 / tick_interval_ms;
	TORRENT_ASSERT(sample >= 0);
	m_5_sec_average = std::int32_t(std::int64_t(m_5_sec_average) * 4 / 5 + sample / 5);
	m_counter = 0;
}

}
