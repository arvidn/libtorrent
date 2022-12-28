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

TORRENT_TEST(transfer_matrix)
{
	using fm = existing_files_mode;

	fm const files = fm::partial_valid;

	for (test_transfer_flags_t piece_size : {test_transfer_flags_t{}, tx::odd_pieces, tx::small_pieces, tx::large_pieces})
		for (test_transfer_flags_t web_seed : {tx::web_seed, test_transfer_flags_t{}})
			for (test_transfer_flags_t corruption : {test_transfer_flags_t{}, tx::corruption})
				for (test_transfer_flags_t bt_version : {test_transfer_flags_t{}, tx::v2_only, tx::v1_only})
					for (test_transfer_flags_t magnet : {test_transfer_flags_t{}, tx::magnet_download})
						for (test_transfer_flags_t multi_file : {test_transfer_flags_t{}, tx::multiple_files})
						{
							if (run_matrix_test(piece_size | bt_version | magnet
								| multi_file | web_seed | corruption, files))
								return;
						}
}

