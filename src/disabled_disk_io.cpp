/*

Copyright (c) 2017, Arvid Norberg, Steven Siloti
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
#include "libtorrent/disabled_disk_io.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/disk_buffer_pool.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/debug.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/peer_request.hpp"

#include <vector>
#include <functional>

namespace libtorrent {

// This is a dummy implementation of the disk_interface. It discards any data
// written to it and when reading, it returns zeroes. It is primarily useful for
// testing and benchmarking
struct TORRENT_EXTRA_EXPORT disabled_disk_io final
	: disk_interface
	, buffer_allocator_interface
{
	disabled_disk_io(io_context& ios, counters&)
		: m_zero_buffer(std::make_unique<char[]>(default_block_size))
		, m_ios(ios)
	{
		std::memset(m_zero_buffer.get(), 0, default_block_size);
	}

	storage_holder new_torrent(storage_params
		, std::shared_ptr<void> const&) override
	{
		return storage_holder(storage_index_t(0), *this);
	}

	void remove_torrent(storage_index_t) override {}

	void abort(bool) override {}

	void set_settings(settings_pack const*) override {}

	void async_read(storage_index_t, peer_request const& r
		, std::function<void(disk_buffer_holder, storage_error const&)> handler
		, disk_job_flags_t) override
	{
		TORRENT_ASSERT(r.length <= default_block_size);
		TORRENT_UNUSED(r);

		post(m_ios, [this, h = std::move(handler)] {
			h(disk_buffer_holder(*this, this->m_zero_buffer.get(), default_block_size)
				, storage_error{});
		});
	}

	bool async_write(storage_index_t
		, peer_request const& r
		, char const*, std::shared_ptr<disk_observer>
		, std::function<void(storage_error const&)> handler
		, disk_job_flags_t) override
	{
		TORRENT_ASSERT(r.length <= default_block_size);
		TORRENT_UNUSED(r);

		post(m_ios, [h = std::move(handler)] { h(storage_error{}); });
		return false;
	}

	void async_hash(storage_index_t
		, piece_index_t piece, disk_job_flags_t
		, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler) override
	{
		// TODO: it would be nice to return a valid hash of zeroes here
		post(m_ios, [h = std::move(handler), piece] { h(piece, sha1_hash{}, storage_error{}); });
	}

	void async_move_storage(storage_index_t
		, std::string p, move_flags_t
		, std::function<void(status_t, std::string const&, storage_error const&)> handler) override
	{
		post(m_ios, [h = std::move(handler), path = std::move(p)] () mutable
			{ h(status_t::no_error, std::move(path), storage_error{}); });
	}

	void async_release_files(storage_index_t, std::function<void()> handler) override
	{
		post(m_ios, [h = std::move(handler)] { h(); });
	}

	void async_delete_files(storage_index_t
		, remove_flags_t, std::function<void(storage_error const&)> handler) override
	{
		post(m_ios, [h = std::move(handler)] { h(storage_error{}); });
	}

	void async_check_files(storage_index_t
		, add_torrent_params const*
		, aux::vector<std::string, file_index_t>
		, std::function<void(status_t, storage_error const&)> handler) override
	{
		post(m_ios, [h = std::move(handler)] { h(status_t::no_error, storage_error{}); });
	}

	void async_rename_file(storage_index_t
		, file_index_t index, std::string name
		, std::function<void(std::string const&, file_index_t, storage_error const&)> handler) override
	{
		post(m_ios, [h = std::move(handler), index, n = std::move(name)]
			{ h(std::move(n), index, storage_error{}); });
	}

	void async_stop_torrent(storage_index_t, std::function<void()> handler) override
	{
		post(m_ios, [h = std::move(handler)] { h(); });
	}

	void async_set_file_priority(storage_index_t
		, aux::vector<download_priority_t, file_index_t> prio
		, std::function<void(storage_error const&
			, aux::vector<download_priority_t, file_index_t>)> handler) override
	{
		post(m_ios, [h = std::move(handler), p = std::move(prio)] () mutable
			{ h(storage_error{}, std::move(p)); });
	}

	void async_clear_piece(storage_index_t
		, piece_index_t const index, std::function<void(piece_index_t)> handler) override
	{
		post(m_ios, [h = std::move(handler), index] { h(index); });
	}

	void update_stats_counters(counters& c) const override
	{
		c.set_value(counters::disk_blocks_in_use, 1);
	}

	// implements buffer_allocator_interface
	// since we just have a single zeroed buffer, we don't need to free anything
	// here. The buffer is owned by the disabled_disk_io object itself
	void free_disk_buffer(char*) override {}

	std::vector<open_file_state> get_status(storage_index_t) const override
	{ return {}; }

	// this submits all queued up jobs to the thread
	void submit_jobs() override {}

private:

	// this is the one buffer of zeroes we hand back to all read jobs
	std::unique_ptr<char[]> m_zero_buffer;

	// this is the main thread io_context. Callbacks are
	// posted on this in order to have them execute in
	// the main thread.
	io_context& m_ios;
};

std::unique_ptr<disk_interface> disabled_disk_io_constructor(
	io_context& ios, counters& cnt)
{
	return std::make_unique<disabled_disk_io>(ios, cnt);
}

}

