#!/usr/bin/env python3
"""
HTTP/2-compatible web server using Quart and Hypercorn
Replacement for web_server.py with proper HTTP/2 support for libcurl testing
"""

import sys
import os
import ssl
import gzip
import asyncio
import traceback
import logging
import base64
from urllib.parse import unquote
from pathlib import Path

try:
    from quart import Quart, Response, request, send_file, redirect, abort
    from hypercorn.config import Config
    from hypercorn.asyncio import serve
except ImportError:
    print("Error: quart and hypercorn are required.")
    print("Please install them: pip install quart hypercorn")
    sys.exit(1)

# --- Configuration and Global State ---

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(message)s')
log = logging.getLogger(__name__)

# Configuration defaults, will be overwritten by args
CONFIG = {
    "chunked_encoding": False,
    "keepalive": True,
    "use_ssl": False,
    "min_interval": "1800",
    "port": 8080
}

# Track retry attempts for /retry_test
retry_attempts = {}
# Lock for safe access in an async environment
retry_lock = asyncio.Lock()

app = Quart(__name__)

# --- Helper Functions ---

def prepare_gzip_file():
    """Ensures test_file.gz exists and is up-to-date."""
    try:
        if os.path.exists('test_file'):
            if not os.path.exists('test_file.gz') or \
               os.path.getmtime('test_file') > os.path.getmtime('test_file.gz'):
                log.info("Gzipping test_file...")
                with open('test_file', 'rb') as fin:
                    with gzip.open('test_file.gz', 'wb') as f:
                        f.writelines(fin)
    except Exception as e:
        log.warning(f"Could not prepare test_file.gz: {e}")

def is_safe_path(path):
    """Helper to check for directory traversal."""
    # Decode URL encoding before normalization
    normalized_path = os.path.normpath(unquote(path.lstrip('/')))
    abs_path = os.path.abspath(normalized_path)
    # Ensure the resolved path is within the current working directory
    if not abs_path.startswith(os.getcwd()):
        log.warning(f"Forbidden (Path Traversal Attempt): {path}")
        return False
    return True

# --- Middleware ---

@app.before_request
async def log_request():
    """Log incoming requests with protocol version."""
    # Get protocol version from request
    protocol = request.scope.get('http_version', 'Unknown')
    remote = request.remote_addr
    
    log.info(f'INCOMING-REQUEST [{protocol}] [from: {remote}]: {request.method} {request.path}')

@app.after_request
async def log_response(response):
    """Log response status and handle keep-alive."""
    log.info(f'...DONE (Status: {response.status_code}) for {request.path}')
    
    return response

@app.errorhandler(Exception)
async def handle_error(error):
    """Handle unexpected errors."""
    # Let HTTP exceptions (like abort(404)) pass through with their proper status
    from werkzeug.exceptions import HTTPException
    if isinstance(error, HTTPException):
        # Return empty body for error responses to match original server
        return Response(b'', status=error.code)
    
    if isinstance(error, (ConnectionResetError, asyncio.CancelledError)):
        log.warning(f'Connection dropped by client during request: {request.path}')
        return Response('', status=500)
    
    log.error(f'UNEXPECTED EXCEPTION handling {request.path}: {error}')
    traceback.print_exc(file=sys.stdout)
    return Response('Internal Server Error', status=500)

# --- Route Handlers ---

@app.route('/redirect')
async def handle_redirect():
    """Handle redirect to /test_file."""
    response = Response(b'', status=301)
    response.headers['Location'] = '/test_file'
    response.headers['Connection'] = 'close'
    return response

@app.route('/infinite_redirect')
async def handle_infinite_redirect():
    """Handle infinite redirect loop."""
    response = Response(b'', status=301)
    response.headers['Location'] = '/infinite_redirect'
    response.headers['Connection'] = 'close'
    return response

