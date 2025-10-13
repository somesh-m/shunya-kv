#!/usr/bin/env python3
import argparse, asyncio, random, time
from collections import deque

CRLF = b"\r\n"

# ---------- hash & shard mapping (matches server) ----------
def fnv1a64(b: bytes) -> int:
    h = 0xcbf29ce484222325
    for c in b:
        h ^= c
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

def cpu_shard(key_bytes: bytes, smp: int) -> int:
    # multiply-high scaling of 64-bit hash into [0, smp)
    return (fnv1a64(key_bytes) * smp) >> 64

# ---------- preload ----------
async def preload_single(host, port, conns, keyspace, value_size):
    async def worker(keys):
        reader, writer = await asyncio.open_connection(host, port)
        for k in keys:
            key = f"k{k}".encode()
            writer.write(b"SET " + key + b" " + (b"x" * value_size) + CRLF)
        await writer.drain()
        for _ in keys:
            _ = await reader.readline()
        writer.close()
        await writer.wait_closed()

    keys = list(range(keyspace))
    chunk = (keyspace + conns - 1) // conns
    tasks = [worker(keys[i:i+chunk]) for i in range(0, keyspace, chunk)]
    await asyncio.gather(*tasks)

async def preload_sharded(host, base_port, smp, conns_per_shard, keyspace, value_size):
    # Bucket keys by shard so we write to the correct port
    buckets = [[] for _ in range(smp)]
    for k in range(keyspace):
        key = f"k{k}".encode()
        sid = cpu_shard(key, smp)
        buckets[sid].append(k)

    async def worker(sid, keys):
        reader, writer = await asyncio.open_connection(host, base_port + sid)
        for k in keys:
            key = f"k{k}".encode()
            writer.write(b"SET " + key + b" " + (b"x" * value_size) + CRLF)
        await writer.drain()
        for _ in keys:
            _ = await reader.readline()
        writer.close()
        await writer.wait_closed()

    tasks = []
    for sid, ks in enumerate(buckets):
        if not ks: continue
        chunk = max(1, (len(ks) + conns_per_shard - 1) // conns_per_shard)
        for i in range(0, len(ks), chunk):
            tasks.append(asyncio.create_task(worker(sid, ks[i:i+chunk])))
    if tasks:
        await asyncio.gather(*tasks)

# ---------- single-port client (as before) ----------
async def run_client_single(host, port, total_duration_s, pipeline, keyspace,
                            get_ratio, value_size, results, measuring_evt: asyncio.Event):
    reader, writer = await asyncio.open_connection(host, port)

    send_times, latencies_ms = deque(), []
    stop = time.perf_counter() + total_duration_s

    async def recv_loop():
        while True:
            line = await reader.readline()
            if not line:
                break
            if send_times:
                t0 = send_times.popleft()
                if measuring_evt.is_set():
                    latencies_ms.append((time.perf_counter() - t0) * 1000.0)

    recv_task = asyncio.create_task(recv_loop())

    def make_cmd():
        if random.random() < get_ratio:
            k = random.randrange(keyspace)
            return b"GET k" + str(k).encode() + b"\r\n"
        else:
            k = random.randrange(keyspace)
            return b"SET k" + str(k).encode() + b" " + (b"x" * value_size) + b"\r\n"

    for _ in range(pipeline):
        writer.write(make_cmd())
        send_times.append(time.perf_counter())
    await writer.drain()

    while time.perf_counter() < stop:
        while len(send_times) < pipeline and time.perf_counter() < stop:
            writer.write(make_cmd())
            send_times.append(time.perf_counter())
        await writer.drain()
        await asyncio.sleep(0)

    await asyncio.sleep(0.2)
    writer.close()
    await writer.wait_closed()
    recv_task.cancel()
    try: await recv_task
    except: pass

    results.append(latencies_ms)

# ---------- per-shard-ports client ----------
class ShardConn:
    def __init__(self, sid, reader, writer):
        self.sid = sid
        self.reader = reader
        self.writer = writer
        self.send_times = deque()
        self.latencies_ms = []
        self.recv_task = None

    async def recv_loop(self, measuring_evt: asyncio.Event):
        while True:
            line = await self.reader.readline()
            if not line:
                break
            if self.send_times:
                t0 = self.send_times.popleft()
                if measuring_evt.is_set():
                    self.latencies_ms.append((time.perf_counter() - t0) * 1000.0)

async def run_client_sharded(host, base_port, smp, conns_per_shard, total_duration_s, pipeline,
                             keyspace, get_ratio, value_size, results, measuring_evt: asyncio.Event):
    # build pools
    pools = [[] for _ in range(smp)]
    for sid in range(smp):
        for _ in range(conns_per_shard):
            r, w = await asyncio.open_connection(host, base_port + sid)
            sc = ShardConn(sid, r, w)
            sc.recv_task = asyncio.create_task(sc.recv_loop(measuring_evt))
            pools[sid].append(sc)

    def pick_conn(sid: int) -> ShardConn:
        return min(pools[sid], key=lambda c: len(c.send_times))

    def make_op():
        k = random.randrange(keyspace)
        key = f"k{k}".encode()
        sid = cpu_shard(key, smp)
        if random.random() < get_ratio:
            cmd = b"GET " + key + b"\r\n"
        else:
            cmd = b"SET " + key + b" " + (b"x" * value_size) + b"\r\n"
        return sid, cmd

    # initial fill per shard
    for sid in range(smp):
        for _ in range(pipeline):
            t_sid, cmd = make_op()
            c = pick_conn(t_sid)
            c.writer.write(cmd)
            c.send_times.append(time.perf_counter())
    for sid in range(smp):
        for c in pools[sid]:
            await c.writer.drain()

    stop = time.perf_counter() + total_duration_s
    while time.perf_counter() < stop:
        for sid in range(smp):
            for c in pools[sid]:
                while len(c.send_times) < pipeline:
                    t_sid, cmd = make_op()
                    c2 = pick_conn(t_sid)
                    c2.writer.write(cmd)
                    c2.send_times.append(time.perf_counter())
        for sid in range(smp):
            for c in pools[sid]:
                await c.writer.drain()
        await asyncio.sleep(0)

    await asyncio.sleep(0.2)
    for sid in range(smp):
        for c in pools[sid]:
            c.writer.close()
            try: await c.writer.wait_closed()
            except: pass
            c.recv_task.cancel()

    # aggregate
    lat = []
    for sid in range(smp):
        for c in pools[sid]:
            lat.extend(c.latencies_ms)
    results.append(lat)

# ---------- utils ----------
def pct(vals, p):
    if not vals: return 0.0
    i = max(0, min(len(vals)-1, int(round(p/100.0*(len(vals)-1)))))
    return sorted(vals)[i]

# ---------- main ----------
async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=7070, help="single port OR base port (per-shard)")
    ap.add_argument("--conns", type=int, default=64)
    ap.add_argument("--pipeline", type=int, default=1)
    ap.add_argument("--duration", type=int, default=120, help="MEASUREMENT window (sec)")
    ap.add_argument("--warmup", type=int, default=20, help="Warm-up to ignore (sec)")
    ap.add_argument("--keyspace", type=int, default=100000)
    ap.add_argument("--get-ratio", type=float, default=0.9, help="0..1 (e.g., 0.9 = 90% GETs)")
    ap.add_argument("--value-size", type=int, default=16)
    ap.add_argument("--preload", action="store_true")
    ap.add_argument("--per-shard-ports", action="store_true", help="route by base_port+sid")
    ap.add_argument("--smp", type=int, default=1, help="server shard count (required with --per-shard-ports)")
    args = ap.parse_args()

    if args.per_shard_ports and args.smp < 1:
        raise SystemExit("--smp must be >=1 when using --per-shard-ports")

    # -------- preload --------
    if args.preload:
        print(f"[preload] {args.keyspace} keys, value={args.value_size}B")
        t0 = time.perf_counter()
        if args.per_shard_ports:
            # spread connections across shards
            cps = max(1, args.conns // max(1, args.smp))
            await preload_sharded(args.host, args.port, args.smp, min(cps, 128), args.keyspace, args.value_size)
        else:
            await preload_single(args.host, args.port, min(args.conns, 128), args.keyspace, args.value_size)
        print(f"[preload] done in {time.perf_counter()-t0:.1f}s")

    total_run = args.warmup + args.duration
    mode = "per-shard" if args.per_shard_ports else "single-port"
    print(f"[run:{mode}] conns={args.conns} pipeline={args.pipeline} total={total_run}s "
          f"(warmup {args.warmup}s + measure {args.duration}s) "
          f"keyspace={args.keyspace} get_ratio={args.get_ratio} value={args.value_size}B smp={args.smp}")

    measuring_evt = asyncio.Event()
    async def flip():
        await asyncio.sleep(args.warmup)
        measuring_evt.set()
    asyncio.create_task(flip())

    results = []
    t0 = time.perf_counter()
    if args.per_shard_ports:
        # divide connections across shards; at least 1 per shard
        cps = max(1, args.conns // max(1, args.smp))
        await run_client_sharded(args.host, args.port, args.smp, cps, total_run, args.pipeline,
                                 args.keyspace, args.get_ratio, args.value_size, results, measuring_evt)
    else:
        clients = [run_client_single(args.host, args.port, total_run, args.pipeline,
                                     args.keyspace, args.get_ratio, args.value_size,
                                     results, measuring_evt)
                   for _ in range(args.conns)]
        await asyncio.gather(*clients)
    elapsed_total = time.perf_counter() - t0

    all_lat = [x for lst in results for x in lst]
    total_ops = sum(len(lst) for lst in results)
    measured_secs = args.duration
    tput = (total_ops / measured_secs) if measured_secs > 0 else 0.0

    if total_ops == 0:
        print("no ops recorded (check server, ports, or warmup/duration settings)")
        return

    print(f"\nTotal runtime: {elapsed_total:.2f}s (ignored first {args.warmup}s)")
    print(f"Ops (measured): {total_ops:,}  Time: {measured_secs:.2f}s  Throughput: {tput:,.0f} ops/s")
    print(f"Latency ms: p50={pct(all_lat,50):.2f}  p95={pct(all_lat,95):.2f}  p99={pct(all_lat,99):.2f}  max={max(all_lat):.2f}")

if __name__ == "__main__":
    try:
        import uvloop
        asyncio.set_event_loop_policy(uvloop.EventLoopPolicy())
    except Exception:
        pass
    asyncio.run(main())
