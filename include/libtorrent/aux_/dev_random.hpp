/*

Copyright (c) 2017-2020, Arvid Norberg
Copyright (c) 2017, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#ifndef TORRENT_DEV_RANDOM_HPP_INCLUDED
#define TORRENT_DEV_RANDOM_HPP_INCLUDED

#include "libtorrent/config.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/error_code.hpp" // for system_error
#include "libtorrent/aux_/throw.hpp"

#include <fcntl.h>

namespace libtorrent { namespace aux {

	struct dev_random
	{
		dev_random()
			: m_fd(open("/dev/random", O_RDONLY))
		{
			if (m_fd < 0)
			{
				throw_ex<system_error>(error_code(errno, system_category()));
			}
		}
		dev_random(dev_random const&) = delete;
		dev_random& operator=(dev_random const&) = delete;

		void read(span<char> buffer)
		{
			std::int64_t const ret = ::read(m_fd, buffer.data()
				, static_cast<std::size_t>(buffer.size()));
			if (ret != int(buffer.size()))
			{
				throw_ex<system_error>(errors::no_entropy);
			}
		}

		~dev_random() { close(m_fd); }

	private:
		int m_fd;
	};
}}

#endif

