#!/usr/bin/python

"""Minimal non-feature complete socks proxy"""

import random
import socket
from SocketServer import StreamRequestHandler, ThreadingTCPServer
from struct import pack, unpack
import threading
import sys

def debug(s):
   print >>sys.stderr, 'socks.py: ', s

def error(s):
   print >>sys.stderr, 'socks.py, ERROR: ', s

class MyTCPServer(ThreadingTCPServer):
    allow_reuse_address = True

    def handle_timeout(self):
        raise Exception('timeout')

CLOSE = object()

VERSION = '\x05'
NOAUTH = '\x00'
USERPASS = '\x02'
CONNECT = '\x01'
UDP_ASSOCIATE = '\x03'
IPV4 = '\x01'
IPV6 = '\x04'
DOMAIN_NAME = '\x03'
SUCCESS = '\x00'

password = None
username = None
allow_v4 = False

def send(dest, msg):
    if msg == CLOSE:
        try: dest.shutdown(socket.SHUT_WR)
        except: pass
        dest.close()
        return 0
    else:
        return dest.sendall(msg)

def recv(source, buffer):
    data = source.recv(buffer)
    if data == '':
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
#        debug('Sending (%d) %r' % (len(data), data))
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
        data = ''
        while len(data) < n:
            extra = self.rfile.read(n)
            if extra == '':
                raise Exception('Connection closed')
            data += extra
        return data

    def handle(self):
        # IMRPOVEMENT: Report who requests are from in logging
        # IMPROVEMENT: Timeout on client
        debug('Connection - authenticating')
        version = self.read(1)

        if allow_v4 and version == '\x04':
            cmd = self.read(1)
            if cmd != CONNECT and cmd != UDP_ASSOCIATE:
                error('Only supports connect and udp-associate method not (%r) closing' % cmd)
                self.close_request()
                return

            raw_dest_port = self.read(2)
            dest_port, = unpack('>H', raw_dest_port)

            raw_dest_address = self.read(4)
            dest_address = '.'.join(map(str, unpack('>4B', raw_dest_address)))

            user_id = ''
            c = self.read(1)
            while c != '\0':
                user_id += c
                c = self.read(1)

            outbound_sock = socket.socket(socket.AF_INET)
            out_address = socket.getaddrinfo(dest_address,dest_port)[0][4]
            debug("Creating forwarder connection to %s:%d" % (out_address[0], out_address[1]))
            outbound_sock.connect(out_address)

            self.send_reply_v4(outbound_sock.getsockname())

            spawn_forwarder(outbound_sock, self.request, 'destination')
            forward(self.request, outbound_sock, 'client')
            return

        if version != '\x05':
            error('Wrong version number (%r) closing...' % version)
            self.close_request()
            return

        nmethods = ord(self.read(1))
        method_list = self.read(nmethods)

        global password
        global username

        if password == None and NOAUTH in method_list:
            self.send_no_auth_method()
            debug('Authenticated (no-auth)')
        elif USERPASS in method_list:
            self.send_user_pass_auth_method()
            auth_version = self.read(1)
            if auth_version != '\x01':
                error('Wrong sub-negotiation version number (%r) closing...' % version)
                self.close_request()
                return
            usr_len = ord(self.read(1))
            usr_name = self.read(usr_len)
            pwd_len = ord(self.read(1))
            pwd = self.read(pwd_len)

            if usr_name != username or pwd != password:
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
        version, cmd, zero, address_type = self.read(4)
        if version != '\x05':
            error('Wrong version number (%r) closing...' % version)
            self.close_request()
        elif cmd != CONNECT and cmd != UDP_ASSOCIATE:
            error('Only supports connect and udp-associate method not (%r) closing' % cmd)
            self.close_request()
        elif zero != '\x00':
            error('Mangled request. Reserved field (%r) is not null' % zero)
            self.close_request()

        if address_type == IPV4:
            raw_dest_address = self.read(4)
            dest_address = '.'.join(map(str, unpack('>4B', raw_dest_address)))
        elif address_type == IPV6:
            raw_dest_address = self.read(16)
            dest_address = ":".join(map(lambda x: hex(x)[2:],unpack('>8H',raw_dest_address)))
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
            out_address = socket.getaddrinfo(dest_address,dest_port)[0][4]
        except Exception, e:
            print e
            return

        if cmd == UDP_ASSOCIATE:
           debug("no UDP support yet, closing")
           return;

        debug("Creating forwarder connection to %s:%d" % (out_address[0], out_address[1]))

        try:
            outbound_sock.connect(out_address)
        except Exception, e:
            print e
            return

        if address_type == IPV6:
            self.send_reply6(outbound_sock.getsockname())
        else:
            self.send_reply(outbound_sock.getsockname())

        spawn_forwarder(outbound_sock, self.request, 'destination')
        try:
            forward(self.request, outbound_sock, 'client')
        except Exception,e:
            print e

    def send_reply_v4(self, (bind_addr, bind_port)):
        self.wfile.write('\0\x5a\0\0\0\0\0\0')
        self.wfile.flush()

    def send_reply(self, (bind_addr, bind_port)):
        bind_tuple = tuple(map(int, bind_addr.split('.')))
        full_address = bind_tuple + (bind_port,)
        debug('Setting up forwarding port %r' % (full_address,))
        msg = pack('>cccc4BH', VERSION, SUCCESS, '\x00', IPV4, *full_address)
        self.wfile.write(msg)

    def send_reply6(self, (bind_addr, bind_port, unused1, unused2)):
        bind_tuple = tuple(map(lambda x: int(x,16), bind_addr.split(':')))
        full_address = bind_tuple + (bind_port,)
        debug('Setting up forwarding port %r' % (full_address,))
        msg = pack('>cccc8HH', VERSION, SUCCESS, '\x00', IPV6, *full_address)
        self.wfile.write(msg)

    def send_no_method(self):
        self.wfile.write('\x05\xff')
        self.close_request()

    def send_no_auth_method(self):
        self.wfile.write('\x05\x00')
        self.wfile.flush()

    def send_user_pass_auth_method(self):
        self.wfile.write('\x05\x02')
        self.wfile.flush()

    def send_authenticated(self):
        self.wfile.write('\x01\x00')
        self.wfile.flush()

if __name__ == '__main__':

    listen_port = 8002
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == '--username':
            username = sys.argv[i+1]
            i += 1
        elif sys.argv[i] == '--password':
            password = sys.argv[i+1]
            i += 1
        elif sys.argv[i] == '--port':
            listen_port = int(sys.argv[i+1])
            i += 1
        elif sys.argv[i] == '--allow-v4':
            allow_v4 = True
        else:
            if sys.argv[i] != '--help': debug('unknown option "%s"' % sys.argv[i])
            print('usage: socks.py [--username <user> --password <password>] [--port <listen-port>]')
            sys.exit(1)
        i += 1

    debug('Listening on port %d...' % listen_port)
    server = MyTCPServer(('localhost', listen_port), SocksHandler)
    server.timeout = 190
    while True:
        server.handle_request()

