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

    def inner_do_GET(s):

        print('INCOMING-REQUEST [from: {}]: {}'.format(s.request.getsockname(), s.requestline))
        print(s.headers)
        sys.stdout.flush()

        global chunked_encoding
        global keepalive

        # if the request contains the hostname and port. strip it
        if s.path.startswith('http://') or s.path.startswith('https://'):
            s.path = s.path[8:]
            s.path = s.path[s.path.find('/'):]

        file_path = os.path.normpath(s.path)
        sys.stdout.flush()

        if s.path == '/password_protected':
            passed = False
            if 'Authorization' in s.headers:
                auth = s.headers['Authorization']
                passed = auth == 'Basic %s' % base64.b64encode(b'testuser:testpass').decode()

            if not passed:
                s.send_response(401)
                s.send_header("Connection", "close")
                s.end_headers()
                return

            s.path = '/test_file'
            file_path = os.path.normpath('/test_file')

        if s.path == '/redirect':
            s.send_response(301)
            s.send_header("Location", "/test_file")
            s.send_header("Connection", "close")
            s.end_headers()
        elif s.path == '/infinite_redirect':
            s.send_response(301)
            s.send_header("Location", "/infinite_redirect")
            s.send_header("Connection", "close")
            s.end_headers()
        elif s.path == '/relative/redirect':
            s.send_response(301)
            s.send_header("Location", "../test_file")
            s.send_header("Connection", "close")
            s.end_headers()
        elif s.path.startswith('/announce'):
            s.send_response(200)
            response = b'd8:intervali1800e8:completei1e10:incompletei1e' + \
                b'12:min intervali' + min_interval.encode() + b'e' + \
                b'5:peers12:AAAABBCCCCDD' + \
                b'6:peers618:EEEEEEEEEEEEEEEEFF' + \
                b'e'
            s.send_header("Content-Length", "%d" % len(response))
            s.send_header("Connection", "close")
            s.end_headers()
            s.wfile.write(response)
            s.request.close()
        else:
            filename = ''
            try:
                filename = os.path.normpath(file_path[1:])
                # serve file by invoking default handler
                f = open(filename, 'rb')
                size = int(os.stat(filename).st_size)
                start_range = 0
                end_range = size
                if 'Range' in s.headers:
                    s.send_response(206)
                    st, e = s.headers['range'][6:].split('-', 1)
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
                    s.send_header('Content-Range', 'bytes ' + str(start_range)
                                  + '-' + str(end_range - 1) + '/' + str(size))
                else:
                    s.send_response(200)
                s.send_header('Accept-Ranges', 'bytes')
                if chunked_encoding:
                    s.send_header('Transfer-Encoding', 'chunked')
                s.send_header('Content-Length', end_range - start_range)
                if filename.endswith('.gz'):
                    s.send_header('Content-Encoding', 'gzip')
                if not keepalive:
                    s.send_header("Connection", "close")
                    if not use_ssl:
                        s.request.shutdown(socket.SHUT_RD)

                s.end_headers()

                f.seek(start_range)
                length = end_range - start_range
                while length > 0:
                    to_send = min(length, 0x900)
                    if chunked_encoding:
                        s.wfile.write(b'%x\r\n' % to_send)
                    data = f.read(to_send)
                    print('read %d bytes' % to_send)
                    sys.stdout.flush()
                    s.wfile.write(data)
                    if chunked_encoding:
                        s.wfile.write(b'\r\n')
                    length -= to_send
                    print('sent %d bytes (%d bytes left)' % (len(data), length))
                    sys.stdout.flush()
                if chunked_encoding:
                    s.wfile.write(b'0\r\n\r\n')
            except Exception as e:
                print('FILE ERROR: ', filename, e)
                traceback.print_exc(file=sys.stdout)
                sys.stdout.flush()
                s.send_response(404)
                s.send_header("Content-Length", "0")
                try:
                    s.end_headers()
                except Exception:
                    pass

        print("...DONE")
        sys.stdout.flush()
        s.wfile.flush()


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
