#!/usr/bin/env python3
# A TCP proxy for tests that acts like an internet connection: everything
# arrives LAG_MS milliseconds later each way (so twice that for a round
# trip), and at most RATE bytes a second (0: no limit).
#
#   tools/lagproxy.py <listen port> <target port> <lag ms> [rate bytes/s]

import asyncio
import sys
import time

listen_port = int(sys.argv[1])
target_port = int(sys.argv[2])
lag = int(sys.argv[3]) / 1000.0
rate = int(sys.argv[4]) if len(sys.argv) > 4 else 0


async def pipe(reader, writer):
    queue = asyncio.Queue()

    async def receive():
        while True:
            data = await reader.read(65536)
            await queue.put((time.monotonic(), data))
            if not data:
                return

    async def send():
        # When the link is free again (the rate limit).
        free_at = 0.0
        while True:
            stamp, data = await queue.get()
            if not data:
                writer.close()
                return
            due = max(stamp + lag, free_at)
            wait = due - time.monotonic()
            if wait > 0:
                await asyncio.sleep(wait)
            writer.write(data)
            await writer.drain()
            if rate > 0:
                free_at = max(due, time.monotonic()) + len(data) / rate

    await asyncio.gather(receive(), send(), return_exceptions=True)


async def handle(client_reader, client_writer):
    try:
        server_reader, server_writer = await asyncio.open_connection("127.0.0.1", target_port)
    except OSError:
        client_writer.close()
        return
    await asyncio.gather(pipe(client_reader, server_writer), pipe(server_reader, client_writer), return_exceptions=True)


async def main():
    server = await asyncio.start_server(handle, "127.0.0.1", listen_port)
    async with server:
        await server.serve_forever()


asyncio.run(main())
