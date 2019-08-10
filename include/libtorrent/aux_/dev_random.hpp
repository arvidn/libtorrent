/*

Copyright (c) 2017, Arvid Norberg
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

