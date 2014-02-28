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
#include "libtorrent/escape_string.hpp" // for from_hex
#include "libtorrent/alert_types.hpp"
#include "libtorrent/bencode.hpp" // for bencode()

#include <stdlib.h>

using namespace libtorrent;

void usage()
{
	fprintf(stderr,
		"USAGE:\ndht <command> <arg>\n\nCOMMANDS:\n"
		"get <hash>             - retrieves and prints out the immutable\n"
		"                         item stored under hash.\n"
		"put <string>           - puts the specified string as an immutable\n"
		"                         item onto the DHT. The resulting target hash\n"
		);
	exit(1);
}

std::auto_ptr<alert> wait_for_alert(session& s, int alert_type)
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

int main(int argc, char* argv[])
{
	session s;
	s.set_alert_mask(0xffffffff);

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

			lazy_entry e;
			error_code ec;
			lazy_bdecode(&state[0], &state[0] + state.size(), e, ec);
			if (ec)
				fprintf(stderr, "failed to parse .dht file: (%d) %s\n"
					, ec.value(), ec.message().c_str());
			else
				s.load_state(e);
		}
		fclose(f);
	}

	printf("bootstrapping\n");
	wait_for_alert(s, dht_bootstrap_alert::alert_type);

	// skip pointer to self
	++argv;
	--argc;

	if (argc < 1) usage();

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
		from_hex(argv[0], 40, (char*)&target[0]);

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

		sha1_hash target = s.dht_put_item(data);
		
		printf("PUT %s\n", to_hex(target.to_string()).c_str());

		wait_for_alert(s, dht_put_alert::alert_type);
	}
	else
	{
		usage();
	}

	entry e;
	s.save_state(e, session::save_dht_state);
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

