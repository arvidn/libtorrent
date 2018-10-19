#!/usr/bin/env python

# Copyright Daniel Wallin 2006. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)


import sys
import atexit
import libtorrent as lt
import time
import os.path


class WindowsConsole:
    def __init__(self):
        self.console = Console.getconsole()

    def clear(self):
        self.console.page()

    def write(self, str):
        self.console.write(str)

    def sleep_and_input(self, seconds):
        time.sleep(seconds)
        if msvcrt.kbhit():
            return msvcrt.getch()
        return None


class UnixConsole:
    def __init__(self):
        self.fd = sys.stdin
        self.old = termios.tcgetattr(self.fd.fileno())
        new = termios.tcgetattr(self.fd.fileno())
        new[3] = new[3] & ~termios.ICANON
        new[6][termios.VTIME] = 0
        new[6][termios.VMIN] = 1
        termios.tcsetattr(self.fd.fileno(), termios.TCSADRAIN, new)

        atexit.register(self._onexit)

    def _onexit(self):
        termios.tcsetattr(self.fd.fileno(), termios.TCSADRAIN, self.old)

    def clear(self):
        sys.stdout.write('\033[2J\033[0;0H')
        sys.stdout.flush()

    def write(self, str):
        sys.stdout.write(str)
        sys.stdout.flush()

    def sleep_and_input(self, seconds):
        read, __, __ = select.select(
            [self.fd.fileno()], [], [], seconds)
        if len(read) > 0:
            return self.fd.read(1)
        return None


if os.name == 'nt':
    import Console
    import msvcrt
else:
    import termios
    import select


def write_line(console, line):
    console.write(line)


def add_suffix(val):
    prefix = ['B', 'kB', 'MB', 'GB', 'TB']
    for i in range(len(prefix)):
        if abs(val) < 1000:
            if i == 0:
                return '%5.3g%s' % (val, prefix[i])
            else:
                return '%4.3g%s' % (val, prefix[i])
        val /= 1000

    return '%6.3gPB' % val


def progress_bar(progress, width):
    assert(progress <= 1)
    progress_chars = int(progress * width + 0.5)
    return progress_chars * '#' + (width - progress_chars) * '-'


def print_peer_info(console, peers):

    out = (' down    (total )   up     (total )'
           '  q  r flags  block progress  client\n')

    for p in peers:

        out += '%s/s ' % add_suffix(p.down_speed)
        out += '(%s) ' % add_suffix(p.total_download)
        out += '%s/s ' % add_suffix(p.up_speed)
        out += '(%s) ' % add_suffix(p.total_upload)
        out += '%2d ' % p.download_queue_length
        out += '%2d ' % p.upload_queue_length

        out += 'I' if p.flags & lt.peer_info.interesting else '.'
        out += 'C' if p.flags & lt.peer_info.choked else '.'
        out += 'i' if p.flags & lt.peer_info.remote_interested else '.'
        out += 'c' if p.flags & lt.peer_info.remote_choked else '.'
        out += 'e' if p.flags & lt.peer_info.supports_extensions else '.'
        out += 'l' if p.flags & lt.peer_info.local_connection else 'r'
        out += ' '

        if p.downloading_piece_index >= 0:
            assert(p.downloading_progress <= p.downloading_total)
            out += progress_bar(float(p.downloading_progress) /
                                p.downloading_total, 15)
        else:
            out += progress_bar(0, 15)
        out += ' '

        if p.flags & lt.peer_info.handshake:
            id = 'waiting for handshake'
        elif p.flags & lt.peer_info.connecting:
            id = 'connecting to peer'
        else:
            id = p.client

        out += '%s\n' % id[:10]

    write_line(console, out)


def print_download_queue(console, download_queue):

    out = ""

    for e in download_queue:
        out += '%4d: [' % e['piece_index']
        for b in e['blocks']:
            s = b['state']
            if s == 3:
                out += '#'
            elif s == 2:
                out += '='
            elif s == 1:
                out += '-'
            else:
                out += ' '
        out += ']\n'

    write_line(console, out)


def add_torrent(ses, filename, options):
    atp = lt.add_torrent_params()
    if filename.startswith('magnet:'):
        atp = lt.parse_magnet_uri(filename)
    else:
        atp.ti = lt.torrent_info(filename)
        try:
            atp.resume_data = open(os.path.join(options.save_path, atp.info.name() + '.fastresume'), 'rb').read()
        except Exception:
            pass

    atp.save_path = options.save_path
    atp.storage_mode = lt.storage_mode_t.storage_mode_sparse
    atp.flags |= lt.torrent_flags.duplicate_is_error \
        | lt.torrent_flags.auto_managed \
        | lt.torrent_flags.duplicate_is_error
    ses.async_add_torrent(atp)


