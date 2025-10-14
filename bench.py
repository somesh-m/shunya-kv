#!/usr/bin/env python3
import argparse, asyncio, json, random, time
from collections import deque
from bisect import bisect_right

CRLF = b"\r\n"

# ---------- hash ----------
def fnv1a64(b: bytes) -> int:
    h = 0xcbf29ce484222325
    for c in b:
        h ^= c
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

def hex64(h: int) -> str:
    return f"{h:016x}"   # 16-char, zero-padded, lowercase, no 0x

# ---------- discovery (MAP with ranges -> ports) ----------
async def discover_map(host: str, probe_port: int, timeout: float = 2.0):
    r, w = await asyncio.wait_for(asyncio.open_connection(host, probe_port), timeout=timeout)
    # Your server uses NODE_INFO
    w.write(b"NODE_INFO\r\n"); await w.drain()
    line = await asyncio.wait_for(r.readline(), timeout=timeout)
    w.close()
    try: await w.wait_closed()
    except Exception: pass

    text = line.decode().strip()
    try:
        info = json.loads(text)
    except Exception:
        import ast
        info = ast.literal_eval(text)

    if "ranges" not in info or not isinstance(info["ranges"], list):
        raise RuntimeError(f"MAP missing ranges: {info}")

    # ranges: [[lo, hi, shard, port], ...]
    ranges = [(int(lo), int(hi), int(sh), int(pt)) for (lo, hi, sh, pt) in info["ranges"]]
    ranges.sort(key=lambda x: x[0])  # by lo

    bounds = [r[1] for r in ranges]      # hi's
    ports  = sorted({r[3] for r in ranges})
    meta = {
        "epoch": info.get("epoch"),
        "hash": info.get("hash"),
        "base_port": info.get("base_port"),
        "port_offset": info.get("port_offset"),
    }
    return ranges, bounds, ports, meta

def key_to_port_shard(key_bytes: bytes, ranges, bounds):
    """Return (port, shard, hash64) using MAP ranges."""
    h = fnv1a64(key_bytes)
    i = bisect_right(bounds, h)
    if i >= len(ranges): i = len(ranges) - 1
    _, _, shard, port = ranges[i]
    return port, shard, h

# ---------- preload ----------
async def preload_single(host, port, conns, keyspace, value_size, trace=False, trace_max=1000):
    async def worker(keys):
        nonlocal trace_max
        reader, writer = await asyncio.open_connection(host, port)
        for k in keys:
            key = f"k{k}".encode()
            if trace and trace_max > 0:
                print(f"[trace:preload] SET key=k{k} port={port}")
                trace_max -= 1
            writer.write(b"SET " + key + b" " + (b"x" * value_size) + CRLF)
        await writer.drain()
        for _ in keys:
            _ = await reader.readline()
        writer.close(); await writer.wait_closed()

    keys = list(range(keyspace))
    chunk = (keyspace + conns - 1) // conns
    tasks = [worker(keys[i:i+chunk]) for i in range(0, keyspace, chunk)]
    await asyncio.gather(*tasks)

