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
//#include "text_ui.hpp"

#include "libtorrent/session.hpp"
#include "alert_handler.hpp"
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

struct external_ip_observer : alert_observer
{
	external_ip_observer(session& s, alert_handler* h)
		: m_alerts(h)
		, m_ses(s)
	{
		m_alerts->subscribe(this, 0, external_ip_alert::alert_type, 0);
	}

	~external_ip_observer()
	{
		m_alerts->unsubscribe(this);
	}

	void handle_alert(alert const* a)
	{
		external_ip_alert const* ip = alert_cast<external_ip_alert>(a);
		if (ip == NULL) return;

		error_code ec;
		printf("EXTERNAL IP: %s\n", ip->external_address.to_string(ec).c_str());

		if (m_last_known_addr != address()
			&& m_last_known_addr != ip->external_address)
		{
			// our external IP changed. stop the session.
			printf("pausing session\n");
			m_ses.pause();
			return;
		}

		if (m_ses.is_paused() && m_last_known_addr == ip->external_address)
		{
			printf("resuming session\n");
			m_ses.resume();
			return;
		}

		m_last_known_addr = ip->external_address;
	}

	alert_handler* m_alerts;
	session& m_ses;
	address m_last_known_addr;
};

int main(int argc, char *const argv[])
{
	session ses(fingerprint("LT", 0, 1, 0, 0)
		, std::make_pair(6881, 6882));

	settings_pack s;
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

//	external_ip_observer eip(ses, &alerts);

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

	bool shutting_down = false;
	while (!quit || !resume.ok_to_quit())
	{
		usleep(500000);
		alerts.dispatch_alerts();
		if (!shutting_down) ses.post_torrent_updates();
		if (quit && !shutting_down)
		{
			resume.save_all();
			shutting_down = true;
			fprintf(stderr, "saving resume data\n");
			signal(SIGTERM, &sighandler_forcequit);
			signal(SIGINT, &sighandler_forcequit);
		}
		if (force_quit)
		{
			fprintf(stderr, "force quitting\n");
			break;
		}
	}

	fprintf(stderr, "abort alerts\n");
	// it's important to disable any more alert subscriptions
	// and cancel the ones in flught now, otherwise the webport
	// may dead-lock. Some of its threads may be blocked waiting
	// for alerts. Those alerts aren't likely to ever arrive at
	// this point.
	alerts.abort();
	fprintf(stderr, "closing web server\n");
	dlg.stop();
	webport.stop();

	fprintf(stderr, "saving settings\n");
	sett.save(ec);

	fprintf(stderr, "destructing session\n");
	return 0;
}

