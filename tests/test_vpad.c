#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "vpad/vpad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                    \
        }                                                               \
    } while (0)

#define SKIP 77

typedef struct {
    int remaining;
    int live;
} failing_alloc_t;

static void *failing_alloc(void *ctx, size_t size)
{
    failing_alloc_t *fa = ctx;

    if (fa->remaining-- == 0)
        return NULL;
    fa->live++;
    return malloc(size);
}

static void failing_free(void *ctx, void *ptr)
{
    failing_alloc_t *fa = ctx;

    fa->live--;
    free(ptr);
}

static int device_unavailable(int status)
{
    return status == VPAD_ERR_UNSUPPORTED || status == VPAD_ERR_NODEVICE || status == VPAD_ERR_ACCESS;
}

static void test_bad_args(void)
{
    char long_name[100];
    vpad_t *pad = NULL;
    vpad_event_t event;

    memset(long_name, 'n', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    CHECK(vpad_create(VPAD_XBOX360, NULL, 0, 0, NULL, NULL, NULL, NULL) == VPAD_ERR_ARG);
    CHECK(vpad_create(7, NULL, 0, 0, NULL, NULL, NULL, &pad) == VPAD_ERR_ARG);
    CHECK(vpad_create(VPAD_GENERIC, "", 0, 0, NULL, NULL, NULL, &pad) == VPAD_ERR_ARG);
    CHECK(vpad_create(VPAD_GENERIC, long_name, 0, 0, NULL, NULL, NULL, &pad) == VPAD_ERR_ARG);
    CHECK(vpad_create(VPAD_GENERIC, NULL, 0, 0, failing_alloc, NULL, NULL, &pad) == VPAD_ERR_ARG);
    CHECK(vpad_create(VPAD_GENERIC, NULL, 0, 0, NULL, failing_free, NULL, &pad) == VPAD_ERR_ARG);
    CHECK(pad == NULL);
    CHECK(vpad_set_button(NULL, VPAD_BTN_A, 1) == VPAD_ERR_ARG);
    CHECK(vpad_set_axis(NULL, VPAD_AXIS_LX, 0) == VPAD_ERR_ARG);
    CHECK(vpad_send(NULL) == VPAD_ERR_ARG);
    CHECK(vpad_neutral(NULL) == VPAD_ERR_ARG);
    CHECK(vpad_next_event(NULL, &event) == VPAD_ERR_ARG);
    CHECK(vpad_handle(NULL) == VPAD_NO_HANDLE);
    CHECK(vpad_backend() != NULL);
    CHECK(strcmp(vpad_strerror(VPAD_ERR_NODEVICE), vpad_strerror(-1)) != 0);
    vpad_destroy(NULL);
}

#if defined(__linux__)

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define BIT_SET(bits, n) ((bits)[(n) / 8] >> ((n) % 8) & 1)

static void pause_ms(long ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int read_line(const char *path, char *out, size_t out_len)
{
    FILE *f = fopen(path, "r");
    size_t n;

    if (f == NULL)
        return 0;
    n = fread(out, 1, out_len - 1, f);
    fclose(f);
    out[n] = '\0';
    if (n > 0 && out[n - 1] == '\n')
        out[n - 1] = '\0';
    return 1;
}

static int find_node(const char *name, char *out_path, size_t out_len)
{
    DIR *dir = opendir("/sys/class/input");
    struct dirent *entry;
    char path[512];
    char found[256];
    int ok = 0;

    if (dir == NULL)
        return 0;
    while (!ok && (entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;
        snprintf(path, sizeof(path), "/sys/class/input/%s/device/name", entry->d_name);
        if (read_line(path, found, sizeof(found)) && strcmp(found, name) == 0) {
            snprintf(out_path, out_len, "/dev/input/%s", entry->d_name);
            ok = 1;
        }
    }
    closedir(dir);
    return ok;
}

static int open_node(const char *name, int flags)
{
    char path[64];
    int fd = -1;
    int i;

    for (i = 0; i < 200 && fd < 0; i++) {
        if (find_node(name, path, sizeof(path)))
            fd = open(path, flags | O_CLOEXEC);
        if (fd < 0)
            pause_ms(10);
    }
    return fd;
}

static int read_report(int fd, struct input_event *events, int cap)
{
    struct pollfd pfd = {fd, POLLIN, 0};
    int count = 0;

    while (count < cap && poll(&pfd, 1, 1000) > 0) {
        if (read(fd, &events[count], sizeof(events[0])) != (ssize_t)sizeof(events[0]))
            continue;
        if (events[count].type == EV_SYN)
            return count;
        if (events[count].type == EV_KEY || events[count].type == EV_ABS)
            count++;
    }
    return -1;
}

static int report_has(const struct input_event *events, int count, int type, int code, int value)
{
    int i;

    for (i = 0; i < count; i++)
        if (events[i].type == type && events[i].code == code && events[i].value == value)
            return 1;
    return 0;
}

static int nothing_pending(int fd)
{
    struct pollfd pfd = {fd, POLLIN, 0};

    return poll(&pfd, 1, 100) == 0;
}

static void check_identity(int fd, const char *name, int vendor, int product)
{
    unsigned char keys[KEY_MAX / 8 + 1];
    unsigned char abs_bits[ABS_MAX / 8 + 1];
    unsigned char ff_bits[FF_MAX / 8 + 1];
    static const int key_list[] = {BTN_A,      BTN_B,     BTN_X,    BTN_Y,      BTN_TL,    BTN_TR,
                                   BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
    struct input_absinfo info;
    struct input_id id;
    char got[256];
    int effects = 0;
    size_t i;

    CHECK(ioctl(fd, EVIOCGNAME(sizeof(got)), got) > 0);
    CHECK(strcmp(got, name) == 0);
    CHECK(ioctl(fd, EVIOCGID, &id) == 0);
    CHECK(id.bustype == BUS_USB && id.vendor == vendor && id.product == product);
    memset(keys, 0, sizeof(keys));
    memset(abs_bits, 0, sizeof(abs_bits));
    memset(ff_bits, 0, sizeof(ff_bits));
    CHECK(ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0);
    CHECK(ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) >= 0);
    CHECK(ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff_bits)), ff_bits) >= 0);
    for (i = 0; i < sizeof(key_list) / sizeof(key_list[0]); i++)
        CHECK(BIT_SET(keys, key_list[i]));
    CHECK(BIT_SET(abs_bits, ABS_X) && BIT_SET(abs_bits, ABS_Y) && BIT_SET(abs_bits, ABS_RX));
    CHECK(BIT_SET(abs_bits, ABS_RY) && BIT_SET(abs_bits, ABS_Z) && BIT_SET(abs_bits, ABS_RZ));
    CHECK(BIT_SET(abs_bits, ABS_HAT0X) && BIT_SET(abs_bits, ABS_HAT0Y));
    CHECK(BIT_SET(ff_bits, FF_RUMBLE) && BIT_SET(ff_bits, FF_PERIODIC) && BIT_SET(ff_bits, FF_GAIN));
    CHECK(ioctl(fd, EVIOCGABS(ABS_X), &info) == 0 && info.minimum == -32768 && info.maximum == 32767);
    CHECK(ioctl(fd, EVIOCGABS(ABS_RZ), &info) == 0 && info.minimum == 0 && info.maximum == 255);
    CHECK(ioctl(fd, EVIOCGABS(ABS_HAT0Y), &info) == 0 && info.minimum == -1 && info.maximum == 1);
    CHECK(ioctl(fd, EVIOCGEFFECTS, &effects) == 0 && effects == 16);
}

