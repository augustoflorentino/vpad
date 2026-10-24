#define VPAD_IMPLEMENTATION
#include "vpad/vpad.h"

int main(void)
{
    vpad_config_t config;
    vpad_t *pad = NULL;
    vpad_event_t event;
    int status;

    vpad_config_init(&config, VPAD_GENERIC);
    config.name = "vpad single file";
    config.vendor = 0x1234;
    config.product = 0x5678;
    status = vpad_create(&config, &pad);
    if (status == VPAD_ERR_UNSUPPORTED || status == VPAD_ERR_NODEVICE || status == VPAD_ERR_ACCESS)
        return vpad_strerror(status) != NULL ? 0 : 1;
    if (status == VPAD_OK)
        status = vpad_set_button(pad, VPAD_BTN_A, 1);
    if (status == VPAD_OK)
        status = vpad_send(pad);
    if (status == VPAD_OK)
        status = vpad_neutral(pad);
    if (status == VPAD_OK && vpad_next_event(pad, &event, sizeof(event)) != VPAD_ERR_AGAIN)
        status = VPAD_ERR_IO;
    vpad_destroy(pad);
    return status == VPAD_OK ? 0 : 1;
}
