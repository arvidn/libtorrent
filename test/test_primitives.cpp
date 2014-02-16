/*

Copyright (c) 2008, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/parse_url.hpp"
#include "libtorrent/http_tracker_connection.hpp"
#include "libtorrent/buffer.hpp"
#include "libtorrent/xml_parse.hpp"
#include "libtorrent/upnp.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/bitfield.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/escape_string.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/identify_client.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/packet_buffer.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/timestamp_history.hpp"
#include "libtorrent/enum_net.hpp"
#include "libtorrent/bloom_filter.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/rsa.hpp"
#ifndef TORRENT_DISABLE_DHT
#include "libtorrent/kademlia/node_id.hpp"
#include "libtorrent/kademlia/routing_table.hpp"
#include "libtorrent/kademlia/node.hpp"
#endif
#include "libtorrent/socket_io.hpp"
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/bind.hpp>
#include <iostream>

#include "test.hpp"

using namespace libtorrent;
using namespace boost::tuples;

namespace libtorrent {
	TORRENT_EXPORT std::string sanitize_path(std::string const& p);
}

sha1_hash to_hash(char const* s)
{
	sha1_hash ret;
	from_hex(s, 40, (char*)&ret[0]);
	return ret;
}

tuple<int, int, bool> feed_bytes(http_parser& parser, char const* str)
{
	tuple<int, int, bool> ret(0, 0, false);
	tuple<int, int, bool> prev(0, 0, false);
	for (int chunks = 1; chunks < 70; ++chunks)
	{
		ret = make_tuple(0, 0, false);
		parser.reset();
		buffer::const_interval recv_buf(str, str);
		for (; *str;)
		{
			int chunk_size = (std::min)(chunks, int(strlen(recv_buf.end)));
			if (chunk_size == 0) break;
			recv_buf.end += chunk_size;
			int payload, protocol;
			bool error = false;
			tie(payload, protocol) = parser.incoming(recv_buf, error);
			ret.get<0>() += payload;
			ret.get<1>() += protocol;
			ret.get<2>() |= error;
//			std::cerr << payload << ", " << protocol << ", " << chunk_size << std::endl;
			TORRENT_ASSERT(payload + protocol == chunk_size);
		}
		TEST_CHECK(prev == make_tuple(0, 0, false) || ret == prev);
		TEST_EQUAL(ret.get<0>() + ret.get<1>(), strlen(str));
		prev = ret;
	}
	return ret;
}

void parser_callback(std::string& out, int token, char const* s, char const* val)
{
	switch (token)
	{
		case xml_start_tag: out += "B"; break;
		case xml_end_tag: out += "F"; break;
		case xml_empty_tag: out += "E"; break;
		case xml_declaration_tag: out += "D"; break;
		case xml_comment: out += "C"; break;
		case xml_string: out += "S"; break;
		case xml_attribute: out += "A"; break;
		case xml_parse_error: out += "P"; break;
		case xml_tag_content: out += "T"; break;
		default: TEST_CHECK(false);
	}
	out += s;
	if (token == xml_attribute)
	{
		TEST_CHECK(val != 0);
		out += "V";
		out += val;
	}
	else
	{
		TEST_CHECK(val == 0);
	}
}

#ifndef TORRENT_DISABLE_DHT	
void add_and_replace(libtorrent::dht::node_id& dst, libtorrent::dht::node_id const& add)
{
	bool carry = false;
	for (int k = 19; k >= 0; --k)
	{
		int sum = dst[k] + add[k] + (carry?1:0);
		dst[k] = sum & 255;
		carry = sum > 255;
	}
}

void node_push_back(void* userdata, libtorrent::dht::node_entry const& n)
{
	using namespace libtorrent::dht;
	std::vector<node_entry>* nv = (std::vector<node_entry>*)userdata;
	nv->push_back(n);
}

void nop(void* userdata, libtorrent::dht::node_entry const& n) {}

#endif

char upnp_xml[] = 
"<root>"
"<specVersion>"
"<major>1</major>"
"<minor>0</minor>"
"</specVersion>"
"<URLBase>http://192.168.0.1:5678</URLBase>"
"<device>"
"<deviceType>"
"urn:schemas-upnp-org:device:InternetGatewayDevice:1"
"</deviceType>"
"<presentationURL>http://192.168.0.1:80</presentationURL>"
"<friendlyName>D-Link Router</friendlyName>"
"<manufacturer>D-Link</manufacturer>"
"<manufacturerURL>http://www.dlink.com</manufacturerURL>"
"<modelDescription>Internet Access Router</modelDescription>"
"<modelName>D-Link Router</modelName>"
"<UDN>uuid:upnp-InternetGatewayDevice-1_0-12345678900001</UDN>"
"<UPC>123456789001</UPC>"
"<serviceList>"
"<service>"
"<serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
"<serviceId>urn:upnp-org:serviceId:L3Forwarding1</serviceId>"
"<controlURL>/Layer3Forwarding</controlURL>"
"<eventSubURL>/Layer3Forwarding</eventSubURL>"
"<SCPDURL>/Layer3Forwarding.xml</SCPDURL>"
"</service>"
"</serviceList>"
"<deviceList>"
"<device>"
"<deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>"
"<friendlyName>WANDevice</friendlyName>"
"<manufacturer>D-Link</manufacturer>"
"<manufacturerURL>http://www.dlink.com</manufacturerURL>"
"<modelDescription>Internet Access Router</modelDescription>"
"<modelName>D-Link Router</modelName>"
"<modelNumber>1</modelNumber>"
"<modelURL>http://support.dlink.com</modelURL>"
"<serialNumber>12345678900001</serialNumber>"
"<UDN>uuid:upnp-WANDevice-1_0-12345678900001</UDN>"
"<UPC>123456789001</UPC>"
"<serviceList>"
"<service>"
"<serviceType>"
"urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1"
"</serviceType>"
"<serviceId>urn:upnp-org:serviceId:WANCommonInterfaceConfig</serviceId>"
"<controlURL>/WANCommonInterfaceConfig</controlURL>"
"<eventSubURL>/WANCommonInterfaceConfig</eventSubURL>"
"<SCPDURL>/WANCommonInterfaceConfig.xml</SCPDURL>"
"</service>"
"</serviceList>"
"<deviceList>"
"<device>"
"<deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>"
"<friendlyName>WAN Connection Device</friendlyName>"
"<manufacturer>D-Link</manufacturer>"
"<manufacturerURL>http://www.dlink.com</manufacturerURL>"
"<modelDescription>Internet Access Router</modelDescription>"
"<modelName>D-Link Router</modelName>"
"<modelNumber>1</modelNumber>"
"<modelURL>http://support.dlink.com</modelURL>"
"<serialNumber>12345678900001</serialNumber>"
"<UDN>uuid:upnp-WANConnectionDevice-1_0-12345678900001</UDN>"
"<UPC>123456789001</UPC>"
"<serviceList>"
"<service>"
"<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
"<serviceId>urn:upnp-org:serviceId:WANIPConnection</serviceId>"
"<controlURL>/WANIPConnection</controlURL>"
"<eventSubURL>/WANIPConnection</eventSubURL>"
"<SCPDURL>/WANIPConnection.xml</SCPDURL>"
"</service>"
"</serviceList>"
"</device>"
"</deviceList>"
"</device>"
"</deviceList>"
"</device>"
"</root>";

char upnp_xml2[] =
"<root>"
"<specVersion>"
"<major>1</major>"
"<minor>0</minor>"
"</specVersion>"
"<URLBase>http://192.168.1.1:49152</URLBase>"
"<device>"
"<deviceType>"
"urn:schemas-upnp-org:device:InternetGatewayDevice:1"
"</deviceType>"
"<friendlyName>LINKSYS WAG200G Gateway</friendlyName>"
"<manufacturer>LINKSYS</manufacturer>"
"<manufacturerURL>http://www.linksys.com</manufacturerURL>"
"<modelDescription>LINKSYS WAG200G Gateway</modelDescription>"
"<modelName>Wireless-G ADSL Home Gateway</modelName>"
"<modelNumber>WAG200G</modelNumber>"
"<modelURL>http://www.linksys.com</modelURL>"
"<serialNumber>123456789</serialNumber>"
"<UDN>uuid:8d401597-1dd2-11b2-a7d4-001ee5947cac</UDN>"
"<UPC>WAG200G</UPC>"
"<serviceList>"
"<service>"
"<serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
"<serviceId>urn:upnp-org:serviceId:L3Forwarding1</serviceId>"
"<controlURL>/upnp/control/L3Forwarding1</controlURL>"
"<eventSubURL>/upnp/event/L3Forwarding1</eventSubURL>"
"<SCPDURL>/l3frwd.xml</SCPDURL>"
"</service>"
"</serviceList>"
"<deviceList>"
"<device>"
"<deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>"
"<friendlyName>WANDevice</friendlyName>"
"<manufacturer>LINKSYS</manufacturer>"
"<manufacturerURL>http://www.linksys.com/</manufacturerURL>"
"<modelDescription>Residential Gateway</modelDescription>"
"<modelName>Internet Connection Sharing</modelName>"
"<modelNumber>1</modelNumber>"
"<modelURL>http://www.linksys.com/</modelURL>"
"<serialNumber>0000001</serialNumber>"
"<UDN>uuid:8d401596-1dd2-11b2-a7d4-001ee5947cac</UDN>"
"<UPC>WAG200G</UPC>"
"<serviceList>"
"<service>"
"<serviceType>"
"urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1"
"</serviceType>"
"<serviceId>urn:upnp-org:serviceId:WANCommonIFC1</serviceId>"
"<controlURL>/upnp/control/WANCommonIFC1</controlURL>"
"<eventSubURL>/upnp/event/WANCommonIFC1</eventSubURL>"
"<SCPDURL>/cmnicfg.xml</SCPDURL>"
"</service>"
"</serviceList>"
"<deviceList>"
"<device>"
"<deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>"
"<friendlyName>WANConnectionDevice</friendlyName>"
"<manufacturer>LINKSYS</manufacturer>"
"<manufacturerURL>http://www.linksys.com/</manufacturerURL>"
"<modelDescription>Residential Gateway</modelDescription>"
"<modelName>Internet Connection Sharing</modelName>"
"<modelNumber>1</modelNumber>"
"<modelURL>http://www.linksys.com/</modelURL>"
"<serialNumber>0000001</serialNumber>"
"<UDN>uuid:8d401597-1dd2-11b2-a7d3-001ee5947cac</UDN>"
"<UPC>WAG200G</UPC>"
"<serviceList>"
"<service>"
"<serviceType>"
"urn:schemas-upnp-org:service:WANEthernetLinkConfig:1"
"</serviceType>"
"<serviceId>urn:upnp-org:serviceId:WANEthLinkC1</serviceId>"
"<controlURL>/upnp/control/WANEthLinkC1</controlURL>"
"<eventSubURL>/upnp/event/WANEthLinkC1</eventSubURL>"
"<SCPDURL>/wanelcfg.xml</SCPDURL>"
"</service>"
"<service>"
"<serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>"
"<serviceId>urn:upnp-org:serviceId:WANPPPConn1</serviceId>"
"<controlURL>/upnp/control/WANPPPConn1</controlURL>"
"<eventSubURL>/upnp/event/WANPPPConn1</eventSubURL>"
"<SCPDURL>/pppcfg.xml</SCPDURL>"
"</service>"
"</serviceList>"
"</device>"
"</deviceList>"
"</device>"
"<device>"
"<deviceType>urn:schemas-upnp-org:device:LANDevice:1</deviceType>"
"<friendlyName>LANDevice</friendlyName>"
"<manufacturer>LINKSYS</manufacturer>"
"<manufacturerURL>http://www.linksys.com/</manufacturerURL>"
"<modelDescription>Residential Gateway</modelDescription>"
"<modelName>Residential Gateway</modelName>"
"<modelNumber>1</modelNumber>"
"<modelURL>http://www.linksys.com/</modelURL>"
"<serialNumber>0000001</serialNumber>"
"<UDN>uuid:8d401596-1dd2-11b2-a7d3-001ee5947cac</UDN>"
"<UPC>WAG200G</UPC>"
"<serviceList>"
"<service>"
"<serviceType>"
"urn:schemas-upnp-org:service:LANHostConfigManagement:1"
"</serviceType>"
"<serviceId>urn:upnp-org:serviceId:LANHostCfg1</serviceId>"
"<controlURL>/upnp/control/LANHostCfg1</controlURL>"
"<eventSubURL>/upnp/event/LANHostCfg1</eventSubURL>"
"<SCPDURL>/lanhostc.xml</SCPDURL>"
"</service>"
"</serviceList>"
"</device>"
"</deviceList>"
"<presentationURL>http://192.168.1.1/index.htm</presentationURL>"
"</device>"
"</root>";

struct parse_state
{
	parse_state(): in_service(false) {}
	void reset(char const* st)
	{
		in_service = false;
		service_type = st;
		tag_stack.clear();
		control_url.clear();
		model.clear();
		url_base.clear();
	}
	bool in_service;
	std::list<std::string> tag_stack;
	std::string control_url;
	char const* service_type;
	std::string model;
	std::string url_base;
};

namespace libtorrent
{
	// defined in torrent_info.cpp
	TORRENT_EXPORT bool verify_encoding(std::string& target, bool path = true);
}

TORRENT_EXPORT void find_control_url(int type, char const* string, parse_state& state);

address rand_v4()
{
	return address_v4((rand() << 16 | rand()) & 0xffffffff);
}

int test_main()
{
	using namespace libtorrent;
	using namespace libtorrent::dht;
	error_code ec;
	int ret = 0;

	// make sure the retry interval keeps growing
	// on failing announces
	announce_entry ae("dummy");
	int last = 0;
	session_settings sett;
	sett.tracker_backoff = 250;
	for (int i = 0; i < 10; ++i)
	{
		ae.failed(sett, 5);
		int delay = ae.next_announce_in();
		TEST_CHECK(delay > last);
		last = delay;
		fprintf(stderr, "%d, ", delay);
	}
	fprintf(stderr, "\n");

#if defined TORRENT_USE_OPENSSL
	// test sign_rsa and verify_rsa
	char private_key[1192];
	int private_len = sizeof(private_key);
	char public_key[268];
	int public_len = sizeof(public_key);

	ret = generate_rsa_keys(public_key, &public_len, private_key, &private_len, 2048);
	fprintf(stderr, "keysizes: pub: %d priv: %d\n", public_len, private_len);

	TEST_CHECK(ret);

	char test_message[1024];
	std::generate(test_message, test_message + 1024, &std::rand);

	char signature[256];
	int sig_len = sign_rsa(hasher(test_message, sizeof(test_message)).final()
		, private_key, private_len, signature, sizeof(signature));

	TEST_CHECK(sig_len == 256);

	ret = verify_rsa(hasher(test_message, sizeof(test_message)).final()
		, public_key, public_len, signature, sig_len);
	TEST_CHECK(ret == 1);
#endif

	lazy_entry ent;

#ifndef TORRENT_DISABLE_DHT
	// test verify_message
	const static key_desc_t msg_desc[] = {
		{"A", lazy_entry::string_t, 4, 0},
		{"B", lazy_entry::dict_t, 0, key_desc_t::optional | key_desc_t::parse_children},
			{"B1", lazy_entry::string_t, 0, 0},
			{"B2", lazy_entry::string_t, 0, key_desc_t::last_child},
		{"C", lazy_entry::dict_t, 0, key_desc_t::optional | key_desc_t::parse_children},
			{"C1", lazy_entry::string_t, 0, 0},
			{"C2", lazy_entry::string_t, 0, key_desc_t::last_child},
	};

	lazy_entry const* msg_keys[7];

	char const test_msg[] = "d1:A4:test1:Bd2:B15:test22:B25:test3ee";
	lazy_bdecode(test_msg, test_msg + sizeof(test_msg)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());

	char error_string[200];
	ret = verify_message(&ent, msg_desc, msg_keys, 7, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	TEST_CHECK(msg_keys[0]);
	if (msg_keys[0]) TEST_EQUAL(msg_keys[0]->string_value(), "test");
	TEST_CHECK(msg_keys[1]);
	TEST_CHECK(msg_keys[2]);
	if (msg_keys[2]) TEST_EQUAL(msg_keys[2]->string_value(), "test2");
	TEST_CHECK(msg_keys[3]);
	if (msg_keys[3]) TEST_EQUAL(msg_keys[3]->string_value(), "test3");
	TEST_CHECK(msg_keys[4] == 0);
	TEST_CHECK(msg_keys[5] == 0);
	TEST_CHECK(msg_keys[6] == 0);

	char const test_msg2[] = "d1:A4:test1:Cd2:C15:test22:C25:test3ee";
	lazy_bdecode(test_msg2, test_msg2 + sizeof(test_msg2)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());

	ret = verify_message(&ent, msg_desc, msg_keys, 7, error_string, sizeof(error_string));
	TEST_CHECK(ret);
	TEST_CHECK(msg_keys[0]);
	if (msg_keys[0]) TEST_EQUAL(msg_keys[0]->string_value(), "test");
	TEST_CHECK(msg_keys[1] == 0);
	TEST_CHECK(msg_keys[2] == 0);
	TEST_CHECK(msg_keys[3] == 0);
	TEST_CHECK(msg_keys[4]);
	TEST_CHECK(msg_keys[5]);
	if (msg_keys[5]) TEST_EQUAL(msg_keys[5]->string_value(), "test2");
	TEST_CHECK(msg_keys[6]);
	if (msg_keys[6]) TEST_EQUAL(msg_keys[6]->string_value(), "test3");


	char const test_msg3[] = "d1:Cd2:C15:test22:C25:test3ee";
	lazy_bdecode(test_msg3, test_msg3 + sizeof(test_msg3)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());

	ret = verify_message(&ent, msg_desc, msg_keys, 7, error_string, sizeof(error_string));
	TEST_CHECK(!ret);
	fprintf(stderr, "%s\n", error_string);
	TEST_EQUAL(error_string, std::string("missing 'A' key"));

	char const test_msg4[] = "d1:A6:foobare";
	lazy_bdecode(test_msg4, test_msg4 + sizeof(test_msg4)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());

	ret = verify_message(&ent, msg_desc, msg_keys, 7, error_string, sizeof(error_string));
	TEST_CHECK(!ret);
	fprintf(stderr, "%s\n", error_string);
	TEST_EQUAL(error_string, std::string("invalid value for 'A'"));

	char const test_msg5[] = "d1:A4:test1:Cd2:C15:test2ee";
	lazy_bdecode(test_msg5, test_msg5 + sizeof(test_msg5)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());

	ret = verify_message(&ent, msg_desc, msg_keys, 7, error_string, sizeof(error_string));
	TEST_CHECK(!ret);
	fprintf(stderr, "%s\n", error_string);
	TEST_EQUAL(error_string, std::string("missing 'C2' key"));
#endif

	// test empty strings [ { "":1 }, "" ]
	char const test_msg6[] = "ld0:i1ee0:e";
	lazy_bdecode(test_msg6, test_msg6 + sizeof(test_msg6)-1, ent, ec);
	fprintf(stderr, "%s\n", print_entry(ent).c_str());
	TEST_CHECK(ent.type() == lazy_entry::list_t);
	if (ent.type() == lazy_entry::list_t)
	{
		TEST_CHECK(ent.list_size() == 2);
		if (ent.list_size() == 2)
		{
			TEST_CHECK(ent.list_at(0)->dict_find_int_value("") == 1);
			TEST_CHECK(ent.list_at(1)->string_value() == "");
		}
	}

	// test external ip voting
	aux::session_impl* ses = new aux::session_impl(std::pair<int, int>(0,0)
		, fingerprint("LT", 0, 0, 0, 0), "0.0.0.0", 0
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		, ""
#endif
		);

	// test a single malicious node
	// adds 50 legitimate responses from different peers
	// and 50 malicious responses from the same peer
	address real_external = address_v4::from_string("5.5.5.5");
	address malicious = address_v4::from_string("4.4.4.4");
	for (int i = 0; i < 50; ++i)
	{
		ses->set_external_address(real_external, aux::session_impl::source_dht, rand_v4());
		ses->set_external_address(rand_v4(), aux::session_impl::source_dht, malicious);
	}
	TEST_CHECK(ses->external_address() == real_external);
	ses->abort();
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
	ses->m_logger.reset();
#endif
	delete ses;
	ses = new aux::session_impl(std::pair<int, int>(0,0)
		, fingerprint("LT", 0, 0, 0, 0), "0.0.0.0", 0
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
		, ""
#endif
		);

	// test a single malicious node
	// adds 50 legitimate responses from different peers
	// and 50 consistent malicious responses from the same peer
	real_external = address_v4::from_string("5.5.5.5");
	malicious = address_v4::from_string("4.4.4.4");
	address malicious_external = address_v4::from_string("3.3.3.3");
	for (int i = 0; i < 50; ++i)
	{
		ses->set_external_address(real_external, aux::session_impl::source_dht, rand_v4());
		ses->set_external_address(malicious_external, aux::session_impl::source_dht, malicious);
	}
	TEST_CHECK(ses->external_address() == real_external);
	ses->abort();
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
	ses->m_logger.reset();
#endif
	delete ses;

	// test bloom_filter
	bloom_filter<32> filter;
	sha1_hash k1 = hasher("test1", 5).final();
	sha1_hash k2 = hasher("test2", 5).final();
	sha1_hash k3 = hasher("test3", 5).final();
	sha1_hash k4 = hasher("test4", 5).final();
	TEST_CHECK(!filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(!filter.find(k4));

	filter.set(k1);
	TEST_CHECK(filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(!filter.find(k4));

	filter.set(k4);
	TEST_CHECK(filter.find(k1));
	TEST_CHECK(!filter.find(k2));
	TEST_CHECK(!filter.find(k3));
	TEST_CHECK(filter.find(k4));

	// test timestamp_history
	{
		timestamp_history h;
		TEST_EQUAL(h.add_sample(0x32, false), 0);
		TEST_EQUAL(h.base(), 0x32);
		TEST_EQUAL(h.add_sample(0x33, false), 0x1);
		TEST_EQUAL(h.base(), 0x32);
		TEST_EQUAL(h.add_sample(0x3433, false), 0x3401);
		TEST_EQUAL(h.base(), 0x32);
		TEST_EQUAL(h.add_sample(0x30, false), 0);
		TEST_EQUAL(h.base(), 0x30);

		// test that wrapping of the timestamp is properly handled
		h.add_sample(0xfffffff3, false);
		TEST_EQUAL(h.base(), 0xfffffff3);
	}

	// test packet_buffer
	{
		packet_buffer pb;

		TEST_EQUAL(pb.capacity(), 0);
		TEST_EQUAL(pb.size(), 0);
		TEST_EQUAL(pb.span(), 0);

		pb.insert(123, (void*)123);
		TEST_EQUAL(pb.at(123 + 16), 0);
		
		TEST_CHECK(pb.at(123) == (void*)123);
		TEST_CHECK(pb.capacity() > 0);
		TEST_EQUAL(pb.size(), 1);
		TEST_EQUAL(pb.span(), 1);
		TEST_EQUAL(pb.cursor(), 123);

		pb.insert(125, (void*)125);

		TEST_CHECK(pb.at(125) == (void*)125);
		TEST_EQUAL(pb.size(), 2);
		TEST_EQUAL(pb.span(), 3);
		TEST_EQUAL(pb.cursor(), 123);

		pb.insert(500, (void*)500);
		TEST_EQUAL(pb.size(), 3);
		TEST_EQUAL(pb.span(), 501 - 123);
		TEST_EQUAL(pb.capacity(), 512);

		pb.insert(500, (void*)501);
		TEST_EQUAL(pb.size(), 3);
		pb.insert(500, (void*)500);
		TEST_EQUAL(pb.size(), 3);

		TEST_CHECK(pb.remove(123) == (void*)123);
		TEST_EQUAL(pb.size(), 2);
		TEST_EQUAL(pb.span(), 501 - 125);
		TEST_EQUAL(pb.cursor(), 125);
		TEST_CHECK(pb.remove(125) == (void*)125);
		TEST_EQUAL(pb.size(), 1);
		TEST_EQUAL(pb.span(), 1);
		TEST_EQUAL(pb.cursor(), 500);

		TEST_CHECK(pb.remove(500) == (void*)500);
		TEST_EQUAL(pb.size(), 0);
		TEST_EQUAL(pb.span(), 0);

		for (int i = 0; i < 0xff; ++i)
		{
			int index = (i + 0xfff0) & 0xffff;
			pb.insert(index, (void*)(index + 1));
			fprintf(stderr, "insert: %u (mask: %x)\n", index, int(pb.capacity() - 1));
			TEST_EQUAL(pb.capacity(), 512);
			if (i >= 14)
			{
				index = (index - 14) & 0xffff;
				fprintf(stderr, "remove: %u\n", index);
				TEST_CHECK(pb.remove(index) == (void*)(index + 1));
				TEST_EQUAL(pb.size(), 14);
			}
		}
	}

	{
		// test wrapping the indices
		packet_buffer pb;

		TEST_EQUAL(pb.size(), 0);

		pb.insert(0xfffe, (void*)1);
		TEST_CHECK(pb.at(0xfffe) == (void*)1);

		pb.insert(2, (void*)2);
		TEST_CHECK(pb.at(2) == (void*)2);

		pb.remove(0xfffe);
		TEST_CHECK(pb.at(0xfffe) == (void*)0);
		TEST_CHECK(pb.at(2) == (void*)2);
	}

	{
		// test wrapping the indices
		packet_buffer pb;

		TEST_EQUAL(pb.size(), 0);

		pb.insert(0xfff3, (void*)1);
		TEST_CHECK(pb.at(0xfff3) == (void*)1);

		int new_index = (0xfff3 + pb.capacity()) & 0xffff;
		pb.insert(new_index, (void*)2);
		TEST_CHECK(pb.at(new_index) == (void*)2);

		void* old = pb.remove(0xfff3);
		TEST_CHECK(old == (void*)1);
		TEST_CHECK(pb.at(0xfff3) == (void*)0);
		TEST_CHECK(pb.at(new_index) == (void*)2);
	}

	TEST_CHECK(error_code(errors::http_error).message() == "HTTP error");
	TEST_CHECK(error_code(errors::missing_file_sizes).message() == "missing or invalid 'file sizes' entry");
	TEST_CHECK(error_code(errors::unsupported_protocol_version).message() == "unsupported protocol version");
	TEST_CHECK(error_code(errors::no_i2p_router).message() == "no i2p router is set up");
	TEST_CHECK(error_code(errors::http_parse_error).message() == "Invalid HTTP header");
	TEST_CHECK(error_code(errors::error_code_max).message() == "Unknown error");

	TEST_CHECK(errors::reserved129 == 129);
	TEST_CHECK(errors::reserved159 == 159);
	TEST_CHECK(errors::reserved114 == 114);

	{
	// test session state load/restore
	session* s = new session(fingerprint("LT",0,0,0,0), 0);

	session_settings sett;
	sett.user_agent = "test";
	sett.tracker_receive_timeout = 1234;
	sett.file_pool_size = 543;
	sett.urlseed_wait_retry = 74;
	sett.file_pool_size = 754;
	sett.initial_picker_threshold = 351;
	sett.upnp_ignore_nonrouters = 5326;
	sett.coalesce_writes = 623;
	sett.auto_scrape_interval = 753;
	sett.close_redundant_connections = 245;
	sett.auto_scrape_interval = 235;
	sett.auto_scrape_min_interval = 62;
	s->set_settings(sett);

#ifndef TORRENT_DISABLE_DHT
	dht_settings dhts;
	dhts.max_peers_reply = 70;
	s->set_dht_settings(dhts);
#endif
/*
#ifndef TORRENT_DISABLE_DHT
	dht_settings dht_sett;
	s->set_dht_settings(dht_sett);
#endif
*/
	entry session_state;
	s->save_state(session_state);

	// test magnet link parsing
	add_torrent_params p;
	p.save_path = ".";
	error_code ec;
	p.url = "magnet:?xt=urn:btih:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
		"&tr=http://1"
		"&tr=http://2"
		"&tr=http://3"
		"&dn=foo"
		"&dht=127.0.0.1:43";
	torrent_handle t = s->add_torrent(p, ec);
	TEST_CHECK(!ec);
	if (ec) fprintf(stderr, "%s\n", ec.message().c_str());

	std::vector<announce_entry> trackers = t.trackers();
	TEST_EQUAL(trackers.size(), 3);
	if (trackers.size() > 0)
	{
		TEST_EQUAL(trackers[0].url, "http://1");
		fprintf(stderr, "1: %s\n", trackers[0].url.c_str());
	}
	if (trackers.size() > 1)
	{
		TEST_EQUAL(trackers[1].url, "http://2");
		fprintf(stderr, "2: %s\n", trackers[1].url.c_str());
	}
	if (trackers.size() > 2)
	{
		TEST_EQUAL(trackers[2].url, "http://3");
		fprintf(stderr, "3: %s\n", trackers[2].url.c_str());
	}

	p.url = "magnet:"
		"?tr=http://1"
		"&tr=http://2"
		"&dn=foo"
		"&dht=127.0.0.1:43"
		"&xt=urn:btih:c352cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd";
	torrent_handle t2 = s->add_torrent(p, ec);
	TEST_CHECK(!ec);
	if (ec) fprintf(stderr, "%s\n", ec.message().c_str());

	trackers = t2.trackers();
	TEST_EQUAL(trackers.size(), 2);

	p.url = "magnet:"
		"?tr=udp%3A%2F%2Ftracker.openbittorrent.com%3A80"
		"&tr=udp%3A%2F%2Ftracker.publicbt.com%3A80"
		"&tr=udp%3A%2F%2Ftracker.ccc.de%3A80"
		"&xt=urn:btih:a38d02c287893842a32825aa866e00828a318f07"
		"&dn=Ubuntu+11.04+%28Final%29";
	torrent_handle t3 = s->add_torrent(p, ec);
	TEST_CHECK(!ec);
	if (ec) fprintf(stderr, "%s\n", ec.message().c_str());

	trackers = t3.trackers();
	TEST_EQUAL(trackers.size(), 3);
	if (trackers.size() > 0)
	{
		TEST_EQUAL(trackers[0].url, "udp://tracker.openbittorrent.com:80");
		fprintf(stderr, "1: %s\n", trackers[0].url.c_str());
	}
	if (trackers.size() > 1)
	{
		TEST_EQUAL(trackers[1].url, "udp://tracker.publicbt.com:80");
		fprintf(stderr, "2: %s\n", trackers[1].url.c_str());
	}
	if (trackers.size() > 2)
	{
		TEST_EQUAL(trackers[2].url, "udp://tracker.ccc.de:80");
		fprintf(stderr, "3: %s\n", trackers[2].url.c_str());
	}

	TEST_EQUAL(to_hex(t.info_hash().to_string()), "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");

	delete s;
	s = new session(fingerprint("LT",0,0,0,0), 0);

	std::vector<char> buf;
	bencode(std::back_inserter(buf), session_state);
	lazy_entry session_state2;
	ret = lazy_bdecode(&buf[0], &buf[0] + buf.size(), session_state2, ec);
	TEST_CHECK(ret == 0);

	fprintf(stderr, "session_state\n%s\n", print_entry(session_state2).c_str());

	// make sure settings that haven't been changed from their defaults are not saved
	TEST_CHECK(session_state2.dict_find("settings")->dict_find("optimistic_disk_retry") == 0);

	s->load_state(session_state2);
