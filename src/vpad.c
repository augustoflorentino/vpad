#include "vpad_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

typedef char vpad_effect_size_check[sizeof(vpad_effect_t) == 24 ? 1 : -1];
typedef char vpad_effect_layout_check[offsetof(vpad_effect_t, size) == 0 && offsetof(vpad_effect_t, type) == 2 &&
                                              offsetof(vpad_effect_t, fade_level) == 22
                                          ? 1
                                          : -1];
typedef char vpad_event_size_check[sizeof(vpad_event_t) == 32 ? 1 : -1];

static int vpad_wake_open(vpad_t *pad);
static void vpad_wake_close(vpad_t *pad);

static const char *const vpad_default_names[] = {"Microsoft X-Box 360 pad", "vpad Virtual Gamepad",
                                                 "Sony Interactive Entertainment Wireless Controller"};
static const uint16_t vpad_default_ids[][2] = {{0x045e, 0x028e}, {0x0000, 0x0000}, {0x054c, 0x09cc}};

static void *vpad_default_alloc(void *ctx, size_t size)
{
    (void)ctx;
    return malloc(size);
}

static void vpad_default_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

void *vpad_mem_alloc(const struct vpad_mem *mem, size_t size)
{
    return mem->alloc_fn(mem->alloc_ctx, size ? size : 1);
}

void vpad_mem_free(const struct vpad_mem *mem, void *ptr)
{
    if (ptr != NULL)
        mem->free_fn(mem->alloc_ctx, ptr);
}

int vpad_queue_space(const vpad_t *pad)
{
    return pad->queue_len < VPAD_QUEUE_LEN;
}

void vpad_queue_push(vpad_t *pad, const vpad_event_t *event)
{
    vpad_event_t *slot = &pad->queue[(pad->queue_head + pad->queue_len) % VPAD_QUEUE_LEN];

    *slot = *event;
    slot->effect.size = sizeof(vpad_effect_t);
    pad->queue_len++;
}

void vpad_hat(const uint8_t *buttons, int *out_x, int *out_y)
{
    *out_x = buttons[VPAD_BTN_DPAD_RIGHT] - buttons[VPAD_BTN_DPAD_LEFT];
    *out_y = buttons[VPAD_BTN_DPAD_DOWN] - buttons[VPAD_BTN_DPAD_UP];
}

void vpad_mark_sent(vpad_t *pad)
{
    memcpy(pad->sent_buttons, pad->buttons, sizeof(pad->buttons));
    memcpy(pad->sent_axes, pad->axes, sizeof(pad->axes));
}

static int vpad_check_config(const vpad_config_t *config)
{
    if (config->size != sizeof(vpad_config_t))
        return VPAD_ERR_ARG;
    if (config->kind != VPAD_XBOX360 && config->kind != VPAD_GENERIC && config->kind != VPAD_DS4)
        return VPAD_ERR_ARG;
    if ((config->flags & ~VPAD_NO_FF) != 0 || (config->alloc_fn == NULL) != (config->free_fn == NULL))
        return VPAD_ERR_ARG;
    if (config->name != NULL && (config->name[0] == '\0' || strlen(config->name) >= VPAD_NAME_MAX))
        return VPAD_ERR_ARG;
    return VPAD_OK;
}

void vpad_config_init(vpad_config_t *config, int kind)
{
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->size = sizeof(*config);
    config->kind = (uint16_t)kind;
}

static void vpad_set_identity(vpad_t *pad, const vpad_config_t *config)
{
    int custom = config->vendor || config->product;

    pad->kind = config->kind;
    pad->flags = config->flags;
    strcpy(pad->name, config->name ? config->name : vpad_default_names[pad->kind]);
    pad->vendor = custom ? config->vendor : vpad_default_ids[pad->kind][0];
    pad->product = custom ? config->product : vpad_default_ids[pad->kind][1];
}

