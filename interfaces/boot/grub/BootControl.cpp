/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "BootControl.h"
#include <cstdint>

#include <android-base/logging.h>

using ndk::ScopedAStatus;

namespace aidl::android::hardware::boot {

namespace {

std::string ConvertMergeStatusToGrubString(MergeStatus merge_status) {
    switch (merge_status) {
        case MergeStatus::NONE:
            return "none";
        case MergeStatus::UNKNOWN:
            return "unknown";
        case MergeStatus::SNAPSHOTTED:
            return "snapshotted";
        case MergeStatus::MERGING:
            return "merging";
        case MergeStatus::CANCELLED:
            return "cancelled";
        default:
            return "invalid_merge_status";
    }
}

MergeStatus ConvertGrubStringToMergeStatus(std::string str) {
    if (str == "none") return MergeStatus::NONE;
    if (str == "unknown") return MergeStatus::UNKNOWN;
    if (str == "snapshotted") return MergeStatus::SNAPSHOTTED;
    if (str == "merging") return MergeStatus::MERGING;
    if (str == "cancelled") return MergeStatus::CANCELLED;
    return MergeStatus::UNKNOWN;
}

}  // namespace

BootControl::BootControl() {
#if defined(__ANDROID_RECOVERY__)
    mBackend =
            new libgrub_boot_control::GrubBootControl("/mnt/vendor/_persist/grubenv_abootctrl");
#else
    mBackend = new libgrub_boot_control::GrubBootControl();
#endif
}

ScopedAStatus BootControl::getActiveBootSlot(int32_t* _aidl_return) {
    *_aidl_return = mBackend->getActiveBootSlot();
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::getCurrentSlot(int32_t* _aidl_return) {
    *_aidl_return = mBackend->getCurrentSlot();
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::getNumberSlots(int32_t* _aidl_return) {
    *_aidl_return = mBackend->getNumberSlots();
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::getSnapshotMergeStatus(MergeStatus* _aidl_return) {
    *_aidl_return = ConvertGrubStringToMergeStatus(mBackend->getSnapshotMergeStatus());
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::getSuffix(int32_t in_slot, std::string* _aidl_return) {
    *_aidl_return = mBackend->getSuffix(in_slot);
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::isSlotBootable(int32_t in_slot, bool* _aidl_return) {
    int32_t val = mBackend->isSlotBootable(in_slot);
    if (val == INVALID_SLOT) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                INVALID_SLOT, (std::string("Invalid slot ") + std::to_string(in_slot)).c_str());
    }
    *_aidl_return = val;
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::isSlotMarkedSuccessful(int32_t in_slot, bool* _aidl_return) {
    int32_t val = mBackend->isSlotMarkedSuccessful(in_slot);
    if (val == INVALID_SLOT) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                INVALID_SLOT, (std::string("Invalid slot ") + std::to_string(in_slot)).c_str());
    }
    *_aidl_return = val;
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::markBootSuccessful() {
    int32_t ret = mBackend->markBootSuccessful();
    if (ret == COMMAND_FAILED) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                  "Operation failed");
    }
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::setActiveBootSlot(int32_t in_slot) {
    int32_t ret = mBackend->setActiveBootSlot(in_slot);
    switch (ret) {
        case COMMAND_FAILED:
            return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                      "Operation failed");
        case INVALID_SLOT:
            return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                    INVALID_SLOT, (std::string("Invalid slot ") + std::to_string(in_slot)).c_str());
        default:
            break;
    }
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::setSlotAsUnbootable(int32_t in_slot) {
    int32_t ret = mBackend->setSlotAsUnbootable(in_slot);
    switch (ret) {
        case COMMAND_FAILED:
            return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                      "Operation failed");
        case INVALID_SLOT:
            return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                    INVALID_SLOT, (std::string("Invalid slot ") + std::to_string(in_slot)).c_str());
        default:
            break;
    }
    return ScopedAStatus::ok();
}

ScopedAStatus BootControl::setSnapshotMergeStatus(MergeStatus in_status) {
    int32_t ret = mBackend->setSnapshotMergeStatus(ConvertMergeStatusToGrubString(in_status));
    if (ret == COMMAND_FAILED) {
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(COMMAND_FAILED,
                                                                  "Operation failed");
    }
    return ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::boot
