import os
import signal
import socket
import subprocess
import time
from pathlib import Path


class ServerProcess:
    def __init__(self, binary: Path, port: int, smp: int = 1) -> None:
        self.binary = binary
        self.port = port
        self.smp = smp
        self.process: subprocess.Popen[str] | None = None

    def start(self) -> None:
        env = os.environ.copy()
        self.process = subprocess.Popen(
            [
                str(self.binary),
                "--port",
                str(self.port),
                "--smp",
                str(self.smp),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            cwd=self.binary.parent,
            env=env,
        )
        self._wait_until_ready()

    def stop(self) -> None:
        if self.process is None:
            return
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process = None

    def _wait_until_ready(self, timeout_s: float = 10.0) -> None:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if self.process is None:
                break
            if self.process.poll() is not None:
                output = self.process.stdout.read() if self.process.stdout else ""
                raise RuntimeError(
                    f"server exited before becoming ready:\n{output}"
                )

            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=0.2):
                    return
            except OSError:
                time.sleep(0.05)

        raise TimeoutError(f"server did not start listening on port {self.port}")


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        sock.listen(1)
        return int(sock.getsockname()[1])