static void test_identity(void)
{
    char name[64];
    vpad_t *pad = NULL;
    int fd;

    CHECK(vpad_create(VPAD_XBOX360, NULL, 0, 0, NULL, NULL, NULL, &pad) == VPAD_OK);
    CHECK(strcmp(vpad_backend(), "uinput") == 0);
    vpad_destroy(pad);
    snprintf(name, sizeof(name), "vpad test 360 %ld", (long)getpid());
    CHECK(vpad_create(VPAD_XBOX360, name, 0, 0, NULL, NULL, NULL, &pad) == VPAD_OK);
    fd = open_node(name, O_RDONLY);
    CHECK(fd >= 0);
    check_identity(fd, name, 0x045e, 0x028e);
    close(fd);
    vpad_destroy(pad);
    snprintf(name, sizeof(name), "vpad test custom %ld", (long)getpid());
    CHECK(vpad_create(VPAD_GENERIC, name, 0x1234, 0xabcd, NULL, NULL, NULL, &pad) == VPAD_OK);
    fd = open_node(name, O_RDONLY);
    CHECK(fd >= 0);
    check_identity(fd, name, 0x1234, 0xabcd);
    close(fd);
    vpad_destroy(pad);
}

static void test_reports(void)
{
    struct input_event events[32];
    char name[64];
    vpad_t *pad = NULL;
    int fd;
    int n;

    snprintf(name, sizeof(name), "vpad test report %ld", (long)getpid());
    CHECK(vpad_create(VPAD_XBOX360, name, 0, 0, NULL, NULL, NULL, &pad) == VPAD_OK);
    fd = open_node(name, O_RDONLY | O_NONBLOCK);
    CHECK(fd >= 0);
    CHECK(vpad_set_button(pad, VPAD_BTN_A, 1) == VPAD_OK);
    CHECK(vpad_set_button(pad, VPAD_BTN_GUIDE, 1) == VPAD_OK);
    CHECK(vpad_set_button(pad, VPAD_BTN_DPAD_UP, 1) == VPAD_OK);
    CHECK(vpad_set_axis(pad, VPAD_AXIS_LX, -32768) == VPAD_OK);
    CHECK(vpad_set_axis(pad, VPAD_AXIS_RY, 32767) == VPAD_OK);
    CHECK(vpad_set_axis(pad, VPAD_AXIS_LT, 200) == VPAD_OK);
    CHECK(vpad_send(pad) == VPAD_OK);
    n = read_report(fd, events, 32);
    CHECK(n == 6);
    CHECK(report_has(events, n, EV_KEY, BTN_A, 1) && report_has(events, n, EV_KEY, BTN_MODE, 1));
    CHECK(report_has(events, n, EV_ABS, ABS_HAT0Y, -1) && report_has(events, n, EV_ABS, ABS_X, -32768));
    CHECK(report_has(events, n, EV_ABS, ABS_RY, 32767) && report_has(events, n, EV_ABS, ABS_Z, 200));
    CHECK(vpad_send(pad) == VPAD_OK);
    CHECK(nothing_pending(fd));
    CHECK(vpad_set_button(pad, VPAD_BTN_DPAD_DOWN, 1) == VPAD_OK);
    CHECK(vpad_set_button(pad, VPAD_BTN_DPAD_LEFT, 1) == VPAD_OK);
    CHECK(vpad_set_button(pad, VPAD_BTN_DPAD_RIGHT, 1) == VPAD_OK);
    CHECK(vpad_send(pad) == VPAD_OK);
    n = read_report(fd, events, 32);
    CHECK(n == 1 && report_has(events, n, EV_ABS, ABS_HAT0Y, 0));
    CHECK(vpad_set_axis(pad, VPAD_AXIS_LT, 256) == VPAD_ERR_ARG);
    CHECK(vpad_set_axis(pad, VPAD_AXIS_RT, -1) == VPAD_ERR_ARG);
    CHECK(vpad_set_axis(pad, VPAD_AXIS_LX, 32768) == VPAD_ERR_ARG);
    CHECK(vpad_set_axis(pad, VPAD_AXIS_COUNT, 0) == VPAD_ERR_ARG);
    CHECK(vpad_set_button(pad, VPAD_BTN_COUNT, 1) == VPAD_ERR_ARG);
    CHECK(vpad_neutral(pad) == VPAD_OK);
    n = read_report(fd, events, 32);
    CHECK(n == 5);
    CHECK(report_has(events, n, EV_KEY, BTN_A, 0) && report_has(events, n, EV_KEY, BTN_MODE, 0));
    CHECK(report_has(events, n, EV_ABS, ABS_X, 0) && report_has(events, n, EV_ABS, ABS_Z, 0));
    CHECK(report_has(events, n, EV_ABS, ABS_RY, 0));
    close(fd);
    vpad_destroy(pad);
}

