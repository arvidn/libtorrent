/*

Copyright (c) 2017-2018, Steven Siloti
Copyright (c) 2019, Arvid Norberg
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

#include <cstdlib>
#include "libtorrent/session.hpp"
#include "libtorrent/session_params.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/disk_interface.hpp"
#include "libtorrent/aux_/storage_utils.hpp" // for iovec_t
#include "libtorrent/fwd.hpp"

#include <iostream>

// -- example begin
struct temp_storage
{
	explicit temp_storage(lt::file_storage const& fs) : m_files(fs) {}

	lt::span<char const> readv(lt::piece_index_t const piece, int const offset, lt::storage_error& ec) const
	{
		auto const i = m_file_data.find(piece);
		if (i == m_file_data.end())
		{
			ec.operation = lt::operation_t::file_read;
			ec.ec = boost::asio::error::eof;
			return {};
		};
		if (int(i->second.size()) <= offset)
		{
			ec.operation = lt::operation_t::file_read;
			ec.ec = boost::asio::error::eof;
			return {};
		}
		return { i->second.data() + offset, int(i->second.size() - offset) };
	}
	void writev(lt::span<char const> const b, lt::piece_index_t const piece, int const offset)
	{
		auto& data = m_file_data[piece];
		if (data.empty())
		{
			// allocate the whole piece, otherwise we'll invalidate the pointers
			// we have returned back to libtorrent
			int const size = piece_size(piece);
			data.resize(size);
		}
		TORRENT_ASSERT(offset + b.size() <= int(data.size()));
		std::memcpy(data.data() + offset, b.data(), b.size());
	}
	lt::sha1_hash hash(lt::piece_index_t const piece
		, lt::span<lt::sha256_hash> const block_hashes, lt::storage_error& ec) const
	{
		auto const i = m_file_data.find(piece);
		if (i == m_file_data.end())
		{
			ec.operation = lt::operation_t::file_read;
			ec.ec = boost::asio::error::eof;
			return {};
		};
		if (!block_hashes.empty())
		{
			int const piece_size2 = m_files.piece_size2(piece);
			int const blocks_in_piece2 = m_files.blocks_in_piece2(piece);
			char const* buf = i->second.data();
			std::int64_t offset = 0;
			for (int k = 0; k < blocks_in_piece2; ++k)
			{
				lt::hasher256 h2;
				std::ptrdiff_t const len2 = std::min(lt::default_block_size, int(piece_size2 - offset));
				h2.update({ buf, len2 });
				buf += len2;
				offset += len2;
				block_hashes[k] = h2.final();
			}
		}
		return lt::hasher(i->second).final();
	}
	lt::sha256_hash hash2(lt::piece_index_t const piece, int const offset, lt::storage_error& ec)
	{
		auto const i = m_file_data.find(piece);
		if (i == m_file_data.end())
		{
			ec.operation = lt::operation_t::file_read;
			ec.ec = boost::asio::error::eof;
			return {};
		};

		int const piece_size = m_files.piece_size2(piece);

		std::ptrdiff_t const len = std::min(lt::default_block_size, piece_size - offset);

		lt::span<char const> b = {i->second.data() + offset, len};
		return lt::hasher256(b).final();
	}

private:
	int piece_size(lt::piece_index_t piece) const
	{
		int const num_pieces = static_cast<int>((m_files.total_size() + m_files.piece_length() - 1) / m_files.piece_length());
		return static_cast<int>(piece) < num_pieces - 1
			? m_files.piece_length() : static_cast<int>(m_files.total_size() - (num_pieces - 1) * m_files.piece_length());
	}

	lt::file_storage const& m_files;
	std::map<lt::piece_index_t, std::vector<char>> m_file_data;
};

namespace {
lt::storage_index_t pop(std::vector<lt::storage_index_t>& q)
{
	TORRENT_ASSERT(!q.empty());
	lt::storage_index_t const ret = q.back();
	q.pop_back();
	return ret;
}
}

struct temp_disk_io final : lt::disk_interface
	, lt::buffer_allocator_interface
{
	explicit temp_disk_io(lt::io_context& ioc): m_ioc(ioc) {}

	void settings_updated() override {}

	lt::storage_holder new_torrent(lt::storage_params params
		, std::shared_ptr<void> const&) override
	{
		lt::storage_index_t const idx = m_free_slots.empty()
			? m_torrents.end_index()
			: pop(m_free_slots);
			auto storage = std::make_unique<temp_storage>(params.files);
		if (idx == m_torrents.end_index()) m_torrents.emplace_back(std::move(storage));
		else m_torrents[idx] = std::move(storage);
		return lt::storage_holder(idx, *this);
	}

	void remove_torrent(lt::storage_index_t const idx) override
	{
		m_torrents[idx].reset();
		m_free_slots.push_back(idx);
	}

	void abort(bool) override {}

	void async_read(lt::storage_index_t storage, lt::peer_request const& r
		, std::function<void(lt::disk_buffer_holder block, lt::storage_error const& se)> handler
		, lt::disk_job_flags_t) override
	{
		// this buffer is owned by the storage. It will remain valid for as
		// long as the torrent remains in the session. We don't need any lifetime
		// management of it.
		lt::storage_error error;
		lt::span<char const> b = m_torrents[storage]->readv(r.piece, r.start, error);

		post(m_ioc, [handler, error, b, this]
			{ handler(lt::disk_buffer_holder(*this, const_cast<char*>(b.data()), int(b.size())), error); });
	}

	bool async_write(lt::storage_index_t storage, lt::peer_request const& r
		, char const* buf, std::shared_ptr<lt::disk_observer>
		, std::function<void(lt::storage_error const&)> handler
		, lt::disk_job_flags_t) override
	{
		lt::span<char const> const b = { buf, r.length };

		m_torrents[storage]->writev(b, r.piece, r.start);

		post(m_ioc, [=]{ handler(lt::storage_error()); });
		return false;
	}

	void async_hash(lt::storage_index_t storage, lt::piece_index_t const piece
		, lt::span<lt::sha256_hash> block_hashes, lt::disk_job_flags_t
		, std::function<void(lt::piece_index_t, lt::sha1_hash const&, lt::storage_error const&)> handler) override
	{
		lt::storage_error error;
		lt::sha1_hash const hash = m_torrents[storage]->hash(piece, block_hashes, error);
		post(m_ioc, [=]{ handler(piece, hash, error); });
	}

	void async_hash2(lt::storage_index_t storage, lt::piece_index_t const piece
		, int const offset, lt::disk_job_flags_t
		, std::function<void(lt::piece_index_t, lt::sha256_hash const&, lt::storage_error const&)> handler) override
	{
		lt::storage_error error;
		lt::sha256_hash const hash = m_torrents[storage]->hash2(piece, offset, error);
		post(m_ioc, [=]{ handler(piece, hash, error); });
	}

	void async_move_storage(lt::storage_index_t, std::string p, lt::move_flags_t
		, std::function<void(lt::status_t, std::string const&, lt::storage_error const&)> handler) override
	{
		post(m_ioc, [=]{
			handler(lt::status_t::fatal_disk_error, p
				, lt::storage_error(lt::error_code(boost::system::errc::operation_not_supported, lt::system_category())));
		});
	}

	void async_release_files(lt::storage_index_t, std::function<void()>) override {}

	void async_delete_files(lt::storage_index_t, lt::remove_flags_t
		, std::function<void(lt::storage_error const&)> handler) override
	{
		post(m_ioc, [=]{ handler(lt::storage_error()); });
	}

	void async_check_files(lt::storage_index_t
		, lt::add_torrent_params const*
		, lt::aux::vector<std::string, lt::file_index_t>
		, std::function<void(lt::status_t, lt::storage_error const&)> handler) override
	{
		post(m_ioc, [=]{ handler(lt::status_t::no_error, lt::storage_error()); });
	}

	void async_rename_file(lt::storage_index_t
		, lt::file_index_t const idx
		, std::string const name
		, std::function<void(std::string const&, lt::file_index_t, lt::storage_error const&)> handler) override
	{
		post(m_ioc, [=]{ handler(name, idx, lt::storage_error()); });
	}

	void async_stop_torrent(lt::storage_index_t, std::function<void()> handler) override
	{
		post(m_ioc, handler);
	}

	void async_set_file_priority(lt::storage_index_t
		, lt::aux::vector<lt::download_priority_t, lt::file_index_t> prio
		, std::function<void(lt::storage_error const&
			, lt::aux::vector<lt::download_priority_t, lt::file_index_t>)> handler) override
	{
		post(m_ioc, [=]{
			handler(lt::storage_error(lt::error_code(
				boost::system::errc::operation_not_supported, lt::system_category())), std::move(prio));
		});
	}

	void async_clear_piece(lt::storage_index_t, lt::piece_index_t index
		, std::function<void(lt::piece_index_t)> handler) override
	{
		post(m_ioc, [=]{ handler(index); });
	}

	// implements buffer_allocator_interface
	void free_disk_buffer(char*) override
	{
		// never free any buffer. We only return buffers owned by the storage
		// object
	}

	void update_stats_counters(lt::counters&) const override {}

	std::vector<lt::open_file_state> get_status(lt::storage_index_t) const override
	{ return {}; }

	void submit_jobs() override {}

private:

	lt::aux::vector<std::shared_ptr<temp_storage>, lt::storage_index_t> m_torrents;

	// slots that are unused in the m_torrents vector
	std::vector<lt::storage_index_t> m_free_slots;

	// callbacks are posted on this
	lt::io_context& m_ioc;
};

std::unique_ptr<lt::disk_interface> temp_disk_constructor(
	lt::io_context& ioc, lt::settings_interface const&, lt::counters&)
{
	return std::make_unique<temp_disk_io>(ioc);
}
// -- example end

int main(int argc, char* argv[]) try
{
	if (argc != 2) {
		std::cerr << "usage: ./custom_storage torrent-file\n"
			"to stop the client, press return.\n";
		return 1;
	}

	lt::session_params ses_params;
	ses_params.disk_io_constructor = temp_disk_constructor;
	lt::session s(ses_params);
	lt::add_torrent_params p;
	p.save_path = "./";
	p.ti = std::make_shared<lt::torrent_info>(argv[1]);
	s.add_torrent(p);

	// wait for the user to end
	char a;
	int ret = std::scanf("%c\n", &a);
	(void)ret; // ignore
	return 0;
}
catch (std::exception const& e) {
	std::cerr << "ERROR: " << e.what() << "\n";
}

