/*

Copyright (c) 2013-2017, 2020-2021, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"

int EXPORT run_http_suite(int proxy, char const* protocol
	, bool chunked_encoding = false, bool test_ban = false
	, bool keepalive = true, bool test_rename = false, bool proxy_peers = true);

void EXPORT test_transfer(lt::session& ses
	, lt::add_torrent_params atp
	, int proxy = 0, char const* protocol = "http"
	, bool url_seed = true, bool chunked_encoding = false
	, bool test_ban = false, bool keepalive = true, bool proxy_peers = true);
