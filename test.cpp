#include "transmission_webui.hpp"
#include "utorrent_webui.hpp"
#include "deluge.hpp"
#include "file_downloader.hpp"
#include "torrent_post.hpp"
#include "libtorrent/session.hpp"

using namespace libtorrent;

int main(int argc, char *const argv[])
{
	session ses(fingerprint("LT", 0, 1, 0, 0)
		, std::make_pair(6881, 6882));

	transmission_webui tr_handler(ses);
	utorrent_webui ut_handler(ses);
	file_downloader file_handler(ses);
	torrent_post post(ses);

	webui_base webport;
	webport.add_handler(&ut_handler);
	webport.add_handler(&tr_handler);
	webport.add_handler(&file_handler);
	webport.add_handler(&post);
	webport.start(8080);

	deluge dlg(ses, "server.pem");
	dlg.start(58846);

	while (getchar() != 'q');

	dlg.stop();
	webport.stop();
}

