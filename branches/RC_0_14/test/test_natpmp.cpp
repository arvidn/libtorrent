#include "libtorrent/natpmp.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/connection_queue.hpp"
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/intrusive_ptr.hpp>

using namespace libtorrent;

void callback(int mapping, int port, std::string const& err)
{
	std::cerr << "mapping: " << mapping << ", port: " << port << ", error: \"" << err << "\"\n";
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
	boost::intrusive_ptr<natpmp> natpmp_handler = new natpmp(ios, address_v4(), &callback);

	deadline_timer timer(ios);

	int tcp_map = natpmp_handler->add_mapping(natpmp::tcp, atoi(argv[1]), atoi(argv[1]));
	int udp_map = natpmp_handler->add_mapping(natpmp::udp, atoi(argv[2]), atoi(argv[2]));

	timer.expires_from_now(seconds(2));
	timer.async_wait(boost::bind(&io_service::stop, boost::ref(ios)));
	std::cerr << "mapping ports TCP: " << argv[1]
		<< " UDP: " << argv[2] << std::endl;

	ios.reset();
	ios.run();
	timer.expires_from_now(seconds(2));
	timer.async_wait(boost::bind(&io_service::stop, boost::ref(ios)));
	std::cerr << "removing mapping " << tcp_map << std::endl;
	natpmp_handler->delete_mapping(tcp_map);

	ios.reset();
	ios.run();
	std::cerr << "removing mappings" << std::endl;
	natpmp_handler->close();

	ios.reset();
	ios.run();
	std::cerr << "closing" << std::endl;
}


