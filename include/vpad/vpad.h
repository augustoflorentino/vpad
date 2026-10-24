#ifndef VPAD_H
#define VPAD_H

#if defined(VPAD_IMPLEMENTATION) && defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>

#if defined(VPAD_STATIC) && defined(__GNUC__)
#define VPAD_API static __attribute__((unused))
#elif defined(VPAD_STATIC)
#define VPAD_API static
#elif defined(_WIN32) && defined(VPAD_BUILD_SHARED)
#define VPAD_API __declspec(dllexport)
#elif defined(_WIN32) && defined(VPAD_SHARED)
#define VPAD_API __declspec(dllimport)
#elif defined(__GNUC__) && defined(VPAD_BUILD_SHARED)
#define VPAD_API __attribute__((visibility("default")))
#else
#define VPAD_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VPAD_OK 0
#define VPAD_ERR_ARG 1
#define VPAD_ERR_NOMEM 2
#define VPAD_ERR_UNSUPPORTED 3
#define VPAD_ERR_NODEVICE 4
#define VPAD_ERR_ACCESS 5
#define VPAD_ERR_IO 6
#define VPAD_ERR_AGAIN 7
#define VPAD_ERR_RANGE 8

#define VPAD_XBOX360 0
#define VPAD_GENERIC 1
#define VPAD_DS4 2

#define VPAD_BTN_A 0
#define VPAD_BTN_B 1
#define VPAD_BTN_X 2
#define VPAD_BTN_Y 3
#define VPAD_BTN_LB 4
#define VPAD_BTN_RB 5
#define VPAD_BTN_BACK 6
#define VPAD_BTN_START 7
#define VPAD_BTN_GUIDE 8
#define VPAD_BTN_LSTICK 9
#define VPAD_BTN_RSTICK 10
#define VPAD_BTN_DPAD_UP 11
#define VPAD_BTN_DPAD_DOWN 12
#define VPAD_BTN_DPAD_LEFT 13
#define VPAD_BTN_DPAD_RIGHT 14
#define VPAD_BTN_COUNT 15

#define VPAD_AXIS_LX 0
#define VPAD_AXIS_LY 1
#define VPAD_AXIS_RX 2
#define VPAD_AXIS_RY 3
#define VPAD_AXIS_LT 4
#define VPAD_AXIS_RT 5
#define VPAD_AXIS_COUNT 6

#define VPAD_RUMBLE 1
#define VPAD_PERIODIC 2
#define VPAD_CONSTANT 3
#define VPAD_RAMP 4

#define VPAD_EV_UPLOAD 1
#define VPAD_EV_ERASE 2
#define VPAD_EV_PLAY 3
#define VPAD_EV_GAIN 4
#define VPAD_EV_PLAYER 5

#if defined(_WIN32)
typedef void *vpad_handle_t;
#define VPAD_NO_HANDLE ((vpad_handle_t)0)
#else
typedef int vpad_handle_t;
#define VPAD_NO_HANDLE (-1)
#endif

typedef struct vpad_effect {
    uint16_t size;
    uint16_t type;
    uint16_t strong;
    uint16_t weak;
    int16_t level;
    int16_t level_end;
    uint16_t delay_ms;
    uint16_t length_ms;
    uint16_t attack_ms;
    uint16_t attack_level;
    uint16_t fade_ms;
    uint16_t fade_level;
} vpad_effect_t;

typedef struct vpad_event {
    uint16_t kind;
    uint16_t slot;
    uint32_t value;
    vpad_effect_t effect;
} vpad_event_t;

typedef struct vpad vpad_t;

typedef void *(*vpad_alloc_fn)(void *ctx, size_t size);
typedef void (*vpad_free_fn)(void *ctx, void *ptr);

#define VPAD_NO_FF 1u

typedef struct vpad_config {
    uint16_t size;
    uint16_t kind;
    uint16_t vendor;
    uint16_t product;
    uint32_t flags;
    const char *name;
    vpad_alloc_fn alloc_fn;
    vpad_free_fn free_fn;
    void *alloc_ctx;
} vpad_config_t;

VPAD_API void vpad_config_init(vpad_config_t *config, int kind);
VPAD_API int vpad_create(const vpad_config_t *config, vpad_t **out_pad);
VPAD_API void vpad_destroy(vpad_t *pad);
VPAD_API int vpad_set_button(vpad_t *pad, int button, int pressed);
VPAD_API int vpad_set_axis(vpad_t *pad, int axis, int32_t value);
VPAD_API int vpad_send(vpad_t *pad);
VPAD_API int vpad_neutral(vpad_t *pad);
VPAD_API int vpad_next_event(vpad_t *pad, vpad_event_t *out_event);
VPAD_API int vpad_wait(vpad_t *pad, long timeout_ms);
VPAD_API vpad_handle_t vpad_handle(const vpad_t *pad);
VPAD_API int vpad_syspath(const vpad_t *pad, char *buf, size_t buf_len, size_t *out_len);
VPAD_API const char *vpad_backend(void);
VPAD_API const char *vpad_strerror(int status);

#ifdef __cplusplus
}
#endif

#endif