@app.route('/relative/redirect')
async def handle_relative_redirect():
    """Handle relative redirect."""
    response = Response(b'', status=301)
    response.headers['Location'] = '../test_file'
    response.headers['Connection'] = 'close'
    return response

@app.route('/status/<int:code>')
async def handle_status(code):
    """Return a specific HTTP status code."""
    if 100 <= code <= 599:
        response = Response(b'', status=code)
        return response
    else:
        abort(400, description="Invalid status code")

@app.route('/retry_test')
async def handle_retry_test():
    """Returns 500 on first attempt, 200 on retry."""
    # Use a fixed key as clients might use different ports for retries
    client_key = 'retry_test_client'
    
    # Async-safe check and update of attempts
    async with retry_lock:
        if client_key not in retry_attempts:
            retry_attempts[client_key] = 1
            attempt_count = 1
            should_succeed = False
        else:
            retry_attempts[client_key] += 1
            attempt_count = retry_attempts[client_key]
            should_succeed = True
            # Clean up after successful retry
            if attempt_count >= 2:
                del retry_attempts[client_key]
    
    if not should_succeed:
        log.info(f'RETRY_TEST: First attempt, returning 500')
        response = Response(b'', status=500)
    else:
        log.info(f'RETRY_TEST: Retry #{attempt_count-1}, returning 200')
        response = Response(b'Success after retry', status=200)
    
    return response

@app.route('/slow')
async def handle_slow():
    """Delays the response by 5 seconds."""
    log.info("SLOW: Starting 5s delay...")
    await asyncio.sleep(5)
    log.info("SLOW: Ending delay.")
    response = Response(b'Slow response', status=200)
    return response

@app.route('/announce', methods=['GET'])
@app.route('/announce/<path:subpath>', methods=['GET'])
async def handle_announce(subpath=None):
    """Simulates a tracker announce response."""
    min_interval = CONFIG['min_interval']
    response_body = (b'd8:intervali1800e8:completei1e10:incompletei1e' +
                     b'12:min intervali' + min_interval.encode() + b'e' +
                     b'5:peers12:AAAABBCCCCDD' +
                     b'6:peers618:EEEEEEEEEEEEEEEEFF' +
                     b'e')
    # Tracker responses usually close the connection
    response = Response(response_body, status=200, content_type='text/plain')
    return response

@app.route('/password_protected')
async def handle_password_protected():
    """Handle basic authentication protected endpoint."""
    auth_header = request.headers.get('Authorization', '')
    
    if not auth_header.startswith('Basic '):
        response = Response('', status=401)
        response.headers['WWW-Authenticate'] = 'Basic realm="Test Area"'
        return response
    
    try:
        encoded = auth_header[6:]  # Remove 'Basic '
        decoded = base64.b64decode(encoded).decode('utf-8')
        username, password = decoded.split(':', 1)
        if username != 'testuser' or password != 'testpass':
            response = Response('', status=401)
            response.headers['WWW-Authenticate'] = 'Basic realm="Test Area"'
            response.headers['Connection'] = 'close'  # Match old server behavior
            return response
    except Exception:
        response = Response('', status=401)
        response.headers['WWW-Authenticate'] = 'Basic realm="Test Area"'
        response.headers['Connection'] = 'close'  # Match old server behavior
        return response
    
    # Authentication successful, serve 'test_file'
    return await serve_static_file('test_file')

@app.route('/<path:path>')
async def handle_files(path):
    """Handle webseed and static file requests."""
    # DEBUG: Log all requests with query parameters
    if request.args:
        log.info(f"DEBUG: Request path='{path}' with args: {dict(request.args)}")
    
    # Check for webseed requests - these have piece and ranges parameters
    # The old server checks: os.path.split(self.path)[1].startswith('seed?')
    # But that's only for BEP19 HTTP seeds, not BEP17 URL seeds
    
    # For BEP19 (HTTP seed): Single file named 'seed' with piece/ranges params
    # For BEP17 (URL seed): Multiple files in directories, no special params typically
    
    # Let's check if this is a piece-based request (BEP19 style)
    if request.args.get('piece') is not None and request.args.get('ranges'):
        # This is a BEP19 webseed request
        filename = os.path.basename(path)
        if filename == 'seed':
            log.info(f"DEBUG: BEP19 webseed request for path='{path}'")
            return await handle_webseed(path)
        else:
            # This shouldn't happen - piece/ranges params on non-seed file
            log.warning(f"WARNING: piece/ranges params on non-seed file: {path}")
    
    # Otherwise serve as static file (including BEP17 URL seed files)
    return await serve_static_file(path)

