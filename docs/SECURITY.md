# IoT threat model and security acceptance criteria

## Threats

Untrusted network peers, spoofed devices, replayed commands, stale/rolled-back firmware, oversized frames, resource exhaustion, device compromise and leaked credentials.

## Mandatory gates before production use

- Authenticated encrypted transport (mTLS/DTLS or vetted equivalent); hostname/device identity validation.
- Per-command authorization scoped to identity; principle of least privilege; no ambient OS capabilities.
- Replay resistance: monotonic sequence or authenticated nonce with persisted replay state.
- Bounded memory, payload, queues, rate and retry budgets across all layers.
- Secure key storage/rotation; no hard-coded secrets in firmware or source control.
- Signed OTA artifacts; rollback protection, fail-safe recovery and staged rollout.
- Fuzzing of protocol parsers and end-to-end integration tests under fault injection.

The v1 frame codec does **none** of the crypto/authenticity/authorization tasks. It must not be exposed as a privileged Internet protocol without external protections. Keep transport implementation and key management in dedicated, reviewed modules.

## Failure containment

The stream parser now rejects calls exceeding a 4096-byte feed budget, fails closed after malformed input and callback errors, and requires explicit reinitialization. These are input-processing constraints, **not** transport authentication or replay protection.

## Replay window integration requirements

The experimental `ores_iot_replay` window tracks a 64-message receive
window, not authenticity. An attacker may advance the window arbitrarily
unless sequence, identity, key epoch and command bytes are covered by an
independently verified MAC or AEAD tag **before** `admit` is invoked.
The caller must enforce a monotonic durable checkpoint in a trusted
transaction **before** a command has external effects. If checkpoint
persistence fails, do not execute the command; if identity rotates,
provision an authenticated new key epoch and replay checkpoint. Never
reuse a persisted checkpoint with a different peer/key; never blindly
reinitialize at reboot. Calls are single-threaded/non-reentrant; use
an externally serialized actor/state-loan if there are concurrent callers.
A bare frame-codec or replay-window acceptance result is NOT authorization.