async def preload_by_ports(host, ranges, bounds, conns_per_port, keyspace, value_size,
                           trace=False, trace_max=1000):
    # bucket keys by target port (using MAP)
    buckets = {}
    for k in range(keyspace):
        key = f"k{k}".encode()
        p, sh, h = key_to_port_shard(key, ranges, bounds)
        buckets.setdefault(p, []).append((k, sh, h))

    async def worker(port, items):
        nonlocal trace_max
        reader, writer = await asyncio.open_connection(host, port)
        for k, sh, h in items:
            key = f"k{k}".encode()
            if trace and trace_max > 0:
                print(f"[trace:preload] SET key=k{k} hash={hex64(h)} shard={sh} port={port}")
                trace_max -= 1
            writer.write(b"SET " + key + b" " + (b"x" * value_size) + CRLF)
        await writer.drain()
        for _ in items:
            _ = await reader.readline()
        writer.close(); await writer.wait_closed()

    tasks = []
    for port, items in buckets.items():
        chunk = max(1, (len(items) + conns_per_port - 1) // conns_per_port)
        for i in range(0, len(items), chunk):
            tasks.append(asyncio.create_task(worker(port, items[i:i+chunk])))
    if tasks:
        await asyncio.gather(*tasks)

# ---------- single-port client ----------
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
            return b"GET k" + str(k).encode() + CRLF
        else:
            k = random.randrange(keyspace)
            return b"SET k" + str(k).encode() + b" " + (b"x" * value_size) + CRLF

    for _ in range(pipeline):
        writer.write(make_cmd()); send_times.append(time.perf_counter())
    await writer.drain()

    while time.perf_counter() < stop:
        while len(send_times) < pipeline and time.perf_counter() < stop:
            writer.write(make_cmd()); send_times.append(time.perf_counter())
        await writer.drain()
        await asyncio.sleep(0)

    await asyncio.sleep(0.2)
    writer.close(); await writer.wait_closed()
    recv_task.cancel()
    try: await recv_task
    except: pass
    results.append(latencies_ms)

# ---------- per-port (from MAP) client ----------
class PortConn:
    def __init__(self, port, reader, writer):
        self.port = port
        self.reader = reader
        self.writer = writer
        self.send_times = deque()
        self.latencies_ms = []
        self.recv_task = None
    async def recv_loop(self, measuring_evt: asyncio.Event):
        while True:
            line = await self.reader.readline()
            if not line: break
            if self.send_times:
                t0 = self.send_times.popleft()
                if measuring_evt.is_set():
                    self.latencies_ms.append((time.perf_counter() - t0) * 1000.0)

async def run_client_by_ports(host, ranges, bounds, port_list, conns_per_port,
                              total_duration_s, pipeline, keyspace, get_ratio, value_size,
                              results, measuring_evt: asyncio.Event,
                              trace=False, trace_max=1000):
    # Build pools keyed by port
    pools = { p: [] for p in port_list }
    for p in port_list:
        for _ in range(conns_per_port):
            r, w = await asyncio.open_connection(host, p)
            pc = PortConn(p, r, w)
            pc.recv_task = asyncio.create_task(pc.recv_loop(measuring_evt))
            pools[p].append(pc)

    def pick_conn(p: int) -> PortConn:
        return min(pools[p], key=lambda c: len(c.send_times))

    def make_op():
        k = random.randrange(keyspace)
        key = f"k{k}".encode()
        p, sh, h = key_to_port_shard(key, ranges, bounds)
        if random.random() < get_ratio:
            cmd = b"GET " + key + CRLF
            op = "GET"
        else:
            cmd = b"SET " + key + b" " + (b"x" * value_size) + CRLF
            op = "SET"
        return p, sh, h, op, key

    # seed one op per port (GET)
    for p in port_list:
        c = pick_conn(p)
        seed = f"seed{p}".encode()
        c.writer.write(b"GET " + seed + CRLF)
        c.send_times.append(time.perf_counter())
    for p in port_list:
        for c in pools[p]:
            await c.writer.drain()

    stop = time.perf_counter() + total_duration_s
    while time.perf_counter() < stop:
        for p in port_list:
            for c in pools[p]:
                while len(c.send_times) < pipeline:
                    t_p, shard, h, op, key = make_op()
                    c2 = pick_conn(t_p)
                    if trace and trace_max > 0:
                        kstr = key.decode()
                        print(f"[trace:req] {op} key={kstr} hash={hex64(h)} shard={shard} port={t_p}")
                        trace_max -= 1
                    c2.writer.write((op.encode() + b" " + key + (b"" if op=="GET" else (b" " + (b"x"*value_size))) + CRLF))
                    c2.send_times.append(time.perf_counter())
        for p in port_list:
            for c in pools[p]:
                await c.writer.drain()
        await asyncio.sleep(0)

    await asyncio.sleep(0.2)
    for p in port_list:
        for c in pools[p]:
            c.writer.close()
            try: await c.writer.wait_closed()
            except: pass
            c.recv_task.cancel()

    lat = []
    for p in port_list:
        for c in pools[p]:
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
    ap.add_argument("--port", type=int, default=60111,
                    help="single port OR ANY data-shard port for discovery (MAP)")
    ap.add_argument("--conns", type=int, default=64)
    ap.add_argument("-- ", type=int, default=1)
    ap.add_argument("--duration", type=int, default=120, help="MEASUREMENT window (sec)")
    ap.add_argument("--warmup", type=int, default=20, help="Warm-up to ignore (sec)")
    ap.add_argument("--keyspace", type=int, default=100000)
    ap.add_argument("--get-ratio", type=float, default=0.9, help="0..1")
    ap.add_argument("--value-size", type=int, default=16)
    ap.add_argument("--preload", action="store_true")
    ap.add_argument("--discover", action="store_true", help="Fetch MAP (NODE_INFO) & route by its ports")
    ap.add_argument("--trace", action="store_true", help="print per-key routing (op, key, hash, shard, port)")
    ap.add_argument("--trace-max", type=int, default=1000, help="max trace lines")
    args = ap.parse_args()

    ranges = bounds = port_list = None
    if args.discover:
        ranges, bounds, port_list, meta = await discover_map(args.host, args.port)
        print(f"[discover] ports={port_list} epoch={meta.get('epoch')} hash={meta.get('hash')}")

    # -------- preload --------
    if args.preload:
        print(f"[preload] {args.keyspace} keys, value={args.value_size}B")
        t0 = time.perf_counter()
        if ranges is not None:
            conns_per_port = max(1, min(args.conns // max(1, len(port_list)), 128))
            await preload_by_ports(args.host, ranges, bounds, conns_per_port,
                                   args.keyspace, args.value_size,
                                   trace=args.trace, trace_max=args.trace_max)
        else:
            # single-port preload (no MAP available)
            await preload_single(args.host, args.port, min(args.conns, 128),
                                 args.keyspace, args.value_size,
                                 trace=args.trace, trace_max=args.trace_max)
        print(f"[preload] done in {time.perf_counter()-t0:.1f}s")

    total_run = args.warmup + args.duration
    mode = "MAP-ports" if ranges is not None else "single-port"
    print(f"[run:{mode}] conns={args.conns} pipeline={args.pipeline} total={total_run}s "
          f"(warmup {args.warmup}s + measure {args.duration}s) "
          f"keyspace={args.keyspace} get_ratio={args.get_ratio} value={args.value_size}B")

    measuring_evt = asyncio.Event()
    async def flip():
        await asyncio.sleep(args.warmup)
        measuring_evt.set()
    asyncio.create_task(flip())

    results = []
    t0 = time.perf_counter()
    if ranges is not None:
        conns_per_port = max(1, args.conns // max(1, len(port_list)))
        await run_client_by_ports(args.host, ranges, bounds, port_list, conns_per_port,
                                  total_run, args.pipeline, args.keyspace,
                                  args.get_ratio, args.value_size,
                                  results, measuring_evt,
                                  trace=args.trace, trace_max=args.trace_max)
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
