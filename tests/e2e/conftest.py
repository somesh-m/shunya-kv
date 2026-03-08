from pathlib import Path

import pytest

from .harness import ServerProcess, find_free_port

_E2E_SMP = 4


@pytest.fixture
def server() -> ServerProcess:
    repo_root = Path(__file__).resolve().parents[2]
    binary = repo_root / "build" / "shunya_store"
    if not binary.exists():
        pytest.fail(f"server binary not found: {binary}")

    proc = ServerProcess(
        binary=binary,
        port=find_free_port(),
        smp=_E2E_SMP,
    )
    proc.start()
    try:
        yield proc
    finally:
        proc.stop()
