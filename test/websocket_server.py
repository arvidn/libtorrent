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

logger = logging.getLogger('websockets')
logger.setLevel(logging.INFO)
logger.addHandler(logging.StreamHandler(sys.stdout))


async def handle(websocket):
    # the tracker behavior to simulate:
    # normal: respond to every announce
    # failure: respond to every announce with a failure reason
    # bare-failure: send a failure reason without an info_hash (like trackers
    #   do for requests they can't parse), then respond normally
    # stale-failure: respond normally, then send a failure reason that isn't
    #   the outcome of the announce (like aquatic does when an answer arrives
    #   for an offer it no longer knows about)
    # duplicate-response: respond to every announce twice
    mode = sys.argv[3] if len(sys.argv) > 3 else 'normal'

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

            if mode == 'failure':
                await websocket.send(json.dumps({
                    "action": "announce",
                    "failure reason": "test failure",
                    "info_hash": info_hash}))
                continue

            if mode == 'bare-failure':
                await websocket.send(json.dumps({
                    "failure reason": "invalid request"}))

            response = {}
            response["action"] = "announce"
            response["info_hash"] = info_hash
            response["interval"] = 120
            response["min_interval"] = 60

            await websocket.send(json.dumps(response))

            if mode == 'duplicate-response':
                await websocket.send(json.dumps(response))

            if mode == 'stale-failure':
                await websocket.send(json.dumps({
                    "action": "announce",
                    "failure reason": "Could not find the offer corresponding"
                    " to your answer. It may have expired.",
                    "info_hash": info_hash}))

    except Exception as e:
        print(e)


async def main() -> None:
    use_ssl = sys.argv[1] != '0'
    print('python version: %s' % sys.version_info.__str__())

    if use_ssl:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain('../ssl/server.pem')
    else:
        ssl_context = None

    server = await websockets.serve(handle, '127.0.0.1', 0, ssl=ssl_context)
    port = server.sockets[0].getsockname()[1]
    print(f'LISTENING_PORT {port}')
    sys.stdout.flush()

if __name__ == '__main__':
    loop = asyncio.new_event_loop()
    loop.run_until_complete(main())
    loop.run_forever()
