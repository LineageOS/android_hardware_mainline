/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libtablet2multitouch.h>

#define LOG_TAG "libtablet2multitouch"

#ifdef DEBUG
#define LOG_ERROR(...) fprintf(stderr, LOG_TAG ": " __VA_ARGS__)
#define LOG_INFO(...) fprintf(stdout, LOG_TAG ": " __VA_ARGS__)
#else
#include <cutils/klog.h>
#define LOG_ERROR(...) KLOG_ERROR(LOG_TAG, __VA_ARGS__)
#define LOG_INFO(...) KLOG_INFO(LOG_TAG, __VA_ARGS__)
#endif

#if defined(__ANDROID_RECOVERY__)
static bool g_report_hover = false;
#else
static bool g_report_hover = true;
#endif

// Function to setup the uinput device
int libtablet2multitouch_setup_uinput_device(const struct uinput_setup* usetup,
                                             struct input_absinfo* abs_x_info,
                                             struct input_absinfo* abs_y_info) {
    struct uinput_abs_setup abs_setup;

    int uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) {
        LOG_ERROR("Failed to open /dev/uinput\n");
        return -1;
    }

    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_BACK);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_MENU);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_UP);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_DOWN);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_VOLUMEUP);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_VOLUMEDOWN);

    ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_X);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_TOOL_TYPE);
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_DISTANCE);

    // Set the INPUT_PROP_DIRECT property
    ioctl(uinput_fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    // Set the ABS_MT_SLOT range
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_MT_SLOT;
    abs_setup.absinfo.minimum = 0;
    abs_setup.absinfo.maximum = 1;  // 2 slots (0 and 1)
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    // Set the ABS_X and ABS_MT_POSITION_X range based on the source
    // device's ABS_X
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_X;
    abs_setup.absinfo = *abs_x_info;
    abs_setup.absinfo.value = 0;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);
    abs_setup.code = ABS_MT_POSITION_X;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    // Set the ABS_Y and ABS_MT_POSITION_Y range based on the source
    // device's ABS_Y
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_Y;
    abs_setup.absinfo = *abs_y_info;
    abs_setup.absinfo.value = 0;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);
    abs_setup.code = ABS_MT_POSITION_Y;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    // Set the ABS_MT_TRACKING_ID range
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_MT_TRACKING_ID;
    abs_setup.absinfo.minimum = 0;
    abs_setup.absinfo.maximum = TRKID_MAX;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    // Set the ABS_MT_TOOL_TYPE range
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_MT_TOOL_TYPE;
    abs_setup.absinfo.minimum = 0;
    abs_setup.absinfo.maximum = MT_TOOL_PEN;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    // Set the ABS_MT_DISTANCE range
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_MT_DISTANCE;
    abs_setup.absinfo.minimum = 0;
    abs_setup.absinfo.maximum = 1;
    ioctl(uinput_fd, UI_ABS_SETUP, &abs_setup);

    if (ioctl(uinput_fd, UI_DEV_SETUP, usetup) < 0) {
        LOG_ERROR("ioctl(UI_DEV_SETUP) failed\n");
        close(uinput_fd);
        return -1;
    }

    if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
        LOG_ERROR("ioctl(UI_DEV_CREATE) failed\n");
        close(uinput_fd);
        return -1;
    }

    return uinput_fd;
}

// Function to send input events
void libtablet2multitouch_send_input_event(int uinput_fd, __u16 type, __u16 code, __s32 value) {
    struct input_event ev = {
            .code = code,
            .type = type,
            .value = value,
    };
    if (write(uinput_fd, &ev, sizeof(ev)) < 0) LOG_ERROR("write(uinput_fd) failed\n");
}

// Function to send SYN_REPORT
void libtablet2multitouch_report_sync(int uinput_fd) {
    libtablet2multitouch_send_input_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
}

