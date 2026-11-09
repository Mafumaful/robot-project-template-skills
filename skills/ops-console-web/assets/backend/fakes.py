"""Test doubles shared by the console tests."""


class Clock:
    def __init__(self, t=1000.0):
        self.t = t

    def __call__(self):
        return self.t

    def advance(self, seconds):
        self.t += seconds


class FakeRunner:
    """Answers run() by the longest matching argv prefix; a response may be a callable(argv)."""
    def __init__(self):
        self.calls = []; self.responses = {}; self.procs = []; self.files = {}

    def on(self, prefix, response=(0, '', '')):
        self.responses[tuple(prefix)] = response

    def run(self, argv, timeout=15):
        self.calls.append(list(argv))
        for n in range(len(argv), 0, -1):
            response = self.responses.get(tuple(argv[:n]))
            if response is not None:
                return response(argv) if callable(response) else response
        return (0, '', '')

    def processes(self):
        return list(self.procs)

    def read(self, path):
        if str(path) not in self.files:
            raise FileNotFoundError(path)
        return self.files[str(path)]

    def commands(self, *prefix):
        return [c for c in self.calls if tuple(c[:len(prefix)]) == prefix]
