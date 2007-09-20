#include <libtorrent/enum_net.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/broadcast_socket.hpp>
#include <vector>

using namespace libtorrent;

int main()
{
	io_service ios;
	asio::error_code ec;
	std::vector<address> const& net = enum_net_interfaces(ios, ec);

	for (std::vector<address>::const_iterator i = net.begin()
		, end(net.end()); i != end; ++i)
	{
		std::cout << *i << " ";
		if (is_multicast(*i)) std::cout << "multicast ";
		if (is_local(*i)) std::cout << "local ";
		if (is_loopback(*i)) std::cout << "loopback ";
		std::cout << std::endl;
	}
}