#define CMP_SET(x) TEST_CHECK(s->settings().x == sett.x)

	CMP_SET(user_agent);
	CMP_SET(tracker_receive_timeout);
	CMP_SET(file_pool_size);
	CMP_SET(urlseed_wait_retry);
	CMP_SET(file_pool_size);
	CMP_SET(initial_picker_threshold);
	CMP_SET(upnp_ignore_nonrouters);
	CMP_SET(coalesce_writes);
	CMP_SET(auto_scrape_interval);
	CMP_SET(close_redundant_connections);
	CMP_SET(auto_scrape_interval);
	CMP_SET(auto_scrape_min_interval);
	CMP_SET(max_peerlist_size);
	CMP_SET(max_paused_peerlist_size);
	CMP_SET(min_announce_interval);
	CMP_SET(prioritize_partial_pieces);
	CMP_SET(auto_manage_startup);
	CMP_SET(rate_limit_ip_overhead);
	CMP_SET(announce_to_all_trackers);
	CMP_SET(announce_to_all_tiers);
	CMP_SET(prefer_udp_trackers);
	CMP_SET(strict_super_seeding);
	CMP_SET(seeding_piece_quota);
	delete s;
	}

	// test path functions
	TEST_EQUAL(combine_path("test1/", "test2"), "test1/test2");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(combine_path("test1\\", "test2"), "test1\\test2");
	TEST_EQUAL(combine_path("test1", "test2"), "test1\\test2");
