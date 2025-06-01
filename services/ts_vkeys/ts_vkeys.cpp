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
} vkey_key_info_t;

typedef struct mt_slot {
    bool active;
    __s32 x;
    __s32 y;
    __u16 key_code;
} mt_slot_t;

/*
Example properties:
    setprop vendor.ts_vkeys.names menu,home,back
    setprop vendor.ts_vkeys.menu.x 160
    setprop vendor.ts_vkeys.menu.y 1344
    setprop vendor.ts_vkeys.menu.key_code 139
    setprop vendor.ts_vkeys.home.x 360
    setprop vendor.ts_vkeys.home.y 1344
    setprop vendor.ts_vkeys.home.key_code 172
    setprop vendor.ts_vkeys.back.x 570
    setprop vendor.ts_vkeys.back.y 1344
    setprop vendor.ts_vkeys.back.key_code 158
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

static int g_uinput_fd;
static std::unordered_map<__u16 /*slot*/, mt_slot_t> g_mt_slots;
static std::unordered_map<__u16 /*y*/, std::unordered_map<__u16 /*x*/, vkey_key_info_t>> g_vkey_map;
static std::vector<__u16> g_vkey_key_codes;

static bool test_bit(size_t bit, unsigned long* array) {
    return (array[bit / BITS_PER_LONG] & (1UL << (bit % BITS_PER_LONG))) != 0;
}

static void setup_uinput_device() {
    g_uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (g_uinput_fd < 0) {
        LOG(ERROR) << "Failed to open /dev/uinput";
        return;
    }

    ioctl(g_uinput_fd, UI_SET_EVBIT, EV_KEY);
    for (const auto& key_code : g_vkey_key_codes) {
        ioctl(g_uinput_fd, UI_SET_KEYBIT, key_code);
    }

    if (ioctl(g_uinput_fd, UI_DEV_SETUP, usetup) < 0) {
        LOG(ERROR) << "ioctl(UI_DEV_SETUP) failed";
        return;
    }

    if (ioctl(g_uinput_fd, UI_DEV_CREATE) < 0) {
        LOG(ERROR) << "ioctl(UI_DEV_CREATE) failed";
        return;
    }
}

static void send_input_event(__u16 type, __u16 code, __s32 value) {
    struct input_event ev = {
            .code = code,
            .type = type,
            .value = value,
    };
    if (write(g_uinput_fd, &ev, sizeof(ev)) < 0) {
        LOG(ERROR) << "write(g_uinput_fd) failed";
    }
}

static void report_key(__u16 code, __s32 value) {
    send_input_event(EV_KEY, code, value);
}

static void report_sync() {
    send_input_event(EV_SYN, SYN_REPORT, 0);
}

static void slot_make_active(__s32 slot) {
    g_mt_slots[slot].active = true;
}

static void slot_make_inactive(__s32 slot) {
    if (g_mt_slots.contains(slot) && g_mt_slots[slot].active) {
        if (g_mt_slots[slot].key_code) {
            report_key(g_mt_slots[slot].key_code, 0);
            report_sync();
            g_mt_slots[slot].key_code = 0;
        }
        g_mt_slots[slot].active = false;
    }
}

static void all_slots_make_inactive() {
    for (auto& s : g_mt_slots) {
        if (s.second.active) {
            if (s.second.key_code) {
                report_key(s.second.key_code, 0);
                report_sync();
                s.second.key_code = 0;
            }
            s.second.active = false;
        }
    }
}

static void all_slots_check_and_report_key() {
    for (auto& s : g_mt_slots) {
        if (!s.second.active) continue;
        if (g_vkey_map.contains(s.second.y) && g_vkey_map[s.second.y].contains(s.second.x)) {
            s.second.key_code = g_vkey_map[s.second.y][s.second.x].key_code;
            report_key(s.second.key_code, 1);
            report_sync();
        }
    }
}

static void handle_event(struct input_event* ev) {
    static __s32 slot = 0;

    __u16* type = &ev->type;
    __u16* code = &ev->code;
    __s32* value = &ev->value;

    switch (*type) {
        case EV_ABS:
            switch (*code) {
                case ABS_MT_SLOT:
                    slot = *value;
                    break;
                case ABS_MT_TRACKING_ID:
                    if (*value < 0) {
                        slot_make_inactive(slot);
                    } else {
                        slot_make_active(slot);
                    }
                    break;
                case ABS_MT_POSITION_X:
                case ABS_X:
                    g_mt_slots[slot].x = *value;
                    break;
                case ABS_MT_POSITION_Y:
                case ABS_Y:
                    g_mt_slots[slot].y = *value;
                    break;
                default:
                    break;
            }
            break;
        case EV_KEY:
            if (*code == BTN_TOUCH) {
                if (*value) {
                    // For supporting legacy touchscreen drivers that does not support MT Protocol B
                    slot_make_active(0);
                } else {
                    all_slots_make_inactive();
                }
            }
            break;
        case EV_SYN:
            if (*value == SYN_REPORT) all_slots_check_and_report_key();
            break;
        default:
            break;
    }
}

int main(int argc, char** argv) {
    int fd, epoll_fd;
    std::string tmp_str;
    std::vector<std::string> vkey_names;
    struct input_event ev;
    struct epoll_event event, events[10];

#if defined(__ANDROID_RECOVERY__)
    android::base::InitLogging(argv, &android::base::KernelLogger);
#endif

    // Parse properties
    tmp_str = GetProperty(kPropPrefix + "names", "");
    vkey_names = Split(tmp_str, ",");
    if (tmp_str.empty() || vkey_names.empty()) {
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
        g_vkey_map[y][x].key_code = key_code;
        g_vkey_key_codes.push_back(key_code);
    }

    // Find the source input device
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];

            tmp_str = "/dev/input/event" + std::to_string(j);
            fd = open(tmp_str.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;

            if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) >= 0 && test_bit(EV_ABS, ev_bits)) {
                goto device_found;
            }

            close(fd);
        }
        LOG(INFO) << "Sleep for 1 second to search for source input device again";
        sleep(1);
    }

    LOG(ERROR) << "Device not found";
    return EXIT_SUCCESS;

device_found:
    LOG(INFO) << "Using device: " << tmp_str;

    // Setup uinput device
    setup_uinput_device();
    if (g_uinput_fd < 0) {
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
        int epoll_ret = epoll_wait(epoll_fd, events, 10, -1);
        if (epoll_ret > 0) {
            for (int i = 0; i < epoll_ret; ++i) {
                if (events[i].events & EPOLLIN) {
                    int read_ret = read(fd, &ev, sizeof(ev));
                    if (read_ret == sizeof(ev)) {
                        handle_event(&ev);
                    } else if (read_ret < 0 && errno != EAGAIN) {
                        LOG(ERROR) << "read() failed";
                        break;
                    }
                }
            }
        } else if (epoll_ret < 0) {
            LOG(ERROR) << "epoll_wait() failed";
            break;
        }
    }

    close(epoll_fd);
err_epoll:
    ioctl(g_uinput_fd, UI_DEV_DESTROY);
    close(g_uinput_fd);
err_setup_uinput:
    close(fd);
    return EXIT_FAILURE;
}
