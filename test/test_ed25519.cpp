/*

Copyright (c) 2016, Alden Torres
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

#ifndef TORRENT_DISABLE_DHT

#include <memory>

#include "libtorrent/ed25519.hpp"
#include "libtorrent/hex.hpp"

#include "test.hpp"

using namespace libtorrent;

namespace
{
	void from_hex(std::string s, unsigned char* out)
	{
		aux::from_hex(s, reinterpret_cast<char*>(out));
	}

	void test_vector(std::string seed, std::string pub, std::string signature, std::string message)
	{
		unsigned char s[32];
		unsigned char sk[64];
		unsigned char pk[32];
		unsigned char sig[64];
		int msg_size = int(message.size()) / 2;
		std::vector<unsigned char> msg(msg_size);

		from_hex(seed, s);
		ed25519_create_keypair(pk, sk, s);

		TEST_EQUAL(aux::to_hex({reinterpret_cast<char const*>(pk), 32}), pub);

		from_hex(message, msg.data());
		ed25519_sign(sig, msg.data(), msg_size, pk, sk);

		TEST_EQUAL(aux::to_hex({reinterpret_cast<char const*>(sig), 64}), signature);

		int r = ed25519_verify(sig, msg.data(), msg_size, pk);

		TEST_CHECK(r);
	}
}

// https://git.gnupg.org/cgi-bin/gitweb.cgi?p=libgcrypt.git;a=blob;f=tests/t-ed25519.inp;hb=HEAD
TORRENT_TEST(ed25519_test_vec1)
{
	// TST: 2
	test_vector(
		  "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb"
		, "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"
		, "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
		  "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"
		, "72"
	);

	// TST: 3
	test_vector(
		  "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7"
		, "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025"
		, "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
		  "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"
		, "af82"
	);

	// TST: 4
	test_vector(
		  "0d4a05b07352a5436e180356da0ae6efa0345ff7fb1572575772e8005ed978e9"
		, "e61a185bcef2613a6c7cb79763ce945d3b245d76114dd440bcf5f2dc1aa57057"
		, "d9868d52c2bebce5f3fa5a79891970f309cb6591e3e1702a70276fa97c24b3a8"
		  "e58606c38c9758529da50ee31b8219cba45271c689afa60b0ea26c99db19b00c"
		, "cbc77b"
	);

	// TST: 47
	test_vector(
		  "89f0d68299ba0a5a83f248ae0c169f8e3849a9b47bd4549884305c9912b46603"
		, "aba3e795aab2012acceadd7b3bd9daeeed6ff5258bdcd7c93699c2a3836e3832"
		, "2c691fa8d487ce20d5d2fa41559116e0bbf4397cf5240e152556183541d66cf7"
		  "53582401a4388d390339dbef4d384743caa346f55f8daba68ba7b9131a8a6e0b"
		, "4f1846dd7ad50e545d4cfbffbb1dc2ff145dc123754d08af4e44ecc0bc8c9141"
		  "1388bc7653e2d893d1eac2107d05"
	);

	// TST: 48
	test_vector(
		  "0a3c1844e2db070fb24e3c95cb1cc6714ef84e2ccd2b9dd2f1460ebf7ecf13b1"
		, "72e409937e0610eb5c20b326dc6ea1bbbc0406701c5cd67d1fbde09192b07c01"
		, "87f7fdf46095201e877a588fe3e5aaf476bd63138d8a878b89d6ac60631b3458"
		  "b9d41a3c61a588e1db8d29a5968981b018776c588780922f5aa732ba6379dd05"
		, "4c8274d0ed1f74e2c86c08d955bde55b2d54327e82062a1f71f70d536fdc8722"
		  "cdead7d22aaead2bfaa1ad00b82957"
	);

	// TST: 49
	test_vector(
		  "c8d7a8818b98dfdb20839c871cb5c48e9e9470ca3ad35ba2613a5d3199c8ab23"
		, "90d2efbba4d43e6b2b992ca16083dbcfa2b322383907b0ee75f3e95845d3c47f"
		, "fa2e994421aef1d5856674813d05cbd2cf84ef5eb424af6ecd0dc6fdbdc2fe60"
		  "5fe985883312ecf34f59bfb2f1c9149e5b9cc9ecda05b2731130f3ed28ddae0b"
		, "783e33c3acbdbb36e819f544a7781d83fc283d3309f5d3d12c8dcd6b0b3d0e89"
		  "e38cfd3b4d0885661ca547fb9764abff"
	);

	// TST: 50
	test_vector(
		  "b482703612d0c586f76cfcb21cfd2103c957251504a8c0ac4c86c9c6f3e429ff"
		, "fd711dc7dd3b1dfb9df9704be3e6b26f587fe7dd7ba456a91ba43fe51aec09ad"
		, "58832bdeb26feafc31b46277cf3fb5d7a17dfb7ccd9b1f58ecbe6feb97966682"
		  "8f239ba4d75219260ecac0acf40f0e5e2590f4caa16bbbcd8a155d347967a607"
		, "29d77acfd99c7a0070a88feb6247a2bce9984fe3e6fbf19d4045042a21ab26cb"
		  "d771e184a9a75f316b648c6920db92b87b"
	);

	// TST: 51
	test_vector(
		  "84e50dd9a0f197e3893c38dbd91fafc344c1776d3a400e2f0f0ee7aa829eb8a2"
		, "2c50f870ee48b36b0ac2f8a5f336fb090b113050dbcc25e078200a6e16153eea"
		, "69e6a4491a63837316e86a5f4ba7cd0d731ecc58f1d0a264c67c89befdd8d382"
		  "9d8de13b33cc0bf513931715c7809657e2bfb960e5c764c971d733746093e500"
		, "f3992cde6493e671f1e129ddca8038b0abdb77bb9035f9f8be54bd5d68c1aeff"
		  "724ff47d29344391dc536166b8671cbbf123"
	);

	// TST: 52
	test_vector(
		  "b322d46577a2a991a4d1698287832a39c487ef776b4bff037a05c7f1812bdeec"
		, "eb2bcadfd3eec2986baff32b98e7c4dbf03ff95d8ad5ff9aa9506e5472ff845f"
		, "c7b55137317ca21e33489ff6a9bfab97c855dc6f85684a70a9125a261b56d5e6"
		  "f149c5774d734f2d8debfc77b721896a8267c23768e9badb910eef83ec258802"
		, "19f1bf5dcf1750c611f1c4a2865200504d82298edd72671f62a7b1471ac3d4a3"
		  "0f7de9e5da4108c52a4ce70a3e114a52a3b3c5"
	);

	// TST: 53
	test_vector(
		  "960cab5034b9838d098d2dcbf4364bec16d388f6376d73a6273b70f82bbc98c0"
		, "5e3c19f2415acf729f829a4ebd5c40e1a6bc9fbca95703a9376087ed0937e51a"
		, "27d4c3a1811ef9d4360b3bdd133c2ccc30d02c2f248215776cb07ee4177f9b13"
		  "fc42dd70a6c2fed8f225c7663c7f182e7ee8eccff20dc7b0e1d5834ec5b1ea01"
		, "f8b21962447b0a8f2e4279de411bea128e0be44b6915e6cda88341a68a0d8183"
		  "57db938eac73e0af6d31206b3948f8c48a447308"
	);
}

#endif // TORRENT_DISABLE_DHT
