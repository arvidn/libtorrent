#!/usr/bin/env python3
# Copyright Arvid Norberg 2008. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
from __future__ import print_function

import sys
import time

import libtorrent as lt

ses = lt.session({"listen_interfaces": "0.0.0.0:6881"})

atp = lt.add_torrent_params()
atp.ti = lt.torrent_info(sys.argv[1])
atp.save_path = "."
h = ses.add_torrent(atp)
s = h.status()
print("starting", s.name)

while not s.is_seeding:
    s = h.status()

    print(
        "\r%.2f%% complete (down: %.1f kB/s up: %.1f kB/s peers: %d) %s"
        % (
            s.progress * 100,
            s.download_rate / 1000,
            s.upload_rate / 1000,
            s.num_peers,
            s.state,
        ),
        end=" ",
    )

    alerts = ses.pop_alerts()
    for a in alerts:
        if a.category() & lt.alert.category_t.error_notification:
            print(a)

    sys.stdout.flush()

    time.sleep(1)

print(h.status().name, "complete")
