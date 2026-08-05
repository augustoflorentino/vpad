#include "vpad_internal.h"

#if defined(_WIN32)

#include <windows.h>
#include <setupapi.h>
#include <winioctl.h>

#include <string.h>

#define VPAD_VIGEM_IOCTL(index, access) CTL_CODE(FILE_DEVICE_BUS_EXTENDER, 0x801 + (index), METHOD_BUFFERED, access)
#define VPAD_IOCTL_PLUGIN VPAD_VIGEM_IOCTL(0x000, FILE_WRITE_DATA)
#define VPAD_IOCTL_UNPLUG VPAD_VIGEM_IOCTL(0x001, FILE_WRITE_DATA)
#define VPAD_IOCTL_CHECK_VERSION VPAD_VIGEM_IOCTL(0x002, FILE_WRITE_DATA)
#define VPAD_IOCTL_WAIT_READY VPAD_VIGEM_IOCTL(0x003, FILE_WRITE_DATA)
#define VPAD_IOCTL_XUSB_NOTIFICATION VPAD_VIGEM_IOCTL(0x200, FILE_WRITE_DATA | FILE_READ_DATA)
#define VPAD_IOCTL_XUSB_SUBMIT VPAD_VIGEM_IOCTL(0x201, FILE_WRITE_DATA)

#define VPAD_VIGEM_VERSION 0x0001
#define VPAD_VIGEM_XBOX360 0
#define VPAD_VIGEM_MAX_SERIAL 16

static const GUID vpad_vigem_guid = {0x96E42B22, 0xF5E9, 0x42F8, {0xB0, 0x43, 0xED, 0x0F, 0x93, 0x2F, 0x01, 0x4F}};

typedef struct {
    ULONG size;
    ULONG serial;
    LONG target_type;
    USHORT vendor;
    USHORT product;
} vpad_vigem_plugin_t;

typedef struct {
    ULONG size;
    ULONG value;
} vpad_vigem_pair_t;

typedef struct {
    USHORT buttons;
    BYTE left_trigger;
    BYTE right_trigger;
    SHORT thumb_lx;
    SHORT thumb_ly;
    SHORT thumb_rx;
    SHORT thumb_ry;
} vpad_xusb_report_t;

typedef struct {
    ULONG size;
    ULONG serial;
    vpad_xusb_report_t report;
} vpad_xusb_submit_t;

typedef struct {
    ULONG size;
    ULONG serial;
    UCHAR large_motor;
    UCHAR small_motor;
    UCHAR led_number;
} vpad_xusb_notification_t;

struct vpad_os {
    HANDLE bus;
    ULONG serial;
    OVERLAPPED ov;
    int pending;
    int playing;
    vpad_xusb_notification_t notification;
};

static const USHORT vpad_xusb_bits[VPAD_BTN_COUNT] = {0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010,
                                                      0x0400, 0x0040, 0x0080, 0x0001, 0x0002, 0x0004, 0x0008};

static int vpad_vigem_ioctl(HANDLE bus, DWORD code, void *data, DWORD data_len, DWORD out_len)
{
    OVERLAPPED ov;
    DWORD done = 0;
    BOOL ok;

    memset(&ov, 0, sizeof(ov));
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ov.hEvent == NULL)
        return 0;
    ok = DeviceIoControl(bus, code, data, data_len, out_len ? data : NULL, out_len, &done, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult(bus, &ov, &done, TRUE);
    CloseHandle(ov.hEvent);
    return ok != 0;
}

static HANDLE vpad_vigem_open_interface(HDEVINFO set, SP_DEVICE_INTERFACE_DATA *iface)
{
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
    DWORD size = 0;
    HANDLE bus = INVALID_HANDLE_VALUE;

    SetupDiGetDeviceInterfaceDetailW(set, iface, NULL, 0, &size, NULL);
    if (size == 0)
        return INVALID_HANDLE_VALUE;
    detail = HeapAlloc(GetProcessHeap(), 0, size);
    if (detail == NULL)
        return INVALID_HANDLE_VALUE;
    detail->cbSize = sizeof(*detail);
    if (SetupDiGetDeviceInterfaceDetailW(set, iface, detail, size, &size, NULL))
        bus = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH |
                                             FILE_FLAG_OVERLAPPED,
                          NULL);
    HeapFree(GetProcessHeap(), 0, detail);
    return bus;
}

