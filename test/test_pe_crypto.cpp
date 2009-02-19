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
#include <iostream>

#include "libtorrent/hasher.hpp"
#include "libtorrent/pe_crypto.hpp"
#include "libtorrent/session.hpp"
#include <boost/filesystem/convenience.hpp>

#include "setup_transfer.hpp"
#include "test.hpp"

#ifndef TORRENT_DISABLE_ENCRYPTION

void display_pe_policy(libtorrent::pe_settings::enc_policy policy)
{
	using namespace libtorrent;
	using std::cerr;
	
	if (policy == pe_settings::disabled) cerr << "disabled ";
	else if (policy == pe_settings::enabled) cerr << "enabled ";
	else if (policy == pe_settings::forced) cerr << "forced ";
}

void display_pe_settings(libtorrent::pe_settings s)
{
	using namespace libtorrent;
	using std::cerr;
	
	cerr << "out_enc_policy - ";
	display_pe_policy(s.out_enc_policy);
	cerr << "\tin_enc_policy - ";
	display_pe_policy(s.in_enc_policy);
	
	cerr << "\nenc_level - ";
	if (s.allowed_enc_level == pe_settings::plaintext) cerr << "plaintext ";
	else if (s.allowed_enc_level == pe_settings::rc4) cerr << "rc4 ";
	else if (s.allowed_enc_level == pe_settings::both) cerr << "both ";
	
	cerr << "\t\tprefer_rc4 - ";
	(s.prefer_rc4) ? cerr << "true" : cerr << "false";
	cerr << "\n\n";
}

void test_transfer(libtorrent::pe_settings::enc_policy policy,
		   libtorrent::pe_settings::enc_level level = libtorrent::pe_settings::both,
		   bool pref_rc4 = false)
{
	using namespace libtorrent;
	using std::cerr;

	session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48800, 49000));
	session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49800, 50000));
	pe_settings s;
	
	s.out_enc_policy = libtorrent::pe_settings::enabled;
	s.in_enc_policy = libtorrent::pe_settings::enabled;
	
	s.allowed_enc_level = pe_settings::both;
	ses2.set_pe_settings(s);

	s.out_enc_policy = policy;
	s.in_enc_policy = policy;
	s.allowed_enc_level = level;
	s.prefer_rc4 = pref_rc4;
	ses1.set_pe_settings(s);

// 	s = ses1.get_pe_settings();
// 	cerr << " Session1 \n";
// 	display_pe_settings(s);
// 	s = ses2.get_pe_settings();
// 	cerr << " Session2 \n";
// 	display_pe_settings(s);

	torrent_handle tor1;
	torrent_handle tor2;

	using boost::tuples::ignore;
	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0, true, false, true, "_pe");	

	std::cerr << "waiting for transfer to complete\n";

	for (int i = 0; i < 50; ++i)
	{
		tor2.status();
		print_alerts(ses1, "ses1");
		print_alerts(ses2, "ses2");

		if (tor2.is_seed()) break;
		test_sleep(1000);
	}

	TEST_CHECK(tor2.is_seed());
 	if (tor2.is_seed()) std::cerr << "done\n";
	ses1.remove_torrent(tor1);
	ses2.remove_torrent(tor2);

	using boost::filesystem::remove_all;
	remove_all("./tmp1_pe");
	remove_all("./tmp2_pe");
	remove_all("./tmp3_pe");
}


int test_main()
{
	using namespace libtorrent;
	int repcount = 1024;

	for (int rep = 0; rep < repcount; ++rep)
	{
		dh_key_exchange DH1, DH2;
		
		DH1.compute_secret(DH2.get_local_key());
		DH2.compute_secret(DH1.get_local_key());
		
		TEST_CHECK(std::equal(DH1.get_secret(), DH1.get_secret() + 96, DH2.get_secret()));
	}

	dh_key_exchange DH1, DH2;
	DH1.compute_secret(DH2.get_local_key());
	DH2.compute_secret(DH1.get_local_key());

	TEST_CHECK(std::equal(DH1.get_secret(), DH1.get_secret() + 96, DH2.get_secret()));

	sha1_hash test1_key = hasher("test1_key",8).final();
	sha1_hash test2_key = hasher("test2_key",8).final();

	RC4_handler RC41(test2_key, test1_key);
	RC4_handler RC42(test1_key, test2_key);

	for (int rep = 0; rep < repcount; ++rep)
	{
		std::size_t buf_len = rand() % (512 * 1024);
		char* buf = new char[buf_len];
		char* zero_buf = new char[buf_len];
		
		std::fill(buf, buf + buf_len, 0);
		std::fill(zero_buf, zero_buf + buf_len, 0);
		
		RC41.encrypt(buf, buf_len);
		RC42.decrypt(buf, buf_len);
		TEST_CHECK(std::equal(buf, buf + buf_len, zero_buf));
		
		RC42.encrypt(buf, buf_len);
		RC41.decrypt(buf, buf_len);
		TEST_CHECK(std::equal(buf, buf + buf_len, zero_buf));
		
		delete[] buf;
		delete[] zero_buf;
	}

	
	test_transfer(pe_settings::disabled);

	test_transfer(pe_settings::forced, pe_settings::plaintext);
	test_transfer(pe_settings::forced, pe_settings::rc4);
	test_transfer(pe_settings::forced, pe_settings::both, false);
	test_transfer(pe_settings::forced, pe_settings::both, true);

	test_transfer(pe_settings::enabled, pe_settings::plaintext);
	test_transfer(pe_settings::enabled, pe_settings::rc4);
	test_transfer(pe_settings::enabled, pe_settings::both, false);
	test_transfer(pe_settings::enabled, pe_settings::both, true);

	return 0;
}

#else

int test_main()
{
	std::cerr << "PE test not run because it's disabled" << std::endl;
	return 0;
}

#endif

