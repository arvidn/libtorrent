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
#include "libtorrent/random.hpp"

#include "setup_transfer.hpp"
#include "test.hpp"

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)

char const* pe_policy(boost::uint8_t policy)
{
	using namespace libtorrent;
	
	if (policy == settings_pack::pe_disabled) return "disabled";
	else if (policy == settings_pack::pe_enabled) return "enabled";
	else if (policy == settings_pack::pe_forced) return "forced";
	return "unknown";
}

void display_settings(libtorrent::settings_pack const& s)
{
	using namespace libtorrent;
	
	fprintf(stderr, "out_enc_policy - %s\tin_enc_policy - %s\n"
		, pe_policy(s.get_int(settings_pack::out_enc_policy))
		, pe_policy(s.get_int(settings_pack::in_enc_policy)));
	
	fprintf(stderr, "enc_level - %s\t\tprefer_rc4 - %s\n"
		, s.get_int(settings_pack::allowed_enc_level) == settings_pack::pe_plaintext ? "plaintext"
		: s.get_int(settings_pack::allowed_enc_level) == settings_pack::pe_rc4 ? "rc4"
		: s.get_int(settings_pack::allowed_enc_level) == settings_pack::pe_both ? "both" : "unknown"
		, s.get_bool(settings_pack::prefer_rc4) ? "true": "false");
}

void test_transfer(libtorrent::settings_pack::enc_policy policy
	, int timeout
	, libtorrent::settings_pack::enc_level level = libtorrent::settings_pack::pe_both
	, bool pref_rc4 = false)
{
	using namespace libtorrent;
	namespace lt = libtorrent;

	// these are declared before the session objects
	// so that they are destructed last. This enables
	// the sessions to destruct in parallel
	session_proxy p1;
	session_proxy p2;

	lt::session ses1(fingerprint("LT", 0, 1, 0, 0), std::make_pair(48800, 49000), "0.0.0.0", 0);
	lt::session ses2(fingerprint("LT", 0, 1, 0, 0), std::make_pair(49800, 50000), "0.0.0.0", 0);
	settings_pack s;
	
	s.set_int(settings_pack::out_enc_policy, settings_pack::pe_enabled);
	s.set_int(settings_pack::in_enc_policy, settings_pack::pe_enabled);
	s.set_int(settings_pack::allowed_enc_level, settings_pack::pe_both);
	ses2.apply_settings(s);

	fprintf(stderr, " Session2 \n");
	display_settings(s);

	s.set_int(settings_pack::out_enc_policy, policy);
	s.set_int(settings_pack::in_enc_policy, policy);
	s.set_int(settings_pack::allowed_enc_level, level);
	s.set_bool(settings_pack::prefer_rc4, pref_rc4);
	ses1.apply_settings(s);

	fprintf(stderr, " Session1 \n");
	display_settings(s);

	torrent_handle tor1;
	torrent_handle tor2;

	using boost::tuples::ignore;
	boost::tie(tor1, tor2, ignore) = setup_transfer(&ses1, &ses2, 0, true, false, true
		, "_pe", 16 * 1024, 0, false, 0, true);	

	fprintf(stderr, "waiting for transfer to complete\n");

	for (int i = 0; i < timeout * 10; ++i)
	{
		torrent_status s = tor2.status();
		print_alerts(ses1, "ses1");
		print_alerts(ses2, "ses2");

		if (s.is_seeding) break;
		test_sleep(100);
	}

	TEST_CHECK(tor2.status().is_seeding);
 	if (tor2.status().is_seeding) fprintf(stderr, "done\n");
	ses1.remove_torrent(tor1);
	ses2.remove_torrent(tor2);

	// this allows shutting down the sessions in parallel
	p1 = ses1.abort();
	p2 = ses2.abort();

	error_code ec;
	remove_all("tmp1_pe", ec);
	remove_all("tmp2_pe", ec);
	remove_all("tmp3_pe", ec);
}

