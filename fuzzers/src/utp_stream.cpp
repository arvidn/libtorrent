/*

Copyright (c) 2026, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

// uTP stream transport fuzzer.
//
// Creates a utp_socket_impl in CONNECTED state (by injecting a SYN) and
// then drives it with an arbitrary sequence of packets and time advances,
// without any session, torrent, or peer connection.
//
// Corpus wire format:
//   repeated until EOF:
//     1 byte: ctrl
//       ctrl == 0  -> read 1 more byte; advance time by
//                     (1 << (byte >> 4)) ms (range 1..32768 ms) and
//                     call tick(). This exposes time-dependent paths
//                     (RTO retransmits, LEDBAT cwnd, keepalive,
//                     close timeouts) to the fuzzer.
//       ctrl >  0  -> read the next ctrl bytes from input and feed them
//                     as one uTP packet to the server socket; time
//                     advances by 1 ms
//
// The seed corpus (tools/gen_corpus.py) should add valid DATA packets with
// a predictable payload pattern (i % 251) so the fuzzer starts from a
// realistic connected state.

#include "libtorrent/aux_/utp_socket_manager.hpp"
#include "libtorrent/aux_/utp_stream.hpp"
#include "libtorrent/aux_/session_settings.hpp"
#include "libtorrent/performance_counters.hpp"
#include "libtorrent/aux_/udp_socket.hpp"
#include "libtorrent/span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace lt;
using namespace std::chrono_literals;

namespace {

	io_context g_ios;
	aux::session_settings g_sett;
	counters g_cnt;

	// packets the server sends out (ACKs, resets, etc.) -- no real peer to receive them
	std::vector<std::vector<char>> g_outgoing;

	void send_packet(std::weak_ptr<aux::utp_socket_interface>,
		udp::endpoint const&,
		span<char const> d,
		error_code&,
		aux::udp_send_flags_t)
	{
		g_outgoing.push_back({d.begin(), d.end()});
	}

	aux::utp_socket_manager g_man(
		&send_packet, [](aux::socket_type) {}, g_ios, g_sett, g_cnt, nullptr);

	// simulated remote peer endpoint
	udp::endpoint const g_peer_ep(make_address("1.0.0.1"), 1000);

	// connection IDs:
	//   The peer (simulated client) uses recv_id=1, send_id=2.
	//   The server uses recv_id=2, send_id=1.
	//
	//   SYN from peer:  connection_id = peer.recv_id = 1 = server.send_id  (check passes)
	//   DATA from peer: connection_id = peer.send_id = 2 = server.recv_id  (check passes)
	//   ACK from server: connection_id = server.send_id = 1 = peer.recv_id (informational)
	static constexpr std::uint16_t SERVER_RECV_ID = 2;
	static constexpr std::uint16_t SERVER_SEND_ID = 1;

	// Initial seq_nr we use in the SYN so the seed corpus can use deterministic seq_nrs.
	static constexpr std::uint16_t CLIENT_ISN = 1;

} // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size)
{
	g_outgoing.clear();

	std::unique_ptr<aux::utp_socket_impl> sock;
	{
		aux::utp_stream str(g_ios);
		sock = std::make_unique<aux::utp_socket_impl>(SERVER_RECV_ID, SERVER_SEND_ID, &str, g_man);
		str.set_impl(sock.get());

		time_point t(seconds(100));

		// Bootstrap: inject a SYN so the socket enters state_t::connected.
		// After this, the socket will accept DATA/STATE/FIN/RESET packets with
		// connection_id == SERVER_RECV_ID and seq_nr advancing from CLIENT_ISN+1.
		aux::utp_header syn{};
		syn.type_ver = static_cast<std::uint8_t>((aux::ST_SYN << 4) | 1);
		syn.extension = 0;
		syn.connection_id = SERVER_SEND_ID; // SYN uses the server's send_id
		syn.timestamp_microseconds = 0;
		syn.timestamp_difference_microseconds = 0;
		syn.wnd_size = 0x100000u;
		syn.seq_nr = CLIENT_ISN;
		syn.ack_nr = 0;

		sock->incoming_packet({reinterpret_cast<char const*>(&syn), sizeof(syn)}, g_peer_ep, t);
		g_man.socket_drained();
		g_outgoing.clear(); // discard SYN-ACK; there is no real peer

		// Set up a receive buffer so in-order data is delivered there rather
		// than accumulating in the socket's internal receive buffer.  We avoid
		// calling issue_read() (which would require a registered async handler)
		// and instead rely on incoming() writing directly to the registered buffer.
		static constexpr int RECV_CAP = 65536;
		std::array<char, RECV_CAP> recv_buf{};
		str.add_read_buffer(recv_buf.data(), RECV_CAP);

		// Process fuzz input: each byte is a control byte.
		//   ctrl == 0  -> read 1 more byte as a log-scale delta;
		//                 advance time by (1 << (byte >> 4)) ms and tick()
		//   ctrl >  0  -> feed the next ctrl bytes as one uTP packet;
		//                 time advances by 1 ms
		span<std::uint8_t const> rem(data, size);
		while (!rem.empty())
		{
			std::uint8_t const ctrl = rem[0];
			rem = rem.subspan(1);

			if (ctrl == 0)
			{
				if (rem.empty()) break;
				int const exp = (rem[0] >> 4) & 0x0f;
				rem = rem.subspan(1);
				t += (1 << exp) * 1ms;
				g_man.tick(t);
				g_man.socket_drained();
				g_outgoing.clear();
			}
			else
			{
				std::size_t const pkt_len = std::min<std::size_t>(ctrl, rem.size());
				span<char const> const pkt(reinterpret_cast<char const*>(rem.data()), pkt_len);
				sock->incoming_packet(pkt, g_peer_ep, t);
				rem = rem.subspan(pkt_len);
				t += 1ms;
				g_man.socket_drained();
				g_outgoing.clear();
			}
		}
	}

	return 0;
}
