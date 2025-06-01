/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "ts_vkeys"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/epoll.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

using android::base::GetProperty;
using android::base::GetUintProperty;
using android::base::Split;

#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define BITS_TO_LONGS(bits) (((bits) + BITS_PER_LONG - 1) / BITS_PER_LONG)

typedef struct vkey_key_info {
    __u16 key_code;
    bool is_pressed;
} vkey_key_info_t;

/*
 * Example properties:
 * "vendor.ts_vkeys.names" = "menu,home,back"
 * "vendor.ts_vkeys.menu.x" = "160"
 * "vendor.ts_vkeys.menu.y" = "1344"
 * "vendor.ts_vkeys.menu.key_code" = "139"
 * "vendor.ts_vkeys.home.x" = "360"
 * "vendor.ts_vkeys.home.y" = "1344"
 * "vendor.ts_vkeys.home.key_code" = "172"
 * "vendor.ts_vkeys.back.x" = "570"
 * "vendor.ts_vkeys.back.y" = "1344"
 * "vendor.ts_vkeys.back.key_code" = "158"
 */
static const std::string kPropPrefix = "vendor.ts_vkeys.";

static const struct uinput_setup usetup = {
        .id =
                {
                        .bustype = BUS_VIRTUAL,
                        .vendor = 0xCAFE,
                        .product = 0x0000,
                },
        .name = "ts_vkeys",
};

static std::unordered_map<__u16 /*x*/, std::unordered_map<__u16 /*y*/, vkey_key_info_t>> g_vkey_map;
static std::vector<__u16> g_vkey_key_codes;

static bool test_bit(size_t bit, unsigned long* array) {
    return (array[bit / BITS_PER_LONG] & (1UL << (bit % BITS_PER_LONG))) != 0;
}

static int setup_uinput_device() {
    int uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) {
        LOG(ERROR) << "Failed to open /dev/uinput";
        return -1;
    }

    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    for (const auto& key_code : g_vkey_key_codes) {
        ioctl(uinput_fd, UI_SET_KEYBIT, key_code);
    }

    if (ioctl(uinput_fd, UI_DEV_SETUP, usetup) < 0) {
        LOG(ERROR) << "ioctl(UI_DEV_SETUP) failed";
        close(uinput_fd);
        return -1;
    }

    if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
        LOG(ERROR) << "ioctl(UI_DEV_CREATE) failed";
        close(uinput_fd);
        return -1;
    }

    return uinput_fd;
}

static void handle_event(int uinput_fd, struct input_event* ev) {
    static __s32 x = 0, y = 0;

    __u16* type = &ev->type;
    __u16* code = &ev->code;
    __s32* value = &ev->value;

    if (*type != EV_ABS) return;

    switch (*code) {
        case ABS_MT_POSITION_X:
        case ABS_X:
            x = *value;
            break;
        case ABS_MT_POSITION_Y:
        case ABS_Y:
            y = *value;
            break;
        default:
            return;
    }

    if (g_vkey_map.contains(x) && g_vkey_map[x].contains(y)) {
        g_vkey_map[x][y].is_pressed = !g_vkey_map[x][y].is_pressed;
        struct input_event write_ev = {
                .type = EV_KEY,
                .code = g_vkey_map[x][y].key_code,
                .value = g_vkey_map[x][y].is_pressed,
        };
        if (write(uinput_fd, &write_ev, sizeof(write_ev)) < 0) {
            LOG(ERROR) << "write(uinput_fd) failed";
        }
    }
}

int main() {
    char buf[64];
    int fd, epoll_fd, uinput_fd;
    struct input_event ev;
    struct epoll_event event, events[10];

    // Parse properties
    std::vector<std::string> vkey_names = Split(GetProperty(kPropPrefix + "names", ""), ",");
    if (vkey_names.empty()) {
        LOG(ERROR) << "No virtual key specified";
        return EXIT_SUCCESS;
    }

    for (const auto& name : vkey_names) {
        auto x = GetUintProperty<__u16>(kPropPrefix + name + ".x", 0);
        auto y = GetUintProperty<__u16>(kPropPrefix + name + ".y", 0);
        auto key_code = GetUintProperty<__u16>(kPropPrefix + name + ".key_code", 0);
        if (!x || !y || !key_code) {
            LOG(ERROR) << "Virtual key " << name << " has missing properties";
            continue;
        }
        g_vkey_map[x][y].key_code = key_code;
        g_vkey_key_codes.push_back(key_code);
    }

    // Find the source input device
    for (int i = 0; i < 10; ++i) {
        unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];

        snprintf(buf, sizeof(buf), "/dev/input/event%d", i);
        fd = open(buf, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        if (!ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) && test_bit(EV_ABS, ev_bits)) {
            goto device_found;
        }

        close(fd);
    }

    LOG(ERROR) << "Device not found";
    return EXIT_SUCCESS;

device_found:
    LOG(INFO) << "Using device:" << std::string(buf);

    // Setup uinput device
    uinput_fd = setup_uinput_device();
    if (uinput_fd < 0) {
        LOG(ERROR) << "Failed to setup uinput device";
        goto err_setup_uinput;
    }

    // Setup epoll
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LOG(ERROR) << "epoll_create1() failed";
        goto err_epoll;
    }

    event.events = EPOLLIN;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
        LOG(ERROR) << "epoll_ctl() failed";
        goto err_epoll;
    }

    while (true) {
        if (epoll_wait(epoll_fd, events, 10, -1) > 0) {
            for (int i = 0; i < ret; ++i) {
                if (events[i].events & EPOLLIN) {
                    int rc = read(fd, &ev, sizeof(ev));
                    if (rc == sizeof(ev)) {
                        handle_event(uinput_fd, &ev);
                    } else if (rc < 0 && errno != EAGAIN) {
                        LOG(ERROR) << "read() failed";
                        break;
                    }
                }
            }
        } else {
            LOG(ERROR) << "epoll_wait() failed";
            break;
        }
    }

    close(epoll_fd);
err_epoll:
    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd);
err_setup_uinput:
    close(fd);
    return EXIT_FAILURE;
}
