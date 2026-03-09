import re
from pathlib import Path

import pytest
import math

from .harness import ServerProcess, find_free_port
from .resp_client import send_resp_command


def _resp_command(*parts: bytes) -> bytes:
    payload = f"*{len(parts)}\r\n".encode()
    for part in parts:
        payload += f"${len(part)}\r\n".encode() + part + b"\r\n"
    return payload


def _bulk_payload(resp: bytes) -> bytes:
    assert resp.startswith(b"$")
    line_end = resp.find(b"\r\n")
    assert line_end != -1
    size = int(resp[1:line_end])
    start = line_end + 2
    end = start + size
    return resp[start:end]


def _bulk_response(payload: bytes) -> bytes:
    return f"${len(payload)}\r\n".encode() + payload + b"\r\n"


def _info_memory_bytes(info_resp: bytes) -> tuple[int, int]:
    payload = _bulk_payload(info_resp).decode()

    allocated_match = re.search(r"allocated_memory: .*?\((\d+) bytes\)", payload)
    total_match = re.search(r"total_memory: .*?\((\d+) bytes\)", payload)

    assert allocated_match is not None
    assert total_match is not None

    return int(allocated_match.group(1)), int(total_match.group(1))


@pytest.fixture
def eviction_server() -> ServerProcess:
    repo_root = Path(__file__).resolve().parents[2]
    binary = repo_root / "build" / "shunya_store"
    if not binary.exists():
        pytest.fail(f"server binary not found: {binary}")

    proc = ServerProcess(binary=binary, port=find_free_port(), smp=1, memory="1G")
    proc.start()
    try:
        yield proc
    finally:
        proc.stop()


def test_eviction_triggers_after_memory_limit_crossed(eviction_server) -> None:
    info = send_resp_command(eviction_server.port, _resp_command(b"INFO"))
    allocated, total = _info_memory_bytes(info)

    threshold_bytes = int(total * 0.7)
    value_size = (64 * 1024) + 11
    steady_state_keys = math.ceil(threshold_bytes / value_size)
    print(steady_state_keys)
    pressure_keys = 20
    eviction_keys = 20
    headroom_to_threshold = max(1, threshold_bytes - allocated)
    # value_size = max(
    #     64 * 1024,
    #     headroom_to_threshold // (steady_state_keys + pressure_keys // 2),
    # )
    value = b"x" * value_size

    total_keys = steady_state_keys + pressure_keys + eviction_keys
    for i in range(total_keys):
        key = f"evict-key-{i}".encode()
        response = send_resp_command(
            eviction_server.port, _resp_command(b"SET", key, value)
        )
        assert response == b"+OK\r\n", (
            f"SET failed at key {i} with value_size={value_size}: {response!r}"
        )

    for i in range(eviction_keys):
        response = send_resp_command(
            eviction_server.port, _resp_command(b"GET", f"evict-key-{i}".encode())
        )
        assert response == b"$-1\r\n"

    for i in range(eviction_keys, eviction_keys * 2):
        response = send_resp_command(
            eviction_server.port, _resp_command(b"GET", f"evict-key-{i}".encode())
        )
        assert response == _bulk_response(value)

    latest_value = send_resp_command(
        eviction_server.port,
        _resp_command(b"GET", f"evict-key-{total_keys - 1}".encode()),
    )
    assert latest_value == _bulk_response(value)
