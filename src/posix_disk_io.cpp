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

#include "libtorrent/config.hpp"
#include "libtorrent/posix_disk_io.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/disk_buffer_pool.hpp"
#include "libtorrent/io_service.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/posix_storage.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/add_torrent_params.hpp"

#include <vector>

namespace libtorrent {

namespace {

	template <typename F>
	struct move_wrapper : F
	{
		move_wrapper(F&& f) : F(std::move(f)) {} // NOLINT

		move_wrapper(move_wrapper&&) = default;
		move_wrapper& operator=(move_wrapper&&) = default;

		move_wrapper(const move_wrapper&);
		move_wrapper& operator=(const move_wrapper&);
	};

	template <typename T>
	auto move_handler(T&& t) -> move_wrapper<typename std::decay<T>::type>
	{
		return std::move(t);
	}

	// the only reason this type exists, is because in C++11 you cannot move
	// objects into a lambda capture, and the disk_buffer_holder has to be moved
	struct call_read_handler
	{
		call_read_handler(
			std::function<void(disk_buffer_holder, storage_error const&)> handler
			, disk_buffer_holder buf
			, storage_error error)
			: m_buf(std::move(buf))
			, m_handler(std::move(handler))
			, m_error(std::move(error))
		{}

		void operator()()
		{
			m_handler(std::move(m_buf), m_error);
		}

	private:

		disk_buffer_holder m_buf;
		std::function<void(disk_buffer_holder, storage_error const&)> m_handler;
		storage_error m_error;
	};

	storage_index_t pop(std::vector<storage_index_t>& q)
	{
		TORRENT_ASSERT(!q.empty());
		storage_index_t const ret = q.back();
		q.pop_back();
		return ret;
	}

	using aux::posix_storage;

} // anonymous namespace

