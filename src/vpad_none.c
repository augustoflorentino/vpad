#include "vpad_internal.h"

#if !defined(__linux__) && !defined(__FreeBSD__) && !defined(__APPLE__) && !defined(_WIN32)

int vpad_os_open(vpad_t *pad)
{
    (void)pad;
    return VPAD_ERR_UNSUPPORTED;
}

void vpad_os_close(vpad_t *pad)
{
    (void)pad;
}

int vpad_os_send(vpad_t *pad)
{
    (void)pad;
    return VPAD_ERR_UNSUPPORTED;
}

int vpad_os_poll(vpad_t *pad)
{
    (void)pad;
    return VPAD_ERR_UNSUPPORTED;
}

vpad_handle_t vpad_os_handle(const vpad_t *pad)
{
    (void)pad;
    return VPAD_NO_HANDLE;
}

int vpad_os_syspath(const vpad_t *pad, char *buf, size_t buf_len)
{
    (void)pad;
    (void)buf;
    (void)buf_len;
    return VPAD_ERR_UNSUPPORTED;
}

const char *vpad_os_backend(void)
{
    return "none";
}

#endif
