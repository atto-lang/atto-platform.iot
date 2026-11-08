# atto-platform.iot

AttoLang platform integration for connected devices, IoT protocols, and edge computing.

## Scope

Device connectivity, edge runtimes, capability-scoped resources, and platform profiles.

## Portability contract

The AttoLang language contract must remain separate from target-specific capabilities. Document supported toolchains, devices, platform APIs, memory limits, runtime constraints, and unsupported features explicitly.

## Verification

Where feasible, use real devices or representative emulators in addition to host-side tests. Record toolchain versions, target profiles, exact commit SHAs, and execution evidence. A successful build alone is not proof of runtime semantics.

## Development status

This is a statement of intended project responsibilities. Consult source files and CI for verified implementation or release status; do not assume all targets are supported.

## Related projects

- [AttoLang](https://github.com/atto-lang)
- [Target profiles](https://github.com/atto-lang/atto-target-profiles)
- [Platform conformance](https://github.com/atto-lang/atto-platform-conformance)

## Repository ownership and contribution routing

**Belongs here:** Edge-device platform adapters, network/device protocols, provisioning and IoT deployment target profiles.

**Belongs elsewhere:** MCU/bare-metal core lives in atto-platform.embedded; product hosting in atto-co; portable networking APIs in library repos.

**Security and portability boundary:** Trust devices and network as untrusted: authenticate provisioning, enforce device capabilities, encrypted transport, quotas and safe update/rollback.

AttoLang is the public name; preserve existing Oreslang/.ores compatibility and use canonical .atto for new portable library source. This README defines scope, not feature readiness.

**Acceptance:** name exact source SHA/tree, runtime/toolchain, positive and negative checks and execution receipt before claiming support. Host-only tests and empty CI steps do not prove guest execution.