	struct TORRENT_EXTRA_EXPORT posix_disk_io final
		: disk_interface
		, buffer_allocator_interface
	{
		posix_disk_io(io_service& ios, counters& cnt)
			: m_buffer_pool(ios)
			, m_stats_counters(cnt)
			, m_ios(ios)
		{}

		void set_settings(settings_pack const* pack) override
		{
			apply_pack(pack, m_settings);
			m_buffer_pool.set_settings(m_settings);
		}

		storage_holder new_torrent(storage_params params
			, std::shared_ptr<void> const&) override
		{
			storage_index_t const idx = m_free_slots.empty()
				? m_torrents.end_index()
				: pop(m_free_slots);
			auto storage = std::make_shared<posix_storage>(std::move(params));
			if (idx == m_torrents.end_index()) m_torrents.emplace_back(std::move(storage));
			else m_torrents[idx] = std::move(storage);
			return storage_holder(idx, *this);
		}

		void remove_torrent(storage_index_t const idx) override
		{
			m_torrents[idx].reset();
			m_free_slots.push_back(idx);
		}

		void abort(bool) override {}

		void async_read(storage_index_t storage, peer_request const& r
			, std::function<void(disk_buffer_holder block, storage_error const& se)> handler
			, disk_job_flags_t) override
		{
			disk_buffer_holder buffer = disk_buffer_holder(*this, m_buffer_pool.allocate_buffer("send buffer"), default_block_size);
			storage_error error;
			if (buffer.get() == nullptr)
			{
				error.ec = errors::no_memory;
				error.operation = operation_t::alloc_cache_piece;
				m_ios.post([=]{ handler(disk_buffer_holder(*this, nullptr, 0), error); });
				return;
			}

			time_point const start_time = clock_type::now();

			iovec_t b = {buffer.get(), std::size_t(r.length)};

			m_torrents[storage]->readv(m_settings, b, r.piece, r.start
				, error);

			if (!error.ec)
			{
				std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

				m_stats_counters.inc_stats_counter(counters::num_read_back);
				m_stats_counters.inc_stats_counter(counters::num_blocks_read);
				m_stats_counters.inc_stats_counter(counters::num_read_ops);
				m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
			}

			m_ios.post(move_handler(call_read_handler(std::move(handler), std::move(buffer), error)));
		}

		bool async_write(storage_index_t storage, peer_request const& r
			, char const* buf, std::shared_ptr<disk_observer>
			, std::function<void(storage_error const&)> handler
			, disk_job_flags_t) override
		{
			// TODO: 3 this const_cast can be removed once iovec_t is no longer a
			// thing, but we just use plain spans
			iovec_t const b = { const_cast<char*>(buf), std::size_t(r.length) };

			time_point const start_time = clock_type::now();

			storage_error error;
			m_torrents[storage]->writev(m_settings, b, r.piece, r.start, error);

			if (!error.ec)
			{
				std::int64_t const write_time = total_microseconds(clock_type::now() - start_time);

				m_stats_counters.inc_stats_counter(counters::num_blocks_written);
				m_stats_counters.inc_stats_counter(counters::num_write_ops);
				m_stats_counters.inc_stats_counter(counters::disk_write_time, write_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, write_time);
			}

			m_ios.post([=]{ handler(error); });
			return false;
		}

		void async_hash(storage_index_t storage, piece_index_t const piece, disk_job_flags_t
			, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler) override
		{
			time_point const start_time = clock_type::now();

			disk_buffer_holder buffer = disk_buffer_holder(*this, m_buffer_pool.allocate_buffer("hash buffer"), default_block_size);
			storage_error error;
			if (buffer.get() == nullptr)
			{
				error.ec = errors::no_memory;
				error.operation = operation_t::alloc_cache_piece;
				m_ios.post([=]{ handler(piece, sha1_hash{}, error); });
				return;
			}
			hasher ph;

			posix_storage* st = m_torrents[storage].get();

			int const piece_size = st->files().piece_size(piece);
			int const blocks_in_piece = (piece_size + default_block_size - 1) / default_block_size;

			int offset = 0;
			for (int i = 0; i < blocks_in_piece; ++i)
			{
				std::size_t const len = aux::numeric_cast<std::size_t>(
					std::min(default_block_size, piece_size - offset));

				iovec_t b = {buffer.get(), len};
				int const ret = st->readv(m_settings, b, piece, offset, error);
				offset += default_block_size;
				if (ret <= 0) break;
				ph.update(b.first(std::size_t(ret)));
			}

			sha1_hash const hash = ph.final();

			if (!error.ec)
			{
				std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

				m_stats_counters.inc_stats_counter(counters::num_read_back);
				m_stats_counters.inc_stats_counter(counters::num_blocks_read);
				m_stats_counters.inc_stats_counter(counters::num_read_ops);
				m_stats_counters.inc_stats_counter(counters::disk_hash_time, read_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
			}

			m_ios.post([=]{ handler(piece, hash, error); });
		}

		void async_move_storage(storage_index_t, std::string p, move_flags_t
			, std::function<void(status_t, std::string const&, storage_error const&)> handler) override
		{
			m_ios.post([=]{
				handler(status_t::fatal_disk_error, p
					, storage_error(error_code(boost::system::errc::operation_not_supported, system_category())));
			});
		}

		void async_release_files(storage_index_t, std::function<void()>) override {}

		void async_delete_files(storage_index_t storage, remove_flags_t const options
			, std::function<void(storage_error const&)> handler) override
		{
			storage_error error;
			posix_storage* st = m_torrents[storage].get();
			st->delete_files(options, error);
			m_ios.post([=]{ handler(error); });
		}

		void async_check_files(storage_index_t storage
			, add_torrent_params const* resume_data
			, aux::vector<std::string, file_index_t>& links
			, std::function<void(status_t, storage_error const&)> handler) override
		{
			posix_storage* st = m_torrents[storage].get();

			add_torrent_params tmp;
			add_torrent_params const* rd = resume_data ? resume_data : &tmp;

			storage_error error;
			status_t ret = status_t::no_error;

			storage_error se;
			if ((rd->have_pieces.empty()
					|| !st->verify_resume_data(*rd, links, error))
				&& !m_settings.get_bool(settings_pack::no_recheck_incomplete_resume))
			{
				bool const has_files = st->has_any_file(se);

				if (has_files && !se)
				{
					ret = status_t::need_full_check;
				}
			}

			if (!se) st->initialize(m_settings, se);

			if (se)
			{
				error = se;
				ret = status_t::fatal_disk_error;
			}

			m_ios.post([=]{ handler(ret, error); });
		}

		void async_rename_file(storage_index_t const storage
			, file_index_t const idx
			, std::string const name
			, std::function<void(std::string const&, file_index_t, storage_error const&)> handler) override
		{
			posix_storage* st = m_torrents[storage].get();
			storage_error error;
			st->rename_file(idx, name, error);
			m_ios.post([=]{ handler(name, idx, error); });
		}

		void async_stop_torrent(storage_index_t
			, std::function<void()> handler) override
		{
			m_ios.post(handler);
		}

		void async_set_file_priority(storage_index_t
			, aux::vector<download_priority_t, file_index_t>
			, std::function<void(storage_error const&)> handler) override
		{
			m_ios.post([=]{
				handler(storage_error(error_code(boost::system::errc::operation_not_supported, system_category())));
			});
		}

		void async_clear_piece(storage_index_t, piece_index_t index
			, std::function<void(piece_index_t)> handler) override
		{
			m_ios.post([=]{ handler(index); });
		}

		// implements buffer_allocator_interface
		void free_disk_buffer(char* b, aux::block_cache_reference const&) override
		{ m_buffer_pool.free_buffer(b); }

		void update_stats_counters(counters&) const override {}

		std::vector<open_file_state> get_status(storage_index_t) const override
		{ return {}; }

		void submit_jobs() override {}

	private:

		aux::vector<std::shared_ptr<posix_storage>, storage_index_t> m_torrents;

		// slots that are unused in the m_torrents vector
		std::vector<storage_index_t> m_free_slots;

		aux::session_settings m_settings;

		// disk cache
		disk_buffer_pool m_buffer_pool;

		counters& m_stats_counters;

		// callbacks are posted on this
		io_service& m_ios;
	};

	TORRENT_EXPORT std::unique_ptr<disk_interface> posix_disk_io_constructor(
		io_service& ios, counters& cnt)
	{
		return std::unique_ptr<disk_interface>(new posix_disk_io(ios, cnt));
	}
}

