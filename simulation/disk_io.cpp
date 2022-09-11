/*

Copyright (c) 2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "disk_io.hpp"
#include "utils.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/io_context.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/aux_/deadline_timer.hpp"
#include "libtorrent/disk_observer.hpp"
#include "libtorrent/aux_/apply_pad_files.hpp"
#include "libtorrent/aux_/random.hpp"

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

std::array<char, 0x4000> generate_block_fill(lt::piece_index_t const p, int const block)
{
	int const v = (static_cast<int>(p) << 8) | (block & 0xff);
	std::array<char, 0x4000> ret;
	for (int i = 0; i < 0x4000; i += 4)
	{
		std::memcpy(ret.data() + i, reinterpret_cast<char const*>(&v), 4);
	}
	return ret;
}

lt::sha1_hash generate_hash1(lt::piece_index_t const p, int const piece_size, int const pad_bytes)
{
	lt::hasher ret;
	int const payload_size = piece_size - pad_bytes;
	int offset = 0;
	for (int block = 0; offset < payload_size; ++block)
	{
		auto const fill = generate_block_fill(p, block);
		for (int i = 0; i < lt::default_block_size;)
		{
			int const bytes = std::min(int(fill.size()), payload_size - offset);
			if (bytes <= 0) break;
			ret.update(fill.data(), bytes);
			offset += bytes;
			i += bytes;
		}
	}
	TORRENT_ASSERT(piece_size - offset == pad_bytes);
	std::array<char, 8> const pad{{0, 0, 0, 0, 0, 0, 0, 0}};
	while (offset < piece_size)
	{
		int const bytes = std::min(int(pad.size()), piece_size - offset);
		ret.update(pad.data(), bytes);
		offset += bytes;
	}
	return ret.final();
}

lt::sha1_hash generate_hash2(lt::piece_index_t p
	, int const piece_size
	, int const piece_size2
	, lt::span<lt::sha256_hash> const hashes, int const pad_bytes)
{
	int const payload_size = piece_size - pad_bytes;
	int const blocks_in_piece = (piece_size + lt::default_block_size - 1) / lt::default_block_size;
	int const blocks_in_piece2 = (piece_size2 + lt::default_block_size - 1) / lt::default_block_size;
	TORRENT_ASSERT(int(hashes.size()) >= blocks_in_piece2);
	int const blocks_to_read = std::max(blocks_in_piece, blocks_in_piece2);

	TORRENT_ASSERT(piece_size - pad_bytes == piece_size2);

	lt::hasher ret;
	int offset = 0;
	for (int block = 0; block < blocks_to_read; ++block)
	{
		lt::hasher256 v2_hash;
		auto const fill = generate_block_fill(p, block);

		bool const v2 = piece_size2 - offset > 0;

		int const block_size = std::min(lt::default_block_size, payload_size - offset);
		for (int i = 0; i < block_size;)
		{
			int const bytes = std::min(int(fill.size()), payload_size - offset);
			TORRENT_ASSERT(bytes > 0);
			ret.update(fill.data(), bytes);

			if (piece_size2 - offset > 0)
				v2_hash.update(fill.data(), std::min(int(fill.size()), piece_size2 - offset));

			offset += bytes;
			i += bytes;
		}
		if (offset < piece_size)
		{
			std::vector<char> padding(piece_size - offset, 0);
			ret.update(padding);
		}
		if (v2)
			hashes[block] = v2_hash.final();
	}
	return ret.final();
}

lt::sha256_hash generate_block_hash(lt::piece_index_t p, int const offset)
{
	// TODO: this function is not correct for files whose size are not divisible
	// by the block size (for the last block)
	lt::hasher256 ret;
	int const block = offset / lt::default_block_size;
	auto const fill = generate_block_fill(p, block);
	for (int i = 0; i < lt::default_block_size; i += fill.size())
		ret.update(fill);
	return ret.final();
}

void generate_block(char* b, lt::peer_request const& r, int const pad_bytes)
{
	auto const fill = generate_block_fill(r.piece, (r.start / lt::default_block_size));

	// for now we don't support unaligned start address
	TORRENT_ASSERT((r.start % fill.size()) == 0);
	char* end = b + r.length - pad_bytes;
	while (b < end)
	{
		int const bytes = std::min(int(fill.size()), int(end - b));
		std::memcpy(b, fill.data(), bytes);
		b += bytes;
	}

	if (pad_bytes > 0)
		std::memset(b, 0, pad_bytes);
}

std::unordered_map<lt::piece_index_t, int> compute_pad_bytes(lt::file_storage const& fs)
{
	std::unordered_map<lt::piece_index_t, int> ret;
	lt::aux::apply_pad_files(fs, [&](lt::piece_index_t p, int bytes)
	{
		ret.emplace(p, bytes);
	});
	return ret;
}

std::unordered_map<lt::piece_index_t, int> compute_pad_bytes(lt::create_torrent const& t)
{
	std::unordered_map<lt::piece_index_t, int> ret;

	std::int64_t off = 0;
	int const piece_size = t.piece_length();
	for (auto const i : t.file_range())
	{
		off += t.file_at(i).size;
		if (!(t.file_at(i).flags & lt::file_storage::flag_pad_file)
			|| t.file_at(i).size == 0)
		{
			continue;
		}

		// this points to the last byte of the pad file
		int piece = (off - 1) / piece_size;
		int const start = (off - 1) & (piece_size - 1);

		// This pad file may be the last file in the torrent, and the
		// last piece may have an odd size.
		if ((start + 1) % piece_size != 0 && i < prev(t.end_file()))
		{
			// this is a pre-requisite of the piece picker. Pad files
			// that don't align with pieces are kind of useless anyway.
			// They probably aren't real padfiles, treat them as normal
			// files.
			continue;
		}

		std::int64_t pad_bytes_left = t.file_at(i).size;

		while (pad_bytes_left > 0)
		{
			// The last piece may have an odd size, that's why
			// we ask for the piece size for every piece. (it would be
			// odd, but it's still possible).
			int const bytes = int(std::min(pad_bytes_left, std::int64_t(piece_size)));
			TORRENT_ASSERT(bytes > 0);
			ret.emplace(piece, bytes);
			pad_bytes_left -= bytes;
			--piece;
		}
	}
	return ret;
}


int pads_in_piece(std::unordered_map<lt::piece_index_t, int> const& pb, lt::piece_index_t const p)
{
	auto it = pb.find(p);
	return (it == pb.end()) ? 0 : it->second;
}

int pads_in_req(std::unordered_map<lt::piece_index_t, int> const& pb
	, lt::peer_request const& r, int const piece_size)
{
	auto it = pb.find(r.piece);
	if (it == pb.end()) return 0;

	int const pad_start = piece_size - it->second;
	int const req_end = r.start + r.length;
	return std::max(0, std::min(req_end - pad_start, r.length));
}

std::shared_ptr<lt::torrent_info> create_test_torrent(int const piece_size
	, int const num_pieces, lt::create_flags_t const flags, int const num_files)
{
	std::vector<lt::create_file_entry> ifs;
	int total_size = num_files * piece_size * num_pieces + 1234;
	if (num_files == 1)
	{
		ifs.emplace_back("file-1", total_size);
	}
	else
	{
		int const file_size = total_size / num_files + 10;
		for (int i = 0; i < num_files; ++i)
		{
			int const this_size = std::min(file_size, total_size);
			ifs.emplace_back("test-torrent/file-" + std::to_string(i + 1), this_size);
			total_size -= this_size;
		}
	}
	lt::create_torrent t(std::move(ifs), piece_size, flags);

	auto const pad_bytes = compute_pad_bytes(t);

	if (flags & lt::create_torrent::v1_only)
	{
		for (auto const i : t.piece_range())
			t.set_hash(i, generate_hash1(i, t.piece_length(), pads_in_piece(pad_bytes, i)));
	}
	else
	{
		int const blocks_per_piece = piece_size / lt::default_block_size;
		TORRENT_ASSERT(blocks_per_piece * lt::default_block_size == piece_size);
		// blocks per piece must be a power of two
		TORRENT_ASSERT((blocks_per_piece & (blocks_per_piece - 1)) == 0);

		lt::aux::vector<lt::sha256_hash> blocks(blocks_per_piece);
		std::vector<lt::sha256_hash> scratch_space;

		std::int64_t file_offset = 0;
		std::int64_t const total_size = t.total_size();
		for (auto const f : t.file_range())
		{
			if (t.file_at(f).flags & lt::file_storage::flag_pad_file)
			{
				file_offset += t.file_at(f).size;
				continue;
			}
			auto piece_offset = file_offset;
			lt::piece_index_t const first_piece(int(file_offset / piece_size));
			file_offset += t.file_at(f).size;
			lt::piece_index_t const end_piece(int((file_offset + piece_size - 1) / piece_size));
			for (auto piece = first_piece; piece < end_piece; ++piece)
			{
				auto const this_piece_size = int(std::min(std::int64_t(piece_size), total_size - piece_offset));
				auto const piece_size2 = std::min(std::int64_t(piece_size), file_offset - piece_offset);
				auto const blocks_in_piece = (piece_size2 + lt::default_block_size - 1)
					/ lt::default_block_size;
				TORRENT_ASSERT(blocks_in_piece > 0);
				TORRENT_ASSERT(blocks_in_piece <= int(blocks.size()));
				auto const hash = generate_hash2(piece
					, this_piece_size
					, piece_size2
					, blocks, pads_in_piece(pad_bytes, piece));
				auto const piece_layer_hash = lt::merkle_root_scratch(
					lt::span<lt::sha256_hash const>(blocks).first(blocks_in_piece)
					, blocks_per_piece
					, {}
					, scratch_space);
				t.set_hash2(f, piece - first_piece, piece_layer_hash);

				if (!(flags & lt::create_torrent::v2_only))
					t.set_hash(piece, hash);
				piece_offset += this_piece_size;
			}
		}
	}

	lt::entry tor = t.generate();

	std::vector<char> tmp;
	bencode(std::back_inserter(tmp), tor);
	lt::error_code ec;
	return std::make_shared<lt::torrent_info>(tmp, ec, lt::from_span);
}

lt::add_torrent_params create_test_torrent(
	int const num_pieces, lt::create_flags_t const flags, int const blocks_per_piece
	, int const num_files)
{
	lt::add_torrent_params params;
	params.ti = ::create_test_torrent(lt::default_block_size * blocks_per_piece, num_pieces, flags, num_files);
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
		m_have.resize(m_files->num_pieces() * m_blocks_per_piece, m_state.files == existing_files_mode::full_valid);
		m_pad_bytes = compute_pad_bytes(fs);

		if (m_state.files == existing_files_mode::partial_valid)
		{
			// we have the first half of the blocks
			for (std::size_t i = 0; i < m_have.size() / 2u; ++i)
				m_have.set_bit(i);
		}

		return lt::storage_holder(lt::storage_index_t{0}, *this);
	}

	void remove_torrent(lt::storage_index_t const idx) override
	{
		TORRENT_ASSERT(static_cast<std::uint32_t>(idx) == 0);
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
		TORRENT_ASSERT(static_cast<std::uint32_t>(storage) == 0);
		TORRENT_ASSERT(m_files);

		// a real diskt I/O implementation would have to support this, but in
		// the simulations, we never send unaligned piece requests.
		TORRENT_ASSERT((r.start % lt::default_block_size) == 0);
		TORRENT_ASSERT((r.length <= lt::default_block_size));

		auto const seek_time = disk_seek(r.piece, r.start, lt::default_block_size);

		queue_event(seek_time + m_state.read_time, [this,r, h=std::move(h)] () mutable {
			lt::disk_buffer_holder buf(*this, new char[lt::default_block_size], r.length);

			if (m_have.get_bit(block_index(r)))
			{
				if (m_state.corrupt_data_in-- <= 0)
					lt::aux::random_bytes(buf);
				else
					generate_block(buf.data(), r, pads_in_req(m_pad_bytes, r, m_files->piece_size(r.piece)));
			}

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

		bool const valid = validate_block(*m_files, buf, r);

		auto const seek_time = disk_seek(r.piece, r.start, lt::default_block_size);

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

		auto const seek_time = disk_seek(piece, 0, m_blocks_per_piece * lt::default_block_size);

		auto const delay = seek_time
			+ m_state.read_time * m_blocks_per_piece
			+ m_state.hash_time * m_blocks_per_piece
			+ m_state.hash_time * block_hashes.size();

		queue_event(delay, [this, piece, block_hashes, h=std::move(handler)] () mutable {

			int const piece_size = m_files->piece_size(piece);
			int const pad_bytes = pads_in_piece(m_pad_bytes, piece);
			int const payload_blocks = piece_size / lt::default_block_size - pad_bytes / lt::default_block_size;
			int const block_idx = static_cast<int>(piece) * m_blocks_per_piece;
			for (int i = 0; i < payload_blocks; ++i)
			{
				if (m_have.get_bit(block_idx + i))
					continue;

				lt::sha1_hash ph{};
				if (m_state.files == existing_files_mode::full_invalid)
				{
					for (auto& h : block_hashes)
						h = rand_sha256();
					ph = rand_sha1();
				}
				// If we're missing a block, return an invalid hash
				post(m_ioc, [h=std::move(h), piece, ph]{ h(piece, ph, lt::storage_error{}); });
				return;
			}

			lt::sha1_hash hash;
			if (block_hashes.empty())
				hash = generate_hash1(piece, m_files->piece_length(), pads_in_piece(m_pad_bytes, piece));
			else
				hash = generate_hash2(piece
					, m_files->piece_size(piece)
					, m_files->piece_size2(piece)
					, block_hashes, pads_in_piece(m_pad_bytes, piece));
			post(m_ioc, [h=std::move(h), piece, hash]{ h(piece, hash, lt::storage_error{}); });
		});
	}

	void async_hash2(lt::storage_index_t storage, lt::piece_index_t const piece
		, int const offset, lt::disk_job_flags_t
		, std::function<void(lt::piece_index_t, lt::sha256_hash const&, lt::storage_error const&)> handler) override
	{
		TORRENT_ASSERT(m_files);

		auto const seek_time = disk_seek(piece, offset, m_blocks_per_piece * lt::default_block_size);

		auto const delay = seek_time + m_state.hash_time + m_state.read_time;
		queue_event(delay, [this, piece, offset, h=std::move(handler)] () mutable {
			int const block_idx = static_cast<int>(piece) * m_blocks_per_piece
				+ offset / lt::default_block_size;
			lt::sha256_hash hash;
			if (!m_have.get_bit(block_idx))
			{
				if (m_state.files == existing_files_mode::full_invalid)
					hash = rand_sha256();
			}
			else
			{
				hash = generate_block_hash(piece, offset);
			}
			post(m_ioc, [h=std::move(h),piece, hash] { h(piece, hash, lt::storage_error{}); });
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

		auto ret = lt::status_t::need_full_check;
		if (p && p->flags & lt::torrent_flags::seed_mode)
			ret = lt::status_t::no_error;
		else if (m_state.files == existing_files_mode::no_files)
			ret = lt::status_t::no_error;

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

	lt::time_duration disk_seek(lt::piece_index_t const piece, int start, int const size = lt::default_block_size)
	{
		std::int64_t const offset = std::int64_t(static_cast<int>(piece)) * m_files->piece_length() + start;
		return (std::exchange(m_last_disk_offset, offset + size) == offset)
			? lt::milliseconds(0) : m_state.seek_time;
	}

	int block_index(lt::peer_request const& r) const
	{
		return static_cast<int>(r.piece) * m_blocks_per_piece + r.start / lt::default_block_size;
	}

	bool validate_block(lt::file_storage const& fs, char const* b, lt::peer_request const& r) const
	{
		auto const fill = generate_block_fill(r.piece, r.start / lt::default_block_size);
		int const piece_size = fs.piece_size(r.piece);
		int payload_bytes = (piece_size - pads_in_piece(m_pad_bytes, r.piece)) - r.start;
		int offset = 0;
		while (offset < r.length && payload_bytes > 0)
		{
			int const to_compare = std::min(payload_bytes, int(fill.size()));
			if (std::memcmp(b, fill.data(), to_compare) != 0) return false;
			b += to_compare;
			offset += to_compare;
			payload_bytes -= to_compare;
		}
		if (offset < r.length)
		{
			// the pad bytes must be zero
			return std::all_of(b, b + r.length - offset, [](char const c) { return c == 0; });
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
			if (f) f();
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

	// events that are supposed to trigger in the future are put in this queue
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

	std::unordered_map<lt::piece_index_t, int> m_pad_bytes;
};

std::unique_ptr<lt::disk_interface> test_disk::operator()(
	lt::io_context& ioc, lt::settings_interface const&, lt::counters&)
{
	return std::make_unique<test_disk_io>(ioc, *this);
}


std::ostream& operator<<(std::ostream& os, existing_files_mode const mode)
{
	switch (mode)
	{
		case existing_files_mode::no_files: return os << "no_files";
		case existing_files_mode::full_invalid: return os << "full_invalid";
		case existing_files_mode::partial_valid: return os << "partial_valid";
		case existing_files_mode::full_valid: return os << "full_valid";
	}
	return os << "<unknown file mode>";
}
