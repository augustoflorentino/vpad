#include "vpad_internal.h"

#if defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDUserDevice.h>
#include <dispatch/dispatch.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define VPAD_MAC_INPUT_ID 1
#define VPAD_MAC_OUTPUT_ID 2
#define VPAD_MAC_INPUT_LEN 13

struct vpad_os {
    IOHIDUserDeviceRef device;
    dispatch_queue_t queue;
    dispatch_semaphore_t cancelled;
    int pipe_fds[2];
    int playing;
};

static const uint8_t vpad_mac_descriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, VPAD_MAC_INPUT_ID, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0b, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x0b, 0x81, 0x02, 0x75, 0x01, 0x95, 0x01, 0x81, 0x03, 0x05, 0x01,
    0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3b, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95,
    0x01, 0x81, 0x42, 0x65, 0x00, 0x45, 0x00, 0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09, 0x34, 0x16,
    0x00, 0x80, 0x26, 0xff, 0x7f, 0x75, 0x10, 0x95, 0x04, 0x81, 0x02, 0x09, 0x32, 0x09, 0x35, 0x15,
    0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02, 0x85, VPAD_MAC_OUTPUT_ID, 0x06, 0x00, 0xff, 0x09,
    0x01, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x02, 0x91, 0x02, 0xc0};

static void vpad_mac_set_number(CFMutableDictionaryRef dict, CFStringRef key, int value)
{
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);

    if (number == NULL)
        return;
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

static CFDictionaryRef vpad_mac_properties(const vpad_t *pad)
{
    CFMutableDictionaryRef dict;
    CFDataRef descriptor;
    CFStringRef name;

    dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                     &kCFTypeDictionaryValueCallBacks);
    if (dict == NULL)
        return NULL;
    descriptor = CFDataCreate(kCFAllocatorDefault, vpad_mac_descriptor, sizeof(vpad_mac_descriptor));
    name = CFStringCreateWithCString(kCFAllocatorDefault, pad->name, kCFStringEncodingUTF8);
    if (descriptor == NULL || name == NULL) {
        if (descriptor != NULL)
            CFRelease(descriptor);
        if (name != NULL)
            CFRelease(name);
        CFRelease(dict);
        return NULL;
    }
    CFDictionarySetValue(dict, CFSTR(kIOHIDReportDescriptorKey), descriptor);
    CFDictionarySetValue(dict, CFSTR(kIOHIDProductKey), name);
    CFDictionarySetValue(dict, CFSTR(kIOHIDTransportKey), CFSTR("Virtual"));
    vpad_mac_set_number(dict, CFSTR(kIOHIDVendorIDKey), pad->vendor);
    vpad_mac_set_number(dict, CFSTR(kIOHIDProductIDKey), pad->product);
    CFRelease(descriptor);
    CFRelease(name);
    return dict;
}

static int vpad_mac_pipe(int fds[2])
{
    int i;

    if (pipe(fds) != 0)
        return VPAD_ERR_IO;
    for (i = 0; i < 2; i++) {
        fcntl(fds[i], F_SETFD, FD_CLOEXEC);
        fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
    }
    return VPAD_OK;
}

static void vpad_mac_register(struct vpad_os *os)
{
    int write_fd = os->pipe_fds[1];
    dispatch_semaphore_t cancelled = os->cancelled;

    IOHIDUserDeviceRegisterSetReportBlock(os->device, ^IOReturn(IOHIDReportType type, uint32_t report_id,
                                                                  const uint8_t *report, CFIndex report_len) {
        uint8_t motors[2];

        if (type != kIOHIDReportTypeOutput || report_id != VPAD_MAC_OUTPUT_ID || report_len < 2)
            return kIOReturnUnsupported;
        motors[0] = report[report_len - 2];
        motors[1] = report[report_len - 1];
        if (write(write_fd, motors, sizeof(motors)) != (ssize_t)sizeof(motors))
            return kIOReturnNoSpace;
        return kIOReturnSuccess;
    });
    IOHIDUserDeviceSetCancelHandler(os->device, ^{
        dispatch_semaphore_signal(cancelled);
    });
    IOHIDUserDeviceSetDispatchQueue(os->device, os->queue);
    IOHIDUserDeviceActivate(os->device);
}

static void vpad_mac_release(vpad_t *pad, struct vpad_os *os)
{
    if (os->device != NULL)
        CFRelease(os->device);
    if (os->queue != NULL)
        dispatch_release(os->queue);
    if (os->cancelled != NULL)
        dispatch_release(os->cancelled);
    if (os->pipe_fds[0] >= 0)
        close(os->pipe_fds[0]);
    if (os->pipe_fds[1] >= 0)
        close(os->pipe_fds[1]);
    vpad_mem_free(&pad->mem, os);
}