#else
	TEST_EQUAL(combine_path("test1", "test2"), "test1/test2");
#endif

#if TORRENT_USE_UNC_PATHS
	TEST_EQUAL(canonicalize_path("c:\\a\\..\\b"), "c:\\b");
	TEST_EQUAL(canonicalize_path("a\\..\\b"), "b");
	TEST_EQUAL(canonicalize_path("a\\..\\.\\b"), "b");
	TEST_EQUAL(canonicalize_path("\\.\\a"), "\\a");
	TEST_EQUAL(canonicalize_path("\\\\bla\\.\\a"), "\\\\bla\\a");
	TEST_EQUAL(canonicalize_path("c:\\bla\\a"), "c:\\bla\\a");
#endif

	TEST_EQUAL(extension("blah"), "");
	TEST_EQUAL(extension("blah.exe"), ".exe");
	TEST_EQUAL(extension("blah.foo.bar"), ".bar");
	TEST_EQUAL(extension("blah.foo."), ".");
	TEST_EQUAL(extension("blah.foo/bar"), "");

	TEST_EQUAL(filename("blah"), "blah");
	TEST_EQUAL(filename("/blah/foo/bar"), "bar");
	TEST_EQUAL(filename("/blah/foo/bar/"), "bar");
	TEST_EQUAL(filename("blah/"), "blah");

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(is_root_path("c:\\blah"), false);
	TEST_EQUAL(is_root_path("c:\\"), true);
	TEST_EQUAL(is_root_path("\\\\"), true);
	TEST_EQUAL(is_root_path("\\\\foobar"), true);
	TEST_EQUAL(is_root_path("\\\\foobar\\"), true);
	TEST_EQUAL(is_root_path("\\\\foobar/"), true);
	TEST_EQUAL(is_root_path("\\\\foo/bar"), false);
	TEST_EQUAL(is_root_path("\\\\foo\\bar\\"), false);
