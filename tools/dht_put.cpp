/*

Copyright (c) 2014, Arvid Norberg
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


#include "libtorrent/session.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp" // for bencode()
#include "libtorrent/kademlia/item.hpp" // for sign_mutable_item
#include "libtorrent/kademlia/ed25519.hpp"
#include "libtorrent/span.hpp"

#include <functional>
#include <cstdio> // for snprintf
#include <cinttypes> // for PRId64 et.al.
#include <cstdlib>
#include <fstream>

using namespace lt;
using namespace lt::dht;
using namespace std::placeholders;

#ifdef TORRENT_DISABLE_DHT

int main(int argc, char* argv[])
{
	std::fprintf(stderr, "not built with DHT support\n");
	return 1;
}

#else

std::string to_hex(lt::span<char const> key)
{
	std::string out;
	for (auto const b : key)
	{
		char buf[20];
		std::snprintf(buf, sizeof(3), "%02x", static_cast<unsigned char>(b));
		out += (char*)buf;
	}
	return out;
}

int hex_to_int(char in)
{
	if (in >= '0' && in <= '9') return int(in) - '0';
	if (in >= 'A' && in <= 'F') return int(in) - 'A' + 10;
	if (in >= 'a' && in <= 'f') return int(in) - 'a' + 10;
	return -1;
}

bool from_hex(span<char const> in, span<char> out)
{
	if (in.size() != out.size() * 2) return false;
	auto o = out.begin();
	for (auto i = in.begin(); i != in.end(); ++i, ++o)
	{
		int const t1 = hex_to_int(*i);
		if (t1 == -1) return false;
		++i;
		if (i == in.end()) return false;
		int const t2 = hex_to_int(*i);
		if (t2 == -1) return false;
		*o = (t1 << 4) | (t2 & 0xf);
	}
	return true;
}

namespace {
void usage()
{
	std::fprintf(stderr,
		"USAGE:\ndht <command> <arg>\n\nCOMMANDS:\n"
		"get <hash>                - retrieves and prints out the immutable\n"
		"                            item stored under hash.\n"
		"put <string>              - puts the specified string as an immutable\n"
		"                            item onto the DHT. The resulting target hash\n"
		"gen-key <key-file>        - generate ed25519 keypair and save it in\n"
		"                            the specified file\n"
		"dump-key <key-file>       - dump ed25519 keypair from the specified key\n"
		"                            file.\n"
		"mput <key-file> <string> [salt]\n"
		"                          - puts the specified string as a mutable\n"
		"                            object under the public key in key-file,\n"
		"                            and optionally specified salt\n"
		"mget <public-key> [salt]  - get a mutable object under the specified\n"
		"                            public key, and salt (optional)\n"
		);
	exit(1);
}

alert* wait_for_alert(lt::session& s, int alert_type)
{
	alert* ret = nullptr;
	bool found = false;
	while (!found)
	{
		s.wait_for_alert(seconds(5));

		std::vector<alert*> alerts;
		s.pop_alerts(&alerts);
		for (auto const a : alerts)
		{
			if (a->type() != alert_type)
			{
				static int spinner = 0;
				static const char anim[] = {'-', '\\', '|', '/'};
				std::printf("\r%c", anim[spinner]);
				std::fflush(stdout);
				spinner = (spinner + 1) & 3;
				//print some alerts?
				continue;
			}
			ret = a;
			found = true;
		}
	}
	std::printf("\n");
	return ret;
}

void put_string(entry& e, std::array<char, 64>& sig
	, std::int64_t& seq
	, std::string const& salt
	, std::array<char, 32> const& pk
	, std::array<char, 64> const& sk
	, char const* str)
{
	using lt::dht::sign_mutable_item;

	e = std::string(str);
	std::vector<char> buf;
	bencode(std::back_inserter(buf), e);
	dht::signature sign;
	++seq;
	sign = sign_mutable_item(buf, salt, dht::sequence_number(seq)
		, dht::public_key(pk.data())
		, dht::secret_key(sk.data()));
	sig = sign.bytes;
}

void bootstrap(lt::session& s)
{
	std::printf("bootstrapping\n");
	wait_for_alert(s, dht_bootstrap_alert::alert_type);
	std::printf("bootstrap done.\n");
}

int dump_key(char const* filename)
{
	std::array<char, 32> seed;

	std::fstream f(filename, std::ios_base::in | std::ios_base::binary);
	f.read(seed.data(), seed.size());
	if (f.fail())
	{
		std::fprintf(stderr, "invalid key file.\n");
		return 1;
	}

	public_key pk;
	secret_key sk;
	std::tie(pk, sk) = ed25519_create_keypair(seed);

	std::printf("public key: %s\nprivate key: %s\n"
		, to_hex(pk.bytes).c_str()
		, to_hex(sk.bytes).c_str());

	return 0;
}

int generate_key(char const* filename)
{
	std::array<char, 32> seed = ed25519_create_seed();

	std::fstream f(filename, std::ios_base::out | std::ios_base::binary);
	f.write(seed.data(), seed.size());
	if (f.fail())
	{
		std::fprintf(stderr, "failed to write key file.\n");
		return 1;
	}

	return 0;
}

void load_dht_state(lt::session& s)
{
	std::fstream f(".dht", std::ios_base::in | std::ios_base::binary | std::ios_base::ate);

	auto const size = f.tellg();
	if (static_cast<int>(size) <= 0) return;
	f.seekg(0, std::ios_base::beg);

	std::vector<char> state;
	state.resize(static_cast<std::size_t>(size));

	f.read(state.data(), size);
	if (f.fail())
	{
		std::fprintf(stderr, "failed to read .dht\n");
		return;
	}

	error_code ec;
	bdecode_node e = bdecode(state, ec);
	if (ec)
	{
		std::fprintf(stderr, "failed to parse .dht file: (%d) %s\n"
			, ec.value(), ec.message().c_str());
	}
	else
	{
		std::printf("load dht state from .dht\n");
		s.load_state(e);
	}
}

int save_dht_state(lt::session& s)
{
	entry e;
	s.save_state(e, session::save_dht_state);
	std::vector<char> state;
	bencode(std::back_inserter(state), e);

	std::fstream f(".dht", std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
	f.write(state.data(), static_cast<std::streamsize>(state.size()));
	return 0;
}
} // anonymous namespace

int main(int argc, char* argv[])
{
	// skip pointer to self
	++argv;
	--argc;

	if (argc < 1) usage();

	if (argv[0] == "dump-key"_sv)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		return dump_key(argv[0]);
	}

	if (argv[0] == "gen-key"_sv)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		return generate_key(argv[0]);
	}

	settings_pack sett;
	sett.set_bool(settings_pack::enable_dht, false);
	sett.set_int(settings_pack::alert_mask, 0x7fffffff);
	lt::session s(sett);

	sett.set_bool(settings_pack::enable_dht, true);
	s.apply_settings(sett);

	load_dht_state(s);

	if (argv[0] == "get"_sv)
	{
		++argv;
		--argc;

		if (argc < 1) usage();

		if (strlen(argv[0]) != 40)
		{
			std::fprintf(stderr, "the hash is expected to be 40 hex characters\n");
			usage();
		}
		sha1_hash target;
		if (!from_hex({argv[0], 40}, target))
		{
			std::fprintf(stderr, "invalid hex encoding of target hash\n");
			return 1;
		}

		bootstrap(s);
		s.dht_get_item(target);

		std::printf("GET %s\n", to_hex(target).c_str());

		alert* a = wait_for_alert(s, dht_immutable_item_alert::alert_type);

		dht_immutable_item_alert* item = alert_cast<dht_immutable_item_alert>(a);

		std::string str = item->item.to_string();
		std::printf("%s\n", str.c_str());
	}
	else if (argv[0] == "put"_sv)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		entry data;
		data = std::string(argv[0]);

		bootstrap(s);
		sha1_hash const target = s.dht_put_item(data);

		std::printf("PUT %s\n", to_hex(target).c_str());

		alert* a = wait_for_alert(s, dht_put_alert::alert_type);
		dht_put_alert* pa = alert_cast<dht_put_alert>(a);
		std::printf("%s\n", pa->message().c_str());
	}
	else if (argv[0] == "mput"_sv)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		std::array<char, 32> seed;

		std::fstream f(argv[0], std::ios_base::in | std::ios_base::binary);
		f.read(seed.data(), seed.size());

		++argv;
		--argc;
		if (argc < 1) usage();

		public_key pk;
		secret_key sk;
		std::tie(pk, sk) = ed25519_create_keypair(seed);
		std::string salt;

		if (argc > 1) salt = argv[1];

		bootstrap(s);
		s.dht_put_item(pk.bytes, std::bind(&put_string, _1, _2, _3, _4
			, pk.bytes, sk.bytes, argv[0]), salt);

		std::printf("MPUT public key: %s [salt: %s]\n", to_hex(pk.bytes).c_str()
			, salt.c_str());

		alert* a = wait_for_alert(s, dht_put_alert::alert_type);
		dht_put_alert* pa = alert_cast<dht_put_alert>(a);
		std::printf("%s\n", pa->message().c_str());
	}
	else if (argv[0] == "mget"_sv)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		auto const len = static_cast<std::ptrdiff_t>(strlen(argv[0]));
		if (len != 64)
		{
			std::fprintf(stderr, "public key is expected to be 64 hex digits\n");
			return 1;
		}
		std::array<char, 32> public_key;
		if (!from_hex({argv[0], len}, public_key))
		{
			std::fprintf(stderr, "invalid hex encoding of public key\n");
			return 1;
		}

		std::string salt;
		if (argc > 1) salt = argv[1];

		bootstrap(s);
		s.dht_get_item(public_key, salt);
		std::printf("MGET %s [salt: %s]\n", argv[0], salt.c_str());

		bool authoritative = false;

		while (!authoritative)
		{
			alert* a = wait_for_alert(s, dht_mutable_item_alert::alert_type);

			dht_mutable_item_alert* item = alert_cast<dht_mutable_item_alert>(a);

			authoritative = item->authoritative;
			std::string str = item->item.to_string();
			std::printf("%s: %s\n", authoritative ? "auth" : "non-auth", str.c_str());
		}
	}
	else
	{
		usage();
	}

	return save_dht_state(s);
}

#endif
