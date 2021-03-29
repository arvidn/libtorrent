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

namespace lt::aux {

	struct dev_random
	{
		// the choice of /dev/urandom over /dev/random is based on:
		// https://www.mail-archive.com/cryptography@randombit.net/msg04763.html
		// https://security.stackexchange.com/questions/3936/is-a-rand-from-dev-urandom-secure-for-a-login-key/3939#3939
		dev_random()
			: m_fd(::open("/dev/urandom", O_RDONLY))
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

		~dev_random() { ::close(m_fd); }

	private:
		int m_fd;
	};
}

#endif

