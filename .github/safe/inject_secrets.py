#!/usr/bin/env python3
"""Write the build-time secrets into the Safe Telegram sources, in CI only.

    inject_secrets.py <repository root>

Reads two environment variables (GitHub Actions secrets):

  SAFE_TRUST_JSON  the "android" block of the trust list: an object with
                   trusted_dialogs / allowed_dialogs / known_dialogs, each an
                   array of non-zero integer dialog ids, and optionally
                   quiet_media_dialogs (chats whose incoming pictures are not
                   displayed, d27). Nothing else is allowed through: names and
                   picks never leave the Mac.
  SAFE_JUDGE_KEY   the picture check's API key.
  SAFE_CALM_URL    Calm mode's runner (https://calm.zakkbates.com).
  SAFE_CALM_KEY    the runner's key.

Each one is written into its header under Telegram/SourceFiles/safe/. A
missing value is NOT an error: the header stays as it is (empty), the app
fails closed (only Saved Messages and Telegram's service account show) and
the build still produces a usable app. A SAFE_TRUST_JSON that is set but
malformed stops the build (exit 1): someone meant to ship a list, and a
fail-closed app in its place would hide every chat without saying why.
Only counts are printed, never an id, a name or the key.
"""
import json
import os
import re
import sys
from pathlib import Path

KEYS = ("trusted_dialogs", "allowed_dialogs", "known_dialogs")
OPTIONAL_KEYS = ("quiet_media_dialogs",)


def write_header(path, name, value, comment):
    """Replace the single `inline constexpr auto <name> = "...";` line."""
    text = path.read_text()
    pattern = re.compile(
        r'^inline constexpr auto %s = ".*";$' % re.escape(name),
        re.MULTILINE)
    if not pattern.search(text):
        sys.exit("%s: no `%s` line to fill" % (path.name, name))
    # C++ escapes: the trust list is ASCII JSON, the key is ASCII too.
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    line = 'inline constexpr auto %s = "%s";' % (name, escaped)
    path.write_text(pattern.sub(lambda _: line, text, count=1))
    print("==> %s: %s" % (path.name, comment))


def clean_trust(raw):
    """The JSON that may be baked in; exits (status 1) with the reason."""
    try:
        doc = json.loads(raw)
    except ValueError as e:
        sys.exit("SAFE_TRUST_JSON does not parse (line %d, column %d)"
                 % (e.lineno, e.colno))
    block = doc.get("android") if isinstance(doc, dict) else None
    if isinstance(block, dict):
        doc = block  # a whole trust.json was pasted into the secret
    if not isinstance(doc, dict):
        sys.exit("SAFE_TRUST_JSON is not an object")
    out = {}
    for key in KEYS + OPTIONAL_KEYS:
        if key in OPTIONAL_KEYS and key not in doc:
            continue
        ids = doc.get(key)
        if not isinstance(ids, list):
            sys.exit("SAFE_TRUST_JSON: %s is missing or not a list" % key)
        for i in ids:
            if isinstance(i, bool) or not isinstance(i, int) or i == 0:
                sys.exit("SAFE_TRUST_JSON: %s holds a value that is not a "
                         "dialog id" % key)
        out[key] = sorted(set(ids))
    print("==> trust list: %d trusted, %d allowed, %d known, %d quiet"
          % (tuple(len(out[k]) for k in KEYS)
             + (len(out.get("quiet_media_dialogs", [])),)))
    return json.dumps(out, separators=(",", ":"))


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: inject_secrets.py <repository root>")
    safe = Path(sys.argv[1]) / "Telegram" / "SourceFiles" / "safe"

    raw = os.environ.get("SAFE_TRUST_JSON", "").strip()
    if not raw:
        print("==> no SAFE_TRUST_JSON secret: the app will show only Saved "
              "Messages and the service account")
    else:
        clean = clean_trust(raw)
        write_header(
            safe / "safe_trust_data.h",
            "kTrustListJson",
            clean,
            "trust list baked in")

    key = os.environ.get("SAFE_JUDGE_KEY", "").strip()
    if not key:
        print("==> no SAFE_JUDGE_KEY secret: the picture check is off, so "
              "checked pictures stay blurred")
    elif not re.fullmatch(r"[A-Za-z0-9_\-]{16,256}", key):
        print("==> SAFE_JUDGE_KEY does not look like a key, leaving it empty")
    else:
        write_header(
            safe / "safe_judge_data.h",
            "kJudgeKey",
            key,
            # Public repository: not one character of the key in the log.
            "picture-check key baked in (%d characters)" % len(key))
    inject_calm(safe)


def inject_calm(safe):
    url = os.environ.get("SAFE_CALM_URL", "").strip()
    key = os.environ.get("SAFE_CALM_KEY", "").strip()
    if not url or not key:
        print("==> no SAFE_CALM_URL / SAFE_CALM_KEY: Calm mode goes straight "
              "to the API fallback")
        return
    if not re.fullmatch(r"https://[A-Za-z0-9.\-]+(:[0-9]+)?(/[A-Za-z0-9._\-/]*)?", url):
        sys.exit("SAFE_CALM_URL is not a plain https address")
    if not re.fullmatch(r"[A-Za-z0-9_\-]{32,256}", key):
        sys.exit("SAFE_CALM_KEY has the wrong shape")
    header = safe / "safe_calm_data.h"
    if not header.exists():
        print("==> safe_calm_data.h not in this tree: skipped")
        return
    write_header(header, "kCalmUrl", url, "Calm runner address baked in")
    write_header(
        header,
        "kCalmKey",
        key,
        "Calm runner key baked in (%d characters)" % len(key))


if __name__ == "__main__":
    main()
