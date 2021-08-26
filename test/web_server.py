#!/usr/bin/env python3

import sys
import os
import ssl
import gzip
import base64
import socket
import traceback

from http.server import HTTPServer, BaseHTTPRequestHandler

chunked_encoding = False
keepalive = True

try:
    fin = open('test_file', 'rb')
    f = gzip.open('test_file.gz', 'wb')
    f.writelines(fin)
    f.close()
    fin.close()
except Exception:
    pass


class http_server_with_timeout(HTTPServer):
    allow_reuse_address = True
    timeout = 250

    def handle_timeout(self):
        print('TIMEOUT')
        raise Exception('timeout')


class http_handler(BaseHTTPRequestHandler):

    def do_GET(self):
        try:
            self.inner_do_GET()
        except Exception:
            print('EXCEPTION')
            traceback.print_exc(file=sys.stdout)
            sys.stdout.flush()

    def inner_do_GET(self):

        print('INCOMING-REQUEST [from: {}]: {}'.format(self.request.getsockname(), self.requestline))
        print(self.headers)
        sys.stdout.flush()

        global chunked_encoding
        global keepalive

        # if the request contains the hostname and port. strip it
        if self.path.startswith('http://') or self.path.startswith('https://'):
            self.path = self.path[8:]
            self.path = self.path[self.path.find('/'):]

        file_path = os.path.normpath(self.path)
        sys.stdout.flush()

        if self.path == '/password_protected':
            passed = False
            if 'Authorization' in self.headers:
                auth = self.headers['Authorization']
                passed = auth == 'Basic %s' % base64.b64encode(b'testuser:testpass').decode()

            if not passed:
                self.send_response(401)
                self.send_header("Connection", "close")
                self.end_headers()
                return

            self.path = '/test_file'
            file_path = os.path.normpath('/test_file')

        if self.path == '/redirect':
            self.send_response(301)
            self.send_header("Location", "/test_file")
            self.send_header("Connection", "close")
            self.end_headers()
        elif self.path == '/infinite_redirect':
            self.send_response(301)
            self.send_header("Location", "/infinite_redirect")
            self.send_header("Connection", "close")
            self.end_headers()
        elif self.path == '/relative/redirect':
            self.send_response(301)
            self.send_header("Location", "../test_file")
            self.send_header("Connection", "close")
            self.end_headers()
        elif self.path.startswith('/announce'):
            self.send_response(200)
            response = b'd8:intervali1800e8:completei1e10:incompletei1e' + \
                b'12:min intervali' + min_interval.encode() + b'e' + \
                b'5:peers12:AAAABBCCCCDD' + \
                b'6:peers618:EEEEEEEEEEEEEEEEFF' + \
                b'e'
            self.send_header("Content-Length", "%d" % len(response))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(response)
            self.request.close()
        elif os.path.split(self.path)[1].startswith('seed?'):
            query = self.path[6:]
            args_raw = query.split('&')
            args = {}
            for a in args_raw:
                kvp = a.split('=')
                args[kvp[0]] = kvp[1]
            piece = int(args['piece'])
            ranges = args['ranges'].split('-')

            filename = ''
            try:
                filename = os.path.normpath(self.path[1:self.path.find('seed?') + 4])
                print('filename = %s' % filename)
                sys.stdout.flush()
                f = open(filename, 'rb')
                f.seek(piece * 32 * 1024 + int(ranges[0]))
                data = f.read(int(ranges[1]) - int(ranges[0]) + 1)
                f.close()

                self.send_response(200)
                print('sending %d bytes' % len(data))
                sys.stdout.flush()
                self.send_header("Content-Length", "%d" % len(data))
                self.end_headers()
                self.wfile.write(data)
            except Exception as e:
                print('FILE ERROR: ', filename, e)
                traceback.print_exc(file=sys.stdout)
                sys.stdout.flush()
                self.send_response(404)
                self.send_header("Content-Length", "0")
                try:
                    self.end_headers()
                except Exception:
                    pass
        else:
            filename = ''
            try:
                filename = os.path.normpath(file_path[1:])
                # serve file by invoking default handler
                f = open(filename, 'rb')
                size = int(os.stat(filename).st_size)
                start_range = 0
                end_range = size
                if 'Range' in self.headers:
                    self.send_response(206)
                    st, e = self.headers['range'][6:].split('-', 1)
                    sl = len(st)
                    el = len(e)
                    if sl > 0:
                        start_range = int(st)
                        if el > 0:
                            end_range = int(e) + 1
                    elif el > 0:
                        ei = int(e)
                        if ei < size:
                            start_range = size - ei
                    self.send_header('Content-Range', 'bytes ' + str(start_range)
                                  + '-' + str(end_range - 1) + '/' + str(size))
                else:
                    self.send_response(200)
                self.send_header('Accept-Ranges', 'bytes')
                if chunked_encoding:
                    self.send_header('Transfer-Encoding', 'chunked')
                self.send_header('Content-Length', end_range - start_range)
                if filename.endswith('.gz'):
                    self.send_header('Content-Encoding', 'gzip')
                if not keepalive:
                    self.send_header("Connection", "close")
                    if not use_ssl:
                        self.request.shutdown(socket.SHUT_RD)

                self.end_headers()

                f.seek(start_range)
                length = end_range - start_range
                while length > 0:
                    to_send = min(length, 0x900)
                    if chunked_encoding:
                        self.wfile.write(b'%x\r\n' % to_send)
                    data = f.read(to_send)
                    print('read %d bytes' % to_send)
                    sys.stdout.flush()
                    self.wfile.write(data)
                    if chunked_encoding:
                        self.wfile.write(b'\r\n')
                    length -= to_send
                    print('sent %d bytes (%d bytes left)' % (len(data), length))
                    sys.stdout.flush()
                if chunked_encoding:
                    self.wfile.write(b'0\r\n\r\n')
            except Exception as e:
                print('FILE ERROR: ', filename, e)
                traceback.print_exc(file=sys.stdout)
                sys.stdout.flush()
                self.send_response(404)
                self.send_header("Content-Length", "0")
                try:
                    self.end_headers()
                except Exception:
                    pass

        print("...DONE")
        sys.stdout.flush()
        self.wfile.flush()


if __name__ == '__main__':
    port = int(sys.argv[1])
    chunked_encoding = sys.argv[2] != '0'
    use_ssl = sys.argv[3] != '0'
    keepalive = sys.argv[4] != '0'
    min_interval = sys.argv[5]
    print('python version: %s' % sys.version_info.__str__())

    http_handler.protocol_version = 'HTTP/1.1'
    httpd = http_server_with_timeout(('127.0.0.1', port), http_handler)
    if use_ssl:
        httpd.socket = ssl.wrap_socket(httpd.socket, certfile='../ssl/server.pem', server_side=True)

    while True:
        httpd.handle_request()
