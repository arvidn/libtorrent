#include "libtorrent/upnp.hpp"
#include "libtorrent/socket.hpp"
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

	if (argc != 4)
	{
		std::cerr << "usage: " << argv[0] << " bind-address tcp-port udp-port" << std::endl;
		return 1;
	}
	
	upnp upnp_handler(ios, address_v4(), user_agent, &callback);

	deadline_timer timer(ios);
	timer.expires_from_now(seconds(2));
	timer.async_wait(boost::bind(&io_service::stop, boost::ref(ios)));

	std::cerr << "broadcasting for UPnP device" << std::endl;

	ios.reset();
	ios.run();

	upnp_handler.rebind(address_v4::from_string(argv[1]));
	std::cerr << "rebinding to IP " << argv[1]
		<< " broadcasting for UPnP device" << std::endl;
	
	ios.reset();
	ios.run();
	upnp_handler.set_mappings(atoi(argv[2]), atoi(argv[3]));
	timer.expires_from_now(seconds(5));
	timer.async_wait(boost::bind(&io_service::stop, boost::ref(ios)));
	std::cerr << "mapping ports TCP: " << argv[2]
		<< " UDP: " << argv[3] << std::endl;

	ios.reset();
	ios.run();
	std::cerr << "removing mappings" << std::endl;
	upnp_handler.close();

	ios.reset();
	ios.run();
	std::cerr << "closing" << std::endl;
}


