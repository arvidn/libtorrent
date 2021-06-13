/*

Copyright (c) 2009, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include <libtorrent.h>
#include <stdio.h>
#include <signal.h>
#ifdef WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif
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
		SET_LISTEN_INTERFACES, "0.0.0.0:6881",
		SET_ALERT_MASK, CAT_ERROR
			| CAT_PORT_MAPPING
			| CAT_STORAGE
			| CAT_TRACKER
			| CAT_IP_BLOCK,
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
#ifndef WIN32
	signal(SIGQUIT, &stop);
#endif

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


		struct libtorrent_alert const* alerts[400];
		int num_alerts = 400;
		session_pop_alerts(ses, alerts, &num_alerts);

		for (int i = 0; i < num_alerts; ++i)
		{
			char msg[500];
			alert_message(alerts[i], msg, sizeof(msg));
			printf("%s\n", msg);
		}

		if (strlen(st.error) > 0)
		{
			fprintf(stderr, "\nERROR: %s\n", st.error);
			break;
		}

		fflush(stdout);
#ifdef WIN32
		Sleep(1000);
#else
		usleep(1000000);
#endif
	}
	printf("\nclosing\n");

exit:

	session_close(ses);
	return ret;
}

