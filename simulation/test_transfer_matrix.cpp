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

#include "transfer_sim.hpp"

void run_matrix_test(test_transfer_flags_t const flags, existing_files_mode const files, bool const corruption)
{
	std::cout << "\n\nTEST CASE: "
		<< ((flags & tx::small_pieces) ? "small-pieces" : (flags & tx::large_pieces) ? "large-pieces" : "normal-pieces")
		<< "-" << (corruption ? "corruption" : "valid")
		<< "-" << ((flags & tx::v2_only) ? "v2_only" : (flags & tx::v1_only) ? "v1_only" : "hybrid")
		<< "-" << ((flags & tx::magnet_download) ? "magnet" : "torrent")
		<< "-" << ((flags & tx::multiple_files) ? "multi_file" : "single_file")
		<< "-" << files
		<< "\n\n";

	auto downloader_disk = test_disk().set_files(files);
	auto seeder_disk = test_disk();
	if (corruption) seeder_disk = seeder_disk.send_corrupt_data(num_pieces(flags) / 4 * blocks_per_piece(flags));
	std::set<lt::piece_index_t> passed;
	run_test(no_init
		, record_finished_pieces(passed)
		, expect_seed(!corruption)
		, flags
		, downloader_disk
		, seeder_disk
		);

	int const expected_pieces = num_pieces(flags);

	// we we send some corrupt pieces, it's not straight-forward to predict
	// exactly how many will pass the hash check, since a failure will cause
	// a re-request and also a request of the block hashes (for v2 torrents)
	if (corruption)
	{
		TEST_CHECK(int(passed.size()) < expected_pieces);
	}
	else
	{
		TEST_EQUAL(int(passed.size()), expected_pieces);
	}
}

TORRENT_TEST(transfer_matrix)
{
	using fm = existing_files_mode;

	for (test_transfer_flags_t piece_size : {test_transfer_flags_t{}, tx::small_pieces, tx::large_pieces})
		for (bool corruption : {false, true})
			for (test_transfer_flags_t bt_version : {test_transfer_flags_t{}, tx::v2_only, tx::v1_only})
				for (test_transfer_flags_t magnet : {test_transfer_flags_t{}, tx::magnet_download})
					for (test_transfer_flags_t multi_file : {test_transfer_flags_t{}, tx::multiple_files})
						for (fm files : {fm::no_files, fm::full_invalid, fm::partial_valid})
						{
							// this will clear the history of all output we've printed so far.
							// if we encounter an error from now on, we'll only print the relevant
							// iteration
							::unit_test::reset_output();

							// re-seed the random engine each iteration, to make the runs
							// deterministic
							lt::aux::random_engine().seed(0x23563a7f);

							run_matrix_test(piece_size | bt_version | magnet | multi_file, files, corruption);
							if (::unit_test::g_test_failures > 0) return;
						}
}

