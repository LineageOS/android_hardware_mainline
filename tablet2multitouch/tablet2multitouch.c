/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <libtablet2multitouch.h>

#define LOG_TAG "tablet2multitouch"

#ifdef DEBUG
#define LOG_ERROR(...) fprintf(stderr, LOG_TAG ": " __VA_ARGS__)
#define LOG_INFO(...) fprintf(stdout, LOG_TAG ": " __VA_ARGS__)
#else
#include <cutils/klog.h>
#define LOG_ERROR(...) KLOG_ERROR(LOG_TAG, __VA_ARGS__)
#define LOG_INFO(...) KLOG_INFO(LOG_TAG, __VA_ARGS__)
#endif

#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define BITS_TO_LONGS(bits) (((bits) + BITS_PER_LONG - 1) / BITS_PER_LONG)

static bool test_bit(size_t bit, unsigned long* array) {
    return (array[bit / BITS_PER_LONG] & (1UL << (bit % BITS_PER_LONG))) != 0;
}

static const char* device_names[] = {"QEMU QEMU USB Tablet", "QEMU Virtio Tablet",
                                     "VirtualPS/2 VMware VMMouse"};

static const struct uinput_setup usetup = {
        .id =
                {
                        .bustype = BUS_VIRTUAL,
                        .vendor = 0xCAFE,
                        .product = 0x7100,
                },
        .name = "virt-tablet2multitouch",
};

int main() {
    bool device_name_matched = false;
    char buf[64];
    int fd, epoll_fd, uinput_fd;
    struct input_absinfo abs_x_info, abs_y_info;
    struct input_event ev;
    struct epoll_event event, events[10];
    unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];

    // Find the source input device
    for (int i = 0; i < 10; ++i) {
        device_name_matched = false;
        memset(ev_bits, 0, sizeof(ev_bits));

        snprintf(buf, sizeof(buf), "/dev/input/event%d", i);
        fd = open(buf, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        ioctl(fd, EVIOCGNAME(sizeof(buf)), buf);
        for (int j = 0; j < sizeof(device_names) / sizeof(device_names[0]); ++j) {
            if (strcmp(buf, device_names[j]) == 0) {
                device_name_matched = true;
                break;
            }
        }

        if (!device_name_matched) {
            LOG_ERROR("%s: Device name mismatching\n", buf);
        } else {
            if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) >= 0 &&
                test_bit(EV_ABS, ev_bits) && test_bit(EV_KEY, ev_bits)) {
                goto device_found;
            } else {
                LOG_ERROR("Device does not meet requirements\n");
            }
        }

        close(fd);
    }

    LOG_ERROR("Device not found\n");
    return EXIT_SUCCESS;

device_found:
    LOG_INFO("Using device: %s\n", buf);

    // Read ABS_X and ABS_Y info from the source device
    if (ioctl(fd, EVIOCGABS(ABS_X), &abs_x_info) < 0) {
        LOG_ERROR("ioctl EVIOCGABS(ABS_X) failed\n");
        goto err_read_abs;
    }

    if (ioctl(fd, EVIOCGABS(ABS_Y), &abs_y_info) < 0) {
        LOG_ERROR("ioctl EVIOCGABS(ABS_Y) failed\n");
        goto err_read_abs;
    }

    // Setup uinput device
    uinput_fd = libtablet2multitouch_setup_uinput_device(&usetup, &abs_x_info, &abs_y_info);
    if (uinput_fd < 0) {
        LOG_ERROR("Failed to setup uinput device\n");
        goto err_setup_uinput;
    }

    // Setup epoll
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LOG_ERROR("epoll_create1() failed\n");
        goto err_epoll;
    }

    event.events = EPOLLIN;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
        LOG_ERROR("epoll_ctl() failed\n");
        goto err_epoll;
    }

    while (true) {
        int epoll_ret = epoll_wait(epoll_fd, events, 10, -1);
        if (epoll_ret > 0) {
            for (int i = 0; i < epoll_ret; ++i) {
                if (events[i].events & EPOLLIN) {
                    int read_ret = read(fd, &ev, sizeof(ev));
                    if (read_ret == sizeof(ev)) {
                        libtablet2multitouch_handle_event(uinput_fd, &ev);
                    } else if (read_ret < 0 && errno != EAGAIN) {
                        LOG_ERROR("read() failed\n");
                        break;
                    }
                }
            }
        } else if (epoll_ret < 0) {
            LOG_ERROR("epoll_wait() failed\n");
            break;
        }
    }

    close(epoll_fd);
err_epoll:
    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd);
err_setup_uinput:
err_read_abs:
    close(fd);
    return EXIT_FAILURE;
}
