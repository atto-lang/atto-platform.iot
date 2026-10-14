# Streaming decoder reference

`ores_iot_stream_feed` handles a byte stream split across arbitrary transport read boundaries or multiple frames in one chunk. It uses fixed storage (1032 bytes per parser) and calls a callback after validating each complete frame.

- Call `ores_iot_stream_init` before first use.
- Only invoke from one thread; callback must not re-enter the stream parser.
- Callback payload view is borrowed and valid only for that invocation.
- Decoder refuses malformed headers, unsupported kind/version and oversize lengths.
- After an invalid frame, the decoder resets and the caller **must close or quarantine the connection**; do not automatically continue on untrusted bytes.
- This is not a secure transport: TLS/identity/authorization/replay resistance are required externally.

Fuzz harnesses under `fuzz/` can be built manually using LLVM libFuzzer; the bootstrap CI does not claim fuzz coverage.

## Fail-closed hardening

After a malformed frame or callback error, the parser is poisoned (`failed=1`) and returns `ORES_IOT_CLOSED` on subsequent feeds until explicitly reinitialized. In network code, close/quarantine the session rather than reinitialize to keep reading untrusted bytes. Single calls have a 4096-byte processing budget to bound callback work. `init` must not be called during a feed or from inside a callback; the caller owns stream lifetime and serialization.