typedef struct {
    const char *name;
    int rumble_id;
    int periodic_id;
    int ok;
} game_t;

static void game_write(int fd, int code, int value)
{
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = EV_FF;
    ev.code = (unsigned short)code;
    ev.value = value;
    CHECK(write(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev));
}

static void *game_thread(void *arg)
{
    game_t *game = arg;
    struct ff_effect effect;
    int fd = open_node(game->name, O_RDWR);

    CHECK(fd >= 0);
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_RUMBLE;
    effect.id = -1;
    effect.u.rumble.strong_magnitude = 0x8000;
    effect.u.rumble.weak_magnitude = 0x4000;
    effect.replay.length = 500;
    effect.replay.delay = 20;
    CHECK(ioctl(fd, EVIOCSFF, &effect) == 0);
    game->rumble_id = effect.id;
    game_write(fd, effect.id, 3);
    game_write(fd, effect.id, 0);
    game_write(fd, FF_GAIN, 0x6000);
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_PERIODIC;
    effect.id = -1;
    effect.u.periodic.waveform = FF_SINE;
    effect.u.periodic.period = 100;
    effect.u.periodic.magnitude = -0x2000;
    effect.u.periodic.envelope.attack_length = 50;
    effect.u.periodic.envelope.attack_level = 0x1000;
    effect.u.periodic.envelope.fade_length = 70;
    effect.u.periodic.envelope.fade_level = 0x7fff;
    effect.replay.length = 300;
    CHECK(ioctl(fd, EVIOCSFF, &effect) == 0);
    game->periodic_id = effect.id;
    CHECK(ioctl(fd, EVIOCRMFF, game->rumble_id) == 0);
    close(fd);
    game->ok = 1;
    return NULL;
}