void test_enc_handler(libtorrent::crypto_plugin* a, libtorrent::crypto_plugin* b)
{
#ifdef TORRENT_USE_VALGRIND
	const int repcount = 10;
#else
	const int repcount = 128;
#endif
	for (int rep = 0; rep < repcount; ++rep)
	{
		std::size_t buf_len = rand() % (512 * 1024);
		char* buf = new char[buf_len];
		char* cmp_buf = new char[buf_len];
		
		std::generate(buf, buf + buf_len, &std::rand);
		std::memcpy(cmp_buf, buf, buf_len);
		
		using namespace boost::asio;
		std::vector<mutable_buffer> iovec;
		iovec.push_back(mutable_buffer(buf, buf_len));
		a->encrypt(iovec);
		TEST_CHECK(!std::equal(buf, buf + buf_len, cmp_buf));
		TEST_CHECK(iovec.empty());
		int consume = 0;
		int produce = buf_len;
		int packet_size = 0;
		iovec.push_back(mutable_buffer(buf, buf_len));
		b->decrypt(iovec, consume, produce, packet_size);
		TEST_CHECK(std::equal(buf, buf + buf_len, cmp_buf));
		TEST_CHECK(iovec.empty());
		TEST_EQUAL(consume, 0);
		TEST_EQUAL(produce, buf_len);
		TEST_EQUAL(packet_size, 0);
		
		iovec.push_back(mutable_buffer(buf, buf_len));
		b->encrypt(iovec);
		TEST_CHECK(!std::equal(buf, buf + buf_len, cmp_buf));
		TEST_CHECK(iovec.empty());
		consume = 0;
		produce = buf_len;
		packet_size = 0;
		iovec.push_back(mutable_buffer(buf, buf_len));
		a->decrypt(iovec, consume, produce, packet_size);
		TEST_CHECK(std::equal(buf, buf + buf_len, cmp_buf));
		TEST_CHECK(iovec.empty());
		TEST_EQUAL(consume, 0);
		TEST_EQUAL(produce, buf_len);
		TEST_EQUAL(packet_size, 0);
		
		delete[] buf;
		delete[] cmp_buf;
	}
}

#endif

int test_main()
{
	using namespace libtorrent;

#if !defined(TORRENT_DISABLE_ENCRYPTION) && !defined(TORRENT_DISABLE_EXTENSIONS)

#ifdef TORRENT_USE_VALGRIND
	const int repcount = 10;
#else
	const int repcount = 128;
#endif

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

	fprintf(stderr, "testing RC4 handler\n");
	rc4_handler rc41;
	rc41.set_incoming_key(&test2_key[0], 20);
	rc41.set_outgoing_key(&test1_key[0], 20);
	rc4_handler rc42;
	rc42.set_incoming_key(&test1_key[0], 20);
	rc42.set_outgoing_key(&test2_key[0], 20);
	test_enc_handler(&rc41, &rc42);
	
#ifdef TORRENT_USE_VALGRIND
	const int timeout = 10;
#else
	const int timeout = 5;
#endif

	test_transfer(settings_pack::pe_disabled, timeout);

	test_transfer(settings_pack::pe_forced, timeout, settings_pack::pe_plaintext);
	test_transfer(settings_pack::pe_forced, timeout, settings_pack::pe_rc4);
	test_transfer(settings_pack::pe_forced, timeout, settings_pack::pe_both, false);
	test_transfer(settings_pack::pe_forced, timeout, settings_pack::pe_both, true);

	test_transfer(settings_pack::pe_enabled, timeout, settings_pack::pe_plaintext);
	test_transfer(settings_pack::pe_enabled, timeout, settings_pack::pe_rc4);
	test_transfer(settings_pack::pe_enabled, timeout, settings_pack::pe_both, false);
	test_transfer(settings_pack::pe_enabled, timeout, settings_pack::pe_both, true);
#else
	fprintf(stderr, "PE test not run because it's disabled\n");
#endif

	return 0;
}

