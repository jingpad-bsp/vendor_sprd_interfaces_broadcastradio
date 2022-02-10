/*
 * Copyright (C) 2017 The Android Open Source Project
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
#ifndef ANDROID_HARDWARE_BROADCASTRADIO_V2_0_TUNER_H
#define ANDROID_HARDWARE_BROADCASTRADIO_V2_0_TUNER_H

#include "VirtualRadio.h"
#include "fmr.h"

#include <android/hardware/broadcastradio/2.0/ITunerCallback.h>
#include <android/hardware/broadcastradio/2.0/ITunerSession.h>
#include <android/hardware/broadcastradio/2.0/types.h>
#include <broadcastradio-utils/WorkerThread.h>
#include <thread>

#include <optional>

namespace vendor {
namespace sprd {
namespace hardware {
namespace broadcastradio {
namespace V2_0 {
namespace implementation {

using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;
using ::android::WorkerThread;

struct BroadcastRadio;

struct TunerSession : public ITunerSession {
    TunerSession(BroadcastRadio& module, const sp<ITunerCallback>& callback);
    ~TunerSession();
    // V2_0::ITunerSession methods
    virtual Return<Result> tune(const ProgramSelector& program) override;
    virtual Return<Result> scan(bool directionUp, bool skipSubChannel) override;
    virtual Return<Result> step(bool directionUp) override;
    virtual Return<void> cancel() override;
    virtual Return<Result> startProgramListUpdates(const ProgramFilter& filter);
    virtual Return<void> stopProgramListUpdates();
    virtual Return<void> isConfigFlagSet(ConfigFlag flag, isConfigFlagSet_cb _hidl_cb);
    virtual Return<Result> setConfigFlag(ConfigFlag flag, bool value);
    virtual Return<void> setParameters(const hidl_vec<VendorKeyValue>& parameters,
                                       setParameters_cb _hidl_cb) override;
    virtual Return<void> getParameters(const hidl_vec<hidl_string>& keys,
                                       getParameters_cb _hidl_cb) override;
    virtual Return<void> close() override;

    std::optional<AmFmBandRange> getAmFmRangeLocked() const;

   private:
    std::mutex mMut;
    std::mutex mSetParametersMut;
    std::mutex mRdsMut; // for rds update.
    WorkerThread mThread;
    bool mIsClosed = true;
    bool mIsPowerUp = false; // add for powerup judgement
    bool mIsRdsSupported = false; // add for rds
    std::atomic<bool> mIsTerminating;
    constexpr static auto kTimeoutDuration = std::chrono::milliseconds(500);
    std::condition_variable mCondRds;
    const sp<ITunerCallback> mCallback;

    std::reference_wrapper<BroadcastRadio> mModule;
    bool mIsTuneCompleted = false;
    ProgramSelector mCurrentProgram = {};
    std::thread* mRdsUpdateThread = nullptr; // add for rds update periodically
    ProgramInfo mCurrentProgramInfo = {};// add for rds update filter

    void cancelLocked();
    void tuneInternalLocked(const ProgramSelector& sel);
    const VirtualRadio& virtualRadio() const;
    const BroadcastRadio& module() const;
    void rdsUpdateThreadLoop();
    bool isRdsUpdateNeeded(ProgramInfo& info);
    void makeDummyProgramInfoForRdsUpdate(ProgramInfo* newInfo, const ProgramSelector& selector); // add for rds update
    // add for hal implements
    bool setRdsOnOff(bool rdsOn);
    int mSpacing = 100;
   public:
    static const int RDS_EVENT_PROGRAMNAME = 0x0008;
    static const int RDS_EVENT_LAST_RADIOTEXT = 0x0040;
    static const int RDS_EVENT_AF = 0x0080;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace broadcastradio
}  // namespace hardware
}  // namespace sprd
}  // namespace vendor

#endif  // ANDROID_HARDWARE_BROADCASTRADIO_V2_0_TUNER_H
