#include "transmission_webui.hpp"
#include "utorrent_webui.hpp"
#include "libtorrent_webui.hpp"
#include "deluge.hpp"
#include "file_downloader.hpp"
#include "auto_load.hpp"
#include "save_settings.hpp"
#include "save_resume.hpp"
#include "torrent_history.hpp"
#include "auth.hpp"
#include "pam_auth.hpp"
#include "text_ui.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/alert_handler.hpp"
#include "rss_filter.hpp"

#include <signal.h>

bool quit = false;
bool force_quit = false;

void sighandler(int s)
{
	quit = true;
}

void sighandler_forcequit(int s)
{
	force_quit = true;
}

using namespace libtorrent;

int main(int argc, char *const argv[])
{
	session ses(fingerprint("LT", 0, 1, 0, 0)
		, std::make_pair(6881, 6882));

	settings_pack s;
	high_performance_seed(s);
	s.set_int(settings_pack::alert_mask, 0xffffffff);
	ses.apply_settings(s);

	alert_handler alerts(ses);

	error_code ec;
	save_settings sett(ses, "settings.dat");
	sett.load(ec);

	torrent_history hist(&alerts);
	auth authorizer;
	ec.clear();
	authorizer.load_accounts("users.conf", ec);
	if (ec)
		authorizer.add_account("admin", "test", 0);
	ec.clear();
//	pam_auth authorizer("bittorrent");

	save_resume resume(ses, "resume.dat", &alerts);
	add_torrent_params p;
	p.save_path = sett.get_str("save_path", ".");
	resume.load(ec, p);

	ses.set_load_function(boost::bind(
		&save_resume::load_torrent, &resume, _1, _2, _3));

	auto_load al(ses, &sett);
	rss_filter_handler rss_filter(alerts, ses);

	transmission_webui tr_handler(ses, &sett, &authorizer);
	utorrent_webui ut_handler(ses, &sett, &al, &hist, &rss_filter, &authorizer);
	file_downloader file_handler(ses, &authorizer);
	libtorrent_webui lt_handler(ses, &hist, &authorizer, &alerts);

	webui_base webport;
	webport.add_handler(&lt_handler);
	webport.add_handler(&ut_handler);
	webport.add_handler(&tr_handler);
	webport.add_handler(&file_handler);
	webport.start(8090);
	if (!webport.is_running())
	{
		fprintf(stderr, "failed to start web server\n");
		return 1;
	}

	deluge dlg(ses, "server.pem", &authorizer);
	dlg.start(58846);

	signal(SIGTERM, &sighandler);
	signal(SIGINT, &sighandler);

	libtorrent::screen text_ui_screen;
	libtorrent::error_log log(text_ui_screen
		, 0, 0, 120, 120
		, &alerts);

	bool shutting_down = false;
	while (!quit || !resume.ok_to_quit())
	{
		usleep(500000);
		alerts.dispatch_alerts();
		text_ui_screen.refresh();
		if (!shutting_down) ses.post_torrent_updates();
		if (quit && !shutting_down)
		{
			resume.save_all();
			shutting_down = true;
			log.log_line("saving resume data");
			signal(SIGTERM, &sighandler_forcequit);
			signal(SIGINT, &sighandler_forcequit);
		}
		if (force_quit)
		{
			log.log_line("force quitting");
			break;
		}
	}

	log.log_line("closing web server");
	dlg.stop();
	webport.stop();

	log.log_line("saving settings");
	sett.save(ec);

	return 0;
}

