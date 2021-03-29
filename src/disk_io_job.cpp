/*

Copyright (c) 2010, 2014, 2016-2020, Arvid Norberg
Copyright (c) 2017-2018, Steven Siloti
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/disk_io_job.hpp"
#include "libtorrent/disk_buffer_holder.hpp"

#include "libtorrent/aux_/disable_warnings_push.hpp"
#include <boost/variant/get.hpp>
#include "libtorrent/aux_/disable_warnings_pop.hpp"

namespace lt::aux {

namespace {
	struct caller_visitor
	{
		explicit caller_visitor(disk_io_job& j)
			: m_job(j) {}

		void operator()(disk_io_job::read_handler& h) const
		{
			if (!h) return;
			h(std::move(std::get<disk_buffer_holder>(m_job.argument))
				, m_job.error);
		}

		void operator()(disk_io_job::write_handler& h) const
		{
			if (!h) return;
			h(m_job.error);
		}

		void operator()(disk_io_job::hash_handler& h) const
		{
			if (!h) return;
			h(m_job.piece, m_job.d.h.piece_hash, m_job.error);
		}

		void operator()(disk_io_job::hash2_handler& h) const
		{
			if (!h) return;
			h(m_job.piece, m_job.d.piece_hash2, m_job.error);
		}

		void operator()(disk_io_job::move_handler& h) const
		{
			if (!h) return;
			h(m_job.ret, std::move(std::get<std::string>(m_job.argument))
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
			h(std::move(std::get<std::string>(m_job.argument))
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
			h(m_job.error, std::move(std::get<aux::vector<download_priority_t, file_index_t>>(m_job.argument)));
		}

	private:
		disk_io_job& m_job;
	};
}

	disk_io_job::disk_io_job()
		: argument(remove_flags_t{})
		, piece(0)
	{
		d.io.offset = 0;
		d.io.buffer_size = 0;
	}

	void disk_io_job::call_callback()
	{
		std::visit(caller_visitor(*this), callback);
	}
}
