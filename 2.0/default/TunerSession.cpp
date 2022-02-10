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

#define LOG_TAG "BcRadioDef.tuner"
#define LOG_NDEBUG 0

#include "TunerSession.h"
#include "BroadcastRadio.h"

#include <broadcastradio-utils-2x/Utils.h>
#include <log/log.h>
#include <pthread.h>
#include <android-base/strings.h>

namespace vendor {
namespace sprd {
namespace hardware {
namespace broadcastradio {
namespace V2_0 {
namespace implementation {


using namespace std::chrono_literals;
using namespace android::hardware::broadcastradio;
using namespace android::hardware::broadcastradio::utils;

using std::lock_guard;
using std::move;
using std::mutex;
using std::sort;
using std::vector;

namespace delay {

static constexpr auto seek = 0ms;
static constexpr auto step = 0ms;
static constexpr auto tune = 0ms;
//change to 0s for real scan
//static constexpr auto list = 0s;
static constexpr auto list = 0s;

}  // namespace delay

static int sprdrds_state = 0;
static int sprdtune_state = 0;

TunerSession::TunerSession(BroadcastRadio& module, const sp<ITunerCallback>& callback)
    : mIsTerminating(false), mCallback(callback), mModule(module) {
    bool result = openDev();
    ALOGD("TunerSession constructor...openDev :%d",result);
    if(result){
        mIsClosed = false;
        tuneInternalLocked(utils::make_selector_amfm(87500));

        if(1 == isRdsSupport()){
            mIsRdsSupported = true;
        }
        ALOGW("TunerSession constructor, mIsRdsSupported:%d",mIsRdsSupported);
        setRdsOnOff(true);
        if(mIsRdsSupported){
            mRdsUpdateThread = new std::thread(&TunerSession::rdsUpdateThreadLoop,this);
        }
        ALOGW("TunerSession constructor, start rds thread ,powerup done...");
    }

}
/*
 * fm driver should powerdown when TunerSession destroy.
 */
TunerSession::~TunerSession(){
    ALOGD("~TunerSession powerdown...");
    //close();
    ALOGD("~TunerSession powerdown, before get mutex...");
    {
        lock_guard<mutex> lk(mRdsMut);
        mIsClosed = true;
        mIsTerminating = true;
        mCondRds.notify_one();
        ALOGD("~TunerSession powerdown, after set mIsTerminating true...");
    }
    if(nullptr != mRdsUpdateThread){
      mRdsUpdateThread->join();
      ALOGD("~TunerSession powerdown after join release done...");
    }
}
// makes ProgramInfo that points to no program
static ProgramInfo makeDummyProgramInfo(const ProgramSelector& selector) {
    ProgramInfo info = {};
    info.selector = selector;
    info.logicallyTunedTo = utils::make_identifier(
        IdentifierType::AMFM_FREQUENCY, utils::getId(selector, IdentifierType::AMFM_FREQUENCY));
    info.physicallyTunedTo = info.logicallyTunedTo;
    return info;
}
/*
* Add for rds update only,similar with makeDummyProgramInfo
* To make sure,do not update rds immediately when freq changed by seek.tune.scan... etc.
* Because fm driver need some time to fresh rds,when change to new program,so do not update rds in above function
*/
void TunerSession:: makeDummyProgramInfoForRdsUpdate(ProgramInfo* newInfo,const ProgramSelector& selector){
    newInfo->selector = selector;
    /*
     * Add for rds info, RDS events
     * PS :  RDS_EVENT_PROGRAMNAME = 0x0008;
     * RT :  RDS_EVENT_LAST_RADIOTEXT = 0x0040;
     */
      int rdsEvents = readRds();
      ALOGD("makeDummyProgramInfoForRdsUpdate,rds events : %d",rdsEvents);
      // when rds events = 0 ,there will be no rds info in program info ,so app will get null for ps and rt,update callback is unnecessary
      if(0 != rdsEvents){
        char* programName = "";
        char* rtName = "";
        if(rdsEvents & RDS_EVENT_PROGRAMNAME){
          programName = getPs();//get ps info
        }
        if(rdsEvents & RDS_EVENT_LAST_RADIOTEXT){
          rtName = getLrText();//get rt info
        }
        ALOGD("makeDummyProgramInfoForRdsUpdate,rds events : %d, ps:%s, rt:%s",rdsEvents,programName,rtName);

        newInfo->metadata = hidl_vec<Metadata>({
          make_metadata(MetadataKey::RDS_PS, programName),
          make_metadata(MetadataKey::RDS_RT, rtName),
          //make_metadata(MetadataKey::SONG_TITLE, songTitle),
          //make_metadata(MetadataKey::SONG_ARTIST, songArtist),
          //make_metadata(MetadataKey::STATION_ICON, resources::demoPngId),
          //make_metadata(MetadataKey::ALBUM_ART, resources::demoPngId),
        });// make metadata
      }else{
        newInfo->metadata = hidl_vec<Metadata>({
          // make metadata null
        });
      }
    newInfo->logicallyTunedTo = utils::make_identifier(
        IdentifierType::AMFM_FREQUENCY, utils::getId(selector, IdentifierType::AMFM_FREQUENCY));
    newInfo->physicallyTunedTo = newInfo->logicallyTunedTo;
}

/*
 * Add for read rds periodically in a single thread.
 * sleep(xx),how long will be correct?
 * start in constructor or not judged by mIsRdsSupported flag
*/
void TunerSession::rdsUpdateThreadLoop(){
  ALOGD("TunerSession, rdsUpdateThreadLoop  start");
  // rds supported,when fm powerup and no closed, update rds per x seconds.
  ProgramInfo newInfo = {};
  while(!mIsTerminating){
    std::unique_lock<mutex> lk(mRdsMut);
    ALOGD("TunerSession come into rdsUpdateThreadLoop ....mIsClosed :%d",mIsClosed);
    if(!mIsClosed){
      if(sprdrds_state == 0){
        makeDummyProgramInfoForRdsUpdate(&newInfo,mCurrentProgram);
      }
      if(isRdsUpdateNeeded(newInfo)){
        mCurrentProgramInfo = newInfo; // add for rds callback filter.update current programinfo
        auto task =[this,newInfo](){
          lock_guard<mutex> lk(mMut);
          mCallback->onCurrentProgramInfoChanged(newInfo);
        };
        mThread.schedule(task,delay::tune);
      }
      // wait for kTimeoutDuration,and break when timeout and mIsClosed = false;
      // but if timeout and mIsClosed = true, will not wait，run immediately.
      ALOGD("rdsUpdateThreadLoop, wait_for pred : %d",(true == mIsClosed));
      mCondRds.wait_for(lk,kTimeoutDuration,[&]{return mIsClosed;});// wait for update rds per 3s.
      ALOGD("TunerSession  rdsUpdateThreadLoop after wait_for sleep 3s....");
      lk.unlock();
   }else{
      // wait until open or ~TunerSession.
      ALOGD("rdsUpdateThreadLoop, wait pred : %d",(!mIsClosed || mIsTerminating));
      // wait when pred is false;  run when (mIsClosed = false or mIsTerminating = true)
      mCondRds.wait(lk,[&]{return !mIsClosed || mIsTerminating;});// wait for update rds per 3s.
      ALOGD("TunerSession  rdsUpdateThreadLoop after wait sleep 3s....");
      lk.unlock();
   }
  }
   ALOGD("TunerSession released, rds thread return....");
   // exit rdsthread function due to TunerSession
   return;
}

/*
*Add for judgement: whether should update rds info;
*@Result: true- update; false- do not update;
*/
bool TunerSession::isRdsUpdateNeeded(ProgramInfo& info){
    auto nps =  utils::getMetadataString(info, MetadataKey::RDS_PS);
    auto nrt =  utils::getMetadataString(info, MetadataKey::RDS_RT);
    ALOGD("rdsUpdateThreadLoop,newInfo.selector:%s, pss:%s, rt:%s",toString(mCurrentProgram).c_str(),nps->c_str(),nrt->c_str());
    //ALOGD("rdsUpdateThreadLoop,pss has_value:%d, rt has_value:%d",nps.has_value(),nrt.has_value());

    auto ops = utils::getMetadataString(mCurrentProgramInfo, MetadataKey::RDS_PS);
    auto ort = utils::getMetadataString(mCurrentProgramInfo, MetadataKey::RDS_RT);
    ALOGD("rdsUpdateThreadLoop,mCurrentProgramInfo.selector:%s, ps:%s, rt:%s",toString(mCurrentProgramInfo.selector).c_str(),ops->c_str(),ort->c_str());
    bool update = true;
    if((info.selector == mCurrentProgramInfo.selector)&&(android::base::Trim(*nps) == android::base::Trim(*ops))&&(android::base::Trim(*nrt) == android::base::Trim(*ort))){
       // freq、rds info are the same as last time, do not to update.
       update = false;
    }
    ALOGD("rdsUpdateThreadLoop, isRdsUpdateNeeded: %d", update);
    return update;
}

void TunerSession::tuneInternalLocked(const ProgramSelector& sel) {
    ALOGD("%s(%s)", __func__, toString(sel).c_str());

    VirtualProgram virtualProgram;
    ProgramInfo programInfo;

    mCurrentProgram = sel;
    auto current = utils::getId(mCurrentProgram, IdentifierType::AMFM_FREQUENCY);
    ALOGD("Tuner::tuneInternalLocked..tune.. current=%lu",current);
    if (mIsClosed) {
        sprdtune_state = 1;         //fix native crash
        return;
    }
    ::tune(current);
    programInfo = makeDummyProgramInfo(sel);
    mCurrentProgramInfo = programInfo; // add for rds callback filter.
    mIsTuneCompleted = true;
    setRdsOnOff(true);
    mCallback->onCurrentProgramInfoChanged(programInfo);
}

const BroadcastRadio& TunerSession::module() const {
    return mModule.get();
}

const VirtualRadio& TunerSession::virtualRadio() const {
    return module().mVirtualRadio;
}

Return<Result> TunerSession::tune(const ProgramSelector& sel) {
    ALOGD("%s(%s)", __func__, toString(sel).c_str());
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::INVALID_STATE;

    if (!utils::isSupported(module().mProperties, sel)) {
        ALOGW("Selector not supported");
        return Result::NOT_SUPPORTED;
    }

    if (!utils::isValid(sel)) {
        ALOGE("ProgramSelector is not valid");
        return Result::INVALID_ARGUMENTS;
    }

    cancelLocked();

    setRdsOnOff(false);
    mIsTuneCompleted = false;
    auto task = [this, sel]() {
        lock_guard<mutex> lk(mMut);
        tuneInternalLocked(sel);
    };
    mThread.schedule(task, delay::tune);

    return Result::OK;
}

Return<Result> TunerSession::scan(bool directionUp, bool /* skipSubChannel */) {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::INVALID_STATE;
    cancelLocked();

 /*Original code here.
  * There maybe two path for this implement:
  *1- scan use seek(startfreq,isup);
  *2- autoscan fresh station list,and then seek this list
  *Attention: seek task timeout is 200ms default,so path 2 may be better.
  *need VTS test to verify.
  *remove original code here for real scan implements
  */
    setRdsOnOff(false);
    auto current = utils::getId(mCurrentProgram, IdentifierType::AMFM_FREQUENCY);
    float seekResult = seek(current,directionUp,mSpacing);
    ALOGD("scan station..,after seek,current=%lu, seekResult=%d",current,(int)seekResult);
    auto tuneTo = utils::make_selector_amfm((int)seekResult);

    mIsTuneCompleted = false;
    auto task = [this, tuneTo, directionUp]() {
        ALOGI("Performing seek up=%d", directionUp);

        lock_guard<mutex> lk(mMut);
        tuneInternalLocked(tuneTo);
    };
    if (sprdtune_state == 1) {
        ALOGD("sprdtune when close TunerSession");
        return Result::INVALID_STATE;
    }
    mThread.schedule(task, delay::seek);

    return Result::OK;
}

Return<Result> TunerSession::step(bool directionUp) {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::INVALID_STATE;

    cancelLocked();

    if (!utils::hasId(mCurrentProgram, IdentifierType::AMFM_FREQUENCY)) {
        ALOGE("Can't step in anything else than AM/FM");
        return Result::NOT_SUPPORTED;
    }

    auto stepTo = utils::getId(mCurrentProgram, IdentifierType::AMFM_FREQUENCY);
    auto range = getAmFmRangeLocked();
    if (!range) {
        ALOGE("Can't find current band");
        return Result::INTERNAL_ERROR;
    }

    setRdsOnOff(false);
    if (directionUp) {
        stepTo += mSpacing;
    } else {
        stepTo -= mSpacing;
    }
    if (stepTo > range->upperBound) stepTo = range->lowerBound;
    if (stepTo < range->lowerBound) stepTo = range->upperBound;

    mIsTuneCompleted = false;
    auto task = [this, stepTo]() {
        ALOGI("Performing step to %s", std::to_string(stepTo).c_str());

        lock_guard<mutex> lk(mMut);

        tuneInternalLocked(utils::make_selector_amfm(stepTo));
    };
    mThread.schedule(task, delay::step);

    return Result::OK;
}

void TunerSession::cancelLocked() {
    ALOGD("%s", __func__);

    mThread.cancelAll();
    if (utils::getType(mCurrentProgram.primaryId) != IdentifierType::INVALID) {
        mIsTuneCompleted = true;
    }
}

Return<void> TunerSession::cancel() {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return {};

    cancelLocked();

    return {};
}

Return<Result> TunerSession::startProgramListUpdates(const ProgramFilter& filter) {
    ALOGD("%s(%s)", __func__, toString(filter).c_str());
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return Result::INVALID_STATE;
       setRdsOnOff(false);
       // real autoScan directly
        ALOGD("start autoScan..");
        int* results = nullptr;
        int length = 0;
        results = autoScan(&length, mSpacing);
        ALOGD("autoScan done..results: %p, length:%d",results,length);
        std::vector<VirtualProgram> mScanedPrograms;
        for(int i=0;i<length;i++){
           ALOGD("after autoScan..make program ,results[i] :%d",results[i]);
           mScanedPrograms.push_back({make_selector_amfm(10*results[i]),"","",""});
        }
        delete results; // need to be deleted after used
        setRdsOnOff(true);

        vector<VirtualProgram> filteredList;
        auto filterCb = [&filter](const VirtualProgram& program) {
            return utils::satisfies(filter, program.selector);
        };
        std::copy_if(mScanedPrograms.begin(), mScanedPrograms.end(), std::back_inserter(filteredList), filterCb);

        auto task = [this, mScanedPrograms]() {
            lock_guard<mutex> lk(mMut);

            ProgramListChunk chunk = {};
            chunk.purge = true;
            chunk.complete = true;
            chunk.modified = hidl_vec<ProgramInfo>(mScanedPrograms.begin(), mScanedPrograms.end());

            mCallback->onProgramListUpdated(chunk);
        };

    mThread.schedule(task, delay::list);

    return Result::OK;
}

/*Original code,no implement
 * Add stopScan for realization
 * SPRD: Don't implement stopScan after autoScan
 */
Return<void> TunerSession::stopProgramListUpdates() {
    ALOGD("%s", __func__);
    return {};
}

Return<void> TunerSession::isConfigFlagSet(ConfigFlag flag, isConfigFlagSet_cb _hidl_cb) {
    ALOGD("%s(%s)", __func__, toString(flag).c_str());

    _hidl_cb(Result::NOT_SUPPORTED, false);
    return {};
}

Return<Result> TunerSession::setConfigFlag(ConfigFlag flag, bool value) {
    ALOGD("%s(%s, %d)", __func__, toString(flag).c_str(), value);

    return Result::NOT_SUPPORTED;
}

Return<void> TunerSession::setParameters(const hidl_vec<VendorKeyValue>& parameters,
                                         setParameters_cb _hidl_cb) {
    ALOGD("%s parameters length = %d", __func__,parameters.size());
    lock_guard<mutex> lk(mSetParametersMut);
    vector<VendorKeyValue> result;
    for(size_t i = 0; i < parameters.size(); ++i) {
        if("spacing" == parameters[i].key){
            ALOGD("set spacing %s",parameters[i].value.c_str());
            int ret = 0;
            if ("50k" == parameters[i].value) {
                mSpacing = 50;
                ret = setStep(0); //SCAN_STEP_50KHZ 0
            } else if("100k" == parameters[i].value) {
                mSpacing = 100;
                ret = setStep(1); //SCAN_STEP_100KHZ 1
            }
            hidl_vec<VendorKeyValue> vec = {{"spacing",std::to_string(ret)}};
            _hidl_cb(vec);
            return Void();
        }else if("antenna" == parameters[i].key){
            int ret = 0;
            ALOGD("switch antenna %s",parameters[i].value.c_str());
            ret = switchAntenna(parameters[i].value == "0" ? 0 : 1);
            hidl_vec<VendorKeyValue> vec = {{"antenna",std::to_string(ret)}};
            _hidl_cb(vec);
            return Void();
        }else if("stopScan" == parameters[i].key){
            int ret = 0;
            ALOGD("stopScan %s",parameters[i].value.c_str());
            ret = stopScan() ? 1 : 0;
            hidl_vec<VendorKeyValue> vec = {{"stopScan",std::to_string(ret)}};
            _hidl_cb(vec);
            return Void();
        }else if("sprdsetrds" == parameters[i].key){
            ALOGD("set sprdsetrds %s",parameters[i].value.c_str());
            int ret = 0;
            if ("sprdrdson" == parameters[i].value) {
                ret = setRds(1); //set rds on
                sprdrds_state = 0;
                ALOGD("set sprdrds on");
            } else if("sprdrdsoff" == parameters[i].value) {
                ret = setRds(0); //set rds off
                sprdrds_state = 1;
                ALOGD("set sprdrds off");
                }
            hidl_vec<VendorKeyValue> vec = {{"sprdsetrds",std::to_string(ret)}};
            _hidl_cb(vec);
            return Void();
       }
    }
    _hidl_cb({});
    return {};
}

Return<void> TunerSession::getParameters(const hidl_vec<hidl_string>&  keys,
                                         getParameters_cb _hidl_cb) {
    ALOGD("%s keys length = %d", __func__,keys.size());
    for(size_t i = 0; i < keys.size(); ++i) {
        if(keys[i] == "rssi") {
            int rssi = getRssi();
            hidl_vec<VendorKeyValue> vec = {{"rssi",std::to_string(rssi)}};
            _hidl_cb(vec);
            return Void();
        }
    }
    _hidl_cb({});
    return {};
}

Return<void> TunerSession::close() {
    ALOGD("%s", __func__);
    lock_guard<mutex> lk(mMut);
    if (mIsClosed) return {};
    {
      ALOGD("TunerSession close before set mIsClosed true...");
      lock_guard<mutex> lk(mRdsMut);
      mIsClosed = true;
      setRdsOnOff(false);
      closeDev();
      mCondRds.notify_one();
      ALOGD("TunerSession close after set mIsClosed true...");
    }
    mThread.cancelAll();

    return {};
}

std::optional<AmFmBandRange> TunerSession::getAmFmRangeLocked() const {
    if (!mIsTuneCompleted) {
        ALOGW("tune operation in process");
        return {};
    }
    if (!utils::hasId(mCurrentProgram, IdentifierType::AMFM_FREQUENCY)) return {};

    auto freq = utils::getId(mCurrentProgram, IdentifierType::AMFM_FREQUENCY);
    for (auto&& range : module().getAmFmConfig().ranges) {
        if (range.lowerBound <= freq && range.upperBound >= freq) return range;
    }

    return {};
}

// set rds function on/off, remove to BroadcastRadio
bool TunerSession::setRdsOnOff(bool rdsOn){
    ALOGD("setRdsOnOff, set rds :%d", rdsOn);
    int result = 0;
    if(mIsRdsSupported){
      result = setRds(rdsOn);
    }else{
      ALOGD("setRdsOnOff, rds not support");
      return false;
    }

    return result ? true:false;
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace broadcastradio
}  // namespace hardware
}  // namespace sprd
}  // namespace vendor
