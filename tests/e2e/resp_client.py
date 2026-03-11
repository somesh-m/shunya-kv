import json
import socket
import threading


_NODE_INFO = b"*1\r\n$9\r\nNODE_INFO\r\n"
_NODE_INFO_MAP = b"*2\r\n$9\r\nNODE_INFO\r\n$3\r\nMAP\r\n"
_FNV1A64_OFFSET = 0xCBF29CE484222325
_FNV1A64_PRIME = 0x100000001B3
_POOL_LOCK = threading.Lock()


class _ShardConnection:
    def __init__(self, sock: socket.socket, timeout_s: float):
        self.sock = sock
        self.sock.settimeout(timeout_s)
        self.file = sock.makefile("rb")
        self.lock = threading.Lock()

    def close(self) -> None:
        try:
            self.file.close()
        finally:
            self.sock.close()

    def send(self, payload: bytes) -> bytes:
        with self.lock:
            self.sock.sendall(payload)
            return _read_resp_frame(self.file)


class _ShardConnectionPool:
    def __init__(
        self,
        base_port: int,
        shard_connections: dict[int, _ShardConnection],
        first_shard: int,
    ):
        self.base_port = base_port
        self.shard_connections = shard_connections
        self.first_shard = first_shard
        self.shard_count = len(shard_connections)
        self.default_connection = next(iter(shard_connections.values()))

    def connection_for_payload(self, payload: bytes) -> _ShardConnection:
        key = _extract_key(payload)
        if key is None or self.shard_count == 0:
            return self.default_connection

        shard = self.first_shard + ((_fnv1a64(key) * self.shard_count) >> 64)
        return self.shard_connections.get(shard, self.default_connection)


_POOLS: dict[int, _ShardConnectionPool] = {}


def _readline_exact(stream) -> bytes:
    line = stream.readline()
    if not line:
        raise ConnectionError("socket closed while reading response")
    return line


def _read_resp_frame(stream) -> bytes:
    head = _readline_exact(stream)
    prefix = head[:1]

    if prefix in (b"+", b"-", b":"):
        return head

    if prefix == b"$":
        size = int(head[1:-2])
        if size < 0:
            return head
        payload = stream.read(size + 2)
        if len(payload) != size + 2:
            raise ConnectionError("truncated bulk response")
        return head + payload

    if prefix == b"*":
        count = int(head[1:-2])
        if count < 0:
            return head
        parts = [head]
        for _ in range(count):
            parts.append(_read_resp_frame(stream))
        return b"".join(parts)

    raise ValueError(f"unsupported RESP prefix: {prefix!r}")


def _single_exchange(port: int, payload: bytes, timeout_s: float) -> bytes:
    with socket.create_connection(("127.0.0.1", port), timeout=timeout_s) as sock:
        sock.settimeout(timeout_s)
        file = sock.makefile("rb")
        try:
            sock.sendall(payload)
            return _read_resp_frame(file)
        finally:
            file.close()


def _resp_bulk_to_bytes(resp: bytes) -> bytes:
    if not resp.startswith(b"$"):
        raise ValueError("expected RESP bulk string")

    line_end = resp.find(b"\r\n")
    if line_end == -1:
        raise ValueError("invalid RESP bulk length")

    size = int(resp[1:line_end])
    start = line_end + 2
    end = start + size
    if size < 0 or len(resp) < end + 2:
        raise ValueError("truncated RESP bulk string")
    return resp[start:end]


def _open_connection(base_port: int, timeout_s: float) -> _ShardConnection:
    sock = socket.create_connection(("127.0.0.1", base_port), timeout=timeout_s)
    return _ShardConnection(sock, timeout_s)


def _fetch_pool(base_port: int, timeout_s: float) -> _ShardConnectionPool:
    with _POOL_LOCK:
        pool = _POOLS.get(base_port)
        if pool is not None:
            return pool

    node_info = _single_exchange(base_port, _NODE_INFO_MAP, timeout_s)
    payload = _resp_bulk_to_bytes(node_info)
    data = json.loads(payload)
    ranges = data.get("ranges", [])
    shard_ids = sorted(int(entry[2]) for entry in ranges)
    if not shard_ids:
        raise RuntimeError("NODE_INFO MAP returned no shards")

    shard_connections: dict[int, _ShardConnection] = {}
    attempts_left = max(len(shard_ids) * 8, 16)
    while len(shard_connections) < len(shard_ids) and attempts_left > 0:
        attempts_left -= 1
        conn = _open_connection(base_port, timeout_s)
        try:
            current_info = conn.send(_NODE_INFO)
            current_payload = _resp_bulk_to_bytes(current_info)
            current_data = json.loads(current_payload)
            shard = int(current_data["current_shard"])
        except Exception:
            conn.close()
            raise

        if shard in shard_connections:
            conn.close()
            continue

        shard_connections[shard] = conn

    if len(shard_connections) != len(shard_ids):
        for conn in shard_connections.values():
            conn.close()
        raise RuntimeError("unable to establish one connection per shard")

    pool = _ShardConnectionPool(base_port, shard_connections, shard_ids[0])
    with _POOL_LOCK:
        return _POOLS.setdefault(base_port, pool)


def _fnv1a64(data: bytes) -> int:
    h = _FNV1A64_OFFSET
    for byte in data:
        h ^= byte
        h = (h * _FNV1A64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


def _extract_key(payload: bytes) -> bytes | None:
    if not payload.startswith(b"*"):
        return None

    line_end = payload.find(b"\r\n")
    if line_end == -1:
        return None

    cursor = line_end + 2
    args: list[bytes] = []
    while cursor < len(payload):
        if payload[cursor:cursor + 1] != b"$":
            return None

        line_end = payload.find(b"\r\n", cursor)
        if line_end == -1:
            return None

        size = int(payload[cursor + 1:line_end])
        data_start = line_end + 2
        data_end = data_start + size
        if size < 0 or data_end + 2 > len(payload):
            return None
        if payload[data_end:data_end + 2] != b"\r\n":
            return None

        args.append(payload[data_start:data_end])
        cursor = data_end + 2

    if len(args) < 2:
        return None

    if args[0].upper() == b"NODE_INFO":
        return None
    return args[1]


def send_resp_command(port: int, payload: bytes, timeout_s: float = 2.0) -> bytes:
    pool = _fetch_pool(port, timeout_s)
    return pool.connection_for_payload(payload).send(payload)