int vpad_create(const vpad_config_t *config, vpad_t **out_pad)
{
    struct vpad_mem mem;
    vpad_t *pad;
    int status;

    if (out_pad == NULL || config == NULL)
        return VPAD_ERR_ARG;
    *out_pad = NULL;
    status = vpad_check_config(config);
    if (status != VPAD_OK)
        return status;
    mem.alloc_fn = config->alloc_fn ? config->alloc_fn : vpad_default_alloc;
    mem.free_fn = config->alloc_fn ? config->free_fn : vpad_default_free;
    mem.alloc_ctx = config->alloc_ctx;
    pad = vpad_mem_alloc(&mem, sizeof(*pad));
    if (pad == NULL)
        return VPAD_ERR_NOMEM;
    memset(pad, 0, sizeof(*pad));
    pad->mem = mem;
    vpad_set_identity(pad, config);
    status = vpad_os_open(pad);
    if (status == VPAD_OK && vpad_wake_open(pad) != VPAD_OK) {
        vpad_os_close(pad);
        status = VPAD_ERR_IO;
    }
    if (status != VPAD_OK) {
        vpad_mem_free(&pad->mem, pad);
        return status;
    }
    *out_pad = pad;
    return VPAD_OK;
}

void vpad_destroy(vpad_t *pad)
{
    if (pad == NULL)
        return;
    vpad_os_close(pad);
    vpad_wake_close(pad);
    vpad_mem_free(&pad->mem, pad);
}

int vpad_set_button(vpad_t *pad, int button, int pressed)
{
    if (pad == NULL || button < 0 || button >= VPAD_BTN_COUNT)
        return VPAD_ERR_ARG;
    pad->buttons[button] = pressed != 0;
    return VPAD_OK;
}

int vpad_set_axis(vpad_t *pad, int axis, int32_t value)
{
    int trigger = axis == VPAD_AXIS_LT || axis == VPAD_AXIS_RT;

    if (pad == NULL || axis < 0 || axis >= VPAD_AXIS_COUNT)
        return VPAD_ERR_ARG;
    if (trigger ? value < 0 || value > 255 : value < -32768 || value > 32767)
        return VPAD_ERR_ARG;
    pad->axes[axis] = value;
    return VPAD_OK;
}

int vpad_send(vpad_t *pad)
{
    if (pad == NULL)
        return VPAD_ERR_ARG;
    return vpad_os_send(pad);
}

int vpad_neutral(vpad_t *pad)
{
    if (pad == NULL)
        return VPAD_ERR_ARG;
    memset(pad->buttons, 0, sizeof(pad->buttons));
    memset(pad->axes, 0, sizeof(pad->axes));
    return vpad_os_send(pad);
}

int vpad_next_event(vpad_t *pad, vpad_event_t *out_event, size_t event_size)
{
    size_t copy = event_size < sizeof(vpad_event_t) ? event_size : sizeof(vpad_event_t);
    vpad_event_t event;
    int status;

    if (pad == NULL || out_event == NULL || event_size < VPAD_EVENT_BASE)
        return VPAD_ERR_ARG;
    if (pad->queue_len == 0) {
        status = vpad_os_poll(pad);
        if (status != VPAD_OK)
            return status;
    }
    if (pad->queue_len == 0)
        return VPAD_ERR_AGAIN;
    event = pad->queue[pad->queue_head];
    event.effect.size = (uint16_t)(copy - offsetof(vpad_event_t, effect));
    memcpy(out_event, &event, copy);
    pad->queue_head = (pad->queue_head + 1) % VPAD_QUEUE_LEN;
    pad->queue_len--;
    return VPAD_OK;
}

#if defined(_WIN32)

static int vpad_wake_open(vpad_t *pad)
{
    pad->wake[0] = CreateEventW(NULL, FALSE, FALSE, NULL);
    pad->wake[1] = pad->wake[0];
    return pad->wake[0] != NULL ? VPAD_OK : VPAD_ERR_IO;
}

static void vpad_wake_close(vpad_t *pad)
{
    CloseHandle(pad->wake[0]);
}

int vpad_wake(vpad_t *pad)
{
    if (pad == NULL)
        return VPAD_ERR_ARG;
    return SetEvent(pad->wake[1]) ? VPAD_OK : VPAD_ERR_IO;
}