def main():
    from optparse import OptionParser

    parser = OptionParser()

    parser.add_option('-p', '--port', type='int', help='set listening port')

    parser.add_option(
        '-i', '--listen-interface', type='string',
        help='set interface for incoming connections', )

    parser.add_option(
        '-o', '--outgoing-interface', type='string',
        help='set interface for outgoing connections')

    parser.add_option(
        '-d', '--max-download-rate', type='float',
        help='the maximum download rate given in kB/s. 0 means infinite.')

    parser.add_option(
        '-u', '--max-upload-rate', type='float',
        help='the maximum upload rate given in kB/s. 0 means infinite.')

    parser.add_option(
        '-s', '--save-path', type='string',
        help='the path where the downloaded file/folder should be placed.')

    parser.add_option(
        '-r', '--proxy-host', type='string',
        help='sets HTTP proxy host and port (separated by \':\')')

    parser.set_defaults(
        port=6881,
        listen_interface='0.0.0.0',
        outgoing_interface='',
        max_download_rate=0,
        max_upload_rate=0,
        save_path='.',
        proxy_host=''
    )

    (options, args) = parser.parse_args()

    if options.port < 0 or options.port > 65525:
        options.port = 6881

    options.max_upload_rate *= 1000
    options.max_download_rate *= 1000

    if options.max_upload_rate <= 0:
        options.max_upload_rate = -1
    if options.max_download_rate <= 0:
        options.max_download_rate = -1

    settings = {
        'user_agent': 'python_client/' + lt.__version__,
        'listen_interfaces': '%s:%d' % (options.listen_interface, options.port),
        'download_rate_limit': int(options.max_download_rate),
        'upload_rate_limit': int(options.max_upload_rate),
        'alert_mask': lt.alert.category_t.all_categories,
        'outgoing_interfaces': options.outgoing_interface,
    }

    if options.proxy_host != '':
        settings['proxy_hostname'] = options.proxy_host.split(':')[0]
        settings['proxy_type'] = lt.proxy_type_t.http
        settings['proxy_port'] = options.proxy_host.split(':')[1]

    ses = lt.session(settings)

    # map torrent_handle to torrent_status
    torrents = {}
    alerts_log = []

    for f in args:
        add_torrent(ses, f, options)

    if os.name == 'nt':
        console = WindowsConsole()
    else:
        console = UnixConsole()

    alive = True
    while alive:
        console.clear()

        out = ''

        for h, t in torrents.items():
            out += 'name: %-40s\n' % t.name[:40]

            if t.state != lt.torrent_status.seeding:
                state_str = ['queued', 'checking', 'downloading metadata',
                             'downloading', 'finished', 'seeding',
                             'allocating', 'checking fastresume']
                out += state_str[t.state] + ' '

                out += '%5.4f%% ' % (t.progress * 100)
                out += progress_bar(t.progress, 49)
                out += '\n'

                out += 'total downloaded: %d Bytes\n' % t.total_done
                out += 'peers: %d seeds: %d distributed copies: %d\n' % \
                    (t.num_peers, t.num_seeds, t.distributed_copies)
                out += '\n'

            out += 'download: %s/s (%s) ' \
                % (add_suffix(t.download_rate), add_suffix(t.total_download))
            out += 'upload: %s/s (%s) ' \
                % (add_suffix(t.upload_rate), add_suffix(t.total_upload))

            if t.state != lt.torrent_status.seeding:
                out += 'info-hash: %s\n' % t.info_hash
                out += 'next announce: %s\n' % t.next_announce
                out += 'tracker: %s\n' % t.current_tracker

            write_line(console, out)

            print_peer_info(console, t.handle.get_peer_info())
            print_download_queue(console, t.handle.get_download_queue())

            if t.state != lt.torrent_status.seeding:
                try:
                    out = '\n'
                    fp = h.file_progress()
                    ti = t.torrent_file
                    for f, p in zip(ti.files(), fp):
                        out += progress_bar(p / float(f.size), 20)
                        out += ' ' + f.path + '\n'
                    write_line(console, out)
                except Exception:
                    pass

        write_line(console, 76 * '-' + '\n')
        write_line(console, '(q)uit), (p)ause), (u)npause), (r)eannounce\n')
        write_line(console, 76 * '-' + '\n')

        alerts = ses.pop_alerts()
        for a in alerts:
            alerts_log.append(a.message())

            # add new torrents to our list of torrent_status
            if isinstance(a, lt.add_torrent_alert):
                h = a.handle
                h.set_max_connections(60)
                h.set_max_uploads(-1)
                torrents[h] = h.status()

            # update our torrent_status array for torrents that have
            # changed some of their state
            if isinstance(a, lt.state_update_alert):
                for s in a.status:
                    torrents[s.handle] = s

        if len(alerts_log) > 20:
            alerts_log = alerts_log[-20:]

        for a in alerts_log:
            write_line(console, a + '\n')

        c = console.sleep_and_input(0.5)

        ses.post_torrent_updates()
        if not c:
            continue

        if c == 'r':
            for h in torrents:
                h.force_reannounce()
        elif c == 'q':
            alive = False
        elif c == 'p':
            for h in torrents:
                h.pause()
        elif c == 'u':
            for h in torrents:
                h.resume()

    ses.pause()
    for h, t in torrents.items():
        if not h.is_valid() or not t.has_metadata:
            continue
        h.save_resume_data()

    while len(torrents) > 0:
        alerts = ses.pop_alerts()
        for a in alerts:
            if isinstance(a, lt.save_resume_data_alert):
                print(a)
                data = lt.write_resume_data_buf(a.params)
                h = a.handle
                if h in torrents:
                    open(os.path.join(options.save_path, torrents[h].name + '.fastresume'), 'wb').write(data)
                    del torrents[h]

            if isinstance(a, lt.save_resume_data_failed_alert):
                h = a.handle
                if h in torrents:
                    print('failed to save resume data for ', torrents[h].name)
                    del torrents[h]
        time.sleep(0.5)


main()
