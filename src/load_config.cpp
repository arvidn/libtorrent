/*

Copyright (c) 2013, Arvid Norberg
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

#include <string>
#include <sys/stat.h>
#include <syslog.h>
#include <stdio.h>
#include "libtorrent/session.hpp"
#include "libtorrent/settings_pack.hpp"
#include "load_config.hpp"


namespace libtorrent
{

// this function lets you load libtorrent configurations straight from a
// simple text file, where each line is a key value pair. The keys are
// the keys used by libtorrent. The values are either strings, integers
// or booleans.
void load_config(std::string const& config_file, session* ses, error_code& ec)
{
	static time_t last_load = 0;

	struct stat st;
	if (stat(config_file.c_str(), &st) < 0)
	{
		ec = error_code(errno, get_system_category());
		return;
	}

	// if the config file hasn't changed, don't do anything
	if (st.st_mtime == last_load) return;
	last_load = st.st_mtime;

	FILE* f = fopen(config_file.c_str(), "r");
	if (f == NULL)
	{
		ec = error_code(errno, get_system_category());
		return;
	}

	settings_pack p;

	char key[512];
	char value[512];

	while (fscanf(f, "%512s %512s\n", key, value) == 2)
	{
		int setting_name = setting_by_name(key);
		if (setting_name < 0) continue;

		int type = setting_name & settings_pack::type_mask;
		switch (type)
		{
			case settings_pack::string_type_base:
				p.set_str(setting_name, value);
				break;
			case settings_pack::int_type_base:
				p.set_int(setting_name, atoi(value));
				break;
			case settings_pack::bool_type_base:
				p.set_bool(setting_name, atoi(value));
				break;
		};
	}

	fclose(f);

	ses->apply_settings(p);
}

}

