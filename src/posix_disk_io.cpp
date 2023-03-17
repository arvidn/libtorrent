/*

Copyright (c) 2016, 2019-2022, Arvid Norberg
Copyright (c) 2017-2018, Steven Siloti
Copyright (c) 2020, Alden Torres
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
#include "libtorrent/aux_/disk_buffer_pool.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/posix_storage.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/aux_/storage_free_list.hpp"

#include <vector>

namespace libtorrent {

namespace {

	using aux::posix_storage;

} // anonymous namespace

	struct TORRENT_EXTRA_EXPORT posix_disk_io final
		: disk_interface
	{
		posix_disk_io(io_context& ios, settings_interface const& sett, counters& cnt)
			: m_settings(sett)
			, m_buffer_pool(ios)
			, m_stats_counters(cnt)
			, m_ios(ios)
		{
			settings_updated();
		}

		void settings_updated() override
		{
			m_buffer_pool.set_settings(m_settings);
		}

		storage_holder new_torrent(storage_params const& params
			, std::shared_ptr<void> const&) override
		{
			// make sure we can remove this torrent without causing a memory
			// allocation, by causing the allocation now instead
			storage_index_t const idx = m_free_slots.new_index(m_torrents.end_index());
			auto storage = std::make_unique<posix_storage>(params);
			if (idx == m_torrents.end_index()) m_torrents.emplace_back(std::move(storage));
			else m_torrents[idx] = std::move(storage);
			return storage_holder(idx, *this);
		}

		void remove_torrent(storage_index_t const idx) override
		{
			m_torrents[idx].reset();
			m_free_slots.add(idx);
		}

		void abort(bool) override {}

		void async_read(storage_index_t storage, peer_request const& r
			, std::function<void(disk_buffer_holder block, storage_error const& se)> handler
			, disk_job_flags_t) override
		{
			disk_buffer_holder buffer = disk_buffer_holder(m_buffer_pool, m_buffer_pool.allocate_buffer("send buffer"), default_block_size);
			storage_error error;
			if (!buffer)
			{
				error.ec = errors::no_memory;
				error.operation = operation_t::alloc_cache_piece;
				post(m_ios, [this, error, h = std::move(handler)]{ h(disk_buffer_holder(m_buffer_pool, nullptr, 0), error); });
				return;
			}

			time_point const start_time = clock_type::now();

			span<char> const buf = {buffer.data(), r.length};

			m_torrents[storage]->read(m_settings, buf, r.piece, r.start, error);

			if (!error.ec)
			{
				std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

				m_stats_counters.inc_stats_counter(counters::num_blocks_read);
				m_stats_counters.inc_stats_counter(counters::num_read_ops);
				m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
			}

			post(m_ios, [h = std::move(handler), b = std::move(buffer), error] () mutable
				{ h(std::move(b), error); });
		}

		bool async_write(storage_index_t storage, peer_request const& r
			, char const* buf, std::shared_ptr<disk_observer>
			, std::function<void(storage_error const&)> handler
			, disk_job_flags_t) override
		{
			span<char> const b = { const_cast<char*>(buf), r.length };

			time_point const start_time = clock_type::now();

			storage_error error;
			m_torrents[storage]->write(m_settings, b, r.piece, r.start, error);

			if (!error.ec)
			{
				std::int64_t const write_time = total_microseconds(clock_type::now() - start_time);

				m_stats_counters.inc_stats_counter(counters::num_blocks_written);
				m_stats_counters.inc_stats_counter(counters::num_write_ops);
				m_stats_counters.inc_stats_counter(counters::disk_write_time, write_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, write_time);
			}

			post(m_ios, [=, h = std::move(handler)]{ h(error); });
			return false;
		}

		void async_hash(storage_index_t storage, piece_index_t const piece
			, span<sha256_hash> block_hashes, disk_job_flags_t flags
			, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler) override
		{
			time_point const start_time = clock_type::now();

			bool const v1 = bool(flags & disk_interface::v1_hash);
			bool const v2 = !block_hashes.empty();

			disk_buffer_holder buffer = disk_buffer_holder(m_buffer_pool, m_buffer_pool.allocate_buffer("hash buffer"), default_block_size);
			storage_error error;
			if (!buffer)
			{
				error.ec = errors::no_memory;
				error.operation = operation_t::alloc_cache_piece;
				post(m_ios, [=, h = std::move(handler)]{ h(piece, sha1_hash{}, error); });
				return;
			}
			hasher ph;

			posix_storage* st = m_torrents[storage].get();

			int const piece_size = v1 ? st->files().piece_size(piece) : 0;
			int const piece_size2 = v2 ? st->files().piece_size2(piece) : 0;
			int const blocks_in_piece = v1 ? (piece_size + default_block_size - 1) / default_block_size : 0;
			int const blocks_in_piece2 = v2 ? st->files().blocks_in_piece2(piece) : 0;

			TORRENT_ASSERT(!v2 || int(block_hashes.size()) >= blocks_in_piece2);

			int offset = 0;
			int const blocks_to_read = std::max(blocks_in_piece, blocks_in_piece2);
			for (int i = 0; i < blocks_to_read; ++i)
			{
				bool const v2_block = i < blocks_in_piece2;

				auto const len = v1 ? std::min(default_block_size, piece_size - offset) : 0;
				auto const len2 = v2_block ? std::min(default_block_size, piece_size2 - offset) : 0;

				span<char> const b = {buffer.data(), std::max(len, len2)};
				int const ret = st->read(m_settings, b, piece, offset, error);
				offset += default_block_size;
				if (ret <= 0) break;
				if (v1)
					ph.update(b.first(std::min(ret, len)));
				if (v2_block)
					block_hashes[i] = hasher256(b.first(std::min(ret, len2))).final();
			}

			sha1_hash const hash = v1 ? ph.final() : sha1_hash();

			if (!error.ec)
			{
				std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

				m_stats_counters.inc_stats_counter(counters::num_read_back, blocks_to_read);
				m_stats_counters.inc_stats_counter(counters::num_blocks_read, blocks_to_read);
				m_stats_counters.inc_stats_counter(counters::num_read_ops, blocks_to_read);
				m_stats_counters.inc_stats_counter(counters::disk_hash_time, read_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
			}

			post(m_ios, [=, h = std::move(handler)]{ h(piece, hash, error); });
		}

		void async_hash2(storage_index_t storage, piece_index_t const piece, int offset, disk_job_flags_t
			, std::function<void(piece_index_t, sha256_hash const&, storage_error const&)> handler) override
		{
			time_point const start_time = clock_type::now();

			disk_buffer_holder buffer = disk_buffer_holder(m_buffer_pool, m_buffer_pool.allocate_buffer("hash buffer"), 0x4000);
			storage_error error;
			if (!buffer)
			{
				error.ec = errors::no_memory;
				error.operation = operation_t::alloc_cache_piece;
				post(m_ios, [=, h = std::move(handler)]{ h(piece, sha256_hash{}, error); });
				return;
			}

			posix_storage* st = m_torrents[storage].get();

			int const piece_size = st->files().piece_size2(piece);

			std::ptrdiff_t const len = std::min(default_block_size, piece_size - offset);

			hasher256 ph;
			span<char> const b = {buffer.data(), len};
			int const ret = st->read(m_settings, b, piece, offset, error);
			if (ret > 0)
				ph.update(b.first(ret));

			sha256_hash const hash = ph.final();

			if (!error.ec)
			{
				std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

				m_stats_counters.inc_stats_counter(counters::num_read_back);
				m_stats_counters.inc_stats_counter(counters::num_blocks_read);
				m_stats_counters.inc_stats_counter(counters::num_read_ops);
				m_stats_counters.inc_stats_counter(counters::disk_hash_time, read_time);
				m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
			}

			post(m_ios, [=, h = std::move(handler)]{ h(piece, hash, error); });
		}


		void async_move_storage(storage_index_t const storage, std::string p
			, move_flags_t const flags
			, std::function<void(status_t, std::string const&, storage_error const&)> handler) override
		{
			posix_storage* st = m_torrents[storage].get();
			storage_error ec;
			status_t ret;
			std::tie(ret, p) = st->move_storage(p, flags, ec);
			post(m_ios, [=, h = std::move(handler)]{ h(ret, p, ec); });
		}

		void async_release_files(storage_index_t storage, std::function<void()> handler) override
		{
			posix_storage* st = m_torrents[storage].get();
			st->release_files();
			if (!handler) return;
			post(m_ios, [=]{ handler(); });
		}

		void async_delete_files(storage_index_t storage, remove_flags_t const options
			, std::function<void(storage_error const&)> handler) override
		{
			storage_error error;
			posix_storage* st = m_torrents[storage].get();
			st->delete_files(options, error);
			post(m_ios, [=, h = std::move(handler)]{ h(error); });
		}

		void async_check_files(storage_index_t storage
			, add_torrent_params const* resume_data
			, aux::vector<std::string, file_index_t> links
			, std::function<void(status_t, storage_error const&)> handler) override
		{
			posix_storage* st = m_torrents[storage].get();

			add_torrent_params tmp;
			add_torrent_params const* rd = resume_data ? resume_data : &tmp;

			storage_error error;
			status_t const ret = [&]
			{
				auto const ret_flag = st->initialize(m_settings, error);
				if (error) return status_t::fatal_disk_error | ret_flag;

				bool const verify_success = st->verify_resume_data(*rd
					, std::move(links), error);

				if (m_settings.get_bool(settings_pack::no_recheck_incomplete_resume))
					return status_t::no_error | ret_flag;

				if (!aux::contains_resume_data(*rd))
				{
					// if we don't have any resume data, we still may need to trigger a
					// full re-check, if there are *any* files.
					storage_error ignore;
					return ((st->has_any_file(ignore))
						? status_t::need_full_check
						: status_t::no_error)
						| ret_flag;
				}

				return (verify_success
					? status_t::no_error
					: status_t::need_full_check)
					| ret_flag;
			}();

			post(m_ios, [error, ret, h = std::move(handler)]{ h(ret, error); });
		}

		void async_rename_file(storage_index_t const storage
			, file_index_t const idx
			, std::string name
			, std::function<void(std::string const&, file_index_t, storage_error const&)> handler) override
		{
			posix_storage* st = m_torrents[storage].get();
			storage_error error;
			st->rename_file(idx, name, error);
			post(m_ios, [idx, error, h = std::move(handler), n = std::move(name)] () mutable
				{ h(std::move(n), idx, error); });
		}

		void async_stop_torrent(storage_index_t, std::function<void()> handler) override
		{
			if (!handler) return;
			post(m_ios, std::move(handler));
		}

		void async_set_file_priority(storage_index_t const storage
			, aux::vector<download_priority_t, file_index_t> prio
			, std::function<void(storage_error const&
				, aux::vector<download_priority_t, file_index_t>)> handler) override
		{
			posix_storage* st = m_torrents[storage].get();
			storage_error error;
			st->set_file_priority(prio, error);
			post(m_ios, [p = std::move(prio), h = std::move(handler), error] () mutable
				{ h(error, std::move(p)); });
		}

		void async_clear_piece(storage_index_t, piece_index_t index
			, std::function<void(piece_index_t)> handler) override
		{
			post(m_ios, [=, h = std::move(handler)]{ h(index); });
		}

		void update_stats_counters(counters&) const override {}

		std::vector<open_file_state> get_status(storage_index_t) const override
		{ return {}; }

		void submit_jobs() override {}

	private:

		aux::vector<std::unique_ptr<posix_storage>, storage_index_t> m_torrents;

		// slots that are unused in the m_torrents vector
		aux::storage_free_list m_free_slots;

		settings_interface const& m_settings;

		// disk cache
		aux::disk_buffer_pool m_buffer_pool;

		counters& m_stats_counters;

		// callbacks are posted on this
		io_context& m_ios;
	};

	TORRENT_EXPORT std::unique_ptr<disk_interface> posix_disk_io_constructor(
		io_context& ios, settings_interface const& sett, counters& cnt)
	{
		return std::make_unique<posix_disk_io>(ios, sett, cnt);
	}
}

