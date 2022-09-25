/*

Copyright (c) 2022, Arvid Norberg
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

#include "libtorrent/config.hpp"
#include "libtorrent/aux_/simple_buffer_pool.hpp"
#include "libtorrent/assert.hpp"
#include "libtorrent/settings_pack.hpp" // for settings_interface
#include "libtorrent/disk_interface.hpp" // for default_block_size
#include "libtorrent/aux_/throw.hpp"

namespace libtorrent {
namespace aux {

	simple_buffer_pool::simple_buffer_pool() = default;
	simple_buffer_pool::~simple_buffer_pool() = default;

	disk_buffer_holder simple_buffer_pool::allocate_buffer(char const* category, int const size)
	{
		TORRENT_UNUSED(category);
		TORRENT_ASSERT(size <= default_block_size);
		char* ret = static_cast<char*>(std::malloc(default_block_size));
		if (ret == nullptr)
			aux::throw_ex<std::bad_alloc>();

		++m_in_use;

		return disk_buffer_holder(*this, ret, size);
	}

	void simple_buffer_pool::free_disk_buffer(char* buf)
	{
		std::free(buf);
		--m_in_use;
	}

}
}