async def handle_webseed(path):
    """Handles BEP 17/19 style requests."""
    # IMPORTANT: In webseed URLs like /web_seed_http/seed?piece=0&ranges=0-16383
    # 'seed' is NOT a suffix to be stripped - it's the actual filename!
    # The test creates a file literally named 'seed' in the web_seed_http directory
    actual_path = path  # Use the full path as-is
    
    if not is_safe_path(actual_path):
        abort(403)
    
    filename = os.path.normpath(unquote(actual_path.lstrip('/')))
    
    # Get query parameters
    piece = request.args.get('piece', type=int)
    ranges = request.args.get('ranges', '')
    
    # If parameters are missing, this is an error
    if piece is None or not ranges:
        log.warning(f"Webseed request missing parameters: piece={piece}, ranges={ranges}")
        abort(400, description="Missing webseed parameters")
    
    try:
        range_parts = ranges.split('-')
        if len(range_parts) != 2:
            raise ValueError("Invalid ranges format")
        
        start_range = int(range_parts[0])
        end_range = int(range_parts[1])  # Inclusive
        data_length = end_range - start_range + 1
        
        if data_length <= 0 or start_range < 0:
            raise ValueError("Invalid range values")
        
    except (ValueError, TypeError) as e:
        log.warning(f'WEBSEED BAD REQUEST: {e}')
        abort(400, description=f"Invalid request parameters: {e}")
    
    log.info(f'Webseed request: filename={filename}, piece={piece}, range={start_range}-{end_range}')
    
    try:
        PIECE_SIZE = 32 * 1024  # As defined in the original script
        offset = piece * PIECE_SIZE + start_range
        
        # Debug: Show the actual file path being opened
        full_path = os.path.abspath(filename)
        log.info(f"Webseed: Attempting to open file: {full_path} (exists: {os.path.exists(full_path)})")
        
        # Read file segment
        with open(filename, 'rb') as f:
            f.seek(offset)
            data = f.read(data_length)
            if len(data) != data_length:
                log.warning(f"Webseed read mismatch: Expected {data_length}, Got {len(data)}")
            log.info(f"Webseed serving {len(data)} bytes from offset {offset} (file: {filename})")
            # Debug: Show first few bytes in hex
            if len(data) > 0:
                preview = ' '.join(f'{b:02x}' for b in data[:min(16, len(data))])
                log.info(f"Data preview (first {min(16, len(data))} bytes): {preview}")
        
        response = Response(data, content_type='application/octet-stream')
        response.headers['Content-Length'] = str(len(data))
        # Note: Even when keepalive is false, we don't force connection close for webseed
        # This is important for SOCKS5 proxy compatibility where the proxy needs to
        # maintain the tunnel even if individual HTTP requests don't use keep-alive
        # The old server did self.request.shutdown(socket.SHUT_RD) which is a half-close
        # that allows SOCKS5 proxies to work correctly
        return response
        
    except FileNotFoundError:
        abort(404)
    except IOError as e:
        log.error(f'WEBSEED FILE I/O ERROR: {e}')
        abort(500, description="Error reading file")

