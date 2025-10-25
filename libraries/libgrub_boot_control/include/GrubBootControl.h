#include <mutex>
#include <string>
#include <vector>

#include <android-base/unique_fd.h>

#include <libgrub_editenv.h>

namespace libgrub_boot_control {

class GrubBootControl {
  public:
    GrubBootControl(std::string grubenv_path = "/mnt/vendor/persist/grubenv_abootctrl",
                    std::vector<std::string> slots = {"a", "b"},
                    std::string var_key_prefix = "abootctrl_");
    ~GrubBootControl();

    void PrintGrubVars();

    // android.hardware.boot
    int getActiveBootSlot();
    int getCurrentSlot();
    int getNumberSlots();
    std::string getSnapshotMergeStatus();
    std::string getSuffix(int slot);
    int isSlotBootable(int slot);
    int isSlotMarkedSuccessful(int slot);
    int markBootSuccessful();
    int setActiveBootSlot(int slot);
    int setSlotAsUnbootable(int slot);
    int setSnapshotMergeStatus(std::string status);

  protected:
    bool IsValidSlot(int slot);

    std::string GetItemKeyForGlobal(std::string item);
    std::string GetItemValueForGlobal(std::string item);
    bool SetItemValueForGlobal(std::string item, std::string value, bool commit = true);

    std::string GetItemKeyForSlot(int slot, std::string item);
    std::string GetItemValueForSlot(int slot, std::string item);
    bool SetItemValueForSlot(int slot, std::string item, std::string value, bool commit = true);
    bool SetItemValueForAllSlots(std::string item, std::string value, bool commit = true);

    std::string GetStringFromSlotNumber(int slot);
    int GetSlotNumberFromString(std::string str);

  private:
    std::vector<std::string> mSlots;
    std::string mVarKeyPrefix;

    ::android::base::unique_fd mFd;
    ::libgrub_editenv::GrubEnvMap mMap;

    std::mutex mMapMutex;
    std::mutex mSnapshotMergeStatusMutex;

    void InitGrubVars();
    bool CommitGrubVars();

    void DecreaseRetryCountForCurrentSlot();
    void RemoveUnusedElementsFromMap();
};

}  // namespace libgrub_boot_control
