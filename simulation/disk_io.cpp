/*

Copyright (c) 2021, Arvid Norberg
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

#include "disk_io.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/disk_observer.hpp"

#include <utility> // for exchange()

namespace {

	// this is posted to the network thread
	void watermark_callback(std::vector<std::weak_ptr<lt::disk_observer>> const& cbs)
	{
		for (auto const& i : cbs)
		{
			std::shared_ptr<lt::disk_observer> o = i.lock();
			if (o) o->on_disk();
		}
	}

} // anonymous namespace

std::array<char, 4> generate_block_fill(lt::piece_index_t const p, int const block)
{
	int const v = (static_cast<int>(p) << 8) | (block & 0xff);
	std::array<char, 4> ret;
	std::memcpy(ret.data(), reinterpret_cast<char const*>(&v), 4);
	return ret;
}

lt::sha1_hash generate_hash1(lt::piece_index_t const p, lt::file_storage const& fs)
{
	lt::hasher ret;
	int const piece_size = fs.piece_size(p);
	int offset = 0;
	for (int block = 0; offset < piece_size; ++block)
	{
		auto const fill = generate_block_fill(p, block);
		for (int i = 0; i < lt::default_block_size; i += fill.size(), offset += fill.size())
			ret.update(fill.data(), std::min(int(fill.size()), piece_size - offset));
	}
	return ret.final();
}

lt::sha1_hash generate_hash2(lt::piece_index_t p, lt::file_storage const& fs
	, lt::span<lt::sha256_hash> const hashes)
{
	int const piece_size = fs.piece_size(p);
	int const piece_size2 = fs.piece_size2(p);
	int const blocks_in_piece = (piece_size + lt::default_block_size - 1) / lt::default_block_size;
	int const blocks_in_piece2 = fs.blocks_in_piece2(p);
	TORRENT_ASSERT(int(hashes.size()) >= blocks_in_piece2);
	int const blocks_to_read = std::max(blocks_in_piece, blocks_in_piece2);

	lt::hasher ret;
	int offset = 0;
	for (int block = 0; block < blocks_to_read; ++block)
	{
		lt::hasher256 v2_hash;
		auto const fill = generate_block_fill(p, block);

		bool const v2 = piece_size2 - offset > 0;

		int const block_size = std::min(lt::default_block_size, std::max(piece_size, piece_size2) - offset);
		for (int i = 0; i < block_size; i += fill.size(), offset += fill.size())
		{
			if (piece_size - offset > 0)
				ret.update(fill.data(), std::min(int(fill.size()), piece_size - offset));
			if (piece_size2 - offset > 0)
				v2_hash.update(fill.data(), std::min(int(fill.size()), piece_size2 - offset));
		}
		if (v2)
			hashes[block] = v2_hash.final();
		else
			hashes[block].clear();
	}
	return ret.final();
}

lt::sha256_hash generate_block_hash(lt::piece_index_t p, int const offset)
{
	lt::hasher256 ret;
	int const block = offset / lt::default_block_size;
	auto const fill = generate_block_fill(p, block);
	for (int i = 0; i < lt::default_block_size; i += fill.size())
		ret.update(fill);
	return ret.final();
}

void generate_block(char* b, lt::peer_request const& r)
{
	auto const fill = generate_block_fill(r.piece, (r.start / lt::default_block_size));
	for (int i = 0; i < lt::default_block_size; i += fill.size())
	{
		std::memcpy(b, fill.data(), fill.size());
		b += fill.size();
	}
}

std::shared_ptr<lt::torrent_info> create_test_torrent(int const piece_size
	, int const num_pieces, lt::create_flags_t const flags)
{
	lt::file_storage fs;
	int const total_size = piece_size * num_pieces;
	fs.add_file("file-1", total_size);
	lt::create_torrent t(fs, piece_size, flags);

	if (flags & lt::create_torrent::v1_only)
	{
		for (auto const i : fs.piece_range())
			t.set_hash(i, generate_hash1(i, fs));
	}
	else
	{
		int const blocks_in_piece = piece_size / lt::default_block_size;
		TORRENT_ASSERT(blocks_in_piece * lt::default_block_size == piece_size);

		int const num_leafs = lt::merkle_num_leafs(blocks_in_piece);
		lt::aux::vector<lt::sha256_hash> v2tree(lt::merkle_num_nodes(num_leafs));
		auto const blocks = lt::span<lt::sha256_hash>(v2tree).subspan(lt::merkle_first_leaf(num_leafs));

		for (auto const i : fs.piece_range())
		{
			auto const hash = generate_hash2(i, fs, blocks);
			lt::merkle_fill_tree(v2tree, num_leafs);
			t.set_hash2(lt::file_index_t{0}, i - 0_piece, v2tree[0]);

			if (!(flags & lt::create_torrent::v2_only))
				t.set_hash(i, hash);
		}
	}

	lt::entry tor = t.generate();

	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), tor);
	lt::error_code ec;
	return std::make_shared<lt::torrent_info>(tmp, ec, lt::from_span);
}

lt::add_torrent_params create_test_torrent(
	int const num_pieces, lt::create_flags_t const flags)
{
	lt::add_torrent_params params;
	params.ti = ::create_test_torrent(lt::default_block_size * 2, num_pieces, flags);
	// this is unused by the test disk I/O
	params.save_path = ".";
	return params;
}

// This is a disk subsystem used for tests (simulations specifically), it:
//
// * supports only a single torrent at a time (to keep it simple)
// * does not support arbitrary data, it generates the data read from it
//   according to a specific function. This keeps the memory footprint down even
//   for large tests
// * it can simulate delays in reading and writing
// * it can simulate disk full

struct test_disk_io final : lt::disk_interface
	, lt::buffer_allocator_interface
{
	explicit test_disk_io(lt::io_context& ioc, test_disk state)
		: m_timer(ioc)
		, m_state(state)
		, m_ioc(ioc)
	{}

	void settings_updated() override {}

	lt::storage_holder new_torrent(lt::storage_params const& params
		, std::shared_ptr<void> const&) override
	{
		TORRENT_ASSERT(m_files == nullptr);
		// This test disk I/O system only supports a single torrent
		// to keep it simple
		lt::file_storage const& fs = params.files;
		m_files = &fs;
		m_blocks_per_piece = fs.piece_length() / lt::default_block_size;
		m_have.resize(m_files->num_pieces() * m_blocks_per_piece, m_state.seed);
		return lt::storage_holder(lt::storage_index_t{0}, *this);
	}

	void remove_torrent(lt::storage_index_t const idx) override
	{
		TORRENT_ASSERT(static_cast<int>(idx) == 0);
		TORRENT_ASSERT(m_files != nullptr);

		queue_event(lt::microseconds(1), [this] () mutable {
			m_files = nullptr;
			m_blocks_per_piece = 0;
			m_have.clear();
		});
	}

	void abort(bool) override {}

	void async_read(lt::storage_index_t const storage, lt::peer_request const& r
		, std::function<void(lt::disk_buffer_holder block, lt::storage_error const& se)> h
		, lt::disk_job_flags_t) override
	{
		TORRENT_ASSERT(static_cast<int>(storage) == 0);
		TORRENT_ASSERT(m_files);

		// a real diskt I/O implementation would have to support this, but in
		// the simulations, we never send unaligned piece requests.
		TORRENT_ASSERT((r.start % lt::default_block_size) == 0);
		TORRENT_ASSERT((r.length <= lt::default_block_size));

		// this block should have been written
		TORRENT_ASSERT(m_have.get_bit(block_index(r)));

		auto const seek_time = disk_seek(std::int64_t(static_cast<int>(r.piece)) * m_files->piece_length() + r.start
			, lt::default_block_size);

		queue_event(seek_time + m_state.read_time, [this,r, h=std::move(h)] () mutable {
			lt::disk_buffer_holder buf(*this, new char[lt::default_block_size], r.length);
			generate_block(buf.data(), r);

			post(m_ioc, [h=std::move(h), b=std::move(buf)] () mutable { h(std::move(b), lt::storage_error{}); });
		});
	}

	bool async_write(lt::storage_index_t storage, lt::peer_request const& r
		, char const* buf, std::shared_ptr<lt::disk_observer> o
		, std::function<void(lt::storage_error const&)> handler
		, lt::disk_job_flags_t) override
	{
		TORRENT_ASSERT(m_files);

		if (m_state.space_left < lt::default_block_size)
		{
			queue_event(lt::milliseconds(1), [this,h=std::move(handler)] () mutable {
				post(m_ioc, [h=std::move(h)]
				{
					h(lt::storage_error(lt::error_code(boost::system::errc::no_space_on_device, lt::generic_category())
						, lt::operation_t::file_write));
				});
				if (m_state.recover_full_disk)
					m_state.space_left = std::numeric_limits<int>::max();
				});

			if (m_write_queue > m_state.high_watermark || m_exceeded_max_size)
			{
				m_observers.push_back(std::move(o));
				return true;
			}
			return false;
		}

		bool const valid = validate_block(buf, r);

		auto const seek_time = disk_seek(std::int64_t(static_cast<int>(r.piece)) * m_files->piece_length() + r.start
			, lt::default_block_size);

		queue_event(seek_time + m_state.write_time, [this,valid,r,h=std::move(handler)] () mutable {

			if (valid)
			{
				m_have.set_bit(block_index(r));
				m_state.space_left -= lt::default_block_size;
			}

			post(m_ioc, [h=std::move(h)]{ h(lt::storage_error()); });

			TORRENT_ASSERT(m_write_queue > 0);
			--m_write_queue;
			check_buffer_level();
		});

		m_write_queue += 1;
		if (m_write_queue > m_state.high_watermark || m_exceeded_max_size)
		{
			m_exceeded_max_size = true;
			m_observers.push_back(std::move(o));
			return true;
		}

		return false;
	}

	void async_hash(lt::storage_index_t storage, lt::piece_index_t const piece
		, lt::span<lt::sha256_hash> block_hashes, lt::disk_job_flags_t
		, std::function<void(lt::piece_index_t, lt::sha1_hash const&, lt::storage_error const&)> handler) override
	{
		TORRENT_ASSERT(m_files);

		auto const seek_time = disk_seek(std::int64_t(static_cast<int>(piece)) * m_files->piece_length()
			, m_blocks_per_piece * lt::default_block_size);

		auto const delay = seek_time
			+ m_state.read_time * m_blocks_per_piece
			+ m_state.hash_time * m_blocks_per_piece
			+ m_state.hash_time * block_hashes.size();

		queue_event(delay, [this, piece, block_hashes, h=std::move(handler)] () mutable {

			int const block_idx = piece * m_blocks_per_piece;
			for (int i = 0; i < m_blocks_per_piece; ++i)
			{
				if (!m_have.get_bit(block_idx + i))
				{
					// If we're missing a block, return an invalida hash
					post(m_ioc, [h=std::move(h), piece]{ h(piece, lt::sha1_hash{}, lt::storage_error{}); });
					return;
				}
			}

			lt::sha1_hash hash;
			if (block_hashes.empty())
				hash = generate_hash1(piece, *m_files);
			else
				hash = generate_hash2(piece, *m_files, block_hashes);
			post(m_ioc, [h=std::move(h), piece, hash]{ h(piece, hash, lt::storage_error{}); });
		});
	}

	void async_hash2(lt::storage_index_t storage, lt::piece_index_t const piece
		, int const offset, lt::disk_job_flags_t
		, std::function<void(lt::piece_index_t, lt::sha256_hash const&, lt::storage_error const&)> handler) override
	{
		TORRENT_ASSERT(m_files);

		auto const seek_time = disk_seek(std::int64_t(static_cast<int>(piece)) * m_files->piece_length() + offset
			, m_blocks_per_piece * lt::default_block_size);

		auto const delay = seek_time + m_state.hash_time + m_state.read_time;
		queue_event(delay, [this, piece, offset, h=std::move(handler)] () mutable {
			int const block_idx = piece * m_blocks_per_piece + offset / lt::default_block_size;
			if (!m_have.get_bit(block_idx))
			{
				post(m_ioc, [h=std::move(h),piece] { h(piece, lt::sha256_hash{}, lt::storage_error{}); });
			}
			else
			{
				lt::sha256_hash const hash = generate_block_hash(piece, offset);
				post(m_ioc, [h=std::move(h),piece, hash] { h(piece, hash, lt::storage_error{}); });
			}
		});
	}

	void async_move_storage(lt::storage_index_t, std::string p, lt::move_flags_t
		, std::function<void(lt::status_t, std::string const&, lt::storage_error const&)> handler) override
	{
		TORRENT_ASSERT(m_files);
		post(m_ioc, [=]{
			handler(lt::status_t::fatal_disk_error, p
				, lt::storage_error(lt::error_code(boost::system::errc::operation_not_supported, lt::system_category())));
		});
	}

	void async_release_files(lt::storage_index_t, std::function<void()> f) override
	{
		TORRENT_ASSERT(m_files);
		queue_event(lt::microseconds(1), std::move(f));
	}

	void async_delete_files(lt::storage_index_t, lt::remove_flags_t
		, std::function<void(lt::storage_error const&)> handler) override
	{
		TORRENT_ASSERT(m_files);

		queue_event(lt::microseconds(1), [this,h=std::move(handler)] () mutable {
			m_have.clear_all();
			post(m_ioc, [h=std::move(h)]{ h(lt::storage_error()); });
		});
	}

	void async_check_files(lt::storage_index_t
		, lt::add_torrent_params const* p
		, lt::aux::vector<std::string, lt::file_index_t>
		, std::function<void(lt::status_t, lt::storage_error const&)> handler) override
	{
		TORRENT_ASSERT(m_files);
		auto const ret = (!m_state.seed || (p->flags & lt::torrent_flags::seed_mode))
			? lt::status_t::no_error
			: lt::status_t::need_full_check;
		queue_event(lt::microseconds(1), [this,ret,h=std::move(handler)] () mutable {
			post(m_ioc, [ret,h=std::move(h)] { h(ret, lt::storage_error()); });
		});
	}

	void async_rename_file(lt::storage_index_t
		, lt::file_index_t const idx
		, std::string const name
		, std::function<void(std::string const&, lt::file_index_t, lt::storage_error const&)> handler) override
	{
		TORRENT_ASSERT(m_files);
		post(m_ioc, [=]{ handler(name, idx, lt::storage_error()); });
	}

	void async_stop_torrent(lt::storage_index_t, std::function<void()> handler) override
	{
		TORRENT_ASSERT(m_files);
		post(m_ioc, handler);
	}

	void async_set_file_priority(lt::storage_index_t
		, lt::aux::vector<lt::download_priority_t, lt::file_index_t> prio
		, std::function<void(lt::storage_error const&
			, lt::aux::vector<lt::download_priority_t, lt::file_index_t>)> handler) override
	{
		TORRENT_ASSERT(m_files);
		post(m_ioc, [=]{
			handler(lt::storage_error(lt::error_code(
				boost::system::errc::operation_not_supported, lt::system_category())), std::move(prio));
		});
	}

	void async_clear_piece(lt::storage_index_t, lt::piece_index_t index
		, std::function<void(lt::piece_index_t)> handler) override
	{
		TORRENT_ASSERT(m_files);
		post(m_ioc, [=]{ handler(index); });
	}

	// implements buffer_allocator_interface
	void free_disk_buffer(char* buf) override
	{
		delete[] buf;
	}

	void update_stats_counters(lt::counters&) const override {}

	std::vector<lt::open_file_state> get_status(lt::storage_index_t) const override
	{ return {}; }

	void submit_jobs() override {}

private:

	lt::time_duration disk_seek(std::int64_t const offset, int const size = lt::default_block_size)
	{
		return (std::exchange(m_last_disk_offset, offset + size) == offset)
			? lt::milliseconds(0) : m_state.seek_time;
	}

	int block_index(lt::peer_request const& r) const
	{
		return r.piece * m_blocks_per_piece + r.start / lt::default_block_size;
	}

	bool validate_block(char const* b, lt::peer_request const& r) const
	{
		auto const fill = generate_block_fill(r.piece, r.start / lt::default_block_size);
		for (int i = 0; i < lt::default_block_size; i += fill.size())
		{
			if (std::memcmp(b, fill.data(), fill.size()) != 0) return false;
			b += fill.size();
		}
		return true;
	}

	void queue_event(lt::time_duration dt, std::function<void()> f)
	{
		if (m_event_queue.empty())
		{
			m_event_queue.push_back({lt::clock_type::now() + dt, std::move(f)});
			m_timer.expires_after(dt);
			using namespace std::placeholders;
			m_timer.async_wait(std::bind(&test_disk_io::on_timer, this, _1));
		}
		else
		{
			m_event_queue.push_back({m_event_queue.back().first + dt, std::move(f)});
		}
	}

	void on_timer(lt::error_code const&)
	{
		if (m_event_queue.empty())
			return;

		{
			auto f = std::move(m_event_queue.front().second);
			m_event_queue.pop_front();
			f();
		}

		if (m_event_queue.empty())
			return;

		m_timer.expires_at(m_event_queue.back().first);
		using namespace std::placeholders;
		m_timer.async_wait(std::bind(&test_disk_io::on_timer, this, _1));
	}

	void check_buffer_level()
	{
		if (!m_exceeded_max_size || m_write_queue > m_state.low_watermark) return;

		m_exceeded_max_size = false;

		std::vector<std::weak_ptr<lt::disk_observer>> cbs;
		m_observers.swap(cbs);
		post(m_ioc, std::bind(&watermark_callback, std::move(cbs)));
	}

	std::vector<std::weak_ptr<lt::disk_observer>> m_observers;
	int m_write_queue = 0;
	bool m_exceeded_max_size = false;

	// events that is supposed to trigger in the future are put in this queue
	std::deque<std::pair<lt::time_point, std::function<void()>>> m_event_queue;
	lt::aux::deadline_timer m_timer;

	// the last read or write operation pushed at the end of the event queue. If
	// the disk operation that's about to be pushed is immediately following
	// this one, there is no seek delay
	std::int64_t m_last_disk_offset = 0;

	test_disk m_state;

	// we only support a single torrent. This is set if it has been added
	lt::file_storage const* m_files = nullptr;

	// marks blocks as they are written (as long as the correct block is written)
	// when computing the hash of a piece where not all blocks are written, will
	// fail
	lt::bitfield m_have;

	int m_blocks_per_piece;

	// callbacks are posted on this
	lt::io_context& m_ioc;
};

std::unique_ptr<lt::disk_interface> test_disk::operator()(
	lt::io_context& ioc, lt::settings_interface const&, lt::counters&)
{
	return std::make_unique<test_disk_io>(ioc, *this);
}

