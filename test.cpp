#include "transmission_webui.hpp"
#include "utorrent_webui.hpp"
#include "deluge.hpp"
#include "file_downloader.hpp"
#include "auto_load.hpp"
#include "save_settings.hpp"
#include "save_resume.hpp"
#include "torrent_history.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/alert_handler.hpp"

#include <signal.h>

bool quit = false;

void sighandler(int s)
{
	quit = true;
}

using namespace libtorrent;

int main(int argc, char *const argv[])
{
	session ses(fingerprint("LT", 0, 1, 0, 0)
		, std::make_pair(6881, 6882));
	ses.set_alert_mask(~0);

	alert_handler alerts;

	error_code ec;
	save_settings sett(ses, "settings.dat");
	sett.load(ec);

	torrent_history hist(&alerts);

	save_resume resume(ses, ".resume", &alerts);
	add_torrent_params p;
	p.save_path = sett.get_str("save_path", ".");
	resume.load(ec, p);

	auto_load al(ses, &sett);

	transmission_webui tr_handler(ses, &sett);
	utorrent_webui ut_handler(ses, &sett, &al, &hist);
	file_downloader file_handler(ses);

	webui_base webport;
	webport.add_handler(&ut_handler);
	webport.add_handler(&tr_handler);
	webport.add_handler(&file_handler);
	webport.start(8080);

	deluge dlg(ses, "server.pem");
	dlg.start(58846);

	signal(SIGTERM, &sighandler);
	signal(SIGINT, &sighandler);

	std::deque<alert*> alert_queue;
	bool shutting_down = false;
	while (!quit || !resume.ok_to_quit())
	{
		if (ses.wait_for_alert(milliseconds(500)))
		{
			alert_queue.clear();
			ses.pop_alerts(&alert_queue);
//			for (std::deque<alert*>::iterator i = alert_queue.begin()
//				, end(alert_queue.end()); i != end; ++i)
//			{
//				printf(" %s\n", (*i)->message().c_str());
//			}
			alerts.dispatch_alerts(alert_queue);
		}
		ses.post_torrent_updates();
		if (quit && !shutting_down)
		{
			resume.save_all();
			shutting_down = true;
		}
	}

	dlg.stop();
	webport.stop();
	sett.save(ec);
}