#else
	TEST_EQUAL(is_root_path("/blah"), false);
	TEST_EQUAL(is_root_path("/"), true);
#endif

	// if has_parent_path() returns false
	// parent_path() should return the empty string
	TEST_EQUAL(parent_path("blah"), "");
	TEST_EQUAL(has_parent_path("blah"), false);
	TEST_EQUAL(parent_path("/blah/foo/bar"), "/blah/foo/");
	TEST_EQUAL(has_parent_path("/blah/foo/bar"), true);
	TEST_EQUAL(parent_path("/blah/foo/bar/"), "/blah/foo/");
	TEST_EQUAL(has_parent_path("/blah/foo/bar/"), true);
	TEST_EQUAL(parent_path("/a"), "/");
	TEST_EQUAL(has_parent_path("/a"), true);
	TEST_EQUAL(parent_path("/"), "");
	TEST_EQUAL(has_parent_path("/"), false);
	TEST_EQUAL(parent_path(""), "");
	TEST_EQUAL(has_parent_path(""), false);
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(parent_path("\\\\"), "");
	TEST_EQUAL(has_parent_path("\\\\"), false);
	TEST_EQUAL(parent_path("c:\\"), "");
	TEST_EQUAL(has_parent_path("c:\\"), false);
	TEST_EQUAL(parent_path("c:\\a"), "c:\\");
	TEST_EQUAL(has_parent_path("c:\\a"), true);
	TEST_EQUAL(has_parent_path("\\\\a"), false);
	TEST_EQUAL(has_parent_path("\\\\foobar/"), false);
	TEST_EQUAL(has_parent_path("\\\\foobar\\"), false);
	TEST_EQUAL(has_parent_path("\\\\foo/bar\\"), true);
#endif

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(is_complete("c:\\"), true);
	TEST_EQUAL(is_complete("c:\\foo\\bar"), true);
	TEST_EQUAL(is_complete("\\\\foo\\bar"), true);
	TEST_EQUAL(is_complete("foo/bar"), false);
	TEST_EQUAL(is_complete("\\\\"), true);
#else
	TEST_EQUAL(is_complete("/foo/bar"), true);
	TEST_EQUAL(is_complete("foo/bar"), false);
	TEST_EQUAL(is_complete("/"), true);
	TEST_EQUAL(is_complete(""), false);
#endif

	// test split_string

	char const* tags[10];
	char tags_str[] = "  this  is\ta test\t string\x01to be split  and it cannot "
		"extend over the limit of elements \t";
	ret = split_string(tags, 10, tags_str);

	TEST_CHECK(ret == 10);
	TEST_CHECK(strcmp(tags[0], "this") == 0);
	TEST_CHECK(strcmp(tags[1], "is") == 0);
	TEST_CHECK(strcmp(tags[2], "a") == 0);
	TEST_CHECK(strcmp(tags[3], "test") == 0);
	TEST_CHECK(strcmp(tags[4], "string") == 0);
	TEST_CHECK(strcmp(tags[5], "to") == 0);
	TEST_CHECK(strcmp(tags[6], "be") == 0);
	TEST_CHECK(strcmp(tags[7], "split") == 0);
	TEST_CHECK(strcmp(tags[8], "and") == 0);
	TEST_CHECK(strcmp(tags[9], "it") == 0);

	// test snprintf

	char msg[10];
	snprintf(msg, sizeof(msg), "too %s format string", "long");
	TEST_CHECK(strcmp(msg, "too long ") == 0);

	// test maybe_url_encode

	TEST_EQUAL(maybe_url_encode("http://test:test@abc.com/abc<>abc"), "http://test:test@abc.com:80/abc%3c%3eabc");
	TEST_EQUAL(maybe_url_encode("http://abc.com/foo bar"), "http://abc.com:80/foo%20bar");
	TEST_EQUAL(maybe_url_encode("abc"), "abc");
	TEST_EQUAL(maybe_url_encode("http://abc.com/abc"), "http://abc.com/abc");
	
	// test sanitize_path

#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize_path("/a/b/c"), "a\\b\\c");
	TEST_EQUAL(sanitize_path("a/../c"), "a\\c");
#else
	TEST_EQUAL(sanitize_path("/a/b/c"), "a/b/c");
	TEST_EQUAL(sanitize_path("a/../c"), "a/c");
#endif
	TEST_EQUAL(sanitize_path("/.././c"), "c");
	TEST_EQUAL(sanitize_path("dev:"), "");
	TEST_EQUAL(sanitize_path("c:/b"), "b");
#ifdef TORRENT_WINDOWS
	TEST_EQUAL(sanitize_path("c:\\.\\c"), "c");
	TEST_EQUAL(sanitize_path("\\c"), "c");
#else
	TEST_EQUAL(sanitize_path("//./c"), "c");
