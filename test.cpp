#include "transmission_webui.hpp"
#include "libtorrent/session.hpp"

using namespace libtorrent;

int main(int argc, char *const argv[])
{
	session ses(fingerprint("LT", 0, 1, 0, 0)
		, std::make_pair(6881, 6882));

	transmission_webui webui(ses);
	webui.start(8080);

	getchar();

	webui.stop();
}

