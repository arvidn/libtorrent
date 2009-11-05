#include "libtorrent/error_code.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/socket_type.hpp"
#include "libtorrent/utp_stream.hpp"

using namespace libtorrent;

void on_connect(error_code const& e)
{
}

int main(int argc, char* argv[])
{
	//int rtt, rtt_var;
	//int max_window, cur_window;
	//int delay_factor, window_factor, scaled_gain;

	session s;
	s.listen_on(std::make_pair(6881, 6889));

	io_service ios;
	connection_queue cc(ios);
	
	error_code ec;
	utp_stream sock(ios, cc);
	sock.bind(udp::endpoint(address_v4::any(), 0), ec);
	
	tcp::endpoint ep(address_v4::from_string("239.192.152.143", ec), 6771);
	
	sock.async_connect(ep, boost::bind(on_connect, _1));

	return 0;
}
