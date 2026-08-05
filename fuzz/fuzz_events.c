#include "vpad.c"
#include "vpad_uinput.c"

#include <stdint.h>
#include <string.h>

static void check_level(uint16_t in, uint16_t out)
{
    if (out != (in >= 32768 ? 65535 : in * 2))
        __builtin_trap();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct ff_effect effect;
    vpad_effect_t out;

    if (size < sizeof(effect))
        return 0;
    memcpy(&effect, data, sizeof(effect));
    if (!vpad_convert(&effect, &out))
        return 0;
    if (out.delay_ms != effect.replay.delay || out.length_ms != effect.replay.length)
        __builtin_trap();
    if (effect.type == FF_PERIODIC) {
        check_level(effect.u.periodic.envelope.attack_level, out.attack_level);
        check_level(effect.u.periodic.envelope.fade_level, out.fade_level);
    }
    return 0;
}