static int vpad_vigem_version_ok(HANDLE bus)
{
    vpad_vigem_pair_t check;

    check.size = sizeof(check);
    check.value = VPAD_VIGEM_VERSION;
    return vpad_vigem_ioctl(bus, VPAD_IOCTL_CHECK_VERSION, &check, sizeof(check), 0);
}

static int vpad_vigem_open_bus(HANDLE *out_bus)
{
    SP_DEVICE_INTERFACE_DATA iface;
    HDEVINFO set;
    HANDLE bus;
    DWORD i;
    int status = VPAD_ERR_NODEVICE;

    set = SetupDiGetClassDevsW(&vpad_vigem_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return VPAD_ERR_NODEVICE;
    iface.cbSize = sizeof(iface);
    for (i = 0; SetupDiEnumDeviceInterfaces(set, NULL, &vpad_vigem_guid, i, &iface); i++) {
        bus = vpad_vigem_open_interface(set, &iface);
        if (bus == INVALID_HANDLE_VALUE) {
            status = GetLastError() == ERROR_ACCESS_DENIED ? VPAD_ERR_ACCESS : status;
            continue;
        }
        if (vpad_vigem_version_ok(bus)) {
            *out_bus = bus;
            SetupDiDestroyDeviceInfoList(set);
            return VPAD_OK;
        }
        CloseHandle(bus);
    }
    SetupDiDestroyDeviceInfoList(set);
    return status;
}

static int vpad_vigem_plugin(vpad_t *pad)
{
    vpad_vigem_plugin_t plugin;
    vpad_vigem_pair_t ready;
    ULONG serial;

    for (serial = 1; serial <= VPAD_VIGEM_MAX_SERIAL; serial++) {
        memset(&plugin, 0, sizeof(plugin));
        plugin.size = sizeof(plugin);
        plugin.serial = serial;
        plugin.target_type = VPAD_VIGEM_XBOX360;
        plugin.vendor = pad->vendor;
        plugin.product = pad->product;
        if (!vpad_vigem_ioctl(pad->os->bus, VPAD_IOCTL_PLUGIN, &plugin, sizeof(plugin), 0))
            continue;
        pad->os->serial = serial;
        ready.size = sizeof(ready);
        ready.value = serial;
        vpad_vigem_ioctl(pad->os->bus, VPAD_IOCTL_WAIT_READY, &ready, sizeof(ready), 0);
        return VPAD_OK;
    }
    return VPAD_ERR_IO;
}

static void vpad_vigem_request(vpad_t *pad)
{
    struct vpad_os *os = pad->os;
    BOOL ok;

    memset(&os->notification, 0, sizeof(os->notification));
    os->notification.size = sizeof(os->notification);
    os->notification.serial = os->serial;
    ResetEvent(os->ov.hEvent);
    ok = DeviceIoControl(os->bus, VPAD_IOCTL_XUSB_NOTIFICATION, &os->notification, sizeof(os->notification),
                         &os->notification, sizeof(os->notification), NULL, &os->ov);
    os->pending = ok || GetLastError() == ERROR_IO_PENDING;
}

int vpad_os_open(vpad_t *pad)
{
    int status;

    pad->os = vpad_mem_alloc(&pad->mem, sizeof(*pad->os));
    if (pad->os == NULL)
        return VPAD_ERR_NOMEM;
    memset(pad->os, 0, sizeof(*pad->os));
    pad->os->ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    status = pad->os->ov.hEvent ? vpad_vigem_open_bus(&pad->os->bus) : VPAD_ERR_IO;
    if (status == VPAD_OK)
        status = vpad_vigem_plugin(pad);
    if (status != VPAD_OK) {
        if (pad->os->bus != NULL)
            CloseHandle(pad->os->bus);
        if (pad->os->ov.hEvent != NULL)
            CloseHandle(pad->os->ov.hEvent);
        vpad_mem_free(&pad->mem, pad->os);
        pad->os = NULL;
        return status;
    }
    vpad_vigem_request(pad);
    return VPAD_OK;
}

void vpad_os_close(vpad_t *pad)
{
    struct vpad_os *os = pad->os;
    vpad_vigem_pair_t unplug;
    DWORD done;

    if (os == NULL)
        return;
    if (os->pending) {
        CancelIoEx(os->bus, &os->ov);
        GetOverlappedResult(os->bus, &os->ov, &done, TRUE);
    }
    unplug.size = sizeof(unplug);
    unplug.value = os->serial;
    vpad_vigem_ioctl(os->bus, VPAD_IOCTL_UNPLUG, &unplug, sizeof(unplug), 0);
    CloseHandle(os->bus);
    CloseHandle(os->ov.hEvent);
    vpad_mem_free(&pad->mem, os);
    pad->os = NULL;
}

static SHORT vpad_flip(int32_t value)
{
    return (SHORT)(-1 - value);
}

int vpad_os_send(vpad_t *pad)
{
    vpad_xusb_submit_t submit;
    int i;

    if (memcmp(pad->buttons, pad->sent_buttons, sizeof(pad->buttons)) == 0 &&
        memcmp(pad->axes, pad->sent_axes, sizeof(pad->axes)) == 0)
        return VPAD_OK;
    memset(&submit, 0, sizeof(submit));
    submit.size = sizeof(submit);
    submit.serial = pad->os->serial;
    for (i = 0; i < VPAD_BTN_COUNT; i++)
        if (pad->buttons[i])
            submit.report.buttons |= vpad_xusb_bits[i];
    submit.report.thumb_lx = (SHORT)pad->axes[VPAD_AXIS_LX];
    submit.report.thumb_ly = vpad_flip(pad->axes[VPAD_AXIS_LY]);
    submit.report.thumb_rx = (SHORT)pad->axes[VPAD_AXIS_RX];
    submit.report.thumb_ry = vpad_flip(pad->axes[VPAD_AXIS_RY]);
    submit.report.left_trigger = (BYTE)pad->axes[VPAD_AXIS_LT];
    submit.report.right_trigger = (BYTE)pad->axes[VPAD_AXIS_RT];
    if (!vpad_vigem_ioctl(pad->os->bus, VPAD_IOCTL_XUSB_SUBMIT, &submit, sizeof(submit), 0))
        return VPAD_ERR_IO;
    vpad_mark_sent(pad);
    return VPAD_OK;
}

static void vpad_vigem_rumble(vpad_t *pad, UCHAR large, UCHAR small)
{
    vpad_event_t event;

    memset(&event, 0, sizeof(event));
    event.kind = VPAD_EV_UPLOAD;
    event.effect.type = VPAD_RUMBLE;
    event.effect.strong = (uint16_t)(large * 257);
    event.effect.weak = (uint16_t)(small * 257);
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
    struct vpad_os *os = pad->os;
    DWORD done = 0;

    if (!os->pending || VPAD_QUEUE_LEN - pad->queue_len < 2 || !HasOverlappedIoCompleted(&os->ov))
        return VPAD_OK;
    os->pending = 0;
    if (!GetOverlappedResult(os->bus, &os->ov, &done, FALSE))
        return VPAD_ERR_IO;
    vpad_vigem_rumble(pad, os->notification.large_motor, os->notification.small_motor);
    vpad_vigem_request(pad);
    return VPAD_OK;
}

vpad_handle_t vpad_os_handle(const vpad_t *pad)
{
    return pad->os->ov.hEvent;
}

const char *vpad_os_backend(void)
{
    return "vigembus";
}

#endif
