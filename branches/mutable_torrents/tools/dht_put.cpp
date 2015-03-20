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
#include "libtorrent/hex.hpp" // for from_hex
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp" // for bencode()
#include "libtorrent/kademlia/item.hpp" // for sign_mutable_item
#include "libtorrent/ed25519.hpp"

#include <boost/bind.hpp>

#include <stdlib.h>

using namespace libtorrent;
namespace lt = libtorrent;

#ifdef TORRENT_DISABLE_DHT

int main(int argc, char* argv[])
{
	fprintf(stderr, "not built with DHT support\n");
	return 1;
}

#else

void usage()
{
	fprintf(stderr,
		"USAGE:\ndht <command> <arg>\n\nCOMMANDS:\n"
		"get <hash>                - retrieves and prints out the immutable\n"
		"                            item stored under hash.\n"
		"put <string>              - puts the specified string as an immutable\n"
		"                            item onto the DHT. The resulting target hash\n"
		"gen-key <key-file>        - generate ed25519 keypair and save it in\n"
		"                            the specified file\n"
		"mput <key-file> <string>  - puts the specified string as a mutable\n"
		"                            object under the public key in key-file\n"
		"mget <public-key>         - get a mutable object under the specified\n"
		"                            public key\n"
		);
	exit(1);
}

std::auto_ptr<alert> wait_for_alert(lt::session& s, int alert_type)
{
	std::auto_ptr<alert> ret;
	bool found = false;
	while (!found)
	{
		s.wait_for_alert(seconds(5));

		std::deque<alert*> alerts;
		s.pop_alerts(&alerts);
		for (std::deque<alert*>::iterator i = alerts.begin()
			, end(alerts.end()); i != end; ++i)
		{
			if ((*i)->type() != alert_type)
			{
				static int spinner = 0;
				static const char anim[] = {'-', '\\', '|', '/'};
				printf("\r%c", anim[spinner]);
				fflush(stdout);
				spinner = (spinner + 1) & 3;
				//print some alerts?
				delete *i;
				continue;
			}
			ret = std::auto_ptr<alert>(*i);
			found = true;
		}
	}
	printf("\n");
	return ret;
}

void put_string(entry& e, boost::array<char, 64>& sig, boost::uint64_t& seq
	, std::string const& salt, char const* public_key, char const* private_key
	, char const* str)
{
	using libtorrent::dht::sign_mutable_item;

	e = std::string(str);
	std::vector<char> buf;
	bencode(std::back_inserter(buf), e);
	++seq;
	sign_mutable_item(std::pair<char const*, int>(&buf[0], buf.size())
		, std::pair<char const*, int>(&salt[0], salt.size())
		, seq
		, public_key
		, private_key
		, sig.data());
}

void bootstrap(lt::session& s)
{
	printf("bootstrapping\n");
	wait_for_alert(s, dht_bootstrap_alert::alert_type);
}

