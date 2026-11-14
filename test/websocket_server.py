#!/usr/bin/env python3

import sys
import ssl
import json
import asyncio
import logging
from datetime import datetime

# Remove '.' from sys.path or we try to import the http.py module
sys.path = sys.path[1:]

import websockets
from websockets.legacy.server import serve

server_mode = 'normal'


class IgnorePongOnceProtocol(websockets.WebSocketServerProtocol):
    connection_count = 0

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.suppress_pongs = server_mode == 'reconnect' and type(self).connection_count == 0
        type(self).connection_count += 1

    async def pong(self, data=b''):
        if self.suppress_pongs:
            # Keep the first connection open but stop it proving liveness.
            return
        await super().pong(data)

connection_count = 0

logger = logging.getLogger('websockets')
logger.setLevel(logging.INFO)
logger.addHandler(logging.StreamHandler(sys.stdout))


async def handle(websocket):
    global connection_count
    connection_number = connection_count
    connection_count += 1

    # the tracker behavior to simulate:
    # normal: respond to every announce
    # failure: respond to every announce with a failure reason
    # stale-failure: respond normally, then send a failure reason that isn't
    #   the outcome of the announce (like aquatic does when an answer arrives
    #   for an offer it no longer knows about)
    # bare-failure: inject a failure response without an info_hash, then send
    #   a normal, correlated response on the same connection. Tests that the
    #   client ignores the uncorrelated failure without closing the connection.
    # reconnect: suppress pongs on the first connection, respond to its initial
    #   hybrid-torrent announces, then ignore further announces on that socket.
    #   A replacement connection responds normally.
    # pong: delay a reannounce response while answering pings; ignore announces
    #   on replacement connections so reconnecting cannot satisfy the test.
    mode = sys.argv[3] if len(sys.argv) > 3 else 'normal'
    announce_count = 0
    stale_connection = mode == 'reconnect' and getattr(websocket, 'suppress_pongs', False)

    try:
        while True:
            message = await websocket.recv()

            print('{} - [{}] WS "{}..."'.format(
                websocket.remote_address[0],
                datetime.now().strftime("%d/%m/%Y %H:%M:%S"),
                message[:33]),
                file=sys.stderr)

            request = json.loads(message)
            info_hash = request["info_hash"]
            announce_count += 1

            # Let the initial hybrid-torrent announces complete on the first
            # connection, then require stale-connection recovery for later ones.
            if stale_connection and announce_count > 2:
                continue

            # This scenario only succeeds if the original connection survives.
            if mode == 'pong' and connection_number > 0:
                continue

            if mode == 'failure':
                await websocket.send(json.dumps({
                    "failure reason": "test failure",
                    "info_hash": info_hash}))
                continue

            if mode == 'bare-failure':
                await websocket.send(json.dumps({
                    "failure reason": "Invalid request"}))

            # Delay a reannounce response without sending application data.
            # The pong scenario must survive this delay on the original socket.
            if mode == 'pong' and announce_count == 3:
                await asyncio.sleep(22)

            response = {}
            response["info_hash"] = info_hash
            response["interval"] = 120
            response["min_interval"] = 60

            await websocket.send(json.dumps(response))

            if mode == 'stale-failure':
                await websocket.send(json.dumps({
                    "failure reason": "Could not find the offer corresponding"
                    " to your answer. It may have expired.",
                    "info_hash": info_hash}))

    except Exception as e:
        print(e)


async def main() -> None:
    global server_mode
    use_ssl = sys.argv[1] != '0'
    print('python version: %s' % sys.version_info.__str__())

    if use_ssl:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain('../ssl/server.pem')
    else:
        ssl_context = None

    server_mode = sys.argv[3] if len(sys.argv) > 3 else 'normal'
    server = await serve(handle, '127.0.0.1', 0, ssl=ssl_context,
        create_protocol=IgnorePongOnceProtocol)
    port = server.sockets[0].getsockname()[1]
    print(f'LISTENING_PORT {port}')
    sys.stdout.flush()

if __name__ == '__main__':
    loop = asyncio.new_event_loop()
    loop.run_until_complete(main())
    loop.run_forever()
