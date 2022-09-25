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

#ifndef TORRENT_SIMPLE_BUFFER_POOL_HPP
#define TORRENT_SIMPLE_BUFFER_POOL_HPP

#include "libtorrent/config.hpp"

#include <atomic>

#include "libtorrent/io_context.hpp"
#include "libtorrent/span.hpp"
#include "libtorrent/disk_buffer_holder.hpp" // for buffer_allocator_interface


namespace libtorrent {

	struct settings_interface;
	struct disk_observer;

namespace aux {

	struct TORRENT_EXTRA_EXPORT simple_buffer_pool final
		: buffer_allocator_interface
	{
		simple_buffer_pool();
		~simple_buffer_pool();
		simple_buffer_pool(simple_buffer_pool const&) = delete;
		simple_buffer_pool& operator=(simple_buffer_pool const&) = delete;

		disk_buffer_holder allocate_buffer(char const* category, int size);

		void free_disk_buffer(char* b) override;

		int in_use() const { return m_in_use; }

	private:

		// number of disk buffers currently allocated
		std::atomic<int> m_in_use{0};
	};

}
}

#endif // TORRENT_DISK_BUFFER_POOL_HPP
