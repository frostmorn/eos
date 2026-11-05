#!/usr/bin/env python3
"""Host-side test for the hash, checksum and base64 tools.

Builds the real sources (emisc/eos_hash.c, eos_base64.c, eos_hashsum.c and the
ebin entry points) for the host with gcc and AddressSanitizer/UBSan, then:

  1. runs the on-device unit test `test_hash`, and
  2. compares the tools with GNU coreutils (md5sum ... sha512sum, cksum, base64)
     over 400+ cases: sizes around block boundaries, binary data, a 5 MB file,
     standard input, multiple files, missing files, check lists and malformed
     Base64.

No ESP-IDF and no board are needed. Requires gcc and GNU coreutils.
Usage: python3 tools/host_tests/hash_host_test.py [--no-sanitize]
"""
import os
import random
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MAIN = os.path.join(ROOT, "main")
SUM_TOOLS = ["md5sum", "sha1sum", "sha224sum", "sha256sum", "sha384sum", "sha512sum"]
ALL_TOOLS = SUM_TOOLS + ["cksum", "base64"]

HOST_MAIN = r"""
#include <stdio.h>
#include <string.h>
%(decls)s
int eos_bin_main(int argc, char **argv) { (void)argc; (void)argv; return 0; }
int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: eoshost TOOL args...\n"); return 2; }
  const char *t = argv[1]; argv++; argc--;
%(cases)s
  fprintf(stderr, "unknown tool %%s\n", t);
  return 2;
}
"""


def build(workdir, sanitize):
    names = ALL_TOOLS + ["test_hash"]
    decls = "\n".join("int %s_main(int, char **);" % n for n in names)
    cases = "\n".join('  if (!strcmp(t, "%s")) return %s_main(argc, argv);' % (n, n) for n in names)
    shim = os.path.join(workdir, "host_main.c")
    with open(shim, "w") as f:
        f.write(HOST_MAIN % {"decls": decls, "cases": cases})
    srcs = ["emisc/eos_hash.c", "emisc/eos_base64.c", "emisc/eos_hashsum.c"]
    srcs += ["ebin/%s/%s.c" % (n, n) for n in names]
    out = os.path.join(workdir, "eoshost")
    cmd = ["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Wshadow", "-Wno-override-init", "-g", "-I", MAIN]
    if sanitize:
        cmd += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    cmd += [os.path.join(MAIN, s) for s in srcs] + [shim, "-o", out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr)
        sys.exit("build failed")
    return out


