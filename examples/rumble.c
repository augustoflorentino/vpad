#include <vpad/vpad.h>

#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <poll.h>
#endif

static void wait_for(vpad_handle_t handle)
{
#if defined(_WIN32)
    WaitForSingleObject(handle, INFINITE);
#else
    struct pollfd pfd;

    pfd.fd = handle;
    pfd.events = POLLIN;
    pfd.revents = 0;
    poll(&pfd, 1, -1);
#endif
}

static void print_event(const vpad_event_t *event)
{
    switch (event->kind) {
    case VPAD_EV_UPLOAD:
        printf("upload slot %u: type %u strong %u weak %u level %d length %u ms\n", event->slot,
               event->effect.type, event->effect.strong, event->effect.weak, event->effect.level,
               event->effect.length_ms);
        break;
    case VPAD_EV_ERASE:
        printf("erase slot %u\n", event->slot);
        break;
    case VPAD_EV_PLAY:
        printf("play slot %u, %lu times\n", event->slot, (unsigned long)event->value);
        break;
    default:
        printf("gain %lu\n", (unsigned long)event->value);
        break;
    }
    fflush(stdout);
}

int main(void)
{
    vpad_t *pad = NULL;
    vpad_event_t event;
    int status;

    status = vpad_create(VPAD_XBOX360, NULL, 0, 0, NULL, NULL, NULL, &pad);
    if (status == VPAD_OK)
        printf("virtual pad ready (%s); start a game and make it rumble\n", vpad_backend());
    while (status == VPAD_OK || status == VPAD_ERR_AGAIN) {
        status = vpad_next_event(pad, &event);
        if (status == VPAD_OK)
            print_event(&event);
        else if (status == VPAD_ERR_AGAIN)
            wait_for(vpad_handle(pad));
    }
    vpad_destroy(pad);
    fprintf(stderr, "error (%s backend): %s\n", vpad_backend(), vpad_strerror(status));
    return 1;
}
