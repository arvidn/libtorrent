import libtorrent as lt

import unittest
import time
import os
import threading
import socket
import select


settings = {
    'alert_mask': lt.alert.category_t.all_categories,
    'enable_dht': False, 'enable_lsd': False, 'enable_natpmp': False,
    'enable_upnp': False, 'listen_interfaces': '0.0.0.0:0', 'file_pool_size': 1}

class test_alerts(unittest.TestCase):

    def test_alert(self):

        ses = lt.session(settings)
        ti = lt.torrent_info('base.torrent')
        h = ses.add_torrent({'ti': ti, 'save_path': os.getcwd()})
        st = h.status()
        time.sleep(1)
        ses.remove_torrent(h)
        ses.wait_for_alert(1000)  # milliseconds
        alerts = ses.pop_alerts()
        for a in alerts:
            if a.what() == 'add_torrent_alert':
                self.assertEqual(a.torrent_name, 'temp')
            print(a.message())
            for field_name in dir(a):
                if field_name.startswith('__'):
                    continue
                field = getattr(a, field_name)
                if callable(field):
                    print('  ', field_name, ' = ', field())
                else:
                    print('  ', field_name, ' = ', field)

        print(st.next_announce)
        self.assertEqual(st.name, 'temp')
        print(st.errc.message())
        print(st.pieces)
        print(st.last_seen_complete)
        print(st.completed_time)
        print(st.progress)
        print(st.num_pieces)
        print(st.distributed_copies)
        print(st.info_hashes)
        print(st.seeding_duration)
        print(st.last_upload)
        print(st.last_download)
        self.assertEqual(st.save_path, os.getcwd())

    def test_alert_fs(self):
        ses = lt.session(settings)
        s1, s2 = socket.socketpair()
        ses.set_alert_fd(s2.fileno())

        ses.pop_alerts()

        # make sure there's an alert to wake us up
        ses.post_session_stats()

        read_sockets, write_sockets, error_sockets = select.select([s1], [], [])

        self.assertEqual(len(read_sockets), 1)
        for s in read_sockets:
            s.recv(10)

    def test_pop_alerts(self):
        ses = lt.session(settings)
        ses.async_add_torrent(
            {"ti": lt.torrent_info("base.torrent"), "save_path": "."})

# this will cause an error (because of duplicate torrents) and the
# torrent_info object created here will be deleted once the alert goes out
# of scope. When that happens, it will decrement the python object, to allow
# it to release the object.
# we're trying to catch the error described in this post, with regards to
# torrent_info.
# https://mail.python.org/pipermail/cplusplus-sig/2007-June/012130.html
        ses.async_add_torrent(
            {"ti": lt.torrent_info("base.torrent"), "save_path": "."})
        time.sleep(1)
        for i in range(0, 10):
            alerts = ses.pop_alerts()
            for a in alerts:
                print(a.message())
            time.sleep(0.1)

    def test_alert_notify(self):
        ses = lt.session(settings)
        event = threading.Event()

        def callback():
            event.set()

        ses.set_alert_notify(callback)
        ses.async_add_torrent(
            {"ti": lt.torrent_info("base.torrent"), "save_path": "."})
        event.wait()
