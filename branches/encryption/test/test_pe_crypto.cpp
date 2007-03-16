/*

Copyright (c) 2007, Un Shyam
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

#include <algorithm>

#include "libtorrent/hasher.hpp"
#include "libtorrent/pe_crypto.hpp"

#include "test.hpp"

int test_main()
{
	using namespace libtorrent;

	DH_key_exchange DH1, DH2;

	DH1.compute_secret(DH2.get_local_key());
	DH2.compute_secret(DH1.get_local_key());

	TEST_CHECK(std::equal(DH1.get_secret(), DH1.get_secret() + 96, DH2.get_secret()));
	
	sha1_hash test1_key = hasher("test1_key",8).final();
	sha1_hash test2_key = hasher("test2_key",8).final();

	RC4_handler RC41 (test2_key, test1_key);
	RC4_handler RC42 (test1_key, test2_key);

	for (int rep = 0; rep < 64; ++rep)
	{
		std::size_t buf_len = rand() % (512 * 1024);
		char* buf = new char[buf_len];
		char* zero_buf = new char[buf_len];
		
		std::fill(buf, buf + sizeof(buf), 0);
		std::fill(zero_buf, zero_buf + sizeof(zero_buf), 0);
		
		RC41.encrypt(buf, buf_len);
		RC42.decrypt(buf, buf_len);
		TEST_CHECK(std::equal(buf, buf + sizeof(buf), zero_buf));
		
		RC42.encrypt(buf, buf_len);
		RC41.decrypt(buf, buf_len);
		TEST_CHECK(std::equal(buf, buf + sizeof(buf), zero_buf));
		
		delete[] buf;
		delete[] zero_buf;
	}
	return 0;
}
