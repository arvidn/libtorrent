/*

Copyright (c) 2011-2016, Arvid Norberg
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
#include "libtorrent/block_cache.hpp" // for cached_piece_entry

namespace libtorrent
{
	struct buffer_allocator_interface;

	namespace {
		struct caller_visitor : boost::static_visitor<>
		{
			explicit caller_visitor(buffer_allocator_interface& alloc, disk_io_job& j)
				: m_job(j), m_alloc(alloc) {}

			void operator()(disk_io_job::read_handler& h) const
			{
				if (!h) return;
				disk_buffer_holder block(m_alloc, m_job.d.io.ref, m_job.buffer.disk_block);
				h(std::move(block), m_job.flags, m_job.error);
			}

			void operator()(disk_io_job::write_handler& h) const
			{
				if (!h) return;
				h(m_job.error);
			}

			void operator()(disk_io_job::hash_handler& h) const
			{
				if (!h) return;
				h(m_job.piece, sha1_hash(m_job.d.piece_hash), m_job.error);
			}

			void operator()(disk_io_job::move_handler& h) const
			{
				if (!h) return;
				h(m_job.ret, std::string(m_job.buffer.string), m_job.error);
			}

			void operator()(disk_io_job::release_handler& h) const
			{
				if (!h) return;
				h();
			}

			void operator()(disk_io_job::check_handler& h) const
			{
				if (!h) return;
				h(m_job.ret, m_job.error);
			}

			void operator()(disk_io_job::rename_handler& h) const
			{
				if (!h) return;
				h(m_job.buffer.string, m_job.file_index, m_job.error);
			}

			void operator()(disk_io_job::clear_piece_handler& h) const
			{
				if (!h) return;
				h(m_job.piece);
			}

		private:
			disk_io_job& m_job;
			buffer_allocator_interface& m_alloc;
		};
	}

	disk_io_job::disk_io_job()
		: piece(0)
		, action(read)
	{
		buffer.disk_block = nullptr;
		d.io.offset = 0;
		d.io.buffer_size = 0;
		d.io.ref.storage = nullptr;
		d.io.ref.piece = piece_index_t{0};
		d.io.ref.block = 0;
	}

	disk_io_job::~disk_io_job()
	{
		if (action == rename_file || action == move_storage)
			free(buffer.string);
	}

	void disk_io_job::call_callback(buffer_allocator_interface& alloc)
	{
		boost::apply_visitor(caller_visitor(alloc, *this), callback);
	}

	bool disk_io_job::completed(cached_piece_entry const* pe, int block_size)
	{
		if (action != write) return false;

		int block_offset = d.io.offset & (block_size - 1);
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
