#include <vpad/vpad.h>

#include <stdio.h>

int main(void)
{
    vpad_config_t config;
    vpad_t *pad = NULL;
    int status;
    int i;

    vpad_config_init(&config, VPAD_XBOX360);
    status = vpad_create(&config, &pad);
    for (i = 0; i < 100 && status == VPAD_OK; i++) {
        vpad_set_button(pad, VPAD_BTN_A, i % 2);
        vpad_set_button(pad, VPAD_BTN_DPAD_LEFT, i % 3 == 0);
        vpad_set_axis(pad, VPAD_AXIS_LX, i * 300 - 15000);
        vpad_set_axis(pad, VPAD_AXIS_LY, 15000 - i * 300);
        vpad_set_axis(pad, VPAD_AXIS_RT, i * 2);
        status = vpad_send(pad);
    }
    if (status == VPAD_OK)
        status = vpad_neutral(pad);
    vpad_destroy(pad);
    if (status == VPAD_ERR_UNSUPPORTED || status == VPAD_ERR_NODEVICE || status == VPAD_ERR_ACCESS) {
        printf("no virtual pad here (%s backend): %s\n", vpad_backend(), vpad_strerror(status));
        return 0;
    }
    if (status != VPAD_OK) {
        fprintf(stderr, "error (%s backend): %s\n", vpad_backend(), vpad_strerror(status));
        return 1;
    }
    printf("sent 100 reports through %s\n", vpad_backend());
    return 0;
}
