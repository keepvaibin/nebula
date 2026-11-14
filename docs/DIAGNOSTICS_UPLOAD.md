# Diagnostics reports

After a benchmark the player may send its result to Nebula's developer. Nothing is sent without the player's press;
the default answer is No; it is one report, sent once. The report is sealed on the player's device to the developer's
public key, so only the developer's private key, which never leaves his machine, can open it. The server stores
ciphertext and cannot read it. The client holds no secret.

Code: `runtime/src/diag_seal.cpp` (crypto, envelope), `runtime/src/diag_report.cpp` (schema, log stripping, the POST,
the host modes), `runtime/include/galaxy/diag_endpoint.h` (endpoint and public key; empty in the sources).
Server: `tools/telemetry/worker/`. The developer's tool: `tools/telemetry/diag_reports.py`.

## What is sent

One JSON object: `{"report":1,"benchmark":<the benchmark result>,"log":[<lines>]}`. `benchmark` is the file
`--benchmark 1` wrote, refused unless every field is in its schema (benchmark result schema 1) with the expected
type and size: device capabilities, settings, per-stage measurements, the recommendation. `log` is present only when
the player ticks "Include the last game log": the last 200 lines (32 KiB at most) of the last run's log
(`logs/latest.log` beside the settings on Windows, `<data>/logs/game.log` elsewhere), each line stripped:

1. Bytes outside printable ASCII become `?`.
2. The user name, the home folder's name and the computer name (`USERNAME`, `USER`, `LOGNAME`, `COMPUTERNAME`,
   `HOSTNAME`, the last part of `USERPROFILE`/`HOME`, `gethostname`), any case, anywhere: `<user>`.
3. A token (a run between spaces, quotes, brackets, commas, semicolons, `=` and `|`) becomes `<path>` when it holds
   a backslash, or a slash and any letter, or starts with `~` or a drive letter and colon; `<email>` when it holds
   `@` with a dot after it; `<address>` when it is four dot-separated numbers.
4. The line is cut at 200 bytes.

Never sent: saves, Miis, settings files, mods, file paths, user or computer names, serial numbers, the IP address
(the server does not store it), anything when the player does not press Send.

## Envelope, version 1

| offset | bytes | field |
|---|---|---|
| 0 | 4 | magic `NBDR` |
| 4 | 1 | version: 1 |
| 5 | 1 | compression: 1 = zlib stream (RFC 1950) |
| 6 | 2 | reserved: 0 |
| 8 | 8 | key id: the first 8 bytes of SHA-256(public key) |
| 16 | 32 | HPKE `enc`: the report's own ephemeral X25519 public key |
| 48 | n + 16 | ChaCha20-Poly1305 ciphertext and tag of the compressed report |

HPKE (RFC 9180) base mode, suite DHKEM(X25519, HKDF-SHA256) 0x0020, HKDF-SHA256 0x0001, ChaCha20Poly1305 0x0003;
info `Nebula diagnostics report v1`; aad = bytes 0 to 47, so a changed header fails the tag; one message, sequence 0.
An envelope is at most 256 KiB; an opened report at most 1 MiB (larger is refused, never truncated).
Sent as one `POST` with `Content-Type: application/x-nebula-report`, HTTPS only, 10 seconds, no redirect, no retry,
no cookie. Windows: WinHTTP; Android: java.net.HttpURLConnection through JNI; Linux and macOS: the system's libcurl,
loaded when needed (without it the send fails with exit 4 and `--save-sealed` still writes the report).

## Host modes

| command | exit codes |
|---|---|
| `--send-report <benchmark.json> [--with-log 1]` | 0 sent (and `<file>.sent` written beside it); 1 usage; 2 uploads off in this build; 3 the file is refused (unreadable, schema, size); 4 network (no connection, timeout, TLS, no HTTPS client); 5 the server refused (the status is printed); 8 this result was sent before |
| `--send-report <file> [--with-log 1] --preview 1` | 0 and exactly the text that would be sealed on stdout (the apps' "Show the full report"); 3 refused |
| `--send-report <file> --save-sealed <out> [--public-key <64 hex>]` | 0 written; 2 no key; 3 refused; 6 not written |
| `--report-enabled 1` | 0 this build can send; 2 it cannot |
| `--open-report <file> --key <private key file>` | 0 the report on stdout; 3 opened but not in the schema; 7 not a report, another key, or changed |
| `--report-keygen <folder>` | 0 key written (`nebula-diag-private.key`, never replaced), public key printed; 6 not written |

One result line on stdout (`[diag] sent: 9123 bytes`) or stderr (`[diag] <why>`). The modes run before the renderer,
the game module and the pak are touched. A build whose `diag_endpoint.h` is empty (every build but the developer's)
refuses `--send-report` with exit 2, and the apps hide the choice.
