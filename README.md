# vpad

Virtual gamepad with force feedback. One API creates the pad, sends its state and hands the
game's rumble requests back to you, on Linux, FreeBSD, macOS and Windows.

- backends: uinput (Linux, FreeBSD), IOHIDUserDevice (macOS 12+), ViGEmBus (Windows); a
  missing service is reported as an error, never ignored (`vpad_backend` names the backend)
- identity: Xbox 360 (`Microsoft X-Box 360 pad`, 045e:028e), generic, or your own name,
  vendor and product
- canonical layout: 15 buttons including the d-pad, sticks from -32768 to 32767 (Y positive
  is down), triggers from 0 to 255; the d-pad becomes a hat where the platform expects one
- one report per `vpad_send`: every change since the last send goes out in a single write with
  its sync, and nothing is written when nothing changed
- force feedback as events: effect upload, erase, play/stop and gain, already answered to the
  game; `vpad_effect_t` has the same fields, order and type values as `pff_effect_t` from
  padff 1.0, so effects go straight into it. Its first field, `size`, is always
  `sizeof(vpad_effect_t)`, which is what padff checks
- `vpad_neutral` releases every button and centers every axis in one report

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/rumble-dark.gif">
  <img src="docs/images/rumble-light.gif" alt="The rumble example printing the force feedback requests of an SDL3 game" width="674">
</picture>

## Requirements

| Platform | Backend | What it needs |
| --- | --- | --- |
| Linux | uinput | write access to `/dev/uinput` (udev rule, ACL or the `input` group) |
| FreeBSD | uinput | the `uinput` module and access to `/dev/uinput` |
| macOS 12+ | IOHIDUserDevice | the `com.apple.developer.hid.virtual.device` entitlement |
| Windows 7+ | ViGEmBus | the ViGEmBus driver installed; no ViGEmClient needed |

Other systems build and return `VPAD_ERR_UNSUPPORTED`.

## Build and install

```sh
cmake -S . -B build -DBUILD_SHARED_LIBS=ON
cmake --build build
cmake --install build --prefix /usr/local
```

Use it from CMake:

```cmake
find_package(vpad REQUIRED)
target_link_libraries(app PRIVATE vpad::vpad)
```

Or with pkg-config:

```sh
cc app.c $(pkg-config --cflags --libs vpad)
```

## Single-file form

`scripts/amalgamate.sh` writes `dist/vpad/vpad.h`. Copy it into your project and, in exactly
one `.c` file, before any other include:

```c
#define VPAD_IMPLEMENTATION
#include "vpad.h"
```

Define `VPAD_STATIC` as well to keep every function private to that file. Link `setupapi` on
Windows and the `IOKit` and `CoreFoundation` frameworks on macOS.

## Usage

```c
vpad_config_t config;
vpad_t *pad = NULL;
vpad_event_t event;
int status;

vpad_config_init(&config, VPAD_XBOX360);
status = vpad_create(&config, &pad);
if (status != VPAD_OK) {
    fprintf(stderr, "%s: %s\n", vpad_backend(), vpad_strerror(status));
    return 1;
}

vpad_set_button(pad, VPAD_BTN_A, 1);
vpad_set_axis(pad, VPAD_AXIS_LX, -32768);
vpad_set_axis(pad, VPAD_AXIS_RT, 255);
vpad_send(pad);

while (vpad_next_event(pad, &event) == VPAD_OK) {
    if (event.kind == VPAD_EV_UPLOAD) {
        pff_effect_t effect;

        memcpy(&effect, &event.effect, sizeof(effect));
        pff_mixer_upload(mixer, event.slot, &effect, now_ms);
    }
}

vpad_neutral(pad);
vpad_destroy(pad);
```

Events:

| `kind` | `slot` | `value` | `effect` |
| --- | --- | --- | --- |
| `VPAD_EV_UPLOAD` | effect slot (0 to 15) | | the new or updated effect |
| `VPAD_EV_ERASE` | effect slot | | |
| `VPAD_EV_PLAY` | effect slot | repeat count, 0 stops | |
| `VPAD_EV_GAIN` | 0 | gain, 0 to 65535 | |
| `VPAD_EV_PLAYER` | 0 | player slot the host gave the pad, 0 to 3 (Windows) | |

New kinds may appear in minor versions; ignore the ones you do not know.

`vpad_next_event` returns `VPAD_ERR_AGAIN` when nothing is waiting. `vpad_wait(pad, timeout_ms)`
blocks until an event may be ready (`VPAD_OK`) or the time passes (`VPAD_ERR_AGAIN`); `-1` waits
without limit. To wait together with other sources, use `vpad_handle` instead: a file
descriptor to `poll` on POSIX, an event to `WaitForSingleObject` on Windows. On Linux a game's upload or erase blocks until you call `vpad_next_event`, so keep
calling it while the pad exists. See [`examples/basic.c`](examples/basic.c) and
[`examples/rumble.c`](examples/rumble.c).

Effect values follow padff: magnitudes 0 to 65535, signed levels -32768 to 32767, envelope
levels 0 to 65535 (the Linux 0 to 32767 scaled up, full to full), times in milliseconds.

`vpad_config_init` fills `size` and the kind and zeroes the rest; then set what you need.
Kinds:

- `VPAD_XBOX360`: Xbox 360 pad (045e:028e), on every backend
- `VPAD_GENERIC`: the same controls under a neutral identity
- `VPAD_DS4`: DualShock 4 (054c:09cc), Linux and FreeBSD only for now (`VPAD_ERR_UNSUPPORTED`
  elsewhere). The pad looks like the kernel's own driver shows a real one: face buttons by
  position (`VPAD_BTN_A` cross, `B` circle, `X` square, `Y` triangle), sticks 0 to 255
  centered at 128 (the API keeps -32768 to 32767), digital L2/R2 pressed while the trigger is
  above 0, and only rumble force feedback

Fields:

- `name`: device name, up to 79 bytes; `NULL` keeps the default for the kind
- `vendor`, `product`: USB ids; both `0` keep the default for the kind
- `flags`: `VPAD_NO_FF` makes a pad without force feedback. Games then see no rumble support
  and get an error at once when they try, instead of waiting on Linux for the
  `vpad_next_event` an input-only program never calls
- `alloc_fn`, `free_fn`, `alloc_ctx`: your allocator, or `NULL` for `malloc`/`free`

Conventions:

- every function that can fail returns `VPAD_OK` or an error code; `vpad_strerror` describes it
- what `vpad_create` returns is released only by `vpad_destroy`, which accepts `NULL` and
  removes the device
- a pad is used from one thread at a time

## Limits

- Windows and macOS: the game only sends two motor speeds. Each change arrives as an upload of
  a rumble effect to slot 0 (length 0, endless), plus one play the first time; zero speeds
  mean silence. No erase or gain events.
- Windows: ViGEmBus shows an Xbox 360 pad; the name is ignored, vendor and product are used.
- macOS: the pad uses its own HID gamepad descriptor (hat d-pad, output report 2 for the two
  motors), buttons 1 to 11 in the order of the `VPAD_BTN_` constants.
- Linux: other programs that open the pad may write force feedback too (Wine sets the gain
  when it finds a new pad); those requests arrive as events like any game's.
- Linux and FreeBSD offer 16 effect slots; unsupported effect types (spring, damper and the
  other condition effects) are refused to the game with `EINVAL`.

## Versioning

Semantic Versioning. Compatible releases bump the minor version; incompatible ones bump the
major version and the soname. See [CHANGELOG.md](CHANGELOG.md).

## License

MIT, see [LICENSE](LICENSE).
