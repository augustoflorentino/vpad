#include "vpad_internal.h"

#if defined(__linux__) || defined(__FreeBSD__)

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/uinput.h>
#else
#include <dev/evdev/input.h>
#include <dev/evdev/uinput.h>
#endif

#if defined(UI_BEGIN_FF_UPLOAD) && defined(EV_UINPUT)
#define VPAD_HAVE_FF 1
#else
#define VPAD_HAVE_FF 0
#endif

#define VPAD_FF_SLOTS 16

struct vpad_os {
    int fd;
};

static const uint16_t vpad_key_codes[] = {BTN_A,      BTN_B,     BTN_X,    BTN_Y,      BTN_TL,    BTN_TR,
                                          BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
static const uint16_t vpad_abs_codes[VPAD_AXIS_COUNT] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ};

#if VPAD_HAVE_FF
static const uint16_t vpad_ff_codes[] = {FF_RUMBLE,   FF_PERIODIC, FF_CONSTANT, FF_RAMP,     FF_SINE,
                                         FF_SQUARE,   FF_TRIANGLE, FF_SAW_UP,   FF_SAW_DOWN, FF_GAIN};
#endif

static int vpad_errno_status(int err)
{
    if (err == ENOENT || err == ENODEV || err == ENXIO)
        return VPAD_ERR_NODEVICE;
    if (err == EACCES || err == EPERM)
        return VPAD_ERR_ACCESS;
    if (err == ENOMEM)
        return VPAD_ERR_NOMEM;
    return VPAD_ERR_IO;
}

static int vpad_ioctl_int(int fd, unsigned long request, int value)
{
    return ioctl(fd, request, value) == 0 ? VPAD_OK : vpad_errno_status(errno);
}

static int vpad_abs_setup(int fd, uint16_t code, int min, int max, int fuzz, int flat)
{
    struct uinput_abs_setup abs;

    memset(&abs, 0, sizeof(abs));
    abs.code = code;
    abs.absinfo.minimum = min;
    abs.absinfo.maximum = max;
    abs.absinfo.fuzz = fuzz;
    abs.absinfo.flat = flat;
    if (ioctl(fd, UI_SET_ABSBIT, code) != 0 || ioctl(fd, UI_ABS_SETUP, &abs) != 0)
        return vpad_errno_status(errno);
    return VPAD_OK;
}

static int vpad_enable_ff(int fd)
{
#if VPAD_HAVE_FF
    size_t i;
    int status = vpad_ioctl_int(fd, UI_SET_EVBIT, EV_FF);

    for (i = 0; i < sizeof(vpad_ff_codes) / sizeof(vpad_ff_codes[0]) && status == VPAD_OK; i++)
        status = vpad_ioctl_int(fd, UI_SET_FFBIT, vpad_ff_codes[i]);
    return status;
#else
    (void)fd;
    return VPAD_OK;
#endif
}

static int vpad_enable(int fd)
{
    size_t i;
    int status = vpad_ioctl_int(fd, UI_SET_EVBIT, EV_KEY);

    if (status == VPAD_OK)
        status = vpad_ioctl_int(fd, UI_SET_EVBIT, EV_ABS);
    for (i = 0; i < sizeof(vpad_key_codes) / sizeof(vpad_key_codes[0]) && status == VPAD_OK; i++)
        status = vpad_ioctl_int(fd, UI_SET_KEYBIT, vpad_key_codes[i]);
    for (i = 0; i < 4 && status == VPAD_OK; i++)
        status = vpad_abs_setup(fd, vpad_abs_codes[i], -32768, 32767, 16, 128);
    for (i = 4; i < VPAD_AXIS_COUNT && status == VPAD_OK; i++)
        status = vpad_abs_setup(fd, vpad_abs_codes[i], 0, 255, 0, 0);
    if (status == VPAD_OK)
        status = vpad_abs_setup(fd, ABS_HAT0X, -1, 1, 0, 0);
    if (status == VPAD_OK)
        status = vpad_abs_setup(fd, ABS_HAT0Y, -1, 1, 0, 0);
    if (status == VPAD_OK)
        status = vpad_enable_ff(fd);
    return status;
}

