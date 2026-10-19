# Changelog

Each release lists what changed, what updating means for existing code, and every security fix.

## Unreleased

## 2.1.0 - 2026-10-19

New feature; no change needed in existing code.

- `VPAD_DS4`: a DualShock 4 on uinput, laid out like the kernel's hid-playstation driver shows
  a real one, so games and SDL that expect a PlayStation pad pick the right prompts and
  mapping. Other backends return `VPAD_ERR_UNSUPPORTED` for it until ViGEm's DS4 target is
  tested on Windows

## 2.0.0 - 2026-10-11

Incompatible change: `vpad_create` takes a configuration.

- `vpad_create(const vpad_config_t *config, vpad_t **out_pad)` replaces the eight-argument
  form. The struct starts with `size`, so later options can be added at the end without
  another major version; `vpad_config_init` fills it
- `VPAD_NO_FF`: a pad without force feedback. Before, every pad offered it, and on Linux a
  game's rumble upload blocked until the program called `vpad_next_event`, which a program
  that only sends input never does
- soname `libvpad.so.2`

### Upgrading

Replace

```c
vpad_create(kind, name, vendor, product, alloc_fn, free_fn, alloc_ctx, &pad);
```

with

```c
vpad_config_t config;

vpad_config_init(&config, kind);
config.name = name;
config.vendor = vendor;
config.product = product;
config.alloc_fn = alloc_fn;
config.free_fn = free_fn;
config.alloc_ctx = alloc_ctx;
vpad_create(&config, &pad);
```

leaving out the fields that were `NULL` or `0`. Rebuild; programs linked to `libvpad.so.1`
keep using the old library.

## 1.0.3 - 2026-09-20

Bug fix; no change needed in existing code.

- Windows: when ViGEmBus refused the rumble notification request at once (instead of failing
  it later), the request was never made again: rumble stopped for good, `vpad_next_event`
  kept returning `VPAD_ERR_AGAIN` and a wait on `vpad_handle` or `vpad_wait` never woke. Now
  the request is retried on each `vpad_next_event`, a failure is reported as `VPAD_ERR_IO`,
  and the handle is signaled so a waiting caller sees it

## 1.0.0 - 2026-09-20

The interface is stable from here on: incompatible changes only in a new major version, with
the soname bumped. The ABI baseline is recorded in `abi/vpad.abi`.

- `vpad_effect_t` starts with `uint16_t size`, always `sizeof(vpad_effect_t)` (24). padff
  1.0.0 added the same field to `pff_effect_t`, and the documented `memcpy` from one to the
  other only works while the two layouts match; with `size`, padff can also accept effects from
  newer versions that add fields at the end
- soname `libvpad.so.1`

### Upgrading

- with padff 1.0.0 the `memcpy` into `pff_effect_t` keeps working unchanged; padff 0.x and
  vpad 1.x no longer share a layout, so update both together
- code that builds a `vpad_effect_t` itself (in tests, for instance) sets `size` to
  `sizeof(vpad_effect_t)`
- rebuild; programs linked to `libvpad.so.0` keep using the old library

## 0.3.0 - 2026-09-03

New features; no change needed in existing code.

- `vpad_wait`: wait for events with a timeout, without the per-system code around
  `vpad_handle`
- Windows: `VPAD_EV_PLAYER` reports the player slot the host assigned to the pad (the ring
  light of a real Xbox 360 pad), once at the start and on each change

## 0.1.1 - 2026-08-31

Bug fixes; no change needed in existing code.

- Linux: envelope attack and fade levels are scaled by 65535/32767 instead of doubled, so the
  full Linux level 32767 arrives as 65535 instead of 65534
- Windows: a failed rumble notification no longer stops all later rumble events; the request
  is issued again and `vpad_next_event` reports `VPAD_ERR_IO` once

## 0.1.0 - 2026-08-05

First release.

- virtual gamepad over uinput (Linux, FreeBSD), IOHIDUserDevice (macOS 12+) and ViGEmBus
  (Windows), with `VPAD_ERR_UNSUPPORTED` elsewhere and clear errors when the service is
  missing or not allowed
- Xbox 360, generic or custom identity
- 15 buttons, two sticks, two triggers and a d-pad hat in one canonical layout
- each `vpad_send` writes every change in a single report; `vpad_neutral` releases everything
- force feedback delivered as upload, erase, play and gain events, in the same effect format
  as padff 0.1.0
- single-file form through `scripts/amalgamate.sh`
