#!/usr/bin/env python3
"""The OAuth 1.0a signing, held to RFC 5849's own worked example.

This suite exists because signing is the one part of this service that is
correct or worthless with nothing in between, and because a fake server that
verifies signatures with code written the same afternoon shares every
assumption the client makes. The RFC's vector does not.

Section 3.4.1.1 publishes a request, its signature base string, and (with the
secrets from section 3.4.1) the resulting signature. Reproducing all three
proves the percent-encoding, the sort order, the parameter merge and the key
construction at once.

Run: .venv/bin/python tests/test_oauth.py
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from bridge import instapaper as ip  # noqa: E402

checks = 0
failures = 0


def ok(condition, what):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print(f"  FAIL: {what}")


# RFC 5849 section 3.4.1.1. The request repeats a3 (once in the query string,
# once in the body), which is why base_string takes pairs.
RFC_PARAMS = [
    ("b5", "=%3D"),
    ("a3", "a"),
    ("c@", ""),
    ("a2", "r b"),
    ("c2", ""),
    ("a3", "2 q"),
    ("oauth_consumer_key", "9djdj82h48djs9d2"),
    ("oauth_token", "kkk9d7dh3k39sjv7"),
    ("oauth_signature_method", "HMAC-SHA1"),
    ("oauth_timestamp", "137131201"),
    ("oauth_nonce", "7d8f3e4a"),
]

RFC_BASE = (
    "POST&http%3A%2F%2Fexample.com%2Frequest&a2%3Dr%2520b%26a3%3D2%2520q"
    "%26a3%3Da%26b5%3D%253D%25253D%26c%2540%3D%26c2%3D%26oauth_consumer_"
    "key%3D9djdj82h48djs9d2%26oauth_nonce%3D7d8f3e4a%26oauth_signature_m"
    "ethod%3DHMAC-SHA1%26oauth_timestamp%3D137131201%26oauth_token%3Dkkk"
    "9d7dh3k39sjv7"
)

# Section 3.1: client shared-secret and token shared-secret.
RFC_CONSUMER_SECRET = "j49sk3j29djd"
RFC_TOKEN_SECRET = "dh893hdasih9"

# The RFC PRINTS bYT5CMsGcbgUdFHObYMEfcx6bsw= for this request and that value
# is wrong -- a long-known erratum. Verified here rather than trusted: pipe
# the RFC's own base string above through an implementation that shares no
# code with this one,
#
#   printf '%s' '<RFC_BASE>' | openssl dgst -sha1 \
#     -hmac 'j49sk3j29djd&dh893hdasih9' -binary | base64
#
# and openssl answers the value below. So the oracle for the base string is
# the document, and the oracle for the HMAC is a second implementation; the
# only thing taken on faith is that two of them cannot be wrong the same way.
RFC_SIGNATURE = "r6/TJjbCOr97/+UU0NsvSne7s5g="


def main():
    built = ip.base_string("POST", "http://example.com/request", RFC_PARAMS)
    ok(built == RFC_BASE, "RFC 5849 3.4.1.1 base string")
    if built != RFC_BASE:
        print(f"    got      {built}")
        print(f"    expected {RFC_BASE}")

    sig = ip.signature(
        "POST", "http://example.com/request", RFC_PARAMS, RFC_CONSUMER_SECRET, RFC_TOKEN_SECRET
    )
    ok(sig == RFC_SIGNATURE, f"RFC 5849 signature (got {sig})")

    # The unreserved set, which is where hand-rolled encoders go wrong. A
    # tilde must survive and a space must not become a plus.
    ok(ip._quote("~-._aZ09") == "~-._aZ09", "unreserved characters pass through")
    ok(ip._quote("a b") == "a%20b", "space encodes as %20, not +")
    ok(ip._quote("/") == "%2F", "slash is encoded (safe= replaces the default)")

    # An empty token secret still contributes its separator, which is the
    # case that matters: xAuth signs the access_token call with no token.
    key_only = ip.signature("POST", "http://example.com/x", {"a": "1"}, "cs", "")
    ok(isinstance(key_only, str) and key_only, "signing works with no token secret")

    # The header carries only oauth_* fields, never the body.
    header = ip.auth_header(
        "POST", "http://example.com/x", {"bookmark_id": "7"}, "ck", "cs", "tok", "ts"
    )
    ok(header.startswith("OAuth "), "header is an OAuth header")
    ok("bookmark_id" not in header, "body parameters stay out of the Authorization header")
    ok("oauth_signature=" in header, "header carries a signature")
    ok('oauth_token="tok"' in header, "header carries the token")

    # compose_have: the shape Instapaper's delta engine reads.
    have = ip.compose_have(
        [
            {"id": 1, "hash": "AAAA", "progress": 0.5, "progressAt": 1288584076},
            {"id": 2, "hash": "BBBB", "progress": 0.0, "progressAt": 0},
            {"id": 3, "hash": "", "progress": 0.9, "progressAt": 5},
            {"id": "nonsense"},
        ]
    )
    ok(have == "1:AAAA:0.500:1288584076,2:BBBB,3", f"have string composed ({have})")

    print(f"{checks} checks, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