int vpad_os_open(vpad_t *pad)
{
    struct vpad_os *os;
    CFDictionaryRef properties;

    if (pad->kind == VPAD_DS4)
        return VPAD_ERR_UNSUPPORTED;
    os = vpad_mem_alloc(&pad->mem, sizeof(*pad->os));
    if (os == NULL)
        return VPAD_ERR_NOMEM;
    memset(os, 0, sizeof(*os));
    os->pipe_fds[0] = -1;
    os->pipe_fds[1] = -1;
    properties = vpad_mac_properties(pad);
    if (properties == NULL || vpad_mac_pipe(os->pipe_fds) != VPAD_OK) {
        if (properties != NULL)
            CFRelease(properties);
        vpad_mac_release(pad, os);
        return VPAD_ERR_NOMEM;
    }
    os->device = IOHIDUserDeviceCreateWithProperties(kCFAllocatorDefault, properties, 0);
    CFRelease(properties);
    os->queue = dispatch_queue_create("vpad", DISPATCH_QUEUE_SERIAL);
    os->cancelled = dispatch_semaphore_create(0);
    if (os->device == NULL || os->queue == NULL || os->cancelled == NULL) {
        int status = os->device == NULL ? VPAD_ERR_ACCESS : VPAD_ERR_NOMEM;

        vpad_mac_release(pad, os);
        return status;
    }
    vpad_mac_register(os);
    pad->os = os;
    return VPAD_OK;
}

void vpad_os_close(vpad_t *pad)
{
    struct vpad_os *os = pad->os;

    if (os == NULL)
        return;
    IOHIDUserDeviceCancel(os->device);
    dispatch_semaphore_wait(os->cancelled, DISPATCH_TIME_FOREVER);
    vpad_mac_release(pad, os);
    pad->os = NULL;
}

static uint8_t vpad_mac_hat(const uint8_t *buttons)
{
    static const uint8_t hats[3][3] = {{7, 0, 1}, {6, 8, 2}, {5, 4, 3}};
    int x;
    int y;

    vpad_hat(buttons, &x, &y);
    return hats[y + 1][x + 1];
}

static void vpad_mac_put16(uint8_t *at, int32_t value)
{
    uint16_t bits = (uint16_t)(int16_t)value;

    at[0] = (uint8_t)(bits & 0xff);
    at[1] = (uint8_t)(bits >> 8);
}

int vpad_os_send(vpad_t *pad)
{
    uint8_t report[VPAD_MAC_INPUT_LEN];
    int i;

    if (memcmp(pad->buttons, pad->sent_buttons, sizeof(pad->buttons)) == 0 &&
        memcmp(pad->axes, pad->sent_axes, sizeof(pad->axes)) == 0)
        return VPAD_OK;
    memset(report, 0, sizeof(report));
    report[0] = VPAD_MAC_INPUT_ID;
    for (i = 0; i <= VPAD_BTN_RSTICK; i++)
        if (pad->buttons[i])
            report[1 + i / 8] |= (uint8_t)(1u << (i % 8));
    report[2] |= (uint8_t)(vpad_mac_hat(pad->buttons) << 4);
    for (i = 0; i < 4; i++)
        vpad_mac_put16(report + 3 + i * 2, pad->axes[i]);
    report[11] = (uint8_t)pad->axes[VPAD_AXIS_LT];
    report[12] = (uint8_t)pad->axes[VPAD_AXIS_RT];
    if (IOHIDUserDeviceHandleReportWithTimeStamp(pad->os->device, 0, report, sizeof(report)) != kIOReturnSuccess)
        return VPAD_ERR_IO;
    vpad_mark_sent(pad);
    return VPAD_OK;
}

static void vpad_mac_rumble(vpad_t *pad, const uint8_t *motors)
{
    vpad_event_t event;

    memset(&event, 0, sizeof(event));
    event.kind = VPAD_EV_UPLOAD;
    event.effect.type = VPAD_RUMBLE;
    event.effect.strong = (uint16_t)(motors[0] * 257);
    event.effect.weak = (uint16_t)(motors[1] * 257);
    vpad_queue_push(pad, &event);
    if (pad->os->playing)
        return;
    memset(&event, 0, sizeof(event));
    event.kind = VPAD_EV_PLAY;
    event.value = 1;
    vpad_queue_push(pad, &event);
    pad->os->playing = 1;
}

int vpad_os_poll(vpad_t *pad)
{
    uint8_t motors[2];
    ssize_t n;

    while (VPAD_QUEUE_LEN - pad->queue_len >= 2 || (pad->flags & VPAD_NO_FF)) {
        n = read(pad->os->pipe_fds[0], motors, sizeof(motors));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return VPAD_OK;
        if (n != (ssize_t)sizeof(motors))
            return VPAD_ERR_IO;
        if (!(pad->flags & VPAD_NO_FF))
            vpad_mac_rumble(pad, motors);
    }
    return VPAD_OK;
}

vpad_handle_t vpad_os_handle(const vpad_t *pad)
{
    return pad->os->pipe_fds[0];
}

const char *vpad_os_backend(void)
{
    return "iohiduserdevice";
}

#endif
