# OresIoT

Connected-device protocols and gateway services for **Oreslang**.

> **Status: bootstrap.** This repository ships a small, independently testable binary framing reference with strict input validation. It does **not** yet implement MQTT, CoAP, provisioning, OTA, network actor transport, authentication, or an Oreslang production runtime.

## Initial deliverable

- A versioned, length-bounded frame codec suitable for portable native interoperability tests.
- Exact-length parsing that rejects malformed versions, kinds, sizes and trailing bytes.
- A runnable example and CI checks.
- Security and architecture contracts for eventual MQTT/CoAP, TLS, identity, and signed OTA integration.

The frame codec is **not** a secure transport protocol. Security requires an authenticated channel, device identity, authorization, replay mitigation and rate limiting.

## Quick start

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

See `docs/ARCHITECTURE.md` and `docs/SECURITY.md`.

Call `ores_iot_stream_finish` at end of input to reject a truncated header or payload. It returns `ORES_IOT_OK` only at a frame boundary and closes the stream until reinitialized. This is framing validation; it does not authenticate a peer.
