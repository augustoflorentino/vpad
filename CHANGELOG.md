# Changelog

Each release lists what changed, what updating means for existing code, and every security fix.

## Unreleased

## Unreleased

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