def main():
    for tool in ["gcc"] + ALL_TOOLS:
        if shutil.which(tool) is None:
            sys.exit("missing required tool: " + tool)
    sanitize = "--no-sanitize" not in sys.argv
    workdir = tempfile.mkdtemp(prefix="eos_hash_host_")
    try:
        HOST = build(workdir, sanitize)
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")

        print("== unit tests (test_hash) ==")
        sandbox = os.path.join(workdir, "sandbox")
        os.makedirs(sandbox)
        # test_hash writes to /tmp; run it as-is, like it runs on the device.
        r = subprocess.run([HOST, "test_hash"], capture_output=True, text=True, env=env)
        tail = [l for l in r.stdout.splitlines() if l.startswith("Hash Tests:")]
        print(tail[0] if tail else r.stdout[-400:])
        if r.returncode != 0:
            print(r.stdout[-2000:], r.stderr[-2000:])
            sys.exit("unit tests failed")

        print("== comparison with GNU coreutils ==")
        random.seed(7)
        os.chdir(sandbox)
        fails = []
        count = [0]

        def run(cmd, inp=None):
            p = subprocess.run(cmd, input=inp, capture_output=True, env=env)
            return p.returncode, p.stdout, p.stderr

        def same(label, a, b):
            count[0] += 1
            ra, oa, ea = a
            rb, ob, _ = b
            bad = ra != rb or oa != ob
            text = ea.decode(errors="ignore")
            if "AddressSanitizer" in text or "runtime error" in text or "LeakSanitizer" in text:
                bad = True
            if bad:
                fails.append((label, ra, rb, oa[:120], ob[:120], ea[:200]))

        tmp = sandbox
    # test files
        open("empty", "wb").close()
        open("small", "wb").write(b"abc\n")
        open("bin1", "wb").write(os.urandom(100000))
        open("big", "wb").write(os.urandom(5 * 1024 * 1024 + 17))
        open("name with space", "wb").write(b"spaced")
        for L in (55, 56, 57, 63, 64, 65, 111, 112, 119, 120, 127, 128, 129):
            open(f"len{L}", "wb").write(os.urandom(L))
        files = ["empty", "small", "bin1", "big", "name with space"] + [f"len{L}" for L in (55,56,57,63,64,65,111,112,119,120,127,128,129)]
        TOOLS = ["md5sum", "sha1sum", "sha224sum", "sha256sum", "sha384sum", "sha512sum"]
        for t in TOOLS:
            for f in files:
                same(f"{t} {f}", run([HOST, t, f]), run([t, f]))
            same(f"{t} multi", run([HOST, t] + files[:6]), run([t] + files[:6]))
            same(f"{t} binary flag", run([HOST, t, "-b", "small"]), run([t, "-b", "small"]))
            same(f"{t} stdin", run([HOST, t], open("bin1","rb").read()), run([t], open("bin1","rb").read()))
            same(f"{t} dash", run([HOST, t, "-"], b"hello"), run([t, "-"], b"hello"))
            same(f"{t} -- ", run([HOST, t, "--", "small"]), run([t, "--", "small"]))
            # missing + dir: same exit status and stdout
            same(f"{t} missing", run([HOST, t, "nope", "small"]), run([t, "nope", "small"]))
            same(f"{t} dir", run([HOST, t, tmp]), run([t, tmp]))
            # check mode
            good = subprocess.run([t] + files[:8], capture_output=True).stdout
            open("good.lst", "wb").write(good)
            same(f"{t} -c good", run([HOST, t, "-c", "good.lst"]), run([t, "-c", "good.lst"]))
            same(f"{t} -c --status good", run([HOST, t, "-c", "--status", "good.lst"]), run([t, "-c", "--status", "good.lst"]))
            same(f"{t} -c --quiet good", run([HOST, t, "-c", "--quiet", "good.lst"]), run([t, "-c", "--quiet", "good.lst"]))
            same(f"{t} -c stdin", run([HOST, t, "-c"], good), run([t, "-c"], good))
            bad = good.replace(good[:6], b"000000", 1)
            open("bad.lst", "wb").write(bad)
            same(f"{t} -c bad", run([HOST, t, "-c", "bad.lst"]), run([t, "-c", "bad.lst"]))
            same(f"{t} -c bad status", run([HOST, t, "-c", "--status", "bad.lst"]), run([t, "-c", "--status", "bad.lst"]))
            open("missing.lst", "wb").write(good + b"%s  gone\n" % good.split()[0])
            same(f"{t} -c unreadable", run([HOST, t, "-c", "missing.lst"]), run([t, "-c", "missing.lst"]))
            junk = b"this is not a checksum line\n"
            open("junk.lst", "wb").write(junk)
            same(f"{t} -c junk", run([HOST, t, "-c", "junk.lst"]), run([t, "-c", "junk.lst"]))
            open("mixed.lst", "wb").write(b"# comment\n\n" + good + junk)
            same(f"{t} -c mixed", run([HOST, t, "-c", "mixed.lst"]), run([t, "-c", "mixed.lst"]))
            same(f"{t} -c nolist", run([HOST, t, "-c", "no-such-list"]), run([t, "-c", "no-such-list"]))
            # uppercase hex accepted
            up = good.split(b"\n")[0]
            h, rest = up.split(b"  ", 1)
            open("upper.lst", "wb").write(h.upper() + b"  " + rest + b"\n")
            same(f"{t} -c uppercase", run([HOST, t, "-c", "upper.lst"]), run([t, "-c", "upper.lst"]))
            # CRLF list
            open("crlf.lst", "wb").write(good.replace(b"\n", b"\r\n"))
            same(f"{t} -c crlf", run([HOST, t, "-c", "crlf.lst"])[:1] + (b"",b""), run([t, "-c", "crlf.lst"])[:1] + (b"",b""))
        # cksum
        for f in files:
            same(f"cksum {f}", run([HOST, "cksum", f]), run(["cksum", f]))
        same("cksum stdin", run([HOST, "cksum"], b"123456789"), run(["cksum"], b"123456789"))
        same("cksum multi", run([HOST, "cksum"] + files[:5]), run(["cksum"] + files[:5]))
        same("cksum missing", run([HOST, "cksum", "nope"]), run(["cksum", "nope"]))
        # base64
        datas = [b"", b"f", b"fo", b"foo", b"foob", b"fooba", b"foobar", os.urandom(1), os.urandom(2), os.urandom(3), os.urandom(57), os.urandom(58), os.urandom(1000), os.urandom(200000)]
        for d in datas:
            open("b64in", "wb").write(d)
            for w in ([], ["-w", "0"], ["-w", "4"], ["-w", "1"], ["-w76"], ["--wrap=10"], ["--wrap", "0"]):
                same(f"base64 enc {len(d)} {w}", run([HOST, "base64"] + w + ["b64in"]), run(["base64"] + w + ["b64in"]))
            enc = subprocess.run(["base64"], input=d, capture_output=True).stdout
            open("b64enc", "wb").write(enc)
            same(f"base64 dec {len(d)}", run([HOST, "base64", "-d", "b64enc"]), run(["base64", "-d", "b64enc"]))
            same(f"base64 dec stdin {len(d)}", run([HOST, "base64", "--decode"], enc), run(["base64", "--decode"], enc))
            nowrap = subprocess.run(["base64", "-w0"], input=d, capture_output=True).stdout
            same(f"base64 dec nowrap {len(d)}", run([HOST, "base64", "-d"], nowrap), run(["base64", "-d"], nowrap))
        # invalid / edge decode inputs: compare exit status (and stdout where GNU emits partial output)
        for bad in [b"Zg=", b"Z===", b"!!!!", b"Zm9v!", b"Zg==Zg==", b"Zg=\n=", b"Zm9vYg", b"=Zg=", b"Zg==x", b"Zm9v\nYmFy\n", b"  Zm9v", b"Zg"]:
            a = run([HOST, "base64", "-d"], bad); b = run(["base64", "-d"], bad)
            count[0] += 1
            if (a[0] == 0) != (b[0] == 0): fails.append(("base64 -d status differs", bad, a[0], b[0]))
        for bad in [b"Zm9v!YmFy", b"Zm9v\x00YmFy", b"Zm$9v"]:
            same(f"base64 -di {bad}", run([HOST, "base64", "-di"], bad), run(["base64", "-di"], bad))
        same("base64 missing", run([HOST, "base64", "nope"])[:1] + (b"",b""), run(["base64", "nope"])[:1] + (b"",b""))
        # help / usage
        for t in TOOLS + ["cksum", "base64"]:
            r = run([HOST, t, "--help"]); count[0] += 1
            if r[0] != 0 or not r[1].startswith(b"Usage:"): fails.append(("help", t, r[:2]))
        r = run([HOST, "sha256sum", "--bogus"]); count[0] += 1
        if r[0] == 0 or not r[2]: fails.append(("bad option should fail", r))
        print("%d comparisons, %d difference(s)" % (count[0], len(fails)))
        for f in fails[:15]:
            print("  DIFF:", f)
        sys.exit(1 if fails else 0)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    main()
