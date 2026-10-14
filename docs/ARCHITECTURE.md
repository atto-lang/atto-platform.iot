# OresIoT architecture

This repository owns IoT-oriented capabilities, messaging gateways, provisioning policy and release workflows. It should reuse Oreslang native-AOT and actor semantics rather than fork the compiler.

## Layers

1. On-device platform: [oreslang-platform.embedded](https://github.com/ores-truffle-oreslang/oreslang-platform.embedded).
2. Edge gateway: Linux ARM64 native-AOT from [oreslang-source.llvm](https://github.com/ores-truffle-oreslang/oreslang-source.llvm) when runtime parity is available.
3. Connectivity: future MQTT 5/CoAP capabilities, offline queue, backpressure, cancellation and reconnect policy.
4. Fleet: authenticated registration, per-device scopes, signed OTA, release coordination and rollback.
5. Observability: metrics, trace context, bounded local logging, privacy limits.

## What actually runs now

The portable C frame codec with exact-length validation, its tests, and a host demo. No sockets, TLS, MQTT, CoAP, actor routing, fleet registration or firmware update functionality exists in this bootstrap.

## Interoperability

The v1 frame is deliberately narrow to exercise native ABI and parser hardening. It is not a substitute for standard MQTT/CoAP wire formats. Any future actor messages need type IDs, versioning, message ownership and replay-safe authentication contracts.
