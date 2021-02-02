import libtorrent as lt

import unittest


settings = {
    'alert_mask': lt.alert.category_t.all_categories,
    'enable_dht': False, 'enable_lsd': False, 'enable_natpmp': False,
    'enable_upnp': False, 'listen_interfaces': '0.0.0.0:0', 'file_pool_size': 1}

class test_peer_class(unittest.TestCase):

    def test_peer_class_ids(self):
        s = lt.session(settings)

        print('global_peer_class_id:', lt.session.global_peer_class_id)
        print('tcp_peer_class_id:', lt.session.tcp_peer_class_id)
        print('local_peer_class_id:', lt.session.local_peer_class_id)

        print('global: ', s.get_peer_class(s.global_peer_class_id))
        print('tcp: ', s.get_peer_class(s.local_peer_class_id))
        print('local: ', s.get_peer_class(s.local_peer_class_id))

    def test_peer_class(self):
        s = lt.session(settings)

        c = s.create_peer_class('test class')
        print('new class: ', s.get_peer_class(c))

        nfo = s.get_peer_class(c)
        self.assertEqual(nfo['download_limit'], 0)
        self.assertEqual(nfo['upload_limit'], 0)
        self.assertEqual(nfo['ignore_unchoke_slots'], False)
        self.assertEqual(nfo['connection_limit_factor'], 100)
        self.assertEqual(nfo['download_priority'], 1)
        self.assertEqual(nfo['upload_priority'], 1)
        self.assertEqual(nfo['label'], 'test class')

        nfo['download_limit'] = 1337
        nfo['upload_limit'] = 1338
        nfo['ignore_unchoke_slots'] = True
        nfo['connection_limit_factor'] = 42
        nfo['download_priority'] = 2
        nfo['upload_priority'] = 3

        s.set_peer_class(c, nfo)

        nfo2 = s.get_peer_class(c)
        self.assertEqual(nfo, nfo2)

    def test_peer_class_filter(self):
        filt = lt.peer_class_type_filter()
        filt.add(lt.peer_class_type_filter.tcp_socket, lt.session.global_peer_class_id)
        filt.remove(lt.peer_class_type_filter.utp_socket, lt.session.local_peer_class_id)

        filt.disallow(lt.peer_class_type_filter.tcp_socket, lt.session.global_peer_class_id)
        filt.allow(lt.peer_class_type_filter.utp_socket, lt.session.local_peer_class_id)

    def test_peer_class_ip_filter(self):
        s = lt.session(settings)
        s.set_peer_class_type_filter(lt.peer_class_type_filter())
        s.set_peer_class_filter(lt.ip_filter())
