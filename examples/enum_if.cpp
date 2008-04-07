#include <libtorrent/enum_net.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/broadcast_socket.hpp>
#include <vector>

using namespace libtorrent;

int main()
{
	io_service ios;
	asio::error_code ec;
	std::vector<ip_interface> const& net = enum_net_interfaces(ios, ec);

	for (std::vector<ip_interface>::const_iterator i = net.begin()
		, end(net.end()); i != end; ++i)
	{
		std::cout << "address: " << i->interface_address << std::endl
			<< "   mask: " << i->netmask << std::endl
			<< "   flags: ";
		if (is_multicast(i->interface_address)) std::cout << "multicast ";
		if (is_local(i->interface_address)) std::cout << "local ";
		if (is_loopback(i->interface_address)) std::cout << "loopback ";
		std::cout << std::endl;
		std::cout << "  router: " << router_for_interface(i->interface_address, ec);
		std::cout << std::endl;
	}

	address local = guess_local_address(ios);

	std::cout << "Local address: " << local << std::endl;
}

