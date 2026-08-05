#include "vpad_internal.h"

#include <stdlib.h>
#include <string.h>

typedef char vpad_effect_size_check[sizeof(vpad_effect_t) == 22 ? 1 : -1];
typedef char vpad_event_size_check[sizeof(vpad_event_t) == 32 ? 1 : -1];

static const char *const vpad_default_names[] = {"Microsoft X-Box 360 pad", "vpad Virtual Gamepad"};
static const uint16_t vpad_default_ids[][2] = {{0x045e, 0x028e}, {0x0000, 0x0000}};

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
    pad->queue[(pad->queue_head + pad->queue_len) % VPAD_QUEUE_LEN] = *event;
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

static int vpad_check_identity(int kind, const char *name)
{
    if (kind != VPAD_XBOX360 && kind != VPAD_GENERIC)
        return VPAD_ERR_ARG;
    if (name != NULL && (name[0] == '\0' || strlen(name) >= VPAD_NAME_MAX))
        return VPAD_ERR_ARG;
    return VPAD_OK;
}

int vpad_create(int kind, const char *name, uint16_t vendor, uint16_t product, vpad_alloc_fn alloc_fn,
                vpad_free_fn free_fn, void *alloc_ctx, vpad_t **out_pad)
{
    vpad_t *pad;
    int status;

    if (out_pad == NULL || (alloc_fn == NULL) != (free_fn == NULL))
        return VPAD_ERR_ARG;
    *out_pad = NULL;
    status = vpad_check_identity(kind, name);
    if (status != VPAD_OK)
        return status;
    if (alloc_fn == NULL) {
        alloc_fn = vpad_default_alloc;
        free_fn = vpad_default_free;
    }
    pad = alloc_fn(alloc_ctx, sizeof(*pad));
    if (pad == NULL)
        return VPAD_ERR_NOMEM;
    memset(pad, 0, sizeof(*pad));
    pad->mem.alloc_fn = alloc_fn;
    pad->mem.free_fn = free_fn;
    pad->mem.alloc_ctx = alloc_ctx;
    pad->kind = kind;
    strcpy(pad->name, name ? name : vpad_default_names[kind]);
    pad->vendor = vendor || product ? vendor : vpad_default_ids[kind][0];
    pad->product = vendor || product ? product : vpad_default_ids[kind][1];
    status = vpad_os_open(pad);
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

int vpad_next_event(vpad_t *pad, vpad_event_t *out_event)
{
    int status;

    if (pad == NULL || out_event == NULL)
        return VPAD_ERR_ARG;
    if (pad->queue_len == 0) {
        status = vpad_os_poll(pad);
        if (status != VPAD_OK)
            return status;
    }
    if (pad->queue_len == 0)
        return VPAD_ERR_AGAIN;
    *out_event = pad->queue[pad->queue_head];
    pad->queue_head = (pad->queue_head + 1) % VPAD_QUEUE_LEN;
    pad->queue_len--;
    return VPAD_OK;
}

vpad_handle_t vpad_handle(const vpad_t *pad)
{
    return pad ? vpad_os_handle(pad) : VPAD_NO_HANDLE;
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
    default:
        return "unknown status";
    }
}
