"""The single place where the console runs external commands or reads /proc (spec §3)."""
from pathlib import Path
import os
import signal
import subprocess


class Runner:
    def run(self, argv, timeout=15):
        try:
            p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        except FileNotFoundError as exc:
            return 127, '', str(exc)
        except subprocess.TimeoutExpired:
            return 124, '', 'timeout'
        return p.returncode, p.stdout, p.stderr

    def processes(self):
        out = []
        for entry in Path('/proc').iterdir():
            if not entry.name.isdigit():
                continue
            try:
                raw = (entry / 'cmdline').read_bytes()
            except OSError:
                continue
            if raw:
                out.append((int(entry.name), raw.rstrip(b'\0').decode(errors='replace').split('\0')))
        return out

    def read(self, path):
        return Path(path).read_text()

    def spawn(self, argv, log_path):
        """A long-running helper in its own session (process group), output appended to log_path."""
        Path(log_path).parent.mkdir(parents=True, exist_ok=True)
        with open(log_path, 'ab') as log:
            return subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                    start_new_session=True)

    def stop_group(self, proc, grace=5.0):
        """SIGINT, then SIGTERM, to the helper's process group (ros2 launch forwards them to its nodes)."""
        for sig in (signal.SIGINT, signal.SIGTERM):
            try:
                os.killpg(proc.pid, sig)
            except ProcessLookupError:
                return True
            try:
                proc.wait(timeout=grace)
                return True
            except subprocess.TimeoutExpired:
                continue
        return False
