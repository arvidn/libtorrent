/*

Copyright (c) 2009, Arvid Norberg
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

#include <libtorrent.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

int quit = 0;

void stop(int signal)
{
	quit = 1;
}

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		fprintf(stderr, "usage: ./simple_client torrent-file\n");
		return 1;
	}

	int ret = 0;
	void* ses = session_create(
		SES_LISTENPORT, 6881,
		SES_LISTENPORT_END, 6889,
		SES_ALERT_MASK, ~(cat_progress | cat_port_mapping | cat_debug | cat_performance_warning | cat_peer),
		TAG_END);

	int t = session_add_torrent(ses,
		TOR_FILENAME, argv[1],
		TOR_SAVE_PATH, "./",
		TAG_END);

	if (t < 0)
	{
		fprintf(stderr, "Failed to add torrent\n");
		ret = 1;
		goto exit;
	}

	struct torrent_status st;

	printf("press ctrl-C to stop\n");

	signal(SIGINT, &stop);
	signal(SIGABRT, &stop);
	signal(SIGQUIT, &stop);

	while (quit == 0)
	{
		char const* message = "";

		char const* state[] = {"queued", "checking", "downloading metadata"
			, "downloading", "finished", "seeding", "allocating"
			, "checking_resume_data"};

		if (torrent_get_status(t, &st, sizeof(st)) < 0) break;
		printf("\r%3.f%% %d kB (%5.f kB/s) up: %d kB (%5.f kB/s) peers: %d '%s' %s  "
			, (double)st.progress * 100.
			, (int)(st.total_payload_download / 1000)
			, (double)st.download_payload_rate / 1000.
			, (int)(st.total_payload_upload / 1000)
			, (double)st.upload_payload_rate / 1000.
			, st.num_peers
			, state[st.state]
			, message);


		char msg[400];
		while (session_pop_alert(ses, msg, sizeof(msg), 0) >= 0)
		{
			printf("%s\n", msg);
		}

		if (strlen(st.error) > 0)
		{
			fprintf(stderr, "\nERROR: %s\n", st.error);
			break;
		}

		fflush(stdout);
		usleep(1000000);
	}
	printf("\nclosing\n");

exit:

	session_close(ses);
	return ret;
}