static int collect_events(vpad_t *pad, vpad_event_t *events, int want)
{
    struct pollfd pfd = {vpad_handle(pad), POLLIN, 0};
    int count = 0;
    int status;

    while (count < want) {
        status = vpad_next_event(pad, &events[count]);
        if (status == VPAD_OK) {
            count++;
            continue;
        }
        CHECK(status == VPAD_ERR_AGAIN);
        if (poll(&pfd, 1, 5000) <= 0)
            break;
    }
    return count;
}

static void test_force_feedback(void)
{
    vpad_event_t events[16];
    pthread_t thread;
    game_t game;
    char name[64];
    vpad_t *pad = NULL;
    vpad_event_t extra;
    int n;

    snprintf(name, sizeof(name), "vpad test ff %ld", (long)getpid());
    CHECK(vpad_create(VPAD_XBOX360, name, 0, 0, NULL, NULL, NULL, &pad) == VPAD_OK);
    CHECK(vpad_next_event(pad, &extra) == VPAD_ERR_AGAIN);
    memset(&game, 0, sizeof(game));
    game.name = name;
    CHECK(pthread_create(&thread, NULL, game_thread, &game) == 0);
    n = collect_events(pad, events, 9);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(game.ok && n == 9);
    CHECK(events[0].kind == VPAD_EV_UPLOAD && events[0].slot == game.rumble_id);
    CHECK(events[0].effect.type == VPAD_RUMBLE && events[0].effect.strong == 0x8000);
    CHECK(events[0].effect.weak == 0x4000 && events[0].effect.length_ms == 500 && events[0].effect.delay_ms == 20);
    CHECK(events[1].kind == VPAD_EV_PLAY && events[1].slot == game.rumble_id && events[1].value == 3);
    CHECK(events[2].kind == VPAD_EV_PLAY && events[2].slot == game.rumble_id && events[2].value == 0);
    CHECK(events[3].kind == VPAD_EV_GAIN && events[3].value == 0x6000);
    CHECK(events[4].kind == VPAD_EV_UPLOAD && events[4].slot == game.periodic_id);
    CHECK(events[4].effect.type == VPAD_PERIODIC && events[4].effect.level == -0x2000);
    CHECK(events[4].effect.attack_ms == 50 && events[4].effect.attack_level == 0x2000);
    CHECK(events[4].effect.fade_ms == 70 && events[4].effect.fade_level == 0xfffe);
    CHECK(events[4].effect.length_ms == 300);
    CHECK(events[5].kind == VPAD_EV_PLAY && events[5].slot == game.rumble_id && events[5].value == 0);
    CHECK(events[6].kind == VPAD_EV_ERASE && events[6].slot == game.rumble_id);
    CHECK(events[7].kind == VPAD_EV_PLAY && events[7].slot == game.periodic_id && events[7].value == 0);
    CHECK(events[8].kind == VPAD_EV_ERASE && events[8].slot == game.periodic_id);
    CHECK(vpad_next_event(pad, &extra) == VPAD_ERR_AGAIN);
    vpad_destroy(pad);
}

static void test_alloc_failure(void)
{
    vpad_t *pad;
    int n;

    for (n = 0;; n++) {
        failing_alloc_t fa = {n, 0};
        int status;

        pad = NULL;
        status = vpad_create(VPAD_GENERIC, NULL, 0, 0, failing_alloc, failing_free, &fa, &pad);
        if (status == VPAD_OK) {
            CHECK(vpad_send(pad) == VPAD_OK);
            vpad_destroy(pad);
        }
        CHECK(fa.live == 0);
        if (status == VPAD_OK)
            break;
        CHECK(status == VPAD_ERR_NOMEM && pad == NULL);
    }
    CHECK(n == 2);
}

#endif

int main(void)
{
    vpad_t *pad = NULL;
    int status;

    test_bad_args();
    status = vpad_create(VPAD_XBOX360, NULL, 0, 0, NULL, NULL, NULL, &pad);
    vpad_destroy(pad);
    if (device_unavailable(status)) {
        printf("skipping device tests (%s backend): %s\n", vpad_backend(), vpad_strerror(status));
        return SKIP;
    }
    CHECK(status == VPAD_OK);
#if defined(__linux__)
    test_identity();
    test_reports();
    test_force_feedback();
    test_alloc_failure();
#endif
    return 0;
}
