#include <string>
#include <vector>

#define LOG_TAG "GrubBootControl"

#include <android-base/logging.h>

#include <GrubBootControl.h>

#if defined(__ANDROID_VENDOR__) || defined(__ANDROID_RECOVERY__) || defined(__ANDROID_APEX__)
#include <android-base/properties.h>
using android::base::GetProperty;
#endif

using libgrub_editenv::GrubEnvMap;

using std::string;
using std::vector;

namespace libgrub_boot_control {

namespace {

const int INVALID_SLOT = -1;
const int COMMAND_FAILED = -2;

// Unused for GRUB by default
const string kItemGlobalSnapshotMergeStatus =
        "snapshot_merge_status";  // whatever string, boot control HAL should take care of validity
const string kItemSlotIsSuccessful = "is_successful";  // default is false

// Read-only for GRUB by default

// Should be set by GRUB prior to booting entry
const string kItemGlobalActiveSlot = "active_slot";    // e.g. "a"
const string kItemGlobalCurrentSlot = "current_slot";  // e.g. "a"
const string kItemSlotRetryCount = "retry_count";  // default is empty. length of its value = times
                                                   // that GRUB tried to boot the slot
const string kItemSlotIsBootable = "is_bootable";  // default is true.

// User settings
const string kItemGlobalNoAutoSlotSwitch = "no_auto_slot_switch";  // "true" for enable

const vector<const string*> kAllGlobalItems = {
        &kItemGlobalActiveSlot,
        &kItemGlobalCurrentSlot,
        &kItemGlobalSnapshotMergeStatus,
};

const vector<const string*> kAllSlotItems = {
        &kItemSlotRetryCount,
        &kItemSlotIsBootable,
        &kItemSlotIsSuccessful,
};

}  // namespace

void GrubBootControl::RemoveUnusedElementsFromMap() {
    GrubEnvMap new_map;

    for (const string* item : kAllGlobalItems) {
        new_map.insert({GetItemKeyForGlobal(*item), GetItemValueForGlobal(*item)});
    }

    for (int i = 0; i < getNumberSlots(); i++) {
        for (const string* item : kAllSlotItems) {
            new_map.insert({GetItemKeyForSlot(i, *item), GetItemValueForSlot(i, *item)});
        }
    }

    mMap = std::move(new_map);
}

void GrubBootControl::InitGrubVars() {
    // Global
    SetItemValueForGlobal(kItemGlobalNoAutoSlotSwitch, "false", false);
    SetItemValueForGlobal(kItemGlobalSnapshotMergeStatus, "none", false);

    // Global: Active slot and Current slot
    string current_slot;
#if defined(__ANDROID_VENDOR__) || defined(__ANDROID_RECOVERY__) || defined(__ANDROID_APEX__)
    current_slot = GetProperty("ro.boot.slot_suffix", "")[1];
#endif
    if (current_slot.empty()) current_slot = mSlots.front();
    SetItemValueForGlobal(kItemGlobalActiveSlot, current_slot, false);
    SetItemValueForGlobal(kItemGlobalCurrentSlot, current_slot, false);

    // Slot
    SetItemValueForAllSlots(kItemSlotRetryCount, "", false);
    SetItemValueForAllSlots(kItemSlotIsBootable, "true", false);
    SetItemValueForAllSlots(kItemSlotIsSuccessful, "false", false);
}

void GrubBootControl::DecreaseRetryCountForCurrentSlot() {
    // getCurrentSlot() may return invalid slot number (on error)
    int slot = getCurrentSlot();
    if (!IsValidSlot(slot)) return;

    string retry_count_str = GetItemValueForSlot(slot, kItemSlotRetryCount);
    if (!retry_count_str.empty()) retry_count_str.pop_back();
    SetItemValueForSlot(slot, kItemSlotRetryCount, retry_count_str);
}

// android.hardware.boot

int GrubBootControl::getActiveBootSlot() {
    string active_slot_str = GetItemValueForGlobal(kItemGlobalActiveSlot);
    int ret = GetSlotNumberFromString(active_slot_str);
    return ret == INVALID_SLOT ? COMMAND_FAILED : ret;
}

int GrubBootControl::getCurrentSlot() {
    string current_slot_str = GetItemValueForGlobal(kItemGlobalCurrentSlot);
    int ret = GetSlotNumberFromString(current_slot_str);
    return ret == INVALID_SLOT ? COMMAND_FAILED : ret;
}

int GrubBootControl::getNumberSlots() {
    return mSlots.size();
}

string GrubBootControl::getSnapshotMergeStatus() {
    mSnapshotMergeStatusMutex.lock();
    string ret = GetItemValueForGlobal(kItemGlobalSnapshotMergeStatus);
    mSnapshotMergeStatusMutex.unlock();
    return ret;
}

string GrubBootControl::getSuffix(int slot) {
    if (!IsValidSlot(slot)) return "";

    return "_" + GetStringFromSlotNumber(slot);
}

int GrubBootControl::isSlotBootable(int slot) {
    if (!IsValidSlot(slot)) return INVALID_SLOT;

    string val = GetItemValueForSlot(slot, kItemSlotIsBootable);
    if (val == "false") return 0;
    return 1;
}

int GrubBootControl::isSlotMarkedSuccessful(int slot) {
    if (!IsValidSlot(slot)) return INVALID_SLOT;

    string val = GetItemValueForSlot(slot, kItemSlotIsSuccessful);
    if (val == "true") return 1;
    return 0;
}

int GrubBootControl::markBootSuccessful() {
    // getCurrentSlot() may return invalid slot number (on error)
    int slot = getCurrentSlot();
    if (!IsValidSlot(slot)) return COMMAND_FAILED;

    // Ensure GRUB would consider the slot as bootable next time
    SetItemValueForSlot(slot, kItemSlotRetryCount, "", false);
    SetItemValueForSlot(slot, kItemSlotIsBootable, "true", false);

    SetItemValueForSlot(slot, kItemSlotIsSuccessful, "true", false);

    if (!CommitGrubVars()) return COMMAND_FAILED;

    return 0;
}

int GrubBootControl::setActiveBootSlot(int slot) {
    if (!IsValidSlot(slot)) return INVALID_SLOT;

    // Ensure GRUB would consider the slot as bootable next time
    SetItemValueForSlot(slot, kItemSlotRetryCount, "", false);
    SetItemValueForSlot(slot, kItemSlotIsBootable, "true", false);

    SetItemValueForGlobal(kItemGlobalActiveSlot, GetStringFromSlotNumber(slot), false);

    if (!CommitGrubVars()) return COMMAND_FAILED;

    return 0;
}

int GrubBootControl::setSlotAsUnbootable(int slot) {
    if (!IsValidSlot(slot)) return INVALID_SLOT;

    if (!SetItemValueForSlot(slot, kItemSlotIsBootable, "false")) return COMMAND_FAILED;

    return 0;
}

int GrubBootControl::setSnapshotMergeStatus(string status) {
    mSnapshotMergeStatusMutex.lock();

    if (!SetItemValueForGlobal(kItemGlobalSnapshotMergeStatus, status)) return COMMAND_FAILED;

    mSnapshotMergeStatusMutex.unlock();

    return 0;
}

}  // namespace libgrub_boot_control
