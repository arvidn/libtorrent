#!/usr/bin/env python3

"""Minimal non-feature complete socks proxy"""

import socket
from struct import pack, unpack
import threading
import sys
import traceback

from socketserver import StreamRequestHandler, ThreadingTCPServer


def debug(s):
    print('socks.py: ', s)
    sys.stdout.flush()


def error(s):
    print('socks.py, ERROR: ', s)
    sys.stdout.flush()


class MyTCPServer(ThreadingTCPServer):
    allow_reuse_address = True

    def handle_timeout(self):
        raise Exception('socks.py: timeout')


CLOSE = object()

VERSION = b'\x05'
NOAUTH = b'\x00'
USERPASS = b'\x02'
CONNECT = b'\x01'
UDP_ASSOCIATE = b'\x03'
IPV4 = b'\x01'
IPV6 = b'\x04'
DOMAIN_NAME = b'\x03'
SUCCESS = b'\x00'

password = None
username = None
allow_v4 = False


def send(dest, msg):
    if msg == CLOSE:
        try:
            dest.shutdown(socket.SHUT_WR)
        except Exception:
            pass
        dest.close()
        return 0
    else:
        return dest.sendall(msg)


def recv(source, n):
    data = source.recv(n)
    if data == b'':
        return CLOSE
    else:
        return data


def forward(source, dest, name):
    while True:
        data = recv(source, 4000)
        if data == CLOSE:
            send(dest, CLOSE)
            debug('%s hung up' % name)
            return
#        debug('Forwarding (%d) %r' % (len(data), data))
        send(dest, data)


def spawn_forwarder(source, dest, name):
    t = threading.Thread(target=forward, args=(source, dest, name))
    t.daemon = True
    t.start()


