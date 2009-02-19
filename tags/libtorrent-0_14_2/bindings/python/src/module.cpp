// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python/module.hpp>

void bind_utility();
void bind_fingerprint();
void bind_big_number();
void bind_session();
void bind_entry();
void bind_torrent_info();
void bind_filesystem();
void bind_torrent_handle();
void bind_torrent_status();
void bind_session_settings();
void bind_version();
void bind_alert();
void bind_datetime();
void bind_extensions();
void bind_peer_plugin();
void bind_torrent();
void bind_peer_info();
void bind_ip_filter();
void bind_magnet_uri();
void bind_converters();

BOOST_PYTHON_MODULE(libtorrent)
{
    Py_Initialize();
    PyEval_InitThreads();

    bind_utility();
    bind_fingerprint();
    bind_big_number();
    bind_entry();
    bind_session();
    bind_torrent_info();
    bind_filesystem();
    bind_torrent_handle();
    bind_torrent_status();
    bind_session_settings();
    bind_version();
    bind_alert();
    bind_datetime();
    bind_extensions();
#ifndef TORRENT_NO_PYTHON_PLUGINS
    bind_peer_plugin();
#endif
    bind_torrent();
    bind_peer_info();
    bind_ip_filter();
    bind_magnet_uri();
    bind_converters();
}
