/*

Copyright (c) 2016, 2019-2020, Arvid Norberg
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
#include "libtorrent/xnvme_disk_io.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/aux_/disk_buffer_pool.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/aux_/path.hpp"
#include "libtorrent/aux_/numeric_cast.hpp"
#include "libtorrent/aux_/xnvme_storage.hpp"
#include "libtorrent/stat_cache.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/aux_/merkle.hpp"

#include <vector>
#include <thread>
#include <condition_variable>
#include <chrono>
using namespace std::chrono_literals;

namespace libtorrent {

namespace {

	storage_index_t pop(std::vector<storage_index_t>& q)
	{
		TORRENT_ASSERT(!q.empty());
		storage_index_t const ret = q.back();
		q.pop_back();
		return ret;
	}

	using aux::xnvme_storage;

} // anonymous namespace

	struct TORRENT_EXTRA_EXPORT xnvme_disk_io final
		: disk_interface
		, buffer_allocator_interface
	{
		xnvme_disk_io(io_context& ios, settings_interface const& sett, counters& cnt)
			: m_settings(sett)
			, m_buffer_pool(ios)
			, m_stats_counters(cnt)
			, m_ios(ios)
			, m_reap_ios(true)
		{
			m_xnvme_backend = sett.get_str(settings_pack::xnvme_backend);
			settings_updated();


			m_io_reaper = std::thread([this]
			{
				while (m_reap_ios.load()) {
					auto ulock = std::unique_lock<std::mutex>(m_torrents_mutex);
					m_io_reaper_cond.wait_for(ulock, 250ms, [](){return true;});
					ulock.unlock();

					for (auto &torrent : m_torrents) {
						auto storage = torrent.get();
						if (storage != NULL) {
							storage->reap_ios();
						}
					}
				}

			});
		}

		void settings_updated() override
		{
			m_buffer_pool.set_settings(m_settings);
		}

		storage_holder new_torrent(storage_params const& params
			, std::shared_ptr<void> const&) override
		{
			auto ulock = std::unique_lock<std::mutex>(m_torrents_mutex);

			// make sure we can remove this torrent without causing a memory
			// allocation, by causing the allocation now instead
			m_free_slots.reserve(m_torrents.size() + 1);
			storage_index_t const idx = m_free_slots.empty()
				? m_torrents.end_index()
				: pop(m_free_slots);
			auto storage = std::make_unique<xnvme_storage>(params, m_xnvme_backend);
			if (idx == m_torrents.end_index()) m_torrents.emplace_back(std::move(storage));
			else m_torrents[idx] = std::move(storage);
			return storage_holder(idx, *this);
		}

		void remove_torrent(storage_index_t const idx) override
		{
			m_torrents[idx].reset();
			m_free_slots.push_back(idx);
		}

		void abort(bool) override {
			m_reap_ios.exchange(false);

			m_io_reaper_cond.notify_all();
			if (m_io_reaper.joinable()) {
				m_io_reaper.join();
			}
		}


		void async_read(storage_index_t storage, peer_request const& r
			, std::function<void(disk_buffer_holder block, storage_error const& se)> handler
			, disk_job_flags_t) override
		{
			time_point const start_time = clock_type::now();

			char *bufz = m_buffer_pool.allocate_buffer("send buffer");

			iovec_t buf = {bufz, r.length};

			auto whandler = [handler = std::move(handler), this, bufz, start_time](storage_error error, uint64_t bytes_read) mutable {
				post(m_ios, [=, h = std::move(handler)]() {
					disk_buffer_holder buffer = disk_buffer_holder(*this, bufz, default_block_size);
					h(std::move(buffer), error);

					if (!error.ec)
					{
						std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

						m_stats_counters.inc_stats_counter(counters::num_read_back);
						m_stats_counters.inc_stats_counter(counters::num_blocks_read);
						m_stats_counters.inc_stats_counter(counters::num_read_ops);
						m_stats_counters.inc_stats_counter(counters::disk_read_time, read_time);
						m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
					}
				});
			};

			int res = m_torrents[storage]->readv2(m_settings, buf, r.piece, r.start, std::move(whandler));
			TORRENT_ASSERT(res >= 0);
		}

		bool async_write(storage_index_t storage, peer_request const& r
			, char const* buf, std::shared_ptr<disk_observer>
			, std::function<void(storage_error const&)> handler
			, disk_job_flags_t) override
		{
			// TODO: 3 this const_cast can be removed once iovec_t is no longer a
			// thing, but we just use plain spans
			iovec_t const b = {const_cast<char *>(buf), r.length};

			time_point const start_time = clock_type::now();

			auto whandler = [=, handler = std::move(handler)](storage_error error, uint64_t bytes_written) mutable {
				post(m_ios, [=, h = std::move(handler)]() {
					h(error);

					if (!error.ec) {
						std::int64_t const write_time = total_microseconds(clock_type::now() - start_time);

						m_stats_counters.inc_stats_counter(counters::num_blocks_written);
						m_stats_counters.inc_stats_counter(counters::num_write_ops);
						m_stats_counters.inc_stats_counter(counters::disk_write_time, write_time);
						m_stats_counters.inc_stats_counter(counters::disk_job_time, write_time);
					}
				});
			};

			int res = m_torrents[storage]->writev(m_settings, b, r.piece, r.start, std::move(whandler));
			TORRENT_ASSERT(res >= 0);

			return false;
		}

		void async_hash(storage_index_t storage, piece_index_t const piece
			, span<sha256_hash> block_hashes, disk_job_flags_t flags
			, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler) override
		{
			time_point const start_time = clock_type::now();

			bool const v1 = bool(flags & disk_interface::v1_hash);
			bool const v2 = !block_hashes.empty();


			xnvme_storage* st = m_torrents[storage].get();

			int const piece_size = v1 ? st->files().piece_size(piece) : 0;
			int const piece_size2 = v2 ? st->orig_files().piece_size2(piece) : 0;
			int const blocks_in_piece = v1 ? (piece_size + default_block_size - 1) / default_block_size : 0;
			int const blocks_in_piece2 = v2 ? st->orig_files().blocks_in_piece2(piece) : 0;

			TORRENT_ASSERT(!v2 || int(block_hashes.size()) >= blocks_in_piece2);

			int offset = 0;
			int const blocks_to_read = std::max(blocks_in_piece, blocks_in_piece2);

			struct completed_io {
				char *buf;
				int size;
			};

			struct cb_arg {
				cb_arg(completed_io *completed_ios_)
				: completed_ios(completed_ios_)
				, ncompleted_ios(0) {}

				completed_io *completed_ios;
				int ncompleted_ios;
			};

			cb_arg *cb_args = new cb_arg(new completed_io[blocks_to_read]);

			// TODO: cb_handler is never free()d
			// TODO: cb_handler isn't exception- and memory safe
			// TODO: how can we do this without allocating cb_handler on the heap?
			//       Problem: we want to std::move() `handler` into the callback in order
			//       to post it once all callbacks have completed. But we can't move()
			//       it more than once... And the same goes for cb_handler.
			//
			auto cb_handler = new std::function<void(storage_error error, char *buffer, int index, int len, int len2, uint64_t bytes_read)>([handler = std::move(handler), blocks_to_read, cb_args, this, v1, v2, blocks_in_piece2, block_hashes, piece, start_time](storage_error error, char *buffer, int io_index, int len, int len2, uint64_t bytes_read) mutable {
				cb_args->ncompleted_ios++;

				if (io_index >= blocks_to_read) {
					fprintf(stderr, "FAILED: async_hash callback: index >= blocks_to_read, %d >= %d\n", io_index, blocks_to_read);
					exit(42);
				}
				cb_args->completed_ios[io_index].buf = buffer;
				cb_args->completed_ios[io_index].size = bytes_read;

				if (cb_args->ncompleted_ios < blocks_to_read) {
					return;
				}

				hasher ph;
				for (int i = 0; i < blocks_to_read; i++) {
					bool v2_block = i < blocks_in_piece2;

					auto io = cb_args->completed_ios[i];
					if (io.size <= 0) {
						break;
					}
					if (v1) {
						ph.update(io.buf, std::min(len, io.size));
					}
					if (v2_block) {
						block_hashes[i] = hasher256(io.buf, std::min(len2, io.size)).final();
					}
					free_disk_buffer(cb_args->completed_ios[i].buf);
				}

				sha1_hash const hash = v1 ? ph.final() : sha1_hash();

				free(cb_args->completed_ios);
				free(cb_args);
				if (!error.ec)
				{
					std::int64_t const read_time = total_microseconds(clock_type::now() - start_time);

					m_stats_counters.inc_stats_counter(counters::num_read_back);
					m_stats_counters.inc_stats_counter(counters::num_blocks_read, blocks_to_read);
					m_stats_counters.inc_stats_counter(counters::num_read_ops);
					m_stats_counters.inc_stats_counter(counters::disk_hash_time, read_time);
					m_stats_counters.inc_stats_counter(counters::disk_job_time, read_time);
				}

				post(m_ios, [=, h = std::move(handler)]{ h(piece, hash, error); });
			});

			for (int i = 0; i < blocks_to_read; ++i)
			{
				auto buffer = m_buffer_pool.allocate_buffer("hash buffer");
				storage_error error;
				if (!buffer)
				{
					fprintf(stderr, "XNVME ERROR: failed to allocate buffer!\n");
					error.ec = errors::no_memory;
					error.operation = operation_t::alloc_cache_piece;
					post(m_ios, [=, h = std::move(handler)]{ h(piece, sha1_hash{}, error); });
					return;
				}

				bool const v2_block = i < blocks_in_piece2;

				auto const len = v1 ? std::min(default_block_size, piece_size - offset) : 0;
				auto const len2 = v2_block ? std::min(default_block_size, piece_size2 - offset) : 0;

				iovec_t b = {buffer, std::max(len, len2)};
				int res = st->readv2(m_settings, b, piece, offset, [len, len2, i, buffer, cb_handler](storage_error err, uint64_t bytes_read) mutable {
					(*cb_handler)(err, buffer, i, len, len2, bytes_read);
				});
				if (res < 0) {
					fprintf(stderr, "FAILED: async_hash piece: %d, res: %d\n", piece, res);
					break;
				}
				offset += default_block_size;
			}
		}

		void async_hash2(storage_index_t storage, piece_index_t const piece, int offset, disk_job_flags_t
			, std::function<void(piece_index_t, sha256_hash const&, storage_error const&)> handler) override
		{
			time_point const start_time = clock_type::now();

			disk_buffer_holder buffer = disk_buffer_holder(*this, m_buffer_pool.allocate_buffer("hash buffer"), 0x4000);
			storage_error error;
			if (!buffer)
			{
				error.ec = errors::no_memory;
				error.operation = operation_t::alloc_cache_piece;
				post(m_ios, [=, h = std::move(handler)]{ h(piece, sha256_hash{}, error); });
				return;
			}

			xnvme_storage* st = m_torrents[storage].get();

			int const piece_size = st->files().piece_size2(piece);

			std::ptrdiff_t const len = std::min(default_block_size, piece_size - offset);

			hasher256 ph;
			iovec_t b = {buffer.data(), len};
			int const ret = st->readv(m_settings, b, piece, offset, error);
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
			xnvme_storage* st = m_torrents[storage].get();
			storage_error ec;
			status_t ret;
			std::tie(ret, p) = st->move_storage(p, flags, ec);
			post(m_ios, [=, h = std::move(handler)]{ h(ret, p, ec); });
		}

		void async_release_files(storage_index_t storage, std::function<void()> handler) override
		{
			xnvme_storage* st = m_torrents[storage].get();
			st->release_files();
			if (!handler) return;
			post(m_ios, [=]{ handler(); });
		}

		void async_delete_files(storage_index_t storage, remove_flags_t const options
			, std::function<void(storage_error const&)> handler) override
		{
			storage_error error;
			xnvme_storage* st = m_torrents[storage].get();
			st->delete_files(options, error);
			post(m_ios, [=, h = std::move(handler)]{ h(error); });
		}

		void async_check_files(storage_index_t storage
			, add_torrent_params const* resume_data
			, aux::vector<std::string, file_index_t> links
			, std::function<void(status_t, storage_error const&)> handler) override
		{
			xnvme_storage* st = m_torrents[storage].get();

			add_torrent_params tmp;
			add_torrent_params const* rd = resume_data ? resume_data : &tmp;

			storage_error error;
			status_t const ret = [&]
			{
				st->initialize(m_settings, error);
				if (error) return status_t::fatal_disk_error;

				bool const verify_success = st->verify_resume_data(*rd
					, std::move(links), error);

				if (m_settings.get_bool(settings_pack::no_recheck_incomplete_resume))
					return status_t::no_error;

				if (!aux::contains_resume_data(*rd))
				{
					// if we don't have any resume data, we still may need to trigger a
					// full re-check, if there are *any* files.
					storage_error ignore;
					return (st->has_any_file(ignore))
						? status_t::need_full_check
						: status_t::no_error;
				}

				return verify_success
					? status_t::no_error
					: status_t::need_full_check;
			}();

			post(m_ios, [error, ret, h = std::move(handler)]{ h(ret, error); });
		}

		void async_rename_file(storage_index_t const storage
			, file_index_t const idx
			, std::string name
			, std::function<void(std::string const&, file_index_t, storage_error const&)> handler) override
		{
			xnvme_storage* st = m_torrents[storage].get();
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
			xnvme_storage* st = m_torrents[storage].get();
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

		// implements buffer_allocator_interface
		void free_disk_buffer(char* b) override
		{
			m_buffer_pool.free_buffer(b);
		}

		void update_stats_counters(counters&) const override {}

		std::vector<open_file_state> get_status(storage_index_t) const override
		{ return {}; }

		void submit_jobs() override {
			m_io_reaper_cond.notify_all();
		}

	private:

		std::mutex m_torrents_mutex;
		aux::vector<std::unique_ptr<xnvme_storage>, storage_index_t> m_torrents;

		// slots that are unused in the m_torrents vector
		std::vector<storage_index_t> m_free_slots;

		settings_interface const& m_settings;

		// disk cache
		aux::disk_buffer_pool m_buffer_pool;

		counters& m_stats_counters;

		// callbacks are posted on this
		io_context& m_ios;

		// xNVMe backend to use when initializing xnvme_storage structs
		std::string m_xnvme_backend;

		std::atomic_bool m_reap_ios;
		std::thread m_io_reaper;
		std::condition_variable m_io_reaper_cond;
		// std::unique_lock<std::mutex> m_io_reaper_mutex;
	};

	TORRENT_EXPORT std::unique_ptr<disk_interface> xnvme_disk_io_constructor(
		io_context& ios, settings_interface const& sett, counters& cnt)
	{
		return std::make_unique<xnvme_disk_io>(ios, sett, cnt);
	}
}