static int vpad_setup(const vpad_t *pad, int fd)
{
    struct uinput_setup setup;

    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_USB;
    setup.id.vendor = pad->vendor;
    setup.id.product = pad->product;
    setup.id.version = 1;
    memcpy(setup.name, pad->name, strlen(pad->name));
#if VPAD_HAVE_FF
    setup.ff_effects_max = VPAD_FF_SLOTS;
#endif
    if (ioctl(fd, UI_DEV_SETUP, &setup) != 0 || ioctl(fd, UI_DEV_CREATE) != 0)
        return vpad_errno_status(errno);
    return VPAD_OK;
}

int vpad_os_open(vpad_t *pad)
{
    int fd;
    int status;

    pad->os = vpad_mem_alloc(&pad->mem, sizeof(*pad->os));
    if (pad->os == NULL)
        return VPAD_ERR_NOMEM;
    fd = open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    status = fd < 0 ? vpad_errno_status(errno) : vpad_enable(fd);
    if (status == VPAD_OK)
        status = vpad_setup(pad, fd);
    if (status != VPAD_OK) {
        if (fd >= 0)
            close(fd);
        vpad_mem_free(&pad->mem, pad->os);
        pad->os = NULL;
        return status;
    }
    pad->os->fd = fd;
    return VPAD_OK;
}

void vpad_os_close(vpad_t *pad)
{
    if (pad->os == NULL)
        return;
    ioctl(pad->os->fd, UI_DEV_DESTROY);
    close(pad->os->fd);
    vpad_mem_free(&pad->mem, pad->os);
    pad->os = NULL;
}

static void vpad_add(struct input_event *events, size_t *count, uint16_t type, uint16_t code, int32_t value)
{
    memset(&events[*count], 0, sizeof(events[*count]));
    events[*count].type = type;
    events[*count].code = code;
    events[*count].value = value;
    (*count)++;
}

static size_t vpad_collect(const vpad_t *pad, struct input_event *events)
{
    size_t count = 0;
    int x, y, old_x, old_y;
    size_t i;

    for (i = 0; i < sizeof(vpad_key_codes) / sizeof(vpad_key_codes[0]); i++)
        if (pad->buttons[i] != pad->sent_buttons[i])
            vpad_add(events, &count, EV_KEY, vpad_key_codes[i], pad->buttons[i]);
    vpad_hat(pad->buttons, &x, &y);
    vpad_hat(pad->sent_buttons, &old_x, &old_y);
    if (x != old_x)
        vpad_add(events, &count, EV_ABS, ABS_HAT0X, x);
    if (y != old_y)
        vpad_add(events, &count, EV_ABS, ABS_HAT0Y, y);
    for (i = 0; i < VPAD_AXIS_COUNT; i++)
        if (pad->axes[i] != pad->sent_axes[i])
            vpad_add(events, &count, EV_ABS, vpad_abs_codes[i], pad->axes[i]);
    return count;
}

int vpad_os_send(vpad_t *pad)
{
    struct input_event events[VPAD_BTN_COUNT + VPAD_AXIS_COUNT + 1];
    size_t count = vpad_collect(pad, events);
    ssize_t n;

    if (count == 0)
        return VPAD_OK;
    vpad_add(events, &count, EV_SYN, SYN_REPORT, 0);
    do
        n = write(pad->os->fd, events, count * sizeof(events[0]));
    while (n < 0 && errno == EINTR);
    if (n != (ssize_t)(count * sizeof(events[0])))
        return n < 0 ? vpad_errno_status(errno) : VPAD_ERR_IO;
    vpad_mark_sent(pad);
    return VPAD_OK;
}

#if VPAD_HAVE_FF

static uint16_t vpad_envelope_level(uint16_t level)
{
    return level >= 32767 ? 65535 : (uint16_t)((uint32_t)level * 65535u / 32767u);
}

static void vpad_envelope(const struct ff_envelope *env, vpad_effect_t *out)
{
    out->attack_ms = env->attack_length;
    out->attack_level = vpad_envelope_level(env->attack_level);
    out->fade_ms = env->fade_length;
    out->fade_level = vpad_envelope_level(env->fade_level);
}

