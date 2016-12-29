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

#include <functional>

namespace libtorrent {

	std::unique_ptr<disk_interface> disabled_disk_io_constructor(
		io_service& ios, counters& cnt)
	{
		return std::unique_ptr<disk_interface>(new disabled_disk_io(ios, cnt));
	}

	disabled_disk_io::disabled_disk_io(io_service& ios, counters&)
		: m_buffer_pool(ios)
		, m_ios(ios)
	{
		m_buffer_pool.set_settings(m_settings);
	}

	storage_holder disabled_disk_io::new_torrent(storage_params
		, std::shared_ptr<void> const&)
	{
		return storage_holder(storage_index_t(0), *this);
	}

	void disabled_disk_io::remove_torrent(storage_index_t)
	{
	}

	void disabled_disk_io::set_settings(settings_pack const* pack)
	{
		apply_pack(pack, m_settings);
		m_buffer_pool.set_settings(m_settings);
	}

	void disabled_disk_io::async_read(storage_index_t, peer_request const& r
		, std::function<void(disk_buffer_holder, storage_error const&)> handler
		, disk_job_flags_t)
	{
		TORRENT_ASSERT(r.length <= default_block_size);
		TORRENT_UNUSED(r);

		m_ios.post([=]()
		{
			disk_buffer_holder holder(*this, this->m_buffer_pool.allocate_buffer("send buffer"), default_block_size);
			std::fill(holder.get(), holder.get() + default_block_size, 0);
			handler(std::move(holder), storage_error{});
		});
	}

	bool disabled_disk_io::async_write(storage_index_t
		, peer_request const& r
		, char const*, std::shared_ptr<disk_observer>
		, std::function<void(storage_error const&)> handler
		, disk_job_flags_t)
	{
		TORRENT_ASSERT(r.length <= default_block_size);
		TORRENT_UNUSED(r);

		m_ios.post([=]() { handler(storage_error{}); });
		return false;
	}

	void disabled_disk_io::async_hash(storage_index_t
		, piece_index_t piece, disk_job_flags_t
		, std::function<void(piece_index_t, sha1_hash const&, storage_error const&)> handler)
	{
		m_ios.post([=]() { handler(piece, sha1_hash{}, storage_error{}); });
	}

	void disabled_disk_io::async_move_storage(storage_index_t
		, std::string p, move_flags_t
		, std::function<void(status_t, std::string const&, storage_error const&)> handler)
	{
		m_ios.post([=]() { handler(status_t::no_error, p, storage_error{}); });
	}

	void disabled_disk_io::async_release_files(storage_index_t
		, std::function<void()> handler)
	{
		m_ios.post([=]() { handler(); });
	}

	void disabled_disk_io::async_delete_files(storage_index_t
		, remove_flags_t, std::function<void(storage_error const&)> handler)
	{
		m_ios.post([=]() { handler(storage_error{}); });
	}

	void disabled_disk_io::async_check_files(storage_index_t
		, add_torrent_params const*
		, aux::vector<std::string, file_index_t>&
		, std::function<void(status_t, storage_error const&)> handler)
	{
		m_ios.post([=]() { handler(status_t::no_error, storage_error{}); });
	}

	void disabled_disk_io::async_rename_file(storage_index_t
		, file_index_t index, std::string name
		, std::function<void(std::string const&, file_index_t, storage_error const&)> handler)
	{
		m_ios.post([=]() { handler(name, index, storage_error{}); });
	}

	void disabled_disk_io::async_stop_torrent(storage_index_t
		, std::function<void()> handler)
	{
		m_ios.post([=]() { handler(); });
	}

	void disabled_disk_io::async_set_file_priority(storage_index_t
		, aux::vector<download_priority_t, file_index_t>
		, std::function<void(storage_error const&)> handler)
	{
		m_ios.post([=]() { handler(storage_error{}); });
	}

	void disabled_disk_io::async_clear_piece(storage_index_t
		, piece_index_t const index, std::function<void(piece_index_t)> handler)
	{
		m_ios.post([=]() { handler(index); });
	}

	void disabled_disk_io::update_stats_counters(counters& c) const
	{
		// gauges
		c.set_value(counters::disk_blocks_in_use, m_buffer_pool.in_use());
	}
}

