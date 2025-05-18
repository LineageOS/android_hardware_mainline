/* SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (C) 2019 The Android Open-Source Project
 * Copyright (C) 2021 GloDroid project
 */

#pragma once

#include <memory>

namespace aidl {
namespace android {
namespace hardware {
namespace vibrator {

class FFDeviceBase {
  public:
    virtual ~FFDeviceBase() = default;
    virtual void vibrate(int duration_ms) = 0;
    virtual void off() = 0;
};

class FFDeviceDummy : public FFDeviceBase {
  public:
    static std::unique_ptr<FFDeviceBase> create() { return std::make_unique<FFDeviceDummy>(); }
    void vibrate(int /*duration_ms*/) override{};
    void off() override{};
};

class FFDevice : public FFDeviceBase {
    int last_effect_id = -1;
    int event_fd;

  public:
    static std::unique_ptr<FFDeviceBase> create(const char* input_path);

    ~FFDevice() override;
    void vibrate(int duration_ms) override;
    void off() override;
};

}  // namespace vibrator
}  // namespace hardware
}  // namespace android
}  // namespace aidl
