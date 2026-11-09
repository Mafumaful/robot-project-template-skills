"""Password login for control requests (spec §6.1). Read-only pages stay open."""
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import threading
import time

DEFAULT_PASSWORD = 'change-me'  # template fallback only; set CONSOLE_DEFAULT_PASSWORD or pass default_password
ITERATIONS = 200_000
IDLE_S = 365 * 24 * 3600  # log in once per browser; sessions survive console restarts and reboots
TOUCH_SAVE_S = 3600  # check() refreshes last-seen in memory, the file only when it moved this much
OPEN_POSTS = frozenset({'/api/estop', '/api/stop', '/api/heartbeat', '/api/login', '/api/logout', '/api/teleop/release',
                        '/api/pause', '/api/params/preview'})  # preview only computes a diff, it changes nothing


def hash_password(password, salt=None, iterations=ITERATIONS):
    salt = salt or secrets.token_hex(16)
    digest = hashlib.pbkdf2_hmac('sha256', password.encode(), bytes.fromhex(salt), iterations).hex()
    return f'pbkdf2_sha256${iterations}${salt}${digest}'


def verify_password(password, stored):
    try:
        scheme, iterations, salt, digest = stored.strip().split('$')
    except ValueError:
        return False
    if scheme != 'pbkdf2_sha256':
        return False
    try:
        iters = int(iterations)
        bytes.fromhex(salt)  # validate salt is proper hex
    except (ValueError, TypeError):
        return False
    return hmac.compare_digest(hash_password(password, salt, iters).split('$')[3], digest)


def session_from_cookie(header):
    for part in (header or '').split(';'):
        name, _, value = part.strip().partition('=')
        if name == 'robot_session':
            return value
    return ''


def _write_private(file, text):
    tmp = file.with_name(file.name + '.tmp')
    tmp.write_text(text); os.chmod(tmp, 0o600); tmp.replace(file)


def cookie_header(sid, clear=False):
    if clear:
        return 'robot_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0'
    return f'robot_session={sid}; Path=/; HttpOnly; SameSite=Strict; Max-Age={IDLE_S}'


class Auth:
    def __init__(self, directory, clock=time.time, default_password=None):  # wall clock: last-seen times are stored across reboots
        # Read the first-run password when the object is built, not at import time: tests and launchers set the env var
        # after other modules may already have imported this one.
        default_password = default_password or os.environ.get('CONSOLE_DEFAULT_PASSWORD') or DEFAULT_PASSWORD
        self.file = Path(directory) / 'password'; self.clock = clock
        self.sessions_file = Path(directory) / 'sessions.json'
        self.attempts = {}; self.lock = threading.Lock()
        self.file.parent.mkdir(parents=True, exist_ok=True)
        if not self.file.exists():
            _write_private(self.file, hash_password(default_password) + '\n')
        token_file = Path(directory) / 'page_token'  # the page's X-Nav-Test-Token; stable so open tabs survive restarts
        if not token_file.exists():
            _write_private(token_file, secrets.token_urlsafe(32) + '\n')
        self.page_token = token_file.read_text().strip()
        try:
            stored = json.loads(self.sessions_file.read_text())
            self.sessions = {str(k): float(v) for k, v in stored.items()}
        except (OSError, ValueError, TypeError, AttributeError):
            self.sessions = {}
        self._saved = dict(self.sessions)

    def _save(self):  # caller holds self.lock
        _write_private(self.sessions_file, json.dumps(self.sessions))
        self._saved = dict(self.sessions)

    def login(self, password, ip):
        now = self.clock()
        with self.lock:
            recent = [t for t in self.attempts.get(ip, []) if now - t < 60]
            if len(recent) >= 10:
                self.attempts[ip] = recent
                raise PermissionError('尝试过于频繁，请一分钟后再试')
            recent.append(now); self.attempts[ip] = recent
            # Opportunistically prune every IP's list: re-filter by the 60 s window,
            # then drop IPs whose filtered list is empty so the dict doesn't grow forever.
            self.attempts = {k: [t for t in v if now - t < 60]
                             for k, v in self.attempts.items()}
            self.attempts = {k: v for k, v in self.attempts.items() if v}
        if not verify_password(str(password), self.file.read_text()):
            raise PermissionError('口令错误')
        sid = secrets.token_urlsafe(32)
        with self.lock:
            self.sessions[sid] = now; self._save()
        return sid

    def check(self, sid):
        now = self.clock()
        with self.lock:
            seen = self.sessions.get(sid or '')
            if seen is None or now - seen > IDLE_S:
                self.sessions.pop(sid or '', None)
                # Opportunistically prune all other expired sessions
                self.sessions = {k: v for k, v in self.sessions.items() if now - v <= IDLE_S}
                if self.sessions.keys() != self._saved.keys():
                    self._save()
                return False
            self.sessions[sid] = now
            if now - self._saved.get(sid, 0) >= TOUCH_SAVE_S:
                self._save()
            return True

    def logout(self, sid):
        with self.lock:
            if self.sessions.pop(sid or '', None) is not None:
                self._save()

    def require(self, path, cookie_header_value):
        if path in OPEN_POSTS:
            return
        if not self.check(session_from_cookie(cookie_header_value)):
            raise PermissionError('需要登录：在 8090 主页右上角登录')
