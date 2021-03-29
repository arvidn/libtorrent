/*

Copyright (c) 2013-2017, 2019-2020, Arvid Norberg
All rights reserved.

You may use, distribute and modify this code under the terms of the BSD license,
see LICENSE file.
*/

#include "test.hpp"

int EXPORT run_http_suite(int proxy, char const* protocol
	, bool chunked_encoding = false, bool test_ban = false
	, bool keepalive = true, bool test_rename = false, bool proxy_peers = true);

void EXPORT test_transfer(lt::session& ses
	, std::shared_ptr<lt::torrent_info> torrent_file
	, int proxy = 0, char const* protocol = "http"
	, bool url_seed = true, bool chunked_encoding = false
	, bool test_ban = false, bool keepalive = true, bool proxy_peers = true);