async def serve_static_file(path):
    """Serve static files with optional chunking and range support."""
    # Don't serve directories or root
    if path.endswith('/') or path == '/' or not path:
        abort(404)
    
    if not is_safe_path(path):
        abort(403)
    
    filename = os.path.normpath(unquote(path.lstrip('/')))
    
    # Check if it's a directory
    if os.path.isdir(filename):
        abort(404)
    
    if not os.path.exists(filename):
        abort(404)
    
    # Handle Range requests
    range_header = request.headers.get('Range')
    
    # Debug logging for Range header
    if range_header:
        log.info(f"Range request for {filename}: {range_header}")
    
    try:
        # Check if we should use chunked encoding
        if CONFIG['chunked_encoding']:
            # For chunked encoding, we need to manually stream the file
            file_size = os.path.getsize(filename)
            
            if range_header:
                # Parse range header
                range_str = range_header.replace('bytes=', '')
                range_parts = range_str.split('-')
                start = int(range_parts[0]) if range_parts[0] else 0
                end = int(range_parts[1]) if range_parts[1] else file_size - 1
                
                async def generate():
                    with open(filename, 'rb') as f:
                        f.seek(start)
                        remaining = end - start + 1
                        chunk_size = 0x900  # Use the buffer size from original script
                        
                        while remaining > 0:
                            to_read = min(chunk_size, remaining)
                            data = f.read(to_read)
                            if not data:
                                break
                            log.info(f'read {len(data)} bytes')
                            remaining -= len(data)
                            yield data
                
                headers = {
                    'Content-Range': f'bytes {start}-{end}/{file_size}',
                    'Content-Length': str(end - start + 1),
                    'Accept-Ranges': 'bytes'
                }
                
                if filename.endswith('.gz'):
                    headers['Content-Encoding'] = 'gzip'
                
                # Note: Don't force connection close for chunked responses
                # SOCKS5 proxy needs to maintain the tunnel
                
                return Response(generate(), status=206, headers=headers,
                              content_type='application/octet-stream')
            else:
                # Stream entire file with chunking
                async def generate():
                    with open(filename, 'rb') as f:
                        chunk_size = 0x900
                        while True:
                            data = f.read(chunk_size)
                            if not data:
                                break
                            log.info(f'read {len(data)} bytes')
                            yield data
                
                headers = {}
                if filename.endswith('.gz'):
                    headers['Content-Encoding'] = 'gzip'
                
                # Note: Don't force connection close for chunked responses
                # SOCKS5 proxy needs to maintain the tunnel
                
                return Response(generate(), headers=headers,
                              content_type='application/octet-stream')
        else:
            # Handle Range requests manually because Quart's conditional=True has a bug
            # where it sets Content-Length to the full file size instead of the range size
            
            if range_header:
                # Parse range header manually
                file_size = os.path.getsize(filename)
                range_str = range_header.replace('bytes=', '')
                range_parts = range_str.split('-')
                
                if range_parts[0] and range_parts[1]:
                    # bytes=start-end
                    start = int(range_parts[0])
                    end = int(range_parts[1])
                elif range_parts[0]:
                    # bytes=start-
                    start = int(range_parts[0])
                    end = file_size - 1
                elif range_parts[1]:
                    # bytes=-suffix
                    suffix_length = int(range_parts[1])
                    start = max(0, file_size - suffix_length)
                    end = file_size - 1
                else:
                    # Invalid range
                    abort(416)  # Range Not Satisfiable
                
                # Validate range
                if start < 0 or start >= file_size or end < start:
                    abort(416)  # Range Not Satisfiable
                
                # Clamp end to file size
                end = min(end, file_size - 1)
                
                # Calculate the actual content length for the range
                content_length = end - start + 1
                
                # Read the requested range
                with open(filename, 'rb') as f:
                    f.seek(start)
                    data = f.read(content_length)
                
                # Create response with correct headers
                response = Response(data, status=206, content_type='application/octet-stream')
                response.headers['Content-Range'] = f'bytes {start}-{end}/{file_size}'
                response.headers['Content-Length'] = str(content_length)  # CRITICAL: Must match the actual range size
                response.headers['Accept-Ranges'] = 'bytes'
                
                if filename.endswith('.gz'):
                    response.headers['Content-Encoding'] = 'gzip'
                
                # Note: Don't force connection close for file responses when using SOCKS5
                # The proxy needs to maintain the tunnel
                
                return response
            else:
                # No Range header, send entire file
                response = await send_file(filename)
                
                if filename.endswith('.gz'):
                    response.headers['Content-Encoding'] = 'gzip'
                
                # Note: Don't force connection close for file responses when using SOCKS5
                # The proxy needs to maintain the tunnel
                
                return response
            
    except FileNotFoundError:
        abort(404)
    except Exception as e:
        log.error(f"Error serving file {filename}: {e}")
        abort(500)