#endif

	// make sure the time classes have correct semantics

	TEST_EQUAL(total_milliseconds(milliseconds(100)), 100);
	TEST_EQUAL(total_milliseconds(milliseconds(1)),  1);
	TEST_EQUAL(total_milliseconds(seconds(1)), 1000);


	if (supports_ipv6())
	{
		// make sure the assumption we use in policy's peer list hold
		std::multimap<address, int> peers;
		std::multimap<address, int>::iterator i;
		peers.insert(std::make_pair(address::from_string("::1", ec), 0));
		peers.insert(std::make_pair(address::from_string("::2", ec), 3));
		peers.insert(std::make_pair(address::from_string("::3", ec), 5));
		i = peers.find(address::from_string("::2", ec));
		TEST_CHECK(i != peers.end());
		if (i != peers.end())
		{
			TEST_CHECK(i->first == address::from_string("::2", ec));
			TEST_CHECK(i->second == 3);
		}
	}

	// test identify_client

	TEST_CHECK(identify_client(peer_id("-AZ1234-............")) == "Azureus 1.2.3.4");
	TEST_CHECK(identify_client(peer_id("-AZ1230-............")) == "Azureus 1.2.3");
	TEST_CHECK(identify_client(peer_id("S123--..............")) == "Shadow 1.2.3");
	TEST_CHECK(identify_client(peer_id("M1-2-3--............")) == "Mainline 1.2.3");

	// test to/from hex conversion

	char const* str = "0123456789012345678901234567890123456789";
	char bin[20];
	TEST_CHECK(from_hex(str, 40, bin));
	char hex[41];
	to_hex(bin, 20, hex);
	TEST_CHECK(strcmp(hex, str) == 0);

	// test is_space

	TEST_CHECK(!is_space('C'));
	TEST_CHECK(!is_space('\b'));
	TEST_CHECK(!is_space('8'));
	TEST_CHECK(!is_space('='));
	TEST_CHECK(is_space(' '));
	TEST_CHECK(is_space('\t'));
	TEST_CHECK(is_space('\n'));
	TEST_CHECK(is_space('\r'));

	// test to_lower

	TEST_CHECK(to_lower('C') == 'c');
	TEST_CHECK(to_lower('c') == 'c');
	TEST_CHECK(to_lower('-') == '-');
	TEST_CHECK(to_lower('&') == '&');

	// test string_equal_no_case

	TEST_CHECK(string_equal_no_case("foobar", "FoobAR"));
	TEST_CHECK(string_equal_no_case("foobar", "foobar"));
	TEST_CHECK(!string_equal_no_case("foobar", "foobar "));
	TEST_CHECK(!string_equal_no_case("foobar", "F00"));

	// test string_begins_no_case

	TEST_CHECK(string_begins_no_case("foobar", "FoobAR --"));
	TEST_CHECK(!string_begins_no_case("foobar", "F00"));

	// test itoa

	TEST_CHECK(to_string(345).elems == std::string("345"));
	TEST_CHECK(to_string(-345).elems == std::string("-345"));
	TEST_CHECK(to_string(0).elems == std::string("0"));
	TEST_CHECK(to_string(1000000000).elems == std::string("1000000000"));

	// test url parsing

	TEST_CHECK(parse_url_components("http://foo:bar@host.com:80/path/to/file", ec)
		== make_tuple("http", "foo:bar", "host.com", 80, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://host.com/path/to/file", ec)
		== make_tuple("http", "", "host.com", 80, "/path/to/file"));

	TEST_CHECK(parse_url_components("ftp://host.com:21/path/to/file", ec)
		== make_tuple("ftp", "", "host.com", 21, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://host.com/path?foo:bar@foo:", ec)
		== make_tuple("http", "", "host.com", 80, "/path?foo:bar@foo:"));

	TEST_CHECK(parse_url_components("http://192.168.0.1/path/to/file", ec)
		== make_tuple("http", "", "192.168.0.1", 80, "/path/to/file"));

	TEST_CHECK(parse_url_components("http://[2001:ff00::1]:42/path/to/file", ec)
		== make_tuple("http", "", "[2001:ff00::1]", 42, "/path/to/file"));

	// base64 test vectors from http://www.faqs.org/rfcs/rfc4648.html

	TEST_CHECK(base64encode("") == "");
	TEST_CHECK(base64encode("f") == "Zg==");
	TEST_CHECK(base64encode("fo") == "Zm8=");
	TEST_CHECK(base64encode("foo") == "Zm9v");
	TEST_CHECK(base64encode("foob") == "Zm9vYg==");
	TEST_CHECK(base64encode("fooba") == "Zm9vYmE=");
	TEST_CHECK(base64encode("foobar") == "Zm9vYmFy");

	// base32 test vectors from http://www.faqs.org/rfcs/rfc4648.html

   TEST_CHECK(base32encode("") == "");
   TEST_CHECK(base32encode("f") == "MY======");
   TEST_CHECK(base32encode("fo") == "MZXQ====");
   TEST_CHECK(base32encode("foo") == "MZXW6===");
   TEST_CHECK(base32encode("foob") == "MZXW6YQ=");
   TEST_CHECK(base32encode("fooba") == "MZXW6YTB");
   TEST_CHECK(base32encode("foobar") == "MZXW6YTBOI======");

   TEST_CHECK(base32decode("") == "");
   TEST_CHECK(base32decode("MY======") == "f");
   TEST_CHECK(base32decode("MZXQ====") == "fo");
   TEST_CHECK(base32decode("MZXW6===") == "foo");
   TEST_CHECK(base32decode("MZXW6YQ=") == "foob");
   TEST_CHECK(base32decode("MZXW6YTB") == "fooba");
   TEST_CHECK(base32decode("MZXW6YTBOI======") == "foobar");

   TEST_CHECK(base32decode("MY") == "f");
   TEST_CHECK(base32decode("MZXW6YQ") == "foob");
   TEST_CHECK(base32decode("MZXW6YTBOI") == "foobar");
   TEST_CHECK(base32decode("mZXw6yTBO1======") == "foobar");

	std::string test;
	for (int i = 0; i < 255; ++i)
		test += char(i);

	TEST_CHECK(base32decode(base32encode(test)) == test);

	// url_has_argument

	TEST_CHECK(url_has_argument("http://127.0.0.1/test", "test") == "");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24", "bar") == "");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24", "foo") == "24");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24&bar=23", "foo") == "24");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24&bar=23", "bar") == "23");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24&bar=23&a=e", "bar") == "23");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24&bar=23&a=e", "a") == "e");
	TEST_CHECK(url_has_argument("http://127.0.0.1/test?foo=24&bar=23&a=e", "b") == "");

	// escape_string
	char const* test_string = "!@#$%^&*()-_=+/,. %?";
	TEST_EQUAL(escape_string(test_string, strlen(test_string))
		, "!%40%23%24%25%5e%26*()-_%3d%2b%2f%2c.%20%25%3f");

	// escape_path
	TEST_EQUAL(escape_path(test_string, strlen(test_string))
		, "!%40%23%24%25%5e%26*()-_%3d%2b/%2c.%20%25%3f");

	TEST_CHECK(unescape_string(escape_path(test_string, strlen(test_string)), ec) == test_string);
	TEST_CHECK(!ec);

	// need_encoding
	char const* test_string2 = "!@$&()-_/,.%?";
	TEST_CHECK(need_encoding(test_string, strlen(test_string)) == true);
	TEST_CHECK(need_encoding(test_string2, strlen(test_string2)) == false);
	TEST_CHECK(need_encoding("\n", 1) == true);

	// maybe_url_encode
	TEST_EQUAL(maybe_url_encode("http://bla.com/\n"), "http://bla.com:80/%0a");
	TEST_EQUAL(maybe_url_encode("http://bla.com/foo%20bar"), "http://bla.com/foo%20bar");
	TEST_EQUAL(maybe_url_encode("http://bla.com/foo%20bar?k=v&k2=v2"), "http://bla.com/foo%20bar?k=v&k2=v2");
	TEST_EQUAL(maybe_url_encode("?&"), "?&");

	// unescape_string
	TEST_CHECK(unescape_string(escape_string(test_string, strlen(test_string)), ec)
		== test_string);
	std::cerr << unescape_string(escape_string(test_string, strlen(test_string)), ec) << std::endl;

	// verify_encoding
	test = "\b?filename=4";
	TEST_CHECK(!verify_encoding(test));
#ifdef TORRENT_WINDOWS
	TEST_CHECK(test == "__filename=4");
#else
	TEST_CHECK(test == "_?filename=4");
