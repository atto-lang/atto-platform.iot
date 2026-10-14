# Reference frame v1

This is an **in-process interoperability experiment**, not Oreslang actor transport, MQTT or an Internet-facing protocol.

## Binary layout

| Bytes | Meaning |
| --- | --- |
| 0..3 | ASCII magic `ORES` |
| 4 | version, exactly `1` |
| 5 | kind: `1` telemetry; `2` command; `3` ack |
| 6..7 | unsigned 16-bit payload length, network byte order |
| 8.. | payload, at most 1024 bytes |

The decoder rejects unknown version/kind, invalid magic, oversized length, truncated frames and **extra bytes**. Decoder output borrows frame memory. Empty payloads are valid.

## Not a security boundary

No MAC, encryption, device identity, timestamp, nonce, sequence number or anti-replay is included. Devices and gateways must authenticate and authorize commands **outside this frame**. Never treat a valid header as proof of origin. Use mutually authenticated TLS/DTLS or an equivalently vetted channel and strict command authorization.
