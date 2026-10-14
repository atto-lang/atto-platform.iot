# Streaming decoder reference

`ores_iot_stream_feed` handles a byte stream split across arbitrary transport read boundaries or multiple frames in one chunk. It uses fixed storage (1032 bytes per parser) and calls a callback after validating each complete frame.

- Call `ores_iot_stream_init` before first use.
- Only invoke from one thread; callback must not re-enter the stream parser.
- Callback payload view is borrowed and valid only for that invocation.
- Decoder refuses malformed headers, unsupported kind/version and oversize lengths.
- After an invalid frame, the decoder resets and the caller **must close or quarantine the connection**; do not automatically continue on untrusted bytes.
- This is not a secure transport: TLS/identity/authorization/replay resistance are required externally.

Fuzz harnesses under `fuzz/` can be built manually using LLVM libFuzzer; the bootstrap CI does not claim fuzz coverage.