static int vpad_wait_handle(vpad_t *pad, vpad_handle_t handle, long timeout_ms)
{
    HANDLE handles[2];
    DWORD r;

    handles[0] = handle;
    handles[1] = pad->wake[0];
    r = WaitForMultipleObjects(2, handles, FALSE, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
    if (r == WAIT_OBJECT_0)
        return VPAD_OK;
    return r == WAIT_TIMEOUT || r == WAIT_OBJECT_0 + 1 ? VPAD_ERR_AGAIN : VPAD_ERR_IO;
}

#else

static int vpad_wake_open(vpad_t *pad)
{
    int i;

    if (pipe(pad->wake) != 0)
        return VPAD_ERR_IO;
    for (i = 0; i < 2; i++) {
        fcntl(pad->wake[i], F_SETFD, FD_CLOEXEC);
        fcntl(pad->wake[i], F_SETFL, fcntl(pad->wake[i], F_GETFL) | O_NONBLOCK);
    }
    return VPAD_OK;
}

static void vpad_wake_close(vpad_t *pad)
{
    close(pad->wake[0]);
    close(pad->wake[1]);
}

int vpad_wake(vpad_t *pad)
{
    ssize_t n;

    if (pad == NULL)
        return VPAD_ERR_ARG;
    do
        n = write(pad->wake[1], "", 1);
    while (n < 0 && errno == EINTR);
    return n == 1 || errno == EAGAIN || errno == EWOULDBLOCK ? VPAD_OK : VPAD_ERR_IO;
}

static int vpad_wake_drain(vpad_t *pad)
{
    char buf[64];
    int woken = 0;

    while (read(pad->wake[0], buf, sizeof(buf)) > 0)
        woken = 1;
    return woken;
}

static int vpad_wait_handle(vpad_t *pad, vpad_handle_t handle, long timeout_ms)
{
    struct pollfd pfds[2];
    int r;

    pfds[0].fd = handle;
    pfds[1].fd = pad->wake[0];
    pfds[0].events = POLLIN;
    pfds[1].events = POLLIN;
    do {
        pfds[0].revents = 0;
        pfds[1].revents = 0;
        r = poll(pfds, 2, timeout_ms > 2000000000L ? 2000000000 : (int)timeout_ms);
    } while (r < 0 && errno == EINTR);
    if (r < 0)
        return VPAD_ERR_IO;
    if (pfds[1].revents != 0 && vpad_wake_drain(pad))
        return VPAD_ERR_AGAIN;
    return pfds[0].revents != 0 ? VPAD_OK : VPAD_ERR_AGAIN;
}

#endif

int vpad_wait(vpad_t *pad, long timeout_ms)
{
    vpad_handle_t handle;

    if (pad == NULL || timeout_ms < -1)
        return VPAD_ERR_ARG;
    if (pad->queue_len > 0)
        return VPAD_OK;
    handle = vpad_os_handle(pad);
    if (handle == VPAD_NO_HANDLE)
        return VPAD_ERR_UNSUPPORTED;
    return vpad_wait_handle(pad, handle, timeout_ms);
}

vpad_handle_t vpad_handle(const vpad_t *pad)
{
    return pad ? vpad_os_handle(pad) : VPAD_NO_HANDLE;
}

int vpad_syspath(const vpad_t *pad, char *buf, size_t buf_len, size_t *out_len)
{
    char path[VPAD_PATH_MAX];
    size_t len;
    int status;

    if (pad == NULL || (buf == NULL && buf_len > 0))
        return VPAD_ERR_ARG;
    status = vpad_os_syspath(pad, path, sizeof(path));
    if (status != VPAD_OK)
        return status;
    len = strlen(path);
    if (out_len != NULL)
        *out_len = len;
    if (buf == NULL)
        return VPAD_OK;
    if (buf_len <= len)
        return VPAD_ERR_RANGE;
    memcpy(buf, path, len + 1);
    return VPAD_OK;
}

const char *vpad_backend(void)
{
    return vpad_os_backend();
}

const char *vpad_strerror(int status)
{
    switch (status) {
    case VPAD_OK:
        return "ok";
    case VPAD_ERR_ARG:
        return "invalid argument";
    case VPAD_ERR_NOMEM:
        return "out of memory";
    case VPAD_ERR_UNSUPPORTED:
        return "no virtual gamepad backend on this system";
    case VPAD_ERR_NODEVICE:
        return "virtual device service not available (uinput, ViGEmBus or IOHIDUserDevice)";
    case VPAD_ERR_ACCESS:
        return "permission denied by the virtual device service";
    case VPAD_ERR_IO:
        return "virtual device input/output failed";
    case VPAD_ERR_AGAIN:
        return "no event pending";
    case VPAD_ERR_RANGE:
        return "buffer too small";
    default:
        return "unknown status";
    }
}
