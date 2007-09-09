#include "libtorrent/upnp.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/connection_queue.hpp"
#include <boost/bind.hpp>
#include <boost/ref.hpp>

using namespace libtorrent;

void callback(int tcp, int udp, std::string const& err)
{
	std::cerr << "tcp: " << tcp << ", udp: " << udp << ", error: \"" << err << "\"\n";
}

int main(int argc, char* argv[])
{
	io_service ios;
	std::string user_agent = "test agent";

	if (argc != 3)
	{
		std::cerr << "usage: " << argv[0] << " tcp-port udp-port" << std::endl;
		return 1;
	}

	connection_queue cc(ios);
	upnp upnp_handler(ios, cc, address_v4(), user_agent, &callback);

	deadline_timer timer(ios);
	timer.expires_from_now(seconds(2));
	timer.async_wait(boost::bind(&io_service::stop, boost::ref(ios)));

	std::cerr << "broadcasting for UPnP device" << std::endl;

	ios.reset();
	ios.run();

	upnp_handler.set_mappings(atoi(argv[1]), atoi(argv[2]));
	timer.expires_from_now(seconds(5));
	timer.async_wait(boost::bind(&io_service::stop, boost::ref(ios)));
	std::cerr << "mapping ports TCP: " << argv[1]
		<< " UDP: " << argv[2] << std::endl;

	ios.reset();
	ios.run();
	std::cerr << "removing mappings" << std::endl;
	upnp_handler.close();

	ios.reset();
	ios.run();
	std::cerr << "closing" << std::endl;
}


