#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
"""curl, for a check that wants no network (make check-rasignin).

src/net.c runs this in curl's place when it is built with NET_CURL. It
takes what net.c gives curl - `-K` and a config file - reads that file as
curl would, and answers as the server and curl would: the body and the
write-out on stdout (or the body in `output`), nothing of a refused body
under `fail`, and curl's exit code. Which answer is chosen by the request:
the `u` field of a POST, else the URL's last segment.

It also holds net.c to its rules, since it is the one place that sees what
curl sees: argv is `-K <config>` and nothing else, so no token or password is
in it; the config is mode 0600; and stdout, where a token comes back, is a
file that was 0600 before a byte was written to it. A rule broken is written
to the file NET_CHECK_LOG names, which the check reads, and the request
fails as curl does when it cannot start (exit 2).
"""
import os
import re
import stat
import sys

# name -> (status, body, exit code). A status of 0 is no answer at all.
TOKEN_REPLY = b'{"Success":true,"User":"good","Token":"tok-good","Score":0}'
ANSWERS = {
    # RetroAchievements' login2
    "good": (200, TOKEN_REPLY, 0),
    "wrong": (401, b'{"Success":false,"Status":401,"Code":"invalid_credentials",'
                   b'"Error":"Invalid user/password combination. Please try again."}', 0),
    "busy": (503, b"<html><body>Service Unavailable</body></html>", 0),
    "moved": (301, b"<html><body>Moved Permanently</body></html>", 0),
    "said-no": (200, b'{"Success":false,"Error":"Your account is not active."}', 0),
    "gone": (0, b"", 6),        # Could not resolve host
    "slow": (0, b"", 28),       # Operation timed out, before any answer
    "cut": (200, TOKEN_REPLY[:20], 18),   # Partial file: the reply cut short
    # GETs, by the URL's last segment
    "key-good": (200, b'{"success":true,"data":[]}', 0),
    "key-bad": (401, b'{"success":false,"errors":["Invalid API key"]}', 0),
    "key-none": (0, b"", 7),    # Failed to connect
    "big": (200, b"x" * 5000, 0),
    "digits": (200, b"the answer ends in 404", 0),
}
UNLOCK = (200, b'{"Success":true,"AchievementID":123}', 0)

ESC = {"n": "\n", "r": "\r", "t": "\t", "v": "\v"}


def log(what):
    path = os.environ.get("NET_CHECK_LOG")
    if path:
        with open(path, "a") as f:
            f.write(what + "\n")


def broke(what):
    log("curl-standin: " + what)
    sys.stderr.write("curl-standin: %s\n" % what)
    sys.exit(2)


def unquote(v):
    """A value as curl's config has it: double quoted, with backslash escapes."""
    if not v.startswith('"'):
        return v.strip()
    out, i = [], 1
    while i < len(v) and v[i] != '"':
        if v[i] == "\\" and i + 1 < len(v):
            i += 1
            out.append(ESC.get(v[i], v[i]))
        else:
            out.append(v[i])
        i += 1
    return "".join(out)


def read_config(path):
    opts = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.match(r"-{0,2}([A-Za-z0-9-]+)\s*(?:[=:]\s*|\s+)?(.*)$", line)
            if not m:
                broke("a config line curl would not read: %r" % line)
            opts.setdefault(m.group(1), []).append(unquote(m.group(2)) if m.group(2) else "")
    return opts


def mode(st):
    return stat.S_IMODE(st.st_mode)


def main():
    if len(sys.argv) != 3 or sys.argv[1] != "-K":
        broke("argv is %r, not -K and a config" % sys.argv[1:])
    cfg = sys.argv[2]
    if mode(os.stat(cfg)) != 0o600:
        broke("the config is %o, not 600" % mode(os.stat(cfg)))
    out_st = os.fstat(1)
    if not stat.S_ISREG(out_st.st_mode) or mode(out_st) != 0o600:
        broke("stdout is not a 0600 file")
    if out_st.st_size != 0:
        broke("stdout already holds %d bytes" % out_st.st_size)

    opts = read_config(cfg)
    url = (opts.get("url") or [""])[-1]
    fields = {}
    for kv in opts.get("data-urlencode", []):
        k, _, v = kv.partition("=")
        fields[k] = v
    if "get" not in opts and fields.get("r") == "awardachievement" and fields.get("u") != "gone":
        status, body, code = UNLOCK
    else:
        name = fields.get("u") or url.rstrip("/").rsplit("/", 1)[-1]
        status, body, code = ANSWERS.get(name, (404, b"<html>Not Found</html>", 0))

    if status >= 400 and "fail" in opts:
        body, code = b"", 22    # curl -f: no body, "The requested URL returned error"
    dest = (opts.get("output") or [None])[-1]
    if dest:
        with open(dest, "wb") as f:
            f.write(body)
    else:
        sys.stdout.buffer.write(body)
    for w in opts.get("write-out", []):
        sys.stdout.buffer.write(w.replace("%{http_code}", "%03d" % status).encode())
    sys.stdout.buffer.flush()
    sys.exit(code)


main()
