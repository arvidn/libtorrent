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
#include "libtorrent/block_cache.hpp" // for cached_piece_entry
#include "libtorrent/disk_buffer_holder.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/variant/get.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace libtorrent {

	namespace {
		struct caller_visitor : boost::static_visitor<>
		{
			explicit caller_visitor(disk_io_job& j)
				: m_job(j) {}

			void operator()(disk_io_job::read_handler& h) const
			{
				if (!h) return;
				h(std::move(boost::get<disk_buffer_holder>(m_job.argument))
					, m_job.flags, m_job.error);
			}

			void operator()(disk_io_job::write_handler& h) const
			{
				if (!h) return;
				h(m_job.error);
			}

			void operator()(disk_io_job::hash_handler& h) const
			{
				if (!h) return;
				h(m_job.piece, m_job.d.piece_hash, m_job.error);
			}

			void operator()(disk_io_job::move_handler& h) const
			{
				if (!h) return;
				h(m_job.ret, std::move(boost::get<std::string>(m_job.argument))
					, m_job.error);
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
				h(std::move(boost::get<std::string>(m_job.argument))
					, m_job.file_index, m_job.error);
			}

			void operator()(disk_io_job::clear_piece_handler& h) const
			{
				if (!h) return;
				h(m_job.piece);
			}

			void operator()(disk_io_job::set_file_prio_handler& h) const
			{
				if (!h) return;
				h(m_job.error, std::move(boost::get<aux::vector<download_priority_t, file_index_t>>(m_job.argument)));
			}

		private:
			disk_io_job& m_job;
		};
	}

	constexpr disk_job_flags_t disk_io_job::fence;
	constexpr disk_job_flags_t disk_io_job::in_progress;
	constexpr disk_job_flags_t disk_io_job::aborted;

	disk_io_job::disk_io_job()
		: argument(remove_flags_t{})
		, piece(0)
	{
		d.io.offset = 0;
		d.io.buffer_size = 0;
	}

	void disk_io_job::call_callback()
	{
		boost::apply_visitor(caller_visitor(*this), callback);
	}

	bool disk_io_job::completed(cached_piece_entry const* pe)
	{
		if (action != job_action_t::write) return false;

		int const block_offset = d.io.offset & (default_block_size - 1);
		int const size = d.io.buffer_size;
		int const start = d.io.offset / default_block_size;
		int const end = block_offset > 0 && (size > default_block_size - block_offset) ? start + 2 : start + 1;

		for (int i = start; i < end; ++i)
		{
			cached_block_entry const& b = pe->blocks[i];
			if (b.dirty || b.pending) return false;
		}

		// if all our blocks are not pending and not dirty, it means they
		// were successfully written to disk. This job is complete
		return true;
	}
}
