#ifndef VPAD_INTERNAL_H
#define VPAD_INTERNAL_H

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif

#include "vpad/vpad.h"

#define VPAD_NAME_MAX 80
#define VPAD_QUEUE_LEN 32

struct vpad_mem {
    vpad_alloc_fn alloc_fn;
    vpad_free_fn free_fn;
    void *alloc_ctx;
};

struct vpad_os;

struct vpad {
    struct vpad_mem mem;
    int kind;
    uint32_t flags;
    char name[VPAD_NAME_MAX];
    uint16_t vendor;
    uint16_t product;
    uint8_t buttons[VPAD_BTN_COUNT];
    int32_t axes[VPAD_AXIS_COUNT];
    uint8_t sent_buttons[VPAD_BTN_COUNT];
    int32_t sent_axes[VPAD_AXIS_COUNT];
    vpad_event_t queue[VPAD_QUEUE_LEN];
    unsigned queue_head;
    unsigned queue_len;
    struct vpad_os *os;
};

void *vpad_mem_alloc(const struct vpad_mem *mem, size_t size);
void vpad_mem_free(const struct vpad_mem *mem, void *ptr);
int vpad_queue_space(const vpad_t *pad);
void vpad_queue_push(vpad_t *pad, const vpad_event_t *event);
void vpad_hat(const uint8_t *buttons, int *out_x, int *out_y);
void vpad_mark_sent(vpad_t *pad);

int vpad_os_open(vpad_t *pad);
void vpad_os_close(vpad_t *pad);
int vpad_os_send(vpad_t *pad);
int vpad_os_poll(vpad_t *pad);
vpad_handle_t vpad_os_handle(const vpad_t *pad);
const char *vpad_os_backend(void);

#endif