#endif

	test = "filename=4";
	TEST_CHECK(verify_encoding(test));
	TEST_CHECK(test == "filename=4");

	// valid 2-byte sequence
	test = "filename\xc2\xa1";
	TEST_CHECK(verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename\xc2\xa1");

	// truncated 2-byte sequence
	test = "filename\xc2";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// valid 3-byte sequence
	test = "filename\xe2\x9f\xb9";
	TEST_CHECK(verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename\xe2\x9f\xb9");

	// truncated 3-byte sequence
	test = "filename\xe2\x9f";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// truncated 3-byte sequence
	test = "filename\xe2";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// valid 4-byte sequence
	test = "filename\xf0\x9f\x92\x88";
	TEST_CHECK(verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename\xf0\x9f\x92\x88");

	// truncated 4-byte sequence
	test = "filename\xf0\x9f\x92";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_");

	// 5-byte utf-8 sequence (not allowed)
	test = "filename\xf8\x9f\x9f\x9f\x9f""foobar";
	TEST_CHECK(!verify_encoding(test));
	fprintf(stderr, "%s\n", test.c_str());
	TEST_CHECK(test == "filename_____foobar");

	// replace_extension
	test = "foo.bar";
	replace_extension(test, "txt");
	TEST_EQUAL(test, "foo.txt");

	test = "_";
	replace_extension(test, "txt");
	TEST_EQUAL(test, "_.txt");

	test = "1.2.3/_";
	replace_extension(test, "txt");
	TEST_EQUAL(test, "1.2.3/_.txt");

	// file class
	file f;
#if TORRENT_USE_UNC_PATHS || !defined WIN32
	TEST_CHECK(f.open("con", file::read_write, ec));
#else
	TEST_CHECK(f.open("test_file", file::read_write, ec));
#endif
	TEST_CHECK(!ec);
	file::iovec_t b = {(void*)"test", 4};
	TEST_CHECK(f.writev(0, &b, 1, ec) == 4);
	TEST_CHECK(!ec);
	char test_buf[5] = {0};
	b.iov_base = test_buf;
	b.iov_len = 4;
	TEST_CHECK(f.readv(0, &b, 1, ec) == 4);
	TEST_CHECK(!ec);
	TEST_CHECK(strcmp(test_buf, "test") == 0);
	f.close();

	// HTTP request parser
	http_parser parser;
	boost::tuple<int, int, bool> received;

	received = feed_bytes(parser
		, "HTTP/1.1 200 OK\r\n"
		"Content-Length: 4\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n"
		"test");

	TEST_CHECK(received == make_tuple(4, 64, false));
	TEST_CHECK(parser.finished());
	TEST_CHECK(std::equal(parser.get_body().begin, parser.get_body().end, "test"));
	TEST_CHECK(parser.header("content-type") == "text/plain");
	TEST_CHECK(atoi(parser.header("content-length").c_str()) == 4);

	parser.reset();

	TEST_CHECK(!parser.finished());

	char const* upnp_response =
		"HTTP/1.1 200 OK\r\n"
		"ST:upnp:rootdevice\r\n"
		"USN:uuid:000f-66d6-7296000099dc::upnp:rootdevice\r\n"
		"Location: http://192.168.1.1:5431/dyndev/uuid:000f-66d6-7296000099dc\r\n"
		"Server: Custom/1.0 UPnP/1.0 Proc/Ver\r\n"
		"EXT:\r\n"
		"Cache-Control:max-age=180\r\n"
		"DATE: Fri, 02 Jan 1970 08:10:38 GMT\r\n\r\n";

	received = feed_bytes(parser, upnp_response);

	TEST_CHECK(received == make_tuple(0, int(strlen(upnp_response)), false));
	TEST_CHECK(parser.get_body().left() == 0);
	TEST_CHECK(parser.header("st") == "upnp:rootdevice");
	TEST_CHECK(parser.header("location")
		== "http://192.168.1.1:5431/dyndev/uuid:000f-66d6-7296000099dc");
	TEST_CHECK(parser.header("ext") == "");
	TEST_CHECK(parser.header("date") == "Fri, 02 Jan 1970 08:10:38 GMT");

	parser.reset();
	TEST_CHECK(!parser.finished());

	char const* upnp_notify =
		"NOTIFY * HTTP/1.1\r\n"
		"Host:239.255.255.250:1900\r\n"
		"NT:urn:schemas-upnp-org:device:MediaServer:1\r\n"
		"NTS:ssdp:alive\r\n"
		"Location:http://10.0.1.15:2353/upnphost/udhisapi.dll?content=uuid:c17f2c31-d19b-4912-af94-651945c8a84e\r\n"
		"USN:uuid:c17f0c32-d1db-4be8-ae94-25f94583026e::urn:schemas-upnp-org:device:MediaServer:1\r\n"
		"Cache-Control:max-age=900\r\n"
		"Server:Microsoft-Windows-NT/5.1 UPnP/1.0 UPnP-Device-Host/1.0\r\n";

	received = feed_bytes(parser, upnp_notify);

	TEST_CHECK(received == make_tuple(0, int(strlen(upnp_notify)), false));
	TEST_CHECK(parser.method() == "notify");
	TEST_CHECK(parser.path() == "*");

	parser.reset();
	TEST_CHECK(!parser.finished());

	char const* bt_lsd = "BT-SEARCH * HTTP/1.1\r\n"
		"Host: 239.192.152.143:6771\r\n"
		"Port: 6881\r\n"
		"Infohash: 12345678901234567890\r\n"
		"\r\n";

	received = feed_bytes(parser, bt_lsd);

	TEST_CHECK(received == make_tuple(0, int(strlen(bt_lsd)), false));
	TEST_CHECK(parser.method() == "bt-search");
	TEST_CHECK(parser.path() == "*");
	TEST_CHECK(atoi(parser.header("port").c_str()) == 6881);
	TEST_CHECK(parser.header("infohash") == "12345678901234567890");

	TEST_CHECK(parser.finished());

	parser.reset();
	TEST_CHECK(!parser.finished());

	// test chunked encoding
	char const* chunked_test = "HTTP/1.1 200 OK\r\n"
		"Content-Length: 20\r\n"
		"Content-Type: text/plain\r\n"
		"Transfer-Encoding: chunked\r\n"
		"\r\n"
		"4\r\n"
		"test\r\n"
		"10\r\n"
		"0123456789abcdef\r\n"
		"0\r\n"
		"Test-header: foobar\r\n"
		"\r\n";

	received = feed_bytes(parser, chunked_test);

	printf("payload: %d protocol: %d\n", received.get<0>(), received.get<1>());
	TEST_CHECK(received == make_tuple(20, strlen(chunked_test) - 20, false));
	TEST_CHECK(parser.finished());
	TEST_CHECK(std::equal(parser.get_body().begin, parser.get_body().end
		, "4\r\ntest\r\n10\r\n0123456789abcdef"));
	TEST_CHECK(parser.header("test-header") == "foobar");
	TEST_CHECK(parser.header("content-type") == "text/plain");
	TEST_CHECK(atoi(parser.header("content-length").c_str()) == 20);
	TEST_CHECK(parser.chunked_encoding());
	typedef std::pair<size_type, size_type> chunk_range;
	std::vector<chunk_range> cmp;
	cmp.push_back(chunk_range(96, 100));
	cmp.push_back(chunk_range(106, 122));
	TEST_CHECK(cmp == parser.chunks());

	// make sure we support trackers with incorrect line endings
	char const* tracker_response =
		"HTTP/1.1 200 OK\n"
		"content-length: 5\n"
		"content-type: test/plain\n"
		"\n"
		"\ntest";

	received = feed_bytes(parser, tracker_response);

	TEST_CHECK(received == make_tuple(5, int(strlen(tracker_response) - 5), false));
	TEST_CHECK(parser.get_body().left() == 5);

	parser.reset();

	// make sure we support content-range responses
	// and that we're case insensitive
	char const* web_seed_response =
		"HTTP/1.1 206 OK\n"
		"contEnt-rAngE: bYTes 0-4\n"
		"conTent-TyPe: test/plain\n"
		"\n"
		"\ntest";

	received = feed_bytes(parser, web_seed_response);

	TEST_CHECK(received == make_tuple(5, int(strlen(web_seed_response) - 5), false));
	TEST_CHECK(parser.content_range() == (std::pair<size_type, size_type>(0, 4)));
	TEST_CHECK(parser.content_length() == 5);

	parser.reset();

	// make sure we support content-range responses
	// and that we're case insensitive
	char const* one_hundred_response =
		"HTTP/1.1 100 Continue\n"
		"\r\n"
		"HTTP/1.1 200 OK\n"
		"Content-Length: 4\r\n"
		"Content-Type: test/plain\r\n"
		"\r\n"
		"test";

	received = feed_bytes(parser, one_hundred_response);

	TEST_CHECK(received == make_tuple(4, int(strlen(one_hundred_response) - 4), false));
	TEST_EQUAL(parser.content_length(), 4);

	{
		// test chunked encoding parser
		char const chunk_header1[] = "f;this is a comment\r\n";
		size_type chunk_size;
		int header_size;
		bool ret = parser.parse_chunk_header(buffer::const_interval(chunk_header1, chunk_header1 + 10)
			, &chunk_size, &header_size);
		TEST_EQUAL(ret, false);
		ret = parser.parse_chunk_header(buffer::const_interval(chunk_header1, chunk_header1 + sizeof(chunk_header1))
			, &chunk_size, &header_size);
		TEST_EQUAL(ret, true);
		TEST_EQUAL(chunk_size, 15);
		TEST_EQUAL(header_size, sizeof(chunk_header1) - 1);

		char const chunk_header2[] =
			"0;this is a comment\r\n"
			"test1: foo\r\n"
			"test2: bar\r\n"
			"\r\n";

		ret = parser.parse_chunk_header(buffer::const_interval(chunk_header2, chunk_header2 + sizeof(chunk_header2))
			, &chunk_size, &header_size);
		TEST_EQUAL(ret, true);
		TEST_EQUAL(chunk_size, 0);
		TEST_EQUAL(header_size, sizeof(chunk_header2) - 1);

		TEST_EQUAL(parser.headers().find("test1")->second, "foo");
		TEST_EQUAL(parser.headers().find("test2")->second, "bar");
	}

	// test xml parser
	char xml1[] = "<a>foo<b/>bar</a>";
	std::string out1;

	xml_parse(xml1, xml1 + sizeof(xml1) - 1, boost::bind(&parser_callback
		, boost::ref(out1), _1, _2, _3));
	std::cerr << out1 << std::endl;
	TEST_CHECK(out1 == "BaSfooEbSbarFa");

	char xml2[] = "<?xml version = \"1.0\"?><c x=\"1\" \t y=\"3\"/><d foo='bar'></d boo='foo'><!--comment-->";
	std::string out2;

	xml_parse(xml2, xml2 + sizeof(xml2) - 1, boost::bind(&parser_callback
		, boost::ref(out2), _1, _2, _3));
	std::cerr << out2 << std::endl;
	TEST_CHECK(out2 == "DxmlAversionV1.0EcAxV1AyV3BdAfooVbarFdAbooVfooCcomment");

	char xml3[] = "<a f=1>foo</a f='b>";
	std::string out3;

	xml_parse(xml3, xml3 + sizeof(xml3) - 1, boost::bind(&parser_callback
		, boost::ref(out3), _1, _2, _3));
	std::cerr << out3 << std::endl;
	TEST_CHECK(out3 == "BaPunquoted attribute valueSfooFaPmissing end quote on attribute");

	char xml4[] = "<a  f>foo</a  v  >";
	std::string out4;

	xml_parse(xml4, xml4 + sizeof(xml4) - 1, boost::bind(&parser_callback
		, boost::ref(out4), _1, _2, _3));
	std::cerr << out4 << std::endl;
	TEST_CHECK(out4 == "BaTfSfooFaTv  ");

	// test upnp xml parser

	parse_state xml_s;
	xml_s.reset("urn:schemas-upnp-org:service:WANIPConnection:1");
	xml_parse(upnp_xml, upnp_xml + sizeof(upnp_xml)
		, boost::bind(&find_control_url, _1, _2, boost::ref(xml_s)));

	std::cerr << "namespace " << xml_s.service_type << std::endl;
	std::cerr << "url_base: " << xml_s.url_base << std::endl;
	std::cerr << "control_url: " << xml_s.control_url << std::endl;
	std::cerr << "model: " << xml_s.model << std::endl;
	TEST_CHECK(xml_s.url_base == "http://192.168.0.1:5678");
	TEST_CHECK(xml_s.control_url == "/WANIPConnection");
	TEST_CHECK(xml_s.model == "D-Link Router");

	xml_s.reset("urn:schemas-upnp-org:service:WANPPPConnection:1");
	xml_parse(upnp_xml2, upnp_xml2 + sizeof(upnp_xml2)
		, boost::bind(&find_control_url, _1, _2, boost::ref(xml_s)));

	std::cerr << "namespace " << xml_s.service_type << std::endl;
	std::cerr << "url_base: " << xml_s.url_base << std::endl;
	std::cerr << "control_url: " << xml_s.control_url << std::endl;
	std::cerr << "model: " << xml_s.model << std::endl;
	TEST_CHECK(xml_s.url_base == "http://192.168.1.1:49152");
	TEST_CHECK(xml_s.control_url == "/upnp/control/WANPPPConn1");
	TEST_CHECK(xml_s.model == "Wireless-G ADSL Home Gateway");

	// test network functions

	TEST_CHECK(is_local(address::from_string("192.168.0.1", ec)));
	TEST_CHECK(is_local(address::from_string("10.1.1.56", ec)));
	TEST_CHECK(!is_local(address::from_string("14.14.251.63", ec)));
	TEST_CHECK(is_loopback(address::from_string("127.0.0.1", ec)));
#if TORRENT_USE_IPV6
	if (supports_ipv6())
	{
		TEST_CHECK(is_loopback(address::from_string("::1", ec)));
		TEST_CHECK(is_any(address_v6::any()));
	}
#endif
	TEST_CHECK(is_any(address_v4::any()));
	TEST_CHECK(!is_any(address::from_string("31.53.21.64", ec)));
	
	TEST_CHECK(match_addr_mask(
		address::from_string("10.0.1.3", ec),
		address::from_string("10.0.3.3", ec),
		address::from_string("255.255.0.0", ec)));

	TEST_CHECK(!match_addr_mask(
		address::from_string("10.0.1.3", ec),
		address::from_string("10.1.3.3", ec),
		address::from_string("255.255.0.0", ec)));

	// test torrent parsing

	entry info;
	info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
	info["name.utf-8"] = "test1";
	info["name"] = "test__";
	info["piece length"] = 16 * 1024;
	info["length"] = 3245;
	entry torrent;
	torrent["info"] = info;

	std::vector<char> buf;
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti(&buf[0], buf.size(), ec);
	std::cerr << ti.name() << std::endl;
	TEST_CHECK(ti.name() == "test1");

#ifdef TORRENT_WINDOWS
	info["name.utf-8"] = "c:/test1/test2/test3";
#else
	info["name.utf-8"] = "/test1/test2/test3";
#endif
	torrent["info"] = info;
	buf.clear();
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti2(&buf[0], buf.size(), ec);
	std::cerr << ti2.name() << std::endl;
#ifdef TORRENT_WINDOWS
	TEST_CHECK(ti2.name() == "test1\\test2\\test3");
#else
	TEST_CHECK(ti2.name() == "test1/test2/test3");
#endif

	info["name.utf-8"] = "test2/../test3/.././../../test4";
	torrent["info"] = info;
	buf.clear();
	bencode(std::back_inserter(buf), torrent);
	torrent_info ti3(&buf[0], buf.size(), ec);
	std::cerr << ti3.name() << std::endl;
#ifdef TORRENT_WINDOWS
	TEST_CHECK(ti3.name() == "test2\\test3\\test4");
#else
	TEST_CHECK(ti3.name() == "test2/test3/test4");
#endif

#ifndef TORRENT_DISABLE_DHT	
	// test kademlia functions

	using namespace libtorrent::dht;

	for (int i = 0; i < 160; i += 8)
	{
		for (int j = 0; j < 160; j += 8)
		{
			node_id a(0);
			a[(159-i) / 8] = 1 << (i & 7);
			node_id b(0);
			b[(159-j) / 8] = 1 << (j & 7);
			int dist = distance_exp(a, b);

			TEST_CHECK(dist >= 0 && dist < 160);
			TEST_CHECK(dist == ((i == j)?0:(std::max)(i, j)));

			for (int k = 0; k < 160; k += 8)
			{
				node_id c(0);
				c[(159-k) / 8] = 1 << (k & 7);

				bool cmp = compare_ref(a, b, c);
				TEST_CHECK(cmp == (distance(a, c) < distance(b, c)));
			}
		}
	}

	// test kademlia routing table
	dht_settings s;
//	s.restrict_routing_ips = false;
	node_id id = to_hash("3123456789abcdef01232456789abcdef0123456");
	dht::routing_table table(id, 10, s);
	std::vector<node_entry> nodes;
	TEST_EQUAL(table.size().get<0>(), 0);

	node_id tmp = id;
	node_id diff = to_hash("15764f7459456a9453f8719b09547c11d5f34061");

	// test a node with the same IP:port changing ID
	add_and_replace(tmp, diff);
	table.node_seen(tmp, udp::endpoint(address::from_string("4.4.4.4"), 4));
	table.find_node(id, nodes, 0, 10);
	TEST_EQUAL(table.bucket_size(0), 1);
	TEST_EQUAL(table.size().get<0>(), 1);
	TEST_EQUAL(nodes.size(), 1);
	if (!nodes.empty())
	{
		TEST_EQUAL(nodes[0].id, tmp);
		TEST_EQUAL(nodes[0].addr, address_v4::from_string("4.4.4.4"));
		TEST_EQUAL(nodes[0].port, 4);
		TEST_EQUAL(nodes[0].timeout_count, 0);
	}

	// set timeout_count to 1
	table.node_failed(tmp, udp::endpoint(address_v4::from_string("4.4.4.4"), 4));

	nodes.clear();
	table.for_each_node(node_push_back, nop, &nodes);
	TEST_EQUAL(nodes.size(), 1);
	if (!nodes.empty())
	{
		TEST_EQUAL(nodes[0].id, tmp);
		TEST_EQUAL(nodes[0].addr, address_v4::from_string("4.4.4.4"));
		TEST_EQUAL(nodes[0].port, 4);
		TEST_EQUAL(nodes[0].timeout_count, 1);
	}

	// add the exact same node again, it should set the timeout_count to 0
	table.node_seen(tmp, udp::endpoint(address::from_string("4.4.4.4"), 4));
	nodes.clear();
	table.for_each_node(node_push_back, nop, &nodes);
	TEST_EQUAL(nodes.size(), 1);
	if (!nodes.empty())
	{
		TEST_EQUAL(nodes[0].id, tmp);
		TEST_EQUAL(nodes[0].addr, address_v4::from_string("4.4.4.4"));
		TEST_EQUAL(nodes[0].port, 4);
		TEST_EQUAL(nodes[0].timeout_count, 0);
	}

	// test adding the same IP:port again with a new node ID (should replace the old one)
	add_and_replace(tmp, diff);
	table.node_seen(tmp, udp::endpoint(address::from_string("4.4.4.4"), 4));
	table.find_node(id, nodes, 0, 10);
	TEST_EQUAL(table.bucket_size(0), 1);
	TEST_EQUAL(nodes.size(), 1);
	if (!nodes.empty())
	{
		TEST_EQUAL(nodes[0].id, tmp);
		TEST_EQUAL(nodes[0].addr, address_v4::from_string("4.4.4.4"));
		TEST_EQUAL(nodes[0].port, 4);
	}

	// test adding the same node ID again with a different IP (should be ignored)
	table.node_seen(tmp, udp::endpoint(address::from_string("4.4.4.4"), 5));
	table.find_node(id, nodes, 0, 10);
	TEST_EQUAL(table.bucket_size(0), 1);
	if (!nodes.empty())
	{
		TEST_EQUAL(nodes[0].id, tmp);
		TEST_EQUAL(nodes[0].addr, address_v4::from_string("4.4.4.4"));
		TEST_EQUAL(nodes[0].port, 4);
	}

	// test adding a node that ends up in the same bucket with an IP
	// very close to the current one (should be ignored)
	// if restrict_routing_ips == true
	table.node_seen(tmp, udp::endpoint(address::from_string("4.4.4.5"), 5));
	table.find_node(id, nodes, 0, 10);
	TEST_EQUAL(table.bucket_size(0), 1);
	if (!nodes.empty())
	{
		TEST_EQUAL(nodes[0].id, tmp);
		TEST_EQUAL(nodes[0].addr, address_v4::from_string("4.4.4.4"));
		TEST_EQUAL(nodes[0].port, 4);
	}

	s.restrict_routing_ips = false;

	add_and_replace(tmp, diff);
	table.node_seen(id, udp::endpoint(rand_v4(), rand()));

	nodes.clear();
	for (int i = 0; i < 7000; ++i)
	{
		table.node_seen(tmp, udp::endpoint(rand_v4(), rand()));
		add_and_replace(tmp, diff);
	}
	TEST_EQUAL(table.num_active_buckets(), 11);
	TEST_CHECK(table.size().get<0>() > 10 * 10);
//#error test num_global_nodes
//#error test need_refresh

#if defined TORRENT_DHT_VERBOSE_LOGGING || defined TORRENT_DEBUG
	table.print_state(std::cerr);
#endif

	table.for_each_node(node_push_back, nop, &nodes);

	std::cout << "nodes: " << nodes.size() << std::endl;

	std::vector<node_entry> temp;

	std::generate(tmp.begin(), tmp.end(), &std::rand);
	table.find_node(tmp, temp, 0, nodes.size() * 2);
	std::cout << "returned: " << temp.size() << std::endl;
	TEST_EQUAL(temp.size(), nodes.size());

	std::generate(tmp.begin(), tmp.end(), &std::rand);
	table.find_node(tmp, temp, 0, 7);
	std::cout << "returned: " << temp.size() << std::endl;
	TEST_EQUAL(temp.size(), 7);

	std::sort(nodes.begin(), nodes.end(), boost::bind(&compare_ref
		, boost::bind(&node_entry::id, _1)
		, boost::bind(&node_entry::id, _2), tmp));

	int hits = 0;
	for (std::vector<node_entry>::iterator i = temp.begin()
		, end(temp.end()); i != end; ++i)
	{
		int hit = std::find_if(nodes.begin(), nodes.end()
			, boost::bind(&node_entry::id, _1) == i->id) - nodes.begin();
//		std::cerr << hit << std::endl;
		if (hit < int(temp.size())) ++hits;
	}
	TEST_CHECK(hits > int(temp.size()) / 2);

	std::generate(tmp.begin(), tmp.end(), &std::rand);
	table.find_node(tmp, temp, 0, 15);
	std::cout << "returned: " << temp.size() << std::endl;
	TEST_EQUAL(int(temp.size()), (std::min)(15, int(nodes.size())));

	std::sort(nodes.begin(), nodes.end(), boost::bind(&compare_ref
		, boost::bind(&node_entry::id, _1)
		, boost::bind(&node_entry::id, _2), tmp));

	hits = 0;
	for (std::vector<node_entry>::iterator i = temp.begin()
		, end(temp.end()); i != end; ++i)
	{
		int hit = std::find_if(nodes.begin(), nodes.end()
			, boost::bind(&node_entry::id, _1) == i->id) - nodes.begin();
//		std::cerr << hit << std::endl;
		if (hit < int(temp.size())) ++hits;
	}
	TEST_CHECK(hits > int(temp.size()) / 2);

	using namespace libtorrent::dht;

	char const* ips[] = {
		"124.31.75.21",
		"21.75.31.124",
		"65.23.51.170",
		"84.124.73.14",
		"43.213.53.83",
	};

	int rs[] = { 1,86,22,65,90 };

	boost::uint8_t prefixes[][3] =
	{
		{ 0x5f, 0xbf, 0xbf },
		{ 0x5a, 0x3c, 0xe9 },
		{ 0xa5, 0xd4, 0x32 },
		{ 0x1b, 0x03, 0x21 },
		{ 0xe5, 0x6f, 0x6c }
	};

	for (int i = 0; i < 5; ++i)
	{
		address a = address_v4::from_string(ips[i]);
		node_id id = generate_id_impl(a, rs[i]);
		TEST_CHECK(id[0] == prefixes[i][0]);
		TEST_CHECK(id[1] == prefixes[i][1]);
		TEST_CHECK((id[2] & 0xf8) == (prefixes[i][2] & 0xf8));

		fprintf(stderr, "%x %x %x\n"
			, prefixes[i][0]
			, prefixes[i][1]
			, prefixes[i][2]
			);
		TEST_CHECK(id[19] == rs[i]);
		fprintf(stderr, "IP address: %s r: %d node ID: %s\n", ips[i]
			, rs[i], to_hex(id.to_string()).c_str());
	}
#endif

	// test peer_id/sha1_hash type

	sha1_hash h1(0);
	sha1_hash h2(0);
	TEST_CHECK(h1 == h2);
	TEST_CHECK(!(h1 != h2));
	TEST_CHECK(!(h1 < h2));
	TEST_CHECK(!(h1 < h2));
	TEST_CHECK(h1.is_all_zeros());

	h1 = to_hash("0123456789012345678901234567890123456789");
	h2 = to_hash("0113456789012345678901234567890123456789");

	TEST_CHECK(h2 < h1);
	TEST_CHECK(h2 == h2);
	TEST_CHECK(h1 == h1);
	h2.clear();
	TEST_CHECK(h2.is_all_zeros());
	
	h2 = to_hash("ffffffffff0000000000ffffffffff0000000000");
	h1 = to_hash("fffff00000fffff00000fffff00000fffff00000");
	h1 &= h2;
	TEST_CHECK(h1 == to_hash("fffff000000000000000fffff000000000000000"));

	h2 = to_hash("ffffffffff0000000000ffffffffff0000000000");
	h1 = to_hash("fffff00000fffff00000fffff00000fffff00000");
	h1 |= h2;
	TEST_CHECK(h1 == to_hash("fffffffffffffff00000fffffffffffffff00000"));
	
	h2 = to_hash("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
	h1 ^= h2;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("f0f0f0f0f0f0f0ff0f0ff0f0f0f0f0f0f0ff0f0f"));
	TEST_CHECK(h1 != h2);

	h2 = sha1_hash("                    ");
	TEST_CHECK(h2 == to_hash("2020202020202020202020202020202020202020"));

	h1 = to_hash("ffffffffff0000000000ffffffffff0000000000");
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	h1 <<= 12;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("fffffff0000000000ffffffffff0000000000000"));
	h1 >>= 12;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("000fffffff0000000000ffffffffff0000000000"));

	h1 = to_hash("7000000000000000000000000000000000000000");
	h1 <<= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("e000000000000000000000000000000000000000"));

	h1 = to_hash("0000000000000000000000000000000000000007");
	h1 <<= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("000000000000000000000000000000000000000e"));

	h1 = to_hash("0000000000000000000000000000000000000007");
	h1 >>= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("0000000000000000000000000000000000000003"));

	h1 = to_hash("7000000000000000000000000000000000000000");
	h1 >>= 1;
