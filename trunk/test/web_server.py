import BaseHTTPServer
import SimpleHTTPServer
import sys
import os
import ssl

chunked_encoding = False

class http_handler(SimpleHTTPServer.SimpleHTTPRequestHandler):

	def do_GET(s):

		#print s.requestline
		global chunked_encoding

		# if the request contains the hostname and port. strip it
		if s.path.startswith('http://') or s.path.startswith('https://'):
			s.path = s.path[8:]
			s.path = s.path[s.path.find('/'):]

		s.path = os.path.normpath(s.path)

		if s.path == '/redirect':
			s.send_response(301)
			s.send_header("Location", "/test_file")
			s.send_header("Connection", "close")
			s.end_headers()
			s.finish()
		elif s.path == '/infinite_redirect':
			s.send_response(301)
			s.send_header("Location", "/infinite_redirect")
			s.send_header("Connection", "close")
			s.end_headers()
			s.finish()
		elif s.path == '/relative/redirect':
			s.send_response(301)
			s.send_header("Location", "../test_file")
			s.send_header("Connection", "close")
			s.end_headers()
			s.finish()
		elif s.path.startswith('/announce'):
			s.send_response(200)
			response = 'd8:intervali1800e8:completei1e10:incompletei1e5:peers0:e'
			s.send_header("Content-Length", "%d" % len(response))
			s.send_header("Connection", "close")
			s.end_headers()
			s.wfile.write(response)
			s.finish()
		elif os.path.split(s.path)[1].startswith('seed?'):
			query = s.path[6:]
			args_raw = query.split('&')
			args = {}
			for a in args_raw:
				kvp = a.split('=')
				args[kvp[0]] = kvp[1]
			piece = int(args['piece'])
			ranges = args['ranges'].split('-')

			try:
				filename = s.path[1:s.path.find('seed?') + 4]
				#print 'filename = %s' % filename
				f = open(filename, 'rb')
				f.seek(piece * 64 * 1024 + int(ranges[0]))
				data = f.read(int(ranges[1]) - int(ranges[0]) + 1)
				f.close()

				s.send_response(200)
				print 'sending %d bytes' % len(data)
				s.send_header("Content-Length", "%d" % len(data))
				s.end_headers()
				s.wfile.write(data);
			except Exception, e:
				print 'FILE NOT FOUND: ', e
				s.send_response(404)
				s.send_header("Content-Length", "0")
				s.end_headers()
		else:
			try:
				filename = s.path[1:]
				# serve file by invoking default handler
				f = open(filename)
				size = int(os.stat(filename).st_size)
				start_range = 0
				end_range = size
				if 'Range' in s.headers:
					s.send_response(206)
					s.send_header('Content-Range', 'bytes ' + str(start_range) + '-' + str(end_range - 1) + '/' + str(size))
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
				else:
					s.send_response(200)
				s.send_header('Accept-Ranges', 'bytes')
				if chunked_encoding:
					s.send_header('Transfer-Encoding', 'chunked')
				s.send_header('Content-Length', end_range - start_range)
				if filename.endswith('.gz'):
					s.send_header('Content-Encoding', 'gzip')
				s.end_headers()
   
				f.seek(start_range)
				length = end_range - start_range
				while length > 0:
					to_send = min(length, 0x900)
					if chunked_encoding:
						s.wfile.write('%x\r\n' % to_send)
					data = f.read(to_send)
					s.wfile.write(data)
					if chunked_encoding:
						s.wfile.write('\r\n')
					length -= to_send
				if chunked_encoding:
					s.wfile.write('0\r\n\r\n')
			except Exception, e:
				print 'FILE NOT FOUND: ', e
				s.send_response(404)
				s.send_header("Content-Length", "0")
				s.end_headers()


if __name__ == '__main__':
	port = int(sys.argv[1])
	chunked_encoding = sys.argv[2] != '0'
	ssl = sys.argv[3] != '0'

	# TODO: SSL support
	http_handler.protocol_version = 'HTTP/1.1'
	httpd = BaseHTTPServer.HTTPServer(('127.0.0.1', port), http_handler)
	if ssl:
		httpd.socket = ssl.wrap_socket(httpd.socket, certfile='../ssl/server.pem', server_side=True)

	try:
		httpd.serve_forever()
	except KeyboardInterrupt:
		pass
	httpd.server_close()

