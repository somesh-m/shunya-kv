import os
import signal
import socket
import subprocess
import time
from pathlib import Path


class ServerProcess:
    def __init__(
        self,
        binary: Path,
        port: int,
        smp: int = 1,
        memory: str | None = None,
        log_path: Path | None = None,
    ) -> None:
        self.binary = binary
        self.port = port
        self.smp = smp
        self.memory = memory
        self.log_path = log_path or Path(f"/tmp/shunya_store_debug_{port}.log")
        self.process: subprocess.Popen[str] | None = None
        self._log_file = None

    def start(self) -> None:
        env = os.environ.copy()
        cmd = [
            str(self.binary),
            "--port",
            str(self.port),
            "--smp",
            str(self.smp),
        ]
        if self.memory is not None:
            cmd.extend(["--memory", self.memory])

        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self._log_file = self.log_path.open("w", encoding="utf-8")
        self.process = subprocess.Popen(
            cmd,
            stdout=self._log_file,
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
        if self._log_file is not None:
            self._log_file.close()
            self._log_file = None

    def _wait_until_ready(self, timeout_s: float = 10.0) -> None:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if self.process is None:
                break
            if self.process.poll() is not None:
                raise RuntimeError(
                    f"server exited before becoming ready; see log at {self.log_path}"
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