int main(int argc, char* argv[])
{
	// skip pointer to self
	++argv;
	--argc;

	if (argc < 1) usage();

	if (strcmp(argv[0], "gen-key") == 0)
	{
		++argv;
		--argc;
		if (argc < 1) usage();
	
		unsigned char seed[32];
		ed25519_create_seed(seed);

		FILE* f = fopen(argv[0], "wb+");
		if (f == NULL)
		{
			fprintf(stderr, "failed to open file for writing \"%s\": (%d) %s\n"
				, argv[0], errno, strerror(errno));
			return 1;
		}

		fwrite(seed, 1, 32, f);
		fclose(f);
		return 0;
	}

	settings_pack sett;
	sett.set_int(settings_pack::alert_mask, 0xffffffff);
	lt::session s(sett);

	s.add_dht_router(std::pair<std::string, int>("router.utorrent.com", 6881));

	FILE* f = fopen(".dht", "rb");
	if (f != NULL)
	{
		fseek(f, 0, SEEK_END);
		int size = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (size > 0)
		{
			std::vector<char> state;
			state.resize(size);
			fread(&state[0], 1, state.size(), f);

			bdecode_node e;
			error_code ec;
			bdecode(&state[0], &state[0] + state.size(), e, ec);
			if (ec)
				fprintf(stderr, "failed to parse .dht file: (%d) %s\n"
					, ec.value(), ec.message().c_str());
			else
				s.load_state(e);
		}
		fclose(f);
	}

	if (strcmp(argv[0], "get") == 0)
	{
		++argv;
		--argc;
	
		if (argc < 1) usage();

		if (strlen(argv[0]) != 40)
		{
			fprintf(stderr, "the hash is expected to be 40 hex characters\n");
			usage();
		}
		sha1_hash target;
		bool ret = from_hex(argv[0], 40, (char*)&target[0]);
		if (!ret)
		{
			fprintf(stderr, "invalid hex encoding of target hash\n");
			return 1;
		}

		bootstrap(s);
		s.dht_get_item(target);

		printf("GET %s\n", to_hex(target.to_string()).c_str());

		std::auto_ptr<alert> a = wait_for_alert(s, dht_immutable_item_alert::alert_type);

		dht_immutable_item_alert* item = alert_cast<dht_immutable_item_alert>(a.get());
		entry data;
		if (item)
			data.swap(item->item);

		printf("%s", data.to_string().c_str());
	}
	else if (strcmp(argv[0], "put") == 0)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		entry data;
		data = std::string(argv[0]);

		bootstrap(s);
		sha1_hash target = s.dht_put_item(data);
		
		printf("PUT %s\n", to_hex(target.to_string()).c_str());

		wait_for_alert(s, dht_put_alert::alert_type);
	}
	else if (strcmp(argv[0], "mput") == 0)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		FILE* f = fopen(argv[0], "rb+");
		if (f == NULL)
		{
			fprintf(stderr, "failed to open file \"%s\": (%d) %s\n"
				, argv[0], errno, strerror(errno));
			return 1;
		}

		unsigned char seed[32];
		fread(seed, 1, 32, f);
		fclose(f);

		++argv;
		--argc;
		if (argc < 1) usage();

		boost::array<char, 32> public_key;
		boost::array<char, 64> private_key;
		ed25519_create_keypair((unsigned char*)public_key.data()
			, (unsigned char*)private_key.data(), seed);
		
		bootstrap(s);
		s.dht_put_item(public_key, boost::bind(&put_string, _1, _2, _3, _4
			, public_key.data(), private_key.data(), argv[0]));

		printf("public key: %s\n", to_hex(std::string(public_key.data()
			, public_key.size())).c_str());

		wait_for_alert(s, dht_put_alert::alert_type);
	}
	else if (strcmp(argv[0], "mget") == 0)
	{
		++argv;
		--argc;
		if (argc < 1) usage();

		int len = strlen(argv[0]);
		if (len != 64)
		{
			fprintf(stderr, "public key is expected to be 64 hex digits\n");
			return 1;
		}
		boost::array<char, 32> public_key;
		bool ret = from_hex(argv[0], len, &public_key[0]);
		if (!ret)
		{
			fprintf(stderr, "invalid hex encoding of public key\n");
			return 1;
		}

		bootstrap(s);
		s.dht_get_item(public_key);

		std::auto_ptr<alert> a = wait_for_alert(s, dht_mutable_item_alert::alert_type);

		dht_mutable_item_alert* item = alert_cast<dht_mutable_item_alert>(a.get());
		entry data;
		if (item)
			data.swap(item->item);

		printf("%s", data.to_string().c_str());
	}
	else
	{
		usage();
	}

	entry e;
	s.save_state(e, lt::session::save_dht_state);
	std::vector<char> state;
	bencode(std::back_inserter(state), e);
	f = fopen(".dht", "wb+");
	if (f == NULL)
	{
		fprintf(stderr, "failed to open file .dht for writing");
		return 1;
	}
	fwrite(&state[0], 1, state.size(), f);
	fclose(f);
}

#endif

