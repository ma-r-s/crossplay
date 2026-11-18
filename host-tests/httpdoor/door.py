"""Fail any request path in HttpDownloader.cpp that skips the fork's door.

Usage: door.py <path to HttpDownloader.cpp>

A request site is a function body that constructs a client (SecureHttpClient,
HTTPClient, WiFiClient, WiFiClientSecure) or calls fetchResumable(). Each one
must, in this order, refuse through `if (!openRequest(` before it builds
anything, pass its client through applyCommonClientSetup(, and end through
closeRequest(. One that takes a cancelFlag must poll it through AbortPoll.
Comments and string literals are stripped first, so a comment naming a helper
satisfies nothing.
"""

import re
import sys

KNOWN_SITES = {"runGetSecure", "HttpDownloader::fetchUrl", "HttpDownloader::postForm", "HttpDownloader::postToFile"}
CLIENT = re.compile(r"\b(?:freeink::)?(?:SecureHttpClient|HTTPClient|WiFiClientSecure|WiFiClient)\s+\w+\s*[;({]")
RESUMABLE = re.compile(r"\bfetchResumable\s*\(")


def strip(source):
    out = []
    i = 0
    n = len(source)
    while i < n:
        c = source[i]
        if source.startswith("//", i):
            j = source.find("\n", i)
            i = n if j < 0 else j
        elif source.startswith("/*", i):
            j = source.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and source[j] != c:
                j += 2 if source[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def functions(code):
    """(name, signature, body) for every function defined at column 0."""
    header = re.compile(r"^[A-Za-z_][^\n;{}()]*?\b([A-Za-z_][\w:]*)\s*\(", re.M)
    for m in header.finditer(code):
        if m.group(1) in ("namespace", "struct", "class", "enum", "union"):
            continue
        # Find the parameter list's close, then require a '{' (not ';').
        depth = 0
        j = m.end() - 1
        while j < len(code):
            if code[j] == "(":
                depth += 1
            elif code[j] == ")":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        rest = code[j + 1:]
        tail = re.match(r"\s*(?:const\s*)?(?:override\s*)?\{", rest)
        if not tail:
            continue
        start = j + 1 + tail.end() - 1
        depth = 0
        k = start
        while k < len(code):
            if code[k] == "{":
                depth += 1
            elif code[k] == "}":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        yield m.group(1), code[m.start():start], code[start:k + 1]


def main():
    code = strip(open(sys.argv[1]).read())
    failures = []
    sites = set()
    checked = 0
    for name, signature, body in functions(code):
        first = [m.start() for m in (CLIENT.search(body), RESUMABLE.search(body)) if m]
        if not first:
            continue
        sites.add(name)
        checked += 1
        build = min(first)
        door = re.search(r"if\s*\(\s*!\s*openRequest\s*\(", body)
        if not door:
            failures.append(f"{name}: builds a request without `if (!openRequest(...))`; with the radio down it panics")
        elif door.start() > build:
            failures.append(f"{name}: openRequest() runs AFTER the client is built; the refusal has to come first")
        else:
            # Nothing with a side effect before the door: no file opened, no radio touched.
            before = body[:door.start()]
            for effect in ("Storage.", "WifiPowerSaveGuard", ".begin("):
                if effect in before:
                    failures.append(f"{name}: `{effect}` runs before openRequest(); a refused request must leave nothing")
        if not re.search(r"\bapplyCommonClientSetup\s*\(\s*\w+\s*,", body):
            failures.append(f"{name}: its client skips applyCommonClientSetup(http, url, sameOrigin); no CrossPlay UA, no device report")
        if not re.search(r"\bcloseRequest\s*\(", body):
            failures.append(f"{name}: never calls closeRequest(); lastStatus() reads the last request's status, and the report is never delivered")
        if "cancelFlag" in signature and not re.search(r"\bAbortPoll\s+\w+\s*\(", body):
            failures.append(f"{name}: takes a cancelFlag but polls it without AbortPoll; Back is dead until the first body byte")

    # The probe must be able to fail: if the parser stopped finding the four
    # paths this file has today, it would pass by checking nothing.
    missing = KNOWN_SITES - sites
    if missing:
        failures.append("found no request in " + ", ".join(sorted(missing)) + "; the scan is broken or a path moved")
    for f in failures:
        print("  FAIL " + f)
    print(f"httpdoor: {checked} request sites ({', '.join(sorted(sites))}), {len(failures)} failed")
    sys.exit(1 if failures else 0)


main()
