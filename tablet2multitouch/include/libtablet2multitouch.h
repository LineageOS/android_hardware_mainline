/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <linux/input.h>
#include <stdbool.h>

#ifndef TRKID_MAX
// Taken from kernel/include/linux/input/mt.h
#define TRKID_MAX 0xffff
#endif

// Function to setup the uinput device
extern int libtablet2multitouch_setup_uinput_device(const struct uinput_setup* usetup,
                                                    struct input_absinfo* abs_x_info,
                                                    struct input_absinfo* abs_y_info);
// Function to send input events
extern void libtablet2multitouch_send_input_event(int uinput_fd, __u16 type, __u16 code,
                                                  __s32 value);
// Function to send SYN_REPORT
extern void libtablet2multitouch_report_sync(int uinput_fd);
// Function to send multitouch events
extern void libtablet2multitouch_report_multitouch(int uinput_fd, bool active, bool contact,
                                                   int tracking_id, __s32 x, __s32 y);
// Function to send key events
extern void libtablet2multitouch_report_key(int uinput_fd, __u16 code, __s32 value);
// Function to handle tablet to multitouch and key translation
extern void libtablet2multitouch_handle_event(int uinput_fd, struct input_event* ev);
