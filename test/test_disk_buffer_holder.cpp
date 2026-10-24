/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/disk_interface.hpp" // default_block_size
#include "libtorrent/span.hpp"

#include "test.hpp"

#include <set>
#include <utility>
#include <vector>

namespace {

// records the buffers freed through it, so a test can tell the allocator
// pointer survived the exclusive-bit tagging
struct recording_allocator final : lt::buffer_allocator_interface
{
	void free_disk_buffer(char* b) override
	{
		freed.push_back(b);
		delete[] b;
	}

	void free_multiple_buffers(lt::span<char*> bufs) override
	{
		for (char* b : bufs)
			free_disk_buffer(b);
	}

	char* allocate() { return new char[lt::default_block_size]; }

	std::vector<char*> freed;
};

} // anonymous namespace

TORRENT_TEST(disk_buffer_holder_exclusive_flag)
{
	recording_allocator alloc;

	// the two-argument overload implies non-exclusive
	{
		lt::disk_buffer_holder h(alloc, alloc.allocate());
		TEST_CHECK(bool(h));
		TEST_CHECK(!h.is_exclusive());
	}

	{
		lt::disk_buffer_holder h(alloc, alloc.allocate(), false);
		TEST_CHECK(!h.is_exclusive());
	}

	{
		lt::disk_buffer_holder h(alloc, alloc.allocate(), true);
		TEST_CHECK(h.is_exclusive());
	}

	// a holder with no allocator must report the conservative answer
	{
		lt::disk_buffer_holder h;
		TEST_CHECK(!bool(h));
		TEST_CHECK(!h.is_exclusive());
	}

	TEST_EQUAL(alloc.freed.size(), 3);
}

// the exclusive flag is tagged into the low bit of the allocator pointer, so
// every operation moving that pointer must preserve both. A masking slip
// corrupts the virtual call in free_disk_buffer()
TORRENT_TEST(disk_buffer_holder_exclusive_survives_move)
{
	recording_allocator alloc;

	for (bool const exclusive : {false, true})
	{
		char* const buf = alloc.allocate();
		lt::disk_buffer_holder h(alloc, buf, exclusive);

		lt::disk_buffer_holder moved(std::move(h));
		TEST_CHECK(moved.is_exclusive() == exclusive);
		TEST_EQUAL(moved.data(), buf);

		lt::disk_buffer_holder assigned;
		assigned = std::move(moved);
		TEST_CHECK(assigned.is_exclusive() == exclusive);
		TEST_EQUAL(assigned.data(), buf);

		alloc.freed.clear();
		assigned.reset();
		TEST_EQUAL(alloc.freed.size(), 1);
		TEST_EQUAL(alloc.freed.front(), buf);
	}
}

TORRENT_TEST(disk_buffer_holder_exclusive_survives_swap)
{
	recording_allocator alloc;

	char* const excl_buf = alloc.allocate();
	char* const shared_buf = alloc.allocate();

	lt::disk_buffer_holder excl(alloc, excl_buf, true);
	lt::disk_buffer_holder shared(alloc, shared_buf, false);

	excl.swap(shared);

	// each holder keeps its own exclusivity, alongside its own buffer
	TEST_CHECK(!excl.is_exclusive());
	TEST_EQUAL(excl.data(), shared_buf);
	TEST_CHECK(shared.is_exclusive());
	TEST_EQUAL(shared.data(), excl_buf);

	excl.reset();
	shared.reset();
	TEST_EQUAL(alloc.freed.size(), 2);
	std::set<char*> const freed(alloc.freed.begin(), alloc.freed.end());
	TEST_CHECK(freed == std::set<char*>({excl_buf, shared_buf}));
}

// an empty holder must not claim exclusive access to a buffer it no longer
// has. Otherwise a caller that asks is_exclusive() before looking at data()
// is told it may modify a buffer that is not there
TORRENT_TEST(disk_buffer_holder_empty_is_not_exclusive)
{
	recording_allocator alloc;

	{
		lt::disk_buffer_holder h(alloc, alloc.allocate(), true);
		lt::disk_buffer_holder moved(std::move(h));

		TEST_CHECK(!bool(h));
		TEST_CHECK(!h.is_exclusive());
	}

	{
		lt::disk_buffer_holder h(alloc, alloc.allocate(), true);
		h.reset();

		TEST_CHECK(!bool(h));
		TEST_CHECK(!h.is_exclusive());

		// reset() leaves the holder in a default-constructed state, so it must
		// also be safe to reset again
		h.reset();
		TEST_CHECK(!h.is_exclusive());
	}

	{
		lt::disk_buffer_holder h(alloc, alloc.allocate(), true);
		lt::disk_buffer_holder other(alloc, alloc.allocate(), true);
		other = std::move(h);

		TEST_CHECK(!bool(h));
		TEST_CHECK(!h.is_exclusive());
	}

	TEST_EQUAL(alloc.freed.size(), 4);
}

// disk_buffer_ref takes the buffer and leaves no allocator behind, so the
// holder it was built from must not report exclusivity either
TORRENT_TEST(disk_buffer_holder_ref_clears_exclusive)
{
	recording_allocator alloc;

	char* const buf = alloc.allocate();
	lt::disk_buffer_holder h(alloc, buf, true);

	lt::bulk_free_buffer to_free(alloc);
	{
		lt::disk_buffer_ref ref(std::move(h));
		TEST_CHECK(!bool(h));
		TEST_CHECK(!h.is_exclusive());
		to_free.add(std::move(ref));
	}

	// the holder no longer owns the buffer, so going out of scope frees nothing
	TEST_CHECK(alloc.freed.empty());
}
