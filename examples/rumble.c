#include <vpad/vpad.h>

#include <stdio.h>

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
    case VPAD_EV_GAIN:
        printf("gain %lu\n", (unsigned long)event->value);
        break;
    case VPAD_EV_PLAYER:
        printf("player %lu\n", (unsigned long)event->value + 1);
        break;
    default:
        break;
    }
    fflush(stdout);
}

int main(void)
{
    vpad_config_t config;
    vpad_t *pad = NULL;
    vpad_event_t event;
    int status;

    vpad_config_init(&config, VPAD_XBOX360);
    status = vpad_create(&config, &pad);
    if (status == VPAD_OK)
        printf("virtual pad ready (%s); start a game and make it rumble\n", vpad_backend());
    while (status == VPAD_OK || status == VPAD_ERR_AGAIN) {
        status = vpad_next_event(pad, &event, sizeof(event));
        if (status == VPAD_OK)
            print_event(&event);
        else if (status == VPAD_ERR_AGAIN)
            status = vpad_wait(pad, -1);
    }
    vpad_destroy(pad);
    fprintf(stderr, "error (%s backend): %s\n", vpad_backend(), vpad_strerror(status));
    return 1;
}