# --- Catch-all route (must be last) ---

@app.route('/')
async def handle_root():
    """Handle root path."""
    abort(404)

# --- Server Startup ---

async def run_hypercorn(host, port, ssl_context=None):
    """Run the Quart app with Hypercorn."""
    config = Config()
    
    if ssl_context:
        # Configure SSL/TLS with HTTP/2 support
        config.certfile = "../ssl/server_signed.pem"
        config.keyfile = config.certfile  # Same file contains both cert and key
        config.keyfile_password = "test"
        config.bind = [f"{host}:{port}"]
        
        # CRUCIAL: Configure ALPN for HTTP/2
        config.alpn_protocols = ["h2", "http/1.1"]
        
        # Enable HTTP/2
        config.h2_max_concurrent_streams = 100
        
        log.info("ALPN configured for h2 and http/1.1")
    else:
        config.bind = [f"{host}:{port}"]
    
    # Set keep-alive timeout
    # When keepalive is disabled, we need a reasonable timeout for proxy operations
    # SOCKS5 proxy has 190s timeout, old server had 250s, we'll use 250s to match
    # the original behavior and ensure proxy handshakes complete successfully
    config.keep_alive_timeout = 75.0 if CONFIG['keepalive'] else 250.0
    
    # Disable Hypercorn's access log as we use custom logging
    config.accesslog = None
    
    server_type = f"HTTP{'S' if ssl_context else ''}"
    h2_status = "(HTTP/2 enabled via TLS)" if ssl_context else "(HTTP/1.1 only)"
    
    log.info(f"--- Starting Hypercorn server (Async with HTTP/2 support) ---")
    log.info(f"Working directory: {os.getcwd()}")
    log.info(f"Serving {server_type} on {host}:{port} {h2_status}")
    log.info(f"Settings: Forced-Chunked={CONFIG['chunked_encoding']}, Keep-Alive={CONFIG['keepalive']}, Min Interval={CONFIG['min_interval']}")
    
    await serve(app, config)

def main():
    """Parse arguments and run the server."""
    if len(sys.argv) < 6:
        print("Usage: python web_server_hypercorn.py <port> <chunked_encoding (0|1)> <use_ssl (0|1)> <keepalive (0|1)> <min_interval>")
        sys.exit(1)
    
    try:
        CONFIG['port'] = int(sys.argv[1])
        CONFIG['chunked_encoding'] = sys.argv[2] != '0'
        CONFIG['use_ssl'] = sys.argv[3] != '0'
        CONFIG['keepalive'] = sys.argv[4] != '0'
        CONFIG['min_interval'] = sys.argv[5]
    except ValueError:
        print("Error: Invalid arguments.")
        sys.exit(1)
    
    log.info(f'Python version: {sys.version_info}')
    
    prepare_gzip_file()
    
    # Create SSL context if needed
    ssl_context = CONFIG['use_ssl']
    
    host = '127.0.0.1'
    port = CONFIG['port']
    
    try:
        # Run the async server
        asyncio.run(run_hypercorn(host, port, ssl_context))
    except OSError as e:
        log.error(f"Failed to start server on port {port} (Is it already in use?): {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        pass
    finally:
        log.info("Server stopped.")

if __name__ == '__main__':
    main()