// Function to send multitouch events
void libtablet2multitouch_report_multitouch(int uinput_fd, bool active, bool contact,
                                            int tracking_id, __s32 x, __s32 y) {
    // input_mt_slot
    libtablet2multitouch_send_input_event(uinput_fd, EV_ABS, ABS_MT_SLOT, 0);
    if (active) {
        // input_mt_report_slot_state
        libtablet2multitouch_send_input_event(uinput_fd, EV_ABS, ABS_MT_TRACKING_ID, tracking_id);
        libtablet2multitouch_send_input_event(uinput_fd, EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PEN);
        // input_report_abs ABS_MT_POSITION_X
        libtablet2multitouch_send_input_event(uinput_fd, EV_ABS, ABS_MT_POSITION_X, x);
        // input_report_abs ABS_MT_POSITION_Y
        libtablet2multitouch_send_input_event(uinput_fd, EV_ABS, ABS_MT_POSITION_Y, y);
        // Custom
        libtablet2multitouch_send_input_event(uinput_fd, EV_ABS, ABS_MT_DISTANCE, !contact);
        // input_mt_sync_frame -> input_mt_report_pointer_emulation
        libtablet2multitouch_send_input_event(uinput_fd, EV_KEY, BTN_TOUCH, contact);
        libtablet2multitouch_send_input_event(uinput_fd, EV_ABS, ABS_X, x);
        libtablet2multitouch_send_input_event(uinput_fd, EV_ABS, ABS_Y, y);
    } else {
        // input_mt_report_slot_state
        libtablet2multitouch_send_input_event(uinput_fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
        // input_mt_sync_frame -> input_mt_report_pointer_emulation
        libtablet2multitouch_send_input_event(uinput_fd, EV_KEY, BTN_TOUCH, 0);
    }
}

// Function to send key events
void libtablet2multitouch_report_key(int uinput_fd, __u16 code, __s32 value) {
    libtablet2multitouch_send_input_event(uinput_fd, EV_KEY, code, value);
}

// Function to handle tablet to multitouch and key translation
void libtablet2multitouch_handle_event(int uinput_fd, struct input_event* ev) {
    static bool active = false, contact = false, prev_active = false;
    static bool pending_report = false, pending_report_multitouch = false;
    static int tracking_id = 0;
    static __s32 x = 0, y = 0;

    bool key_report_up = false;
    __u16 trans_keycode;

    __u16* type = &ev->type;
    __u16* code = &ev->code;
    __s32* value = &ev->value;

    if (g_report_hover)
        active = true;
    else
        contact = true;

    switch (*type) {
        case EV_KEY:
            if (*code == BTN_LEFT) {
                if (g_report_hover) {
                    contact = !!*value;
                } else {
                    active = !!*value;
                }
                pending_report_multitouch = true;
                return;
            }

            switch (*code) {
                case BTN_MIDDLE:
                    trans_keycode = KEY_MENU;
                    break;
                case BTN_RIGHT:
                    trans_keycode = KEY_BACK;
                    break;
                case BTN_GEAR_DOWN:
                    key_report_up = true;
                    trans_keycode = KEY_DOWN;
                    break;
                case BTN_GEAR_UP:
                    key_report_up = true;
                    trans_keycode = KEY_UP;
                    break;
                default:
                    return;
            }
            if (key_report_up) {
                libtablet2multitouch_report_key(uinput_fd, trans_keycode, 1);
                libtablet2multitouch_report_key(uinput_fd, trans_keycode, 0);
            } else {
                libtablet2multitouch_report_key(uinput_fd, trans_keycode, *value);
            }
            pending_report = true;
            return;

        case EV_REL:
            if (*code != REL_WHEEL) return;

            if (*value == 1)
                trans_keycode = KEY_VOLUMEUP;
            else
                trans_keycode = KEY_VOLUMEDOWN;

            libtablet2multitouch_report_key(uinput_fd, trans_keycode, 1);
            libtablet2multitouch_report_key(uinput_fd, trans_keycode, 0);

            pending_report = true;
            return;

        case EV_ABS:
            switch (*code) {
                case ABS_X:
                    x = *value;
                    break;
                case ABS_Y:
                    y = *value;
                    break;
                default:
                    return;
            }
            pending_report_multitouch = true;
            return;

        case EV_SYN:
            if (*code != SYN_REPORT) return;

            if (pending_report_multitouch && !(prev_active == false && active == false)) {
                pending_report = true;
                libtablet2multitouch_report_multitouch(uinput_fd, active, contact, tracking_id, x,
                                                       y);
                if (!active) {
                    tracking_id++;
                    if (tracking_id > TRKID_MAX) {
                        tracking_id = 0;
                    }
                }
                pending_report_multitouch = false;
                prev_active = active;
            }

            if (pending_report) {
                libtablet2multitouch_report_sync(uinput_fd);
                pending_report = false;
            }
            return;
        default:
            return;
    }
}
