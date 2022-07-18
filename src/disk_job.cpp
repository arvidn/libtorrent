/*

Copyright (c) 2017-2018, Steven Siloti
Copyright (c) 2010, 2014, 2016-2022, Arvid Norberg
Copyright (c) 2020, Alden Torres
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/aux_/disk_job.hpp"

namespace libtorrent {
namespace aux {

namespace {
	struct caller_visitor
	{
		explicit caller_visitor(disk_job& j) : m_job(j) {}

		void operator()(job::read& j) const
		{
			if (!j.handler) return;
			j.handler(std::move(j.buf), m_job.error);
		}

		void operator()(job::write& j) const
		{
			if (!j.handler) return;
			j.handler(m_job.error);
		}

		void operator()(job::hash& j) const
		{
			if (!j.handler) return;
			j.handler(j.piece, j.piece_hash, m_job.error);
		}

		void operator()(job::hash2& j) const
		{
			if (!j.handler) return;
			j.handler(j.piece, j.piece_hash2, m_job.error);
		}

		void operator()(job::move_storage& j) const
		{
			if (!j.handler) return;
			j.handler(m_job.ret, std::move(j.path), m_job.error);
		}

		void operator()(job::release_files& j) const
		{
			if (!j.handler) return;
			j.handler();
		}

		void operator()(job::delete_files& j) const
		{
			if (!j.handler) return;
			j.handler(m_job.error);
		}

		void operator()(job::check_fastresume& j) const
		{
			if (!j.handler) return;
			j.handler(m_job.ret, m_job.error);
		}

		void operator()(job::rename_file& j) const
		{
			if (!j.handler) return;
			j.handler(std::move(j.name), j.file_index, m_job.error);
		}

		void operator()(job::stop_torrent& j) const
		{
			if (!j.handler) return;
			j.handler();
		}

		void operator()(job::file_priority& j) const
		{
			if (!j.handler) return;
			j.handler(m_job.error, std::move(j.prio));
		}

		void operator()(job::clear_piece& j) const
		{
			if (!j.handler) return;
			j.handler(j.piece);
		}

		void operator()(job::partial_read& j) const
		{
			if (!j.handler) return;
			j.handler(std::move(j.buf), m_job.error);
		}

		void operator()(job::kick_hasher&) const {}

	private:
		disk_job& m_job;
	};
}
	void disk_job::call_callback()
	{
		std::visit(caller_visitor(*this), action);
	}
}
}
