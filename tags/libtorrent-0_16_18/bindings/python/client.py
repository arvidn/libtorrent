#!/usr/bin/env python

# Copyright Daniel Wallin 2006. Use, modification and distribution is
# subject to the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

import sys
import atexit
import libtorrent as lt
import time
import os.path
import sys

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
        read,_,_ = select.select([self.fd.fileno()], [], [], seconds)
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

    out = ' down    (total )   up     (total )  q  r flags  block progress  client\n'

    for p in peers:

        out += '%s/s ' % add_suffix(p.down_speed)
        out += '(%s) ' % add_suffix(p.total_download)
        out += '%s/s ' % add_suffix(p.up_speed)
        out += '(%s) ' % add_suffix(p.total_upload)
        out += '%2d ' % p.download_queue_length
        out += '%2d ' % p.upload_queue_length

        if p.flags & lt.peer_info.interesting: out += 'I'
        else: out += '.'
        if p.flags & lt.peer_info.choked: out += 'C'
        else: out += '.'
        if p.flags & lt.peer_info.remote_interested: out += 'i'
        else: out += '.'
        if p.flags & lt.peer_info.remote_choked: out += 'c'
        else: out += '.'
        if p.flags & lt.peer_info.supports_extensions: out += 'e'
        else: out += '.'
        if p.flags & lt.peer_info.local_connection: out += 'l'
        else: out += 'r'
        out += ' '

        if p.downloading_piece_index >= 0:
            assert(p.downloading_progress <= p.downloading_total)
            out += progress_bar(float(p.downloading_progress) / p.downloading_total, 15)
        else:
            out += progress_bar(0, 15)
        out += ' '

        if p.flags & lt.peer_info.handshake:
            id = 'waiting for handshake'
        elif p.flags & lt.peer_info.connecting:
            id =  'connecting to peer'
        elif p.flags & lt.peer_info.queued:
            id =  'queued'
        else:
            id = p.client

        out += '%s\n' % id[:10]

    write_line(console, out)


def print_download_queue(console, download_queue):

    out = ""

    for e in download_queue:
        out += '%4d: [' % e['piece_index'];
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

def main():
    from optparse import OptionParser

    parser = OptionParser()

    parser.add_option('-p', '--port',
        type='int', help='set listening port')

    parser.add_option('-d', '--max-download-rate',
        type='float', help='the maximum download rate given in kB/s. 0 means infinite.')

    parser.add_option('-u', '--max-upload-rate',
        type='float', help='the maximum upload rate given in kB/s. 0 means infinite.')

    parser.add_option('-s', '--save-path',
        type='string', help='the path where the downloaded file/folder should be placed.')

    parser.add_option('-a', '--allocation-mode',
        type='string', help='sets mode used for allocating the downloaded files on disk. Possible options are [full | compact]')

    parser.add_option('-r', '--proxy-host',
        type='string', help='sets HTTP proxy host and port (separated by \':\')')

    parser.set_defaults(
        port=6881
      , max_download_rate=0
      , max_upload_rate=0
      , save_path='./'
      , allocation_mode='compact'
      , proxy_host=''
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

    compact_allocation = options.allocation_mode == 'compact'

    settings = lt.session_settings()
    settings.user_agent = 'python_client/' + lt.version

    ses = lt.session()
    ses.set_download_rate_limit(int(options.max_download_rate))
    ses.set_upload_rate_limit(int(options.max_upload_rate))
    ses.listen_on(options.port, options.port + 10)
    ses.set_settings(settings)
    ses.set_alert_mask(0xfffffff)

    if options.proxy_host != '':
        ps = lt.proxy_settings()
        ps.type = lt.proxy_type.http
        ps.hostname = options.proxy_host.split(':')[0]
        ps.port = int(options.proxy_host.split(':')[1])
        ses.set_proxy(ps)

    handles = []
    alerts = []

    for f in args:

        atp = {}
        atp["save_path"] = options.save_path
        atp["storage_mode"] = lt.storage_mode_t.storage_mode_sparse
        atp["paused"] = False
        atp["auto_managed"] = True
        atp["duplicate_is_error"] = True
        if f.startswith('magnet:') or f.startswith('http://') or f.startswith('https://'):
            atp["url"] = f
        else:
            e = lt.bdecode(open(f, 'rb').read())
            info = lt.torrent_info(e)
            print('Adding \'%s\'...' % info.name())

            try:
                atp["resume_data"] = open(os.path.join(options.save_path, info.name() + '.fastresume'), 'rb').read()
            except:
                pass

            atp["ti"] = info

        h = ses.add_torrent(atp)

        handles.append(h)

        h.set_max_connections(60)
        h.set_max_uploads(-1)

    if os.name == 'nt':
        console = WindowsConsole()
    else:
        console = UnixConsole()

    alive = True
    while alive:
        console.clear()

        out = ''

        for h in handles:
            if h.has_metadata():
                name = h.get_torrent_info().name()[:40]
            else:
                name = '-'
            out += 'name: %-40s\n' % name

            s = h.status()

            if s.state != lt.torrent_status.seeding:
                state_str = ['queued', 'checking', 'downloading metadata', \
                             'downloading', 'finished', 'seeding', \
                             'allocating', 'checking fastresume']
                out += state_str[s.state] + ' '

                out += '%5.4f%% ' % (s.progress*100)
                out += progress_bar(s.progress, 49)
                out += '\n'

                out += 'total downloaded: %d Bytes\n' % s.total_done
                out += 'peers: %d seeds: %d distributed copies: %d\n' % \
                    (s.num_peers, s.num_seeds, s.distributed_copies)
                out += '\n'

            out += 'download: %s/s (%s) ' \
                % (add_suffix(s.download_rate), add_suffix(s.total_download))
            out += 'upload: %s/s (%s) ' \
                % (add_suffix(s.upload_rate), add_suffix(s.total_upload))

            if s.state != lt.torrent_status.seeding:
                out += 'info-hash: %s\n' % h.info_hash()
                out += 'next announce: %s\n' % s.next_announce
                out += 'tracker: %s\n' % s.current_tracker

            write_line(console, out)

            print_peer_info(console, h.get_peer_info())
            print_download_queue(console, h.get_download_queue())

            if s.state != lt.torrent_status.seeding:
                try:
                    out = '\n'
                    fp = h.file_progress()
                    ti = h.get_torrent_info()
                    for f,p in zip(ti.files(), fp):
                        out += progress_bar(p / float(f.size), 20)
                        out += ' ' + f.path + '\n'
                    write_line(console, out)
                except:
                    pass

        write_line(console, 76 * '-' + '\n')
        write_line(console, '(q)uit), (p)ause), (u)npause), (r)eannounce\n')
        write_line(console, 76 * '-' + '\n')

        while 1:
            a = ses.pop_alert()
            if not a: break
            alerts.append(a)

        if len(alerts) > 8:
            del alerts[:len(alerts) - 8]

        for a in alerts:
            if type(a) == str:
                write_line(console, a + '\n')
            else:
                write_line(console, a.message() + '\n')

        c = console.sleep_and_input(0.5)

        if not c:
            continue

        if c == 'r':
            for h in handles: h.force_reannounce()
        elif c == 'q':
            alive = False
        elif c == 'p':
            for h in handles: h.pause()
        elif c == 'u':
            for h in handles: h.resume()

    ses.pause()
    for h in handles:
        if not h.is_valid() or not h.has_metadata():
            continue
        data = lt.bencode(h.write_resume_data())
        open(os.path.join(options.save_path, h.get_torrent_info().name() + '.fastresume'), 'wb').write(data)

main()

