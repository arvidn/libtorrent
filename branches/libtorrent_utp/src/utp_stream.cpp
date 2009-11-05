#include "libtorrent/utp_stream.hpp"

namespace libtorrent {

utp_stream::~utp_stream()
{
}

void utp_stream::on_receive(error_code const& e, udp::endpoint const& ep
			, char const* buf, int size)
{
    // ignore resposes before we've sent any requests
    if (m_state == action_error) return;

    if (!m_sock.is_open()) return; // the operation was aborted

    // ignore packet not sent from the peer
    if (m_remote_endpoint != ep) return;
}

void utp_stream::bind(endpoint_type const& ep, error_code& ec)
{
    udp::endpoint udp_ep(ep.address(), ep.port());
    m_sock.bind(udp_ep, ec);
}

void utp_stream::bind(udp::endpoint const& ep, error_code& ec)
{
    m_sock.bind(ep, ec);
}

}
