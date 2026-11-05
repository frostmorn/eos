# Hash, Checksum & Base64 Tools

Eight commands for verifying and encoding data: `md5sum`, `sha1sum`, `sha224sum`, `sha256sum`, `sha384sum`, `sha512sum`, `cksum`, and `base64`. Command syntax is in the [binaries catalog](./catalog.md); this page covers design, limits, and testing.

## 1. Layout

| Piece | Location | Depends on |
| :--- | :--- | :--- |
| Digests, CRC-32, POSIX cksum, hex helpers | `main/emisc/eos_hash.{h,c}` | `<stdint.h>`, `<string.h>` only |
| Streaming Base64 encoder/decoder | `main/emisc/eos_base64.{h,c}` | `<stdint.h>` only |
| File streaming, `*sum` option parsing and check mode | `main/emisc/eos_hashsum.{h,c}` | POSIX `open`/`read`/`usleep`, `psram_alloc.h` |
| Command entry points | `main/ebin/{md5sum,sha1sum,sha224sum,sha256sum,sha384sum,sha512sum,cksum,base64}/` | the above |
| Unit tests | `main/ebin/test_hash/` | the above |

Each `*sum` binary is a three-line wrapper around `eos_hashsum_main()` with its algorithm selected, so adding another digest costs one manifest.

## 2. Design decisions

* **No mbedTLS dependency.** The digests are portable C. They behave identically on Xtensa, RISC-V and the host, they do not tie the tools to the mbedTLS API of a particular ESP-IDF release, and they can be tested on a PC. The cost is no hardware SHA acceleration. `eos_hash.h` is small enough to be re-backed by mbedTLS later without touching the tools.
* **Streaming everywhere.** Files and pipes are read in 4 KB chunks; contexts are under 300 bytes. Memory use does not grow with input size.
* **Memory placement.** The read buffer (4 KB) and the check-mode line buffers are allocated with `eos_psram_malloc_prefer()` and freed before the command returns.
* **CPU yield.** After every 64 KB the reader sleeps for 1 ms, so hashing a large file from tmpfs does not starve lower-priority tasks.
* **Diagnostics go to stderr, results to stdout**, so `sha256sum -c list > report.txt` keeps the report clean.
* **Constant-time comparison** (`eos_hash_equal`) is used when verifying digests.

## 3. Behaviour and differences from GNU coreutils

The output of the `*sum` tools, `cksum` and `base64` was compared byte-for-byte with GNU coreutils for 400+ cases (empty, block-boundary, binary, 5 MB, multi-file, missing file, directory, standard input, check mode variants, Base64 wrap sizes and malformed input). Known differences:

| Area | EOS | GNU |
| :--- | :--- | :--- |
| Check list format | `<hex>  <name>` / `<hex> *<name>` only | Also accepts BSD `--tag` lines |
| Unsupported options | `--strict`, `--zero`, `--tag`, `--ignore-missing`, escaped file names | Supported |
| `base64 -d` and CR | CR is skipped like LF | CR is an error |
| Diagnostic wording | Equivalent, not identical | |

Exit status: `0` success; `1` for unreadable files, mismatches, an unusable check list, or invalid options and input.

## 4. Limits and cautions

* `md5sum` and `sha1sum` are for matching published checksums. They are broken against deliberate collisions.
* A checksum list stored next to the file it describes proves nothing about who produced it. Use it to detect corruption, not tampering.
* Throughput is limited by the software implementation. Measure on your board before relying on it for large files.

## 5. Testing

| Layer | How to run | What it covers |
| :--- | :--- | :--- |
| On-device unit tests | `test_hash` (62 checks) | Published vectors, padding lengths, streaming, CRCs, Base64, tool exit codes |
| Shell integration | `python3 tools/hardware_test.py --suite hash` (23 checks) | Output format, pipes, redirection, check mode on tampered and missing files, Base64 round trip |
| Host build and GNU comparison | `python3 tools/host_tests/hash_host_test.py` | Builds the real sources with gcc and sanitizers; runs `test_hash`; compares 400+ cases against GNU coreutils, `hashlib` and `zlib`. Needs no ESP-IDF or board |

Expected values in `test_hash` were generated from Python `hashlib`, `zlib` and GNU `cksum`, not from the code under test.