#if TORRENT_USE_IOSTREAM
	std::cerr << h1 << std::endl;
#endif
	TEST_CHECK(h1 == to_hash("3800000000000000000000000000000000000000"));
	
	// CIDR distance test
	h1 = to_hash("0123456789abcdef01232456789abcdef0123456");
	h2 = to_hash("0123456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 160);
	h2 = to_hash("0120456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 14);
	h2 = to_hash("012f456789abcdef01232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 12);
	h2 = to_hash("0123456789abcdef11232456789abcdef0123456");
	TEST_CHECK(common_bits(&h1[0], &h2[0], 20) == 16 * 4 + 3);


	// test bitfield
	bitfield test1(10, false);
	TEST_CHECK(test1.count() == 0);
	test1.set_bit(9);
	TEST_CHECK(test1.count() == 1);
	test1.clear_bit(9);
	TEST_CHECK(test1.count() == 0);
	test1.set_bit(2);
	TEST_CHECK(test1.count() == 1);
	test1.set_bit(1);
	test1.set_bit(9);
	TEST_CHECK(test1.count() == 3);
	test1.clear_bit(2);
	TEST_CHECK(test1.count() == 2);
	int distance = std::distance(test1.begin(), test1.end());
	std::cerr << distance << std::endl;
	TEST_CHECK(distance == 10);

	test1.set_all();
	TEST_CHECK(test1.count() == 10);

	test1.clear_all();
	TEST_CHECK(test1.count() == 0);

	test1.resize(2);
	test1.set_bit(0);
	test1.resize(16, true);
	TEST_CHECK(test1.count() == 15);
	test1.resize(20, true);
	TEST_CHECK(test1.count() == 19);
	test1.set_bit(1);
	test1.resize(1);
	TEST_CHECK(test1.count() == 1);

	// test merkle_*() functions

	// this is the structure:
	//             0
	//      1              2
	//   3      4       5       6
	//  7 8    9 10   11 12   13 14
	// num_leafs = 8

	TEST_EQUAL(merkle_num_leafs(1), 1);
	TEST_EQUAL(merkle_num_leafs(2), 2);
	TEST_EQUAL(merkle_num_leafs(3), 4);
	TEST_EQUAL(merkle_num_leafs(4), 4);
	TEST_EQUAL(merkle_num_leafs(5), 8);
	TEST_EQUAL(merkle_num_leafs(6), 8);
	TEST_EQUAL(merkle_num_leafs(7), 8);
	TEST_EQUAL(merkle_num_leafs(8), 8);
	TEST_EQUAL(merkle_num_leafs(9), 16);
	TEST_EQUAL(merkle_num_leafs(10), 16);
	TEST_EQUAL(merkle_num_leafs(11), 16);
	TEST_EQUAL(merkle_num_leafs(12), 16);
	TEST_EQUAL(merkle_num_leafs(13), 16);
	TEST_EQUAL(merkle_num_leafs(14), 16);
	TEST_EQUAL(merkle_num_leafs(15), 16);
	TEST_EQUAL(merkle_num_leafs(16), 16);
	TEST_EQUAL(merkle_num_leafs(17), 32);
	TEST_EQUAL(merkle_num_leafs(18), 32);

	// parents
	TEST_EQUAL(merkle_get_parent(1), 0);
	TEST_EQUAL(merkle_get_parent(2), 0);
	TEST_EQUAL(merkle_get_parent(3), 1);
	TEST_EQUAL(merkle_get_parent(4), 1);
	TEST_EQUAL(merkle_get_parent(5), 2);
	TEST_EQUAL(merkle_get_parent(6), 2);
	TEST_EQUAL(merkle_get_parent(7), 3);
	TEST_EQUAL(merkle_get_parent(8), 3);
	TEST_EQUAL(merkle_get_parent(9), 4);
	TEST_EQUAL(merkle_get_parent(10), 4);
	TEST_EQUAL(merkle_get_parent(11), 5);
	TEST_EQUAL(merkle_get_parent(12), 5);
	TEST_EQUAL(merkle_get_parent(13), 6);
	TEST_EQUAL(merkle_get_parent(14), 6);

	// siblings
	TEST_EQUAL(merkle_get_sibling(1), 2);
	TEST_EQUAL(merkle_get_sibling(2), 1);
	TEST_EQUAL(merkle_get_sibling(3), 4);
	TEST_EQUAL(merkle_get_sibling(4), 3);
	TEST_EQUAL(merkle_get_sibling(5), 6);
	TEST_EQUAL(merkle_get_sibling(6), 5);
	TEST_EQUAL(merkle_get_sibling(7), 8);
	TEST_EQUAL(merkle_get_sibling(8), 7);
	TEST_EQUAL(merkle_get_sibling(9), 10);
	TEST_EQUAL(merkle_get_sibling(10), 9);
	TEST_EQUAL(merkle_get_sibling(11), 12);
	TEST_EQUAL(merkle_get_sibling(12), 11);
	TEST_EQUAL(merkle_get_sibling(13), 14);
	TEST_EQUAL(merkle_get_sibling(14), 13);

	// total number of nodes given the number of leafs
	TEST_EQUAL(merkle_num_nodes(1), 1);
	TEST_EQUAL(merkle_num_nodes(2), 3);
	TEST_EQUAL(merkle_num_nodes(4), 7);
	TEST_EQUAL(merkle_num_nodes(8), 15);
	TEST_EQUAL(merkle_num_nodes(16), 31);

	// make_magnet_uri
	{
		entry info;
		info["pieces"] = "aaaaaaaaaaaaaaaaaaaa";
		info["name"] = "slightly shorter name, it's kind of sad that people started the trend of incorrectly encoding the regular name field and then adding another one with correct encoding";
		info["name.utf-8"] = "this is a long ass name in order to try to make make_magnet_uri overflow and hopefully crash. Although, by the time you read this that particular bug should have been fixed";
		info["piece length"] = 16 * 1024;
		info["length"] = 3245;
		entry torrent;
		torrent["info"] = info;
		entry::list_type& al1 = torrent["announce-list"].list();
		al1.push_back(entry::list_type());
		entry::list_type& al = al1.back().list();
		al.push_back(entry("http://bigtorrent.org:2710/announce"));
		al.push_back(entry("http://bt.careland.com.cn:6969/announce"));
		al.push_back(entry("http://bt.e-burg.org:2710/announce"));
		al.push_back(entry("http://bttrack.9you.com/announce"));
		al.push_back(entry("http://coppersurfer.tk:6969/announce"));
		al.push_back(entry("http://erdgeist.org/arts/software/opentracker/announce"));
		al.push_back(entry("http://exodus.desync.com/announce"));
		al.push_back(entry("http://fr33dom.h33t.com:3310/announce"));
		al.push_back(entry("http://genesis.1337x.org:1337/announce"));
		al.push_back(entry("http://inferno.demonoid.me:3390/announce"));
		al.push_back(entry("http://inferno.demonoid.ph:3390/announce"));
		al.push_back(entry("http://ipv6.tracker.harry.lu/announce"));
		al.push_back(entry("http://lnxroot.com:6969/announce"));
		al.push_back(entry("http://nemesis.1337x.org/announce"));
		al.push_back(entry("http://puto.me:6969/announce"));
		al.push_back(entry("http://sline.net:2710/announce"));
		al.push_back(entry("http://tracker.beeimg.com:6969/announce"));
		al.push_back(entry("http://tracker.ccc.de/announce"));
		al.push_back(entry("http://tracker.coppersurfer.tk/announce"));
		al.push_back(entry("http://tracker.coppersurfer.tk:6969/announce"));
		al.push_back(entry("http://tracker.cpleft.com:2710/announce"));
		al.push_back(entry("http://tracker.istole.it/announce"));
		al.push_back(entry("http://tracker.kamyu.net/announce"));
		al.push_back(entry("http://tracker.novalayer.org:6969/announce"));
		al.push_back(entry("http://tracker.torrent.to:2710/announce"));
		al.push_back(entry("http://tracker.torrentbay.to:6969/announce"));
		al.push_back(entry("udp://tracker.openbittorrent.com:80"));
		al.push_back(entry("udp://tracker.publicbt.com:80"));

		std::vector<char> buf;
		bencode(std::back_inserter(buf), torrent);
		printf("%s\n", &buf[0]);
		torrent_info ti(&buf[0], buf.size(), ec);

		TEST_EQUAL(al.size(), ti.trackers().size());

		std::string magnet = make_magnet_uri(ti);
		printf("%s len: %d\n", magnet.c_str(), int(magnet.size()));
	}

	// test address_to_bytes
	TEST_EQUAL(address_to_bytes(address_v4::from_string("10.11.12.13")), "\x0a\x0b\x0c\x0d");
	TEST_EQUAL(address_to_bytes(address_v4::from_string("16.5.127.1")), "\x10\x05\x7f\x01");

	// test endpoint_to_bytes
	TEST_EQUAL(endpoint_to_bytes(udp::endpoint(address_v4::from_string("10.11.12.13"), 8080)), "\x0a\x0b\x0c\x0d\x1f\x90");
	TEST_EQUAL(endpoint_to_bytes(udp::endpoint(address_v4::from_string("16.5.127.1"), 12345)), "\x10\x05\x7f\x01\x30\x39");
	return 0;
}

