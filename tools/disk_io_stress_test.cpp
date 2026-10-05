/*

Copyright (c) 2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/session.hpp" // for default_disk_io_constructor
#include "libtorrent/disabled_disk_io.hpp"
#include "libtorrent/mmap_disk_io.hpp"
#include "libtorrent/pread_disk_io.hpp"
#include "libtorrent/posix_disk_io.hpp"

#include "libtorrent/disk_interface.hpp"
#include "libtorrent/disk_observer.hpp"
#include "libtorrent/settings_pack.hpp"
#include "libtorrent/file_storage.hpp"
#include "libtorrent/flags.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/add_torrent_params.hpp"
#include "libtorrent/aux_/scope_end.hpp"

#include <filesystem>

#include <random>
#include <algorithm>
#include <chrono>
#include <map>
#include <vector>
#include <iostream>
#include <iomanip>

using disk_test_mode_t = lt::flags::bitfield_flag<std::uint8_t, struct disk_test_mode_tag>;

using lt::operator""_bit;
using lt::operator ""_sv;

namespace test_mode {
constexpr disk_test_mode_t sparse = 0_bit;
constexpr disk_test_mode_t even_file_sizes = 1_bit;
constexpr disk_test_mode_t read_random_order = 2_bit;
constexpr disk_test_mode_t flush_files = 3_bit;
constexpr disk_test_mode_t clear_pieces = 4_bit;
constexpr disk_test_mode_t unaligned_read = 5_bit;
constexpr disk_test_mode_t hot_read = 6_bit;
}

std::mt19937 random_engine(std::random_device{}());

// disk_observer implementation used to resume write submission after back-pressure is lifted
struct write_throttle final : lt::disk_observer
{
	explicit write_throttle(bool& flag) : m_exceeded(flag) {}
	void on_disk() override { m_exceeded = false; }
private:
	bool& m_exceeded;
};

// bytes past the end of the piece's payload are pad bytes, which are zero
bool check_block_fill(lt::peer_request const& req, int const payload, lt::span<char const> buf)
{
	// generate_block_fill() fills each block with the 4-byte word value derived
	// from the block index, repeated. So the expected byte at piece offset `off`
	// is byte (off % 4) of that block's word. Verifying per byte (rather than per
	// word) handles reads at any offset and length -- including unaligned ones
	// that span a block boundary -- not just word-aligned block reads.
	for (int i = 0; i < buf.size(); ++i)
	{
		int const off = req.start + i;
		int const v = (static_cast<int>(req.piece) << 8) | ((off / lt::default_block_size) & 0xff);
		char const expected = off >= payload ? '\0' : reinterpret_cast<char const*>(&v)[off % 4];
		if (buf[i] != expected)
		{
			std::cout << "buffer diverged at offset: " << i << '\n';
			return false;
		}
	}
	return true;
}

void generate_block_fill(lt::peer_request const& req, lt::span<char> buf)
{
	int const v = (static_cast<int>(req.piece) << 8) | ((req.start / lt::default_block_size) & 0xff);
	int offset = 0;
	int const tail = buf.size() % 4;
	for (; offset < buf.size() - tail; offset += 4)
		std::memcpy(buf.data() + offset, reinterpret_cast<char const*>(&v), 4);
	if (tail > 0)
		std::memcpy(buf.data() + offset, reinterpret_cast<char const*>(&v), tail);
}

enum class torrent_type
{
	v1,
	v2,
	hybrid,
};

struct test_case
{
	int num_files;
	int queue_size;
	int num_threads;
	int read_multiplier;
	int file_pool_size;
	disk_test_mode_t flags;
	std::string disk_backend;
	torrent_type version = torrent_type::v1;
};

int run_test(test_case const& t)
{
	lt::file_storage fs;

	std::int64_t file_size = (t.flags & test_mode::even_file_sizes)
		? 0x1000
		: 1337;

	int const piece_length = 0x8000;

	bool const has_v1 = t.version != torrent_type::v2;
	bool const has_v2 = t.version != torrent_type::v1;

	{
		fs.set_name("test");
		for (int i = 0; i < t.num_files; ++i)
		{
			lt::error_code ec;
			fs.add_file(
				ec, std::to_string(i), false, lt::aux::path_element::torrent_root, file_size);

			// the files of v2 torrents start at piece boundaries. The last file isn't
			// padded
			std::int64_t const tail = file_size % piece_length;
			if (has_v2 && tail != 0 && i + 1 < t.num_files)
			{
				// pad files never store a name
				fs.add_file(ec,
					{},
					false,
					lt::aux::path_element::pad_directory,
					piece_length - tail,
					lt::file_storage::flag_pad_file);
			}
			file_size *= 2;
		}
		std::int64_t const total_size = fs.total_size();
		int const num_pieces = static_cast<int>((total_size + piece_length - 1) / piece_length);
		fs.set_num_pieces(num_pieces);
		fs.set_piece_length(piece_length);
	}

	// the two sizes only differ for hybrid torrents, where a file's last
	// piece has a pad tail. The bittorrent engine never writes blocks that
	// are entirely pad, but the block straddling the end of the file data is
	// written in full, pad bytes included, so that's what the back-end is
	// tested against
	lt::aux::vector<int, lt::piece_index_t> written_size;
	lt::aux::vector<int, lt::piece_index_t> data_size;
	for (lt::piece_index_t const p : fs.piece_range())
	{
		written_size.push_back(has_v1 ? fs.piece_size(p) : fs.piece_size2(p));
		data_size.push_back(has_v2 ? fs.piece_size2(p) : fs.piece_size(p));
	}
	lt::io_context ioc;
	lt::counters cnt;
	lt::settings_pack pack;
	pack.set_int(lt::settings_pack::aio_threads, t.num_threads);
	pack.set_int(lt::settings_pack::file_pool_size, t.file_pool_size);
	pack.set_int(lt::settings_pack::max_queued_disk_bytes, t.queue_size * lt::default_block_size);

	std::unique_ptr<lt::disk_interface> disk_io;

#if TORRENT_HAVE_MMAP || TORRENT_HAVE_MAP_VIEW_OF_FILE
	if (t.disk_backend == "mmap"_sv)
		disk_io = lt::mmap_disk_io_constructor(ioc, pack, cnt);
	else
#endif
	{
		if (t.disk_backend  == "posix"_sv)
			disk_io = lt::posix_disk_io_constructor(ioc, pack, cnt);
		else if (t.disk_backend  == "pread"_sv)
			disk_io = lt::pread_disk_io_constructor(ioc, pack, cnt);
		else if (t.disk_backend  == "disabled"_sv)
			disk_io = lt::disabled_disk_io_constructor(ioc, pack, cnt);
		else
		{
			if (t.disk_backend != "default"_sv)
			{
				std::fprintf(stderr, "unknown disk-io subsystem: \"%s\". Using default.\n", t.disk_backend.c_str());
			}
			disk_io = lt::default_disk_io_constructor(ioc, pack, cnt);
		}
	}

	std::cerr << "RUNNING: -f " << t.num_files << " -q " << t.queue_size << " -t " << t.num_threads
			  << " -r " << t.read_multiplier << " -p " << t.file_pool_size << " -V "
			  << (t.version == torrent_type::v1			 ? "v1"
						 : t.version == torrent_type::v2 ? "v2"
														 : "hybrid")
			  << ((t.flags & test_mode::sparse) ? "" : " alloc")
			  << ((t.flags & test_mode::even_file_sizes) ? " even-size" : "")
			  << ((t.flags & test_mode::read_random_order) ? " random-read" : "")
			  << ((t.flags & test_mode::flush_files) ? " flush" : "")
			  << ((t.flags & test_mode::clear_pieces) ? " clear" : "")
			  << ((t.flags & test_mode::unaligned_read) ? " unaligned-read" : "")
			  << ((t.flags & test_mode::hot_read) ? " hot-read" : "") << " -d " << t.disk_backend
			  << "\n";

	try
	{
		std::filesystem::remove_all("scratch-area");

		// TODO: add test mode where some file priorities are 0

		lt::aux::vector<lt::download_priority_t, lt::file_index_t> prios;
		std::string save_path = "./scratch-area";
		lt::renamed_files rf;
		lt::storage_params params(fs,
			rf,
			save_path,
			{},
			(t.flags & test_mode::sparse) ? lt::storage_mode_sparse : lt::storage_mode_allocate,
			prios,
			lt::sha1_hash("01234567890123456789"),
			has_v1,
			has_v2);

		auto abort_disk = lt::aux::scope_end([&] { disk_io->abort(true); });

		lt::storage_holder const tor = disk_io->new_torrent(params, {});

		std::vector<lt::peer_request> blocks_to_write;
		for (lt::piece_index_t p : fs.piece_range())
		{
			for (int offset = 0; offset < data_size[p]; offset += lt::default_block_size)
			{
				blocks_to_write.push_back(
					{p, offset, std::min(lt::default_block_size, written_size[p] - offset)});
			}
		}
		std::shuffle(blocks_to_write.begin(), blocks_to_write.end(), random_engine);

		// count blocks per piece so we know when all writes have been submitted
		// and can issue async_hash() to hash and flush the piece
		std::map<lt::piece_index_t, int> blocks_per_piece;
		for (auto const& b : blocks_to_write)
			++blocks_per_piece[b.piece];

		lt::aux::vector<lt::peer_request> blocks_to_read;
		blocks_to_read.reserve(blocks_to_write.size());

		std::vector<char> write_buffer(lt::default_block_size);

		int outstanding_read = 0;
		int outstanding_write = 0;
		int outstanding_hash = 0;
		int outstanding_release = 0;
		int outstanding_clear = 0;
		std::set<int> in_flight;

		// when async_write() returns true the disk's write queue is full;
		// the write_throttle observer is notified (via on_disk()) once the
		// queue has drained below the low watermark, clearing this flag.
		bool write_exceeded = false;
		auto observer = std::make_shared<write_throttle>(write_exceeded);

		// the bittorrent engine never reads a block before its piece has passed
		// async_hash(), so back-ends aren't required to serve earlier reads
		std::map<lt::piece_index_t, std::vector<lt::peer_request>> deferred_reads;

		auto const queue_read = [&](lt::peer_request const& req) {
			if (t.flags & test_mode::read_random_order)
			{
				std::uniform_int_distribution<> d(0, blocks_to_read.end_index());
				blocks_to_read.insert(blocks_to_read.begin() + d(random_engine), req);
			}
			else
			{
				blocks_to_read.push_back(req);
			}
			// if read_multiplier > 1, put this block more times in the
			// read queue
			for (int i = 1; i < t.read_multiplier; ++i)
			{
				std::uniform_int_distribution<> d(0, blocks_to_read.end_index());
				blocks_to_read.insert(blocks_to_read.begin() + d(random_engine), req);
			}
		};

		lt::add_torrent_params atp;

		int job_idx = 0;
		in_flight.insert(job_idx);
		++outstanding_read;
		disk_io->async_check_files(tor, &atp, lt::aux::vector<std::string, lt::file_index_t>{}
			, [&, job_idx](lt::status_t, lt::storage_error const&) {
				TORRENT_ASSERT(in_flight.count(job_idx));
				in_flight.erase(job_idx);
				TORRENT_ASSERT(outstanding_read > 0);
				--outstanding_read;
			});
		++job_idx;
		disk_io->submit_jobs();

		while (outstanding_read > 0)
		{
			ioc.run_one();
			ioc.restart();
		}

		int job_counter = 0;

		auto const issue_read = [&](lt::peer_request req) {
			// the piece is complete and hashed by the time it's read back, so
			// any range of it can be read. Random offsets and lengths exercise
			// the back-end's unaligned and block-spanning read path
			if (t.flags & test_mode::unaligned_read)
			{
				int const this_piece_size = written_size[req.piece];
				std::uniform_int_distribution<int> start_dist(0, this_piece_size - 1);
				req.start = start_dist(random_engine);
				int const max_len = std::min(lt::default_block_size, this_piece_size - req.start);
				std::uniform_int_distribution<int> len_dist(1, max_len);
				req.length = len_dist(random_engine);
			}

			in_flight.insert(job_idx);
			++outstanding_read;
			disk_io->async_read(
				tor, req, [&, req, job_idx](lt::disk_buffer_holder h, lt::storage_error const& ec) {
					TORRENT_ASSERT(in_flight.count(job_idx));
					in_flight.erase(job_idx);
					TORRENT_ASSERT(outstanding_read > 0);
					--outstanding_read;
					++job_counter;
					if (ec)
					{
						std::cerr << "async_read() failed: " << ec.ec.message() << " "
								  << lt::operation_name(ec.operation) << " "
								  << static_cast<int>(ec.file()) << "\n";
						throw std::runtime_error("async_read failed");
					}

					if (!check_block_fill(req, data_size[req.piece], {h.data(), req.length}))
					{
						std::cerr << "read buffer mismatch: (" << req.piece << ", " << req.start
								  << ")\n";
						throw std::runtime_error("read buffer mismatch!");
					}
				});
			++job_idx;
		};

		using clock = std::chrono::steady_clock;
		auto last_print = clock::now();

		while (!blocks_to_write.empty()
			|| !blocks_to_read.empty()
			|| outstanding_read + outstanding_write + outstanding_hash
				+ outstanding_release + outstanding_clear > 0)
		{
			auto const now = clock::now();
			if (now - last_print >= std::chrono::milliseconds(300))
			{
				last_print = now;
				printf("w: %d (%d) r: %d(%d) h: %d f: %d c: %d %s         \r"
					, int(blocks_to_write.size())
					, outstanding_write
					, int(blocks_to_read.size())
					, outstanding_read
					, outstanding_hash
					, outstanding_release
					, outstanding_clear
					, write_exceeded ? "wait" : "");
				fflush(stdout);
			}
			for (int i = 0; i < t.read_multiplier; ++i)
			{
				if (!blocks_to_read.empty() && outstanding_read < t.queue_size)
				{
					auto const req = blocks_to_read.back();
					blocks_to_read.erase(blocks_to_read.end() - 1);
					issue_read(req);
				}
			}

			if (!blocks_to_write.empty() && !write_exceeded)
			{
				auto const req = blocks_to_write.back();
				blocks_to_write.erase(blocks_to_write.end() - 1);

				generate_block_fill(req, {write_buffer.data(), lt::default_block_size});
				int const payload = data_size[req.piece];
				if (req.start + req.length > payload)
				{
					std::fill(write_buffer.begin() + (payload - req.start),
						write_buffer.begin() + req.length,
						'\0');
				}

				in_flight.insert(job_idx);
				++outstanding_write;
				bool const exceeded = disk_io->async_write(tor, req, write_buffer.data()
					, observer, [&, job_idx](lt::storage_error const& ec)
					{
						TORRENT_ASSERT(in_flight.count(job_idx));
						in_flight.erase(job_idx);
						TORRENT_ASSERT(outstanding_write > 0);
						--outstanding_write;
						++job_counter;
						if (ec)
						{
							std::cerr << "async_write() failed: " << ec.ec.message()
								<< " " << lt::operation_name(ec.operation)
								<< " " << static_cast<int>(ec.file()) << "\n";
							throw std::runtime_error("async_write failed");
						}
					});
				if (exceeded) write_exceeded = true;
				++job_idx;
				deferred_reads[req.piece].push_back(req);

				// once all blocks of a piece have been submitted, issue async_hash()
				// to verify and flush it, matching real libtorrent behaviour
				auto it = blocks_per_piece.find(req.piece);
				TORRENT_ASSERT(it != blocks_per_piece.end());
				it->second -= 1;
				if (it->second == 0)
				{
					blocks_per_piece.erase(it);
					++outstanding_hash;
					// kept alive by the handler, async_hash() fills it in
					auto const v2_hashes = std::make_shared<std::vector<lt::sha256_hash>>(
						has_v2 ? std::size_t(fs.blocks_in_piece2(req.piece)) : 0);
					disk_io->async_hash(tor,
						req.piece,
						*v2_hashes,
						(has_v1 ? lt::disk_interface::v1_hash : lt::disk_job_flags_t{})
							| lt::disk_interface::flush_piece,
						[&, v2_hashes](lt::piece_index_t const piece,
							lt::sha1_hash const&,
							lt::storage_error const& ec) {
							TORRENT_ASSERT(outstanding_hash > 0);
							--outstanding_hash;
							++job_counter;
							if (ec)
							{
								std::cerr << "async_hash() failed: " << ec.ec.message()
									<< " " << lt::operation_name(ec.operation) << "\n";
								throw std::runtime_error("async_hash failed");
							}
							auto const dr = deferred_reads.find(piece);
							if (dr != deferred_reads.end())
							{
								for (auto const& deferred : dr->second)
								{
									// read some blocks right away, while they may still be
									// in the write cache
									if ((t.flags & test_mode::hot_read)
										&& std::uniform_int_distribution<int>(0, 9)(random_engine)
											== 0)
										issue_read(deferred);
									else
										queue_read(deferred);
								}
								deferred_reads.erase(dr);
							}
						});
				}
			}

			if ((t.flags & test_mode::flush_files) && (job_counter % 500) == 499)
			{
				in_flight.insert(job_idx);
				++outstanding_release;
				disk_io->async_release_files(tor, [&, job_idx]()
				{
					TORRENT_ASSERT(in_flight.count(job_idx));
					in_flight.erase(job_idx);
					TORRENT_ASSERT(outstanding_release > 0);
					--outstanding_release;
					++job_counter;
				});
				++job_idx;
			}

			if ((t.flags & test_mode::clear_pieces) && (job_counter % 300) == 299)
			{
				lt::piece_index_t const p = blocks_to_write.front().piece;
				in_flight.insert(job_idx);
				++outstanding_clear;
				disk_io->async_clear_piece(tor, p, [&, job_idx](lt::piece_index_t)
					{
					TORRENT_ASSERT(in_flight.count(job_idx));
					in_flight.erase(job_idx);
					TORRENT_ASSERT(outstanding_clear > 0);
					--outstanding_clear;
					++job_counter;
					});
				++job_idx;
				// TODO: technically all blocks for this piece should be added
				// to blocks_to_write again here
			}

			// TODO: add test_mode for async_move_storage
			// TODO: add test_mode for abort_hash_jobs
			// TODO: add test_mode for async_delete_files
			// TODO: add test_mode for async_rename_file
			// TODO: add test_mode for async_set_file_priority

			disk_io->submit_jobs();
			// block when the disk's write queue is full (write_exceeded) so that
			// we wait for the on_disk() callback to clear write_exceeded before
			// submitting more writes. Also block when too many reads are in flight.
			if (outstanding_read >= t.queue_size || write_exceeded
				|| (outstanding_hash > 0 && blocks_to_write.empty() && blocks_to_read.empty()))
				ioc.run_one();
			else
				ioc.poll();
			ioc.restart();
		}

		std::cerr << "OK (" << job_counter << " jobs)   \n";
		return 0;
	}
	catch (std::exception const& e)
	{
		std::cerr << "FAILED WITH EXCEPTION: " << e.what() << '\n';

		auto const ps = fs.piece_length();
		for (lt::file_index_t f : fs.file_range())
		{
			auto const off = fs.file_offset(f);
			std::cout << " test/" << std::setw(2) << int(f)
			   << " size: " << std::setw(10) << fs.file_size(f)
			   << " first piece: (" << (off / ps) << " offset: " << (off % ps) << ")"
			   << '\n';
		}
		auto const total_size = fs.total_size();
		auto const num_pieces = fs.num_pieces();
		std::cout << "                           last piece: ("
		   << (total_size / ps) << " offset: " << (total_size % ps) << ")\n";
		std::cout << "num pieces: " << num_pieces << '\n';
		return 1;
	}
}

void print_usage()
{
	std::cerr << "USAGE: disk_io_stress_test <options>\n"
				 "If no options are specified, the default suite of tests are run\n"
				 "If the single argument \"smoke\" is given, a small subset of the\n"
				 "default suite is run with orders of magnitude fewer blocks (for CI)\n\n"
				 "OPTIONS:\n"
				 "   alloc\n"
				 "      open files in pre-allocate mode\n"
				 "   even-size\n"
				 "      make test files even multiples of 1 kB\n"
				 "   random-read\n"
				 "      instead of reading blocks back in the same order they were written,\n"
				 "      read them back in random order\n"
				 "   flush\n"
				 "      issue a 'release-files' disk job every 500 jobs\n"
				 "   clear\n"
				 "      issue a 'clear_piece' disk job every 300 jobs\n"
				 "   unaligned-read\n"
				 "      read completed pieces back with requests of random offset and\n"
				 "      size (a mix of aligned, unaligned and block-spanning reads),\n"
				 "      exercising the disk back-end's spanning/partial read path\n"
				 "   hot-read\n"
				 "      read a tenth of the blocks of a piece as soon as it has been hashed,\n"
				 "      to hit blocks that are still in the write cache. These reads are\n"
				 "      issued once, regardless of -r, and bypass the -q queue limit\n"
				 "   -f <val>\n"
				 "      specifies the number of files to use in the test torrent\n"
				 "   -q <val>\n"
				 "      specifies the job queue size. i.e. the max number of outstanding\n"
				 "      read jobs to post to the disk I/O subsystem. write jobs depend on\n"
				 "      the disk subsystem's back pressure.\n"
				 "   -t <val>\n"
				 "      specifies the number of disk I/O threads to use\n"
				 "   -r <val>\n"
				 "      specifies the read multiplier. Each block that's written, is read this "
				 "      many times\n"
				 "   -p <val>\n"
				 "      specifies the file pool size. This is the number of files to keep open\n"
				 "   -d <disk-backend>\n"
				 "      Specifies which disk back-end to test. options are: default, mmap, pread, "
				 "      posix, disabled\n"
				 "   -V <version>\n"
				 "      the kind of torrent to test: v1, v2 or hybrid (default is v1). The files\n"
				 "      of v2 and hybrid torrents are aligned to piece boundaries\n";
}

int main(int argc, char const* argv[])
{
	if (argc == 1 || (argc == 2 && argv[1] == "smoke"_sv))
	{
		namespace tm = test_mode;

		// a curated subset of the full suite, just to catch trivial hangs and
		// crashes: baseline, random read order, release-files, small file pool,
		// unaligned reads, v2 and hybrid torrents, hot reads
		bool const smoke = (argc == 2);

		std::vector<test_case> tests;
		for (char const* backend : {"mmap", "posix", "pread"})
		{
			if (smoke)
			{
				// files, queue, threads, read-mult, pool, flags, disk_backend
				tests.push_back({7, 32, 4, 3, 10, tm::sparse, backend});
				tests.push_back({7,
					32,
					4,
					3,
					10,
					tm::sparse | tm::read_random_order | tm::even_file_sizes,
					backend});
				tests.push_back({7,
					32,
					4,
					3,
					10,
					tm::flush_files | tm::sparse | tm::read_random_order,
					backend});
				tests.push_back({7, 32, 8, 3, 1, tm::sparse | tm::read_random_order, backend});
				// random-offset/size reads of completed pieces, exercising the
				// disk back-end's unaligned/spanning read path. Use more files (and
				// thus more pieces) than the other smoke cases: unaligned reads only
				// fire on already-completed pieces, so a handful of pieces hardly
				// covers the path.
				tests.push_back({13,
					32,
					4,
					3,
					10,
					tm::sparse | tm::read_random_order | tm::unaligned_read,
					backend});
				tests.push_back({7,
					32,
					4,
					3,
					10,
					tm::sparse | tm::read_random_order | tm::unaligned_read,
					backend,
					torrent_type::v2});
				// hybrid is the only case where the v1 piece size differs from the
				// v2 one, the tail being pad bytes
				tests.push_back({7,
					32,
					4,
					3,
					10,
					tm::sparse | tm::read_random_order | tm::unaligned_read,
					backend,
					torrent_type::hybrid});
				tests.push_back({7, 32, 4, 3, 10, tm::sparse | tm::hot_read, backend});
				continue;
			}

			// the full test suite

			// files, queue, threads, read-mult, pool, flags, disk_backend
			tests.push_back({20, 32, 16, 3, 10, tm::sparse | tm::even_file_sizes, backend});
			tests.push_back({20, 32, 16, 3, 10, tm::sparse, backend});
			tests.push_back({20, 32, 16, 3, 10, tm::sparse | tm::read_random_order, backend});
			tests.push_back({20, 32, 16, 3, 10, tm::sparse | tm::read_random_order | tm::even_file_sizes, backend});
			tests.push_back({20, 32, 16, 3, 10, tm::flush_files | tm::sparse | tm::read_random_order | tm::even_file_sizes, backend});

			// random-offset/size reads of completed pieces, exercising the disk
			// back-end's unaligned/spanning read path
			tests.push_back({20,
				32,
				16,
				3,
				10,
				tm::sparse | tm::read_random_order | tm::unaligned_read,
				backend});

			// v2 torrents
			tests.push_back(
				{20, 32, 16, 3, 10, tm::sparse | tm::read_random_order, backend, torrent_type::v2});
			tests.push_back({20,
				32,
				16,
				3,
				10,
				tm::sparse | tm::read_random_order | tm::unaligned_read,
				backend,
				torrent_type::v2});

			// hybrid torrents, whose pieces have a pad-byte tail
			tests.push_back({20,
				32,
				16,
				3,
				10,
				tm::sparse | tm::read_random_order,
				backend,
				torrent_type::hybrid});
			tests.push_back({20,
				32,
				16,
				3,
				10,
				tm::sparse | tm::read_random_order | tm::unaligned_read,
				backend,
				torrent_type::hybrid});

			// reads of blocks that may still be in the write cache
			tests.push_back({20, 32, 16, 3, 10, tm::sparse | tm::hot_read, backend});
			tests.push_back({20,
				32,
				16,
				3,
				10,
				tm::sparse | tm::hot_read | tm::unaligned_read,
				backend,
				torrent_type::v2});

			// test with small pool size
			tests.push_back({10, 32, 16, 3, 1, tm::sparse | tm::read_random_order, backend});

			// test with many threads pool size
			tests.push_back({10, 32, 64, 3, 9, tm::sparse | tm::read_random_order, backend});
		}

		int ret = 0;
		for (auto const& t : tests)
			ret |= run_test(t);

		return ret;
	}

	// strip program name
	argc -= 1;
	argv += 1;
	test_case tc{20, 32, 16, 3, 10, test_mode::sparse, "default"};
	while (argc > 0)
	{
		lt::string_view opt(argv[0]);

		if (opt == "-h" || opt == "--help")
		{
			print_usage();
			return 0;
		}

		if (opt.substr(0, 1) == "-")
		{
			if (argc < 2)
			{
				std::cerr << "missing value associated with \"" << opt << "\"\n";
				print_usage();
				return 1;
			}
			if (opt == "-f")
				tc.num_files = std::atoi(argv[1]);
			else if (opt == "-q")
				tc.queue_size = std::atoi(argv[1]);
			else if (opt == "-t")
				tc.num_threads = std::atoi(argv[1]);
			else if (opt == "-r")
				tc.read_multiplier = std::atoi(argv[1]);
			else if (opt == "-p")
				tc.file_pool_size = std::atoi(argv[1]);
			else if (opt == "-d")
				tc.disk_backend = argv[1];
			else if (opt == "-V")
			{
				lt::string_view const v(argv[1]);
				if (v == "v1")
					tc.version = torrent_type::v1;
				else if (v == "v2")
					tc.version = torrent_type::v2;
				else if (v == "hybrid")
					tc.version = torrent_type::hybrid;
				else
				{
					std::cerr << "invalid torrent version \"" << v << "\"\n";
					print_usage();
					return 1;
				}
			}
			else
			{
				std::cerr << "unknown option \"" << opt << "\"\n";
				print_usage();
				return 1;
			}

			argc -= 1;
			argv += 1;
		}
		else if (opt == "alloc")
			tc.flags &= ~test_mode::sparse;
		else if (opt == "even-size")
			tc.flags |= test_mode::even_file_sizes;
		else if (opt == "random-read")
			tc.flags |= test_mode::read_random_order;
		else if (opt == "flush")
			tc.flags |= test_mode::flush_files;
		else if (opt == "clear")
			tc.flags |= test_mode::clear_pieces;
		else if (opt == "unaligned-read")
			tc.flags |= test_mode::unaligned_read;
		else if (opt == "hot-read")
			tc.flags |= test_mode::hot_read;
		else
		{
			std::cerr << "unknown option \"" << opt << "\"\n";
			print_usage();
			return 1;
		}

		argc -= 1;
		argv += 1;
	}

	return run_test(tc);
}