class SocksHandler(StreamRequestHandler):
    """Highly feature incomplete SOCKS 5 implementation"""

    def close_request(self):
        self.server.close_request(self.request)

    def read(self, n):
        data = b''
        while len(data) < n:
            extra = self.rfile.read(n)
            if extra == b'':
                raise Exception('Connection closed')
            data += extra
        return data

    def handle(self):
        try:
            self.inner_handle()
        except Exception:
            error("Unhandled exception")
            traceback.print_exc(file=sys.stdout)
            sys.stdout.flush()

    def inner_handle(self):
        # IMRPOVEMENT: Report who requests are from in logging
        # IMPROVEMENT: Timeout on client
        debug('Connection - authenticating')
        version = self.read(1)

        if allow_v4 and version == b'\x04':
            cmd = self.read(1)
            if cmd != CONNECT and cmd != UDP_ASSOCIATE:
                error('Only supports connect and udp-associate method not (%r) closing' % cmd)
                self.close_request()
                return

            raw_dest_port = self.read(2)
            dest_port, = unpack('>H', raw_dest_port)

            raw_dest_address = self.read(4)
            dest_address = '.'.join(map(str, unpack('>4B', raw_dest_address)))

            user_id = b''
            c = self.read(1)
            while c != b'\0':
                user_id += c
                c = self.read(1)

            outbound_sock = socket.socket(socket.AF_INET)
            out_address = socket.getaddrinfo(dest_address, dest_port)[0][4]
            debug("Creating forwarder connection to %s:%d" % (out_address[0], out_address[1]))
            outbound_sock.connect(out_address)

            self.send_reply_v4(outbound_sock.getsockname())

            spawn_forwarder(outbound_sock, self.request, 'destination')
            forward(self.request, outbound_sock, 'client')
            return

        if version != b'\x05':
            error('Wrong version number (%r) closing...' % version)
            self.close_request()
            return

        nmethods = ord(self.read(1))
        method_list = self.read(nmethods)

        global password
        global username

        if password is None and NOAUTH in method_list:
            self.send_no_auth_method()
            debug('Authenticated (no-auth)')
        elif USERPASS in method_list:
            self.send_user_pass_auth_method()
            auth_version = self.read(1)
            if auth_version != b'\x01':
                error('Wrong sub-negotiation version number (%r) closing...' % version)
                self.close_request()
                return
            usr_len = ord(self.read(1))
            usr_name = self.read(usr_len)
            pwd_len = ord(self.read(1))
            pwd = self.read(pwd_len)

            if usr_name != username.encode() or pwd != password.encode():
                error('Invalid username or password')
                self.close_request()
                return
            debug('Authenticated (user/password)')
            self.send_authenticated()
        else:
            error('Server only supports NOAUTH and user/pass')
            self.send_no_method()
            return

        # If we were authenticating it would go here
        version = self.read(1)
        cmd = self.read(1)
        zero = self.read(1)
        address_type = self.read(1)
        if version != b'\x05':
            error('Wrong version number (%r) closing...' % version)
            self.close_request()
        elif cmd != CONNECT and cmd != UDP_ASSOCIATE:
            error('Only supports connect and udp-associate method not (%r) closing' % cmd)
            self.close_request()
        elif zero != b'\x00':
            error('Mangled request. Reserved field (%r) is not null' % zero)
            self.close_request()

        if address_type == IPV4:
            raw_dest_address = self.read(4)
            dest_address = '.'.join(map(str, unpack('>4B', raw_dest_address)))
        elif address_type == IPV6:
            raw_dest_address = self.read(16)
            dest_address = ":".join([hex(x)[2:] for x in unpack('>8H', raw_dest_address)])
        elif address_type == DOMAIN_NAME:
            dns_length = ord(self.read(1))
            dns_name = self.read(dns_length)
            dest_address = dns_name
        else:
            error('Unknown addressing (%r)' % address_type)
            self.close_request()

        raw_dest_port = self.read(2)
        dest_port, = unpack('>H', raw_dest_port)

        if address_type == IPV6:
            outbound_sock = socket.socket(socket.AF_INET6)
        else:
            outbound_sock = socket.socket(socket.AF_INET)
        try:
            out_address = socket.getaddrinfo(dest_address, dest_port)[0][4]
        except Exception as e:
            error('%s' % e)
            traceback.print_exc(file=sys.stdout)
            return

        if cmd == UDP_ASSOCIATE:
            debug("no UDP support yet, closing")
            return

        debug("Creating forwarder connection to %s:%d" % (out_address[0], out_address[1]))

        try:
            outbound_sock.connect(out_address)
        except Exception as e:
            error('%s' % e)
            traceback.print_exc(file=sys.stdout)
            return

        if address_type == IPV6:
            self.send_reply6(outbound_sock.getsockname())
        else:
            self.send_reply(outbound_sock.getsockname())

        spawn_forwarder(outbound_sock, self.request, 'destination')
        try:
            forward(self.request, outbound_sock, 'client')
        except Exception as e:
            error('%s' % e)
            traceback.print_exc(file=sys.stdout)

    def send_reply_v4(self, xxx_todo_changeme):
        (bind_addr, bind_port) = xxx_todo_changeme
        self.wfile.write(b'\0\x5a\0\0\0\0\0\0')
        self.wfile.flush()

    def send_reply(self, xxx_todo_changeme1):
        (bind_addr, bind_port) = xxx_todo_changeme1
        bind_tuple = tuple(map(int, bind_addr.split('.')))
        full_address = bind_tuple + (bind_port,)
        debug('Setting up forwarding port %r' % (full_address,))
        msg = pack('>cccc4BH', VERSION, SUCCESS, b'\x00', IPV4, *full_address)
        self.wfile.write(msg)

    def send_reply6(self, xxx_todo_changeme2):
        (bind_addr, bind_port, unused1, unused2) = xxx_todo_changeme2
        bind_tuple = tuple([int(x, 16) for x in bind_addr.split(':')])
        full_address = bind_tuple + (bind_port,)
        debug('Setting up forwarding port %r' % (full_address,))
        msg = pack('>cccc8HH', VERSION, SUCCESS, b'\x00', IPV6, *full_address)
        self.wfile.write(msg)

    def send_no_method(self):
        self.wfile.write(b'\x05\xff')
        self.close_request()

    def send_no_auth_method(self):
        self.wfile.write(b'\x05\x00')
        self.wfile.flush()

    def send_user_pass_auth_method(self):
        self.wfile.write(b'\x05\x02')
        self.wfile.flush()

    def send_authenticated(self):
        self.wfile.write(b'\x01\x00')
        self.wfile.flush()


if __name__ == '__main__':

    debug('starting socks.py %s' % " ".join(sys.argv))
    debug('python version: %s' % sys.version_info.__str__())
    listen_port = 8002
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == '--username':
            username = sys.argv[i + 1]
            i += 1
        elif sys.argv[i] == '--password':
            password = sys.argv[i + 1]
            i += 1
        elif sys.argv[i] == '--port':
            listen_port = int(sys.argv[i + 1])
            i += 1
        elif sys.argv[i] == '--allow-v4':
            allow_v4 = True
        else:
            if sys.argv[i] != '--help':
                debug('unknown option "%s"' % sys.argv[i])
            print('usage: socks.py [--username <user> --password <password>] [--port <listen-port>]')
            sys.stdout.flush()
            sys.exit(1)
        i += 1

    debug('Listening on port %d...' % listen_port)
    server = MyTCPServer(('localhost', listen_port), SocksHandler)
    server.timeout = 190
    while True:
        server.handle_request()
