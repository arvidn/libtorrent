/*

Copyright (c) 2011-2018, Arvid Norberg
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

#include "libtorrent/disk_io_job.hpp"
#include "libtorrent/storage.hpp"
#include "libtorrent/block_cache.hpp" // for cached_piece_entry
#include "libtorrent/entry.hpp"

namespace libtorrent
{
	disk_io_job::disk_io_job()
		: requester(0)
		, piece(0)
		, action(read)
		, ret(0)
		, flags(0)
#if defined TORRENT_DEBUG || defined TORRENT_RELEASE_ASSERTS
		, in_use(false)
		, job_posted(false)
		, callback_called(false)
		, blocked(false)
#endif
	{
		buffer.disk_block = 0;
		d.io.offset = 0;
		d.io.buffer_size = 0;
		d.io.ref.storage = 0;
		d.io.ref.piece = 0;
		d.io.ref.block = 0;
	}

	disk_io_job::~disk_io_job()
	{
		if (action == rename_file || action == move_storage)
			free(buffer.string);
		else if (action == save_resume_data)
			delete static_cast<entry*>(buffer.resume_data);
	}

	bool disk_io_job::completed(cached_piece_entry const* pe, int block_size)
	{
		if (action != write) return false;

		int block_offset = d.io.offset & (block_size-1);
		int size = d.io.buffer_size;
		int start = d.io.offset / block_size;
		int end = block_offset > 0 && (size > block_size - block_offset) ? start + 2 : start + 1;

		for (int i = start; i < end; ++i)
			if (pe->blocks[i].dirty || pe->blocks[i].pending) return false;

		// if all our blocks are not pending and not dirty, it means they
		// were successfully written to disk. This job is complete
		return true;
	}
}

