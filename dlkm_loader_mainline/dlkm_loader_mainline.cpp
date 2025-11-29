/*
 * SPDX-FileCopyrightText: 2019 The Android Open Source Project
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <android-base/logging.h>
#include <modprobe/modprobe.h>
#include "android-base/properties.h"

// Note: This also handles _dlkm partitions: base partition path is a symlink to them
// https://source.android.com/docs/core/architecture/partitions/gki-partitions#mount
static const std::vector<std::string> kBasePaths = {
        "/vendor/lib/modules",
        "/odm/lib/modules",
        "/system/lib/modules",
};

int main(int, char** argv) {
    android::base::InitLogging(argv, android::base::KernelLogger);
    LOG(INFO) << "dlkm loader successfully initialized";

    Modprobe m(kBasePaths, "modules.load");

    // We should continue loading kernel modules even if some modules fail to
    // load. If we abort loading early, the unloaded modules can cause more
    // problems, making debugging hard.
    // e.g. , bluetooth module break, but we
    // might also see graphics problems, because graphics module gets loaded
    // after bluetooth, and we aborted loading early.
    CHECK(m.LoadListedModules(false)) << "modules from vendor dlkm weren't loaded correctly";

    LOG(INFO) << "module load count is " << m.GetModuleCount();

    android::base::SetProperty("vendor.dlkm.modules.ready", "true");
    return 0;
}