static int vpad_convert(const struct ff_effect *in, vpad_effect_t *out)
{
    memset(out, 0, sizeof(*out));
    out->delay_ms = in->replay.delay;
    out->length_ms = in->replay.length;
    switch (in->type) {
    case FF_RUMBLE:
        out->type = VPAD_RUMBLE;
        out->strong = in->u.rumble.strong_magnitude;
        out->weak = in->u.rumble.weak_magnitude;
        return 1;
    case FF_PERIODIC:
        out->type = VPAD_PERIODIC;
        out->level = in->u.periodic.magnitude;
        vpad_envelope(&in->u.periodic.envelope, out);
        return 1;
    case FF_CONSTANT:
        out->type = VPAD_CONSTANT;
        out->level = in->u.constant.level;
        vpad_envelope(&in->u.constant.envelope, out);
        return 1;
    case FF_RAMP:
        out->type = VPAD_RAMP;
        out->level = in->u.ramp.start_level;
        out->level_end = in->u.ramp.end_level;
        vpad_envelope(&in->u.ramp.envelope, out);
        return 1;
    default:
        return 0;
    }
}

static int vpad_upload(vpad_t *pad, int32_t request)
{
    struct uinput_ff_upload up;
    vpad_event_t event;
    int accepted;

    memset(&up, 0, sizeof(up));
    up.request_id = (uint32_t)request;
    if (ioctl(pad->os->fd, UI_BEGIN_FF_UPLOAD, &up) != 0)
        return vpad_errno_status(errno);
    memset(&event, 0, sizeof(event));
    accepted = vpad_convert(&up.effect, &event.effect) && up.effect.id >= 0 && up.effect.id < VPAD_FF_SLOTS;
    up.retval = accepted ? 0 : -EINVAL;
    if (ioctl(pad->os->fd, UI_END_FF_UPLOAD, &up) != 0)
        return vpad_errno_status(errno);
    if (!accepted)
        return VPAD_OK;
    event.kind = VPAD_EV_UPLOAD;
    event.slot = (uint16_t)up.effect.id;
    vpad_queue_push(pad, &event);
    return VPAD_OK;
}

static int vpad_erase(vpad_t *pad, int32_t request)
{
    struct uinput_ff_erase erase;
    vpad_event_t event;

    memset(&erase, 0, sizeof(erase));
    erase.request_id = (uint32_t)request;
    if (ioctl(pad->os->fd, UI_BEGIN_FF_ERASE, &erase) != 0)
        return vpad_errno_status(errno);
    erase.retval = 0;
    if (ioctl(pad->os->fd, UI_END_FF_ERASE, &erase) != 0)
        return vpad_errno_status(errno);
    memset(&event, 0, sizeof(event));
    event.kind = VPAD_EV_ERASE;
    event.slot = (uint16_t)erase.effect_id;
    vpad_queue_push(pad, &event);
    return VPAD_OK;
}

static int vpad_handle_input(vpad_t *pad, const struct input_event *in)
{
    vpad_event_t event;

    if (in->type == EV_UINPUT && in->code == UI_FF_UPLOAD)
        return vpad_upload(pad, in->value);
    if (in->type == EV_UINPUT && in->code == UI_FF_ERASE)
        return vpad_erase(pad, in->value);
    if (in->type != EV_FF || (in->code != FF_GAIN && in->code >= VPAD_FF_SLOTS))
        return VPAD_OK;
    memset(&event, 0, sizeof(event));
    event.kind = in->code == FF_GAIN ? VPAD_EV_GAIN : VPAD_EV_PLAY;
    event.slot = in->code == FF_GAIN ? 0 : in->code;
    event.value = in->value < 0 ? 0 : (uint32_t)in->value;
    vpad_queue_push(pad, &event);
    return VPAD_OK;
}

#else

static int vpad_handle_input(vpad_t *pad, const struct input_event *in)
{
    (void)pad;
    (void)in;
    return VPAD_OK;
}

#endif

int vpad_os_poll(vpad_t *pad)
{
    struct input_event in;
    ssize_t n;
    int status = VPAD_OK;

    while (status == VPAD_OK && vpad_queue_space(pad)) {
        n = read(pad->os->fd, &in, sizeof(in));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return VPAD_OK;
        if (n != (ssize_t)sizeof(in))
            return n < 0 ? vpad_errno_status(errno) : VPAD_ERR_IO;
        status = vpad_handle_input(pad, &in);
    }
    return status;
}

vpad_handle_t vpad_os_handle(const vpad_t *pad)
{
    return pad->os->fd;
}

const char *vpad_os_backend(void)
{
    return "uinput";
}

#endif
