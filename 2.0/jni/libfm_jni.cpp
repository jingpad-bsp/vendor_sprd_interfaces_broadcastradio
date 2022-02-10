/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include <jni.h>
#include "../default/fmr.h"
#include "jni_helper.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "FMLIB_JNI"

static int g_idx = -1;
extern struct fmr_ds fmr_data;

jboolean nativeOpenDev(JNIEnv *env, jobject thiz)
{
    (void) env;
	(void) thiz;
    int ret = 0;

    ret = FMR_open_dev(g_idx); // if success, then ret = 0; else ret < 0

    LOGD("%s, [ret=%d]\n", __func__, ret);
    return ret?JNI_FALSE:JNI_TRUE;
}

jboolean nativeCloseDev(JNIEnv *env, jobject thiz)
{
    (void) env;
	(void) thiz;
    int ret = 0;

    ret = FMR_close_dev(g_idx);

    LOGD("%s, [ret=%d]\n", __func__, ret);
    return ret?JNI_FALSE:JNI_TRUE;
}

jboolean nativePowerUp(JNIEnv *env, jobject thiz, jfloat freq)
{
    (void) env;
	(void) thiz;
    int ret = 0;
    int tmp_freq;

    LOGI("%s, [freq=%d]\n", __func__, (int)freq);
    tmp_freq = (int)(freq * 10);        //Eg, 87.5 * 10 --> 875
    ret = FMR_pwr_up(g_idx, tmp_freq);

    LOGD("%s, [ret=%d]\n", __func__, ret);
    return ret?JNI_FALSE:JNI_TRUE;
}

jboolean nativePowerDown(JNIEnv *env, jobject thiz, jint type)
{
    (void) env;
	(void) thiz;
    int ret = 0;

    ret = FMR_pwr_down(g_idx, type);

    LOGD("%s, [ret=%d]\n", __func__, ret);
    return ret?JNI_FALSE:JNI_TRUE;
}

jboolean nativeTune(JNIEnv *env, jobject thiz, jfloat freq)
{
    (void) env;
	(void) thiz;
    int ret = 0;
    int tmp_freq;
    tmp_freq = (int)(freq * 100);        //Eg, 87.5 * 100 --> 8750
    ret = FMR_tune(g_idx, tmp_freq);

    LOGD("%s, [ret=%d]\n", __func__, ret);
    return ret?JNI_FALSE:JNI_TRUE;
}

jfloat nativeSeek(JNIEnv *env, jobject thiz, jfloat freq, jboolean isUp) //jboolean isUp;
{
    (void) env;
	(void) thiz;
    int ret = 0;
    int tmp_freq;
    int ret_freq;
    float val;

    tmp_freq = (int)(freq * 100);       //Eg, 87.55 * 100 --> 8755
    ret = FMR_set_mute(g_idx, 1);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
    }
    LOGD("%s, [mute] [ret=%d]\n", __func__, ret);

    ret = FMR_seek(g_idx, tmp_freq, (int)isUp, &ret_freq, 10);
    if (ret) {
        ret_freq = tmp_freq; //seek error, so use original freq
    }

    LOGD("%s, [freq=%d] [ret=%d]\n", __func__, ret_freq, ret);

    val = (float)ret_freq/100;   //Eg, 8755 / 100 --> 87.55

    return val;
}

jshortArray nativeAutoScan(JNIEnv *env, jobject thiz,  jint startFreq)
{
	(void) thiz;
#define FM_SCAN_CH_SIZE_MAX 200
    int ret = 0;
    jshortArray scanChlarray;
    int chl_cnt = FM_SCAN_CH_SIZE_MAX;
    int ScanTBL[FM_SCAN_CH_SIZE_MAX];

    LOGI("%s, [tbl=%p]\n", __func__, ScanTBL);
    FMR_Pre_Search(g_idx);
    ret = FMR_scan(g_idx, ScanTBL, &chl_cnt, startFreq, 10);
    if (ret < 0) {
        LOGE("scan failed!\n");
        scanChlarray = NULL;
        goto out;
    }
    if (chl_cnt > 0) {
        scanChlarray = env->NewShortArray(chl_cnt);
        env->SetShortArrayRegion(scanChlarray, 0, chl_cnt, (const jshort*)&ScanTBL[0]);
    } else {
        LOGE("cnt error, [cnt=%d]\n", chl_cnt);
        scanChlarray = NULL;
    }
    FMR_Restore_Search(g_idx);

    if (fmr_data.scan_stop == fm_true) {
        ret = FMR_tune(g_idx, fmr_data.cur_freq);
        LOGI("scan stop!!! tune ret=%d",ret);
    }

out:
    LOGD("%s, [cnt=%d] [ret=%d]\n", __func__, chl_cnt, ret);
    return scanChlarray;
}

jshort nativeReadRds(JNIEnv *env, jobject thiz)
{
    (void) env;
	(void) thiz;
    int ret = 0;
    uint16_t status = 0;

    ret = FMR_read_rds_data(g_idx, &status);

    if (ret) {
        //LOGE("%s,status = 0,[ret=%d]\n", __func__, ret);
        status = 0; //there's no event or some error happened
    }

    return status;
}

jbyteArray nativeGetPs(JNIEnv *env, jobject thiz)
{
	(void) thiz;
    int ret = 0;
    jbyteArray PSname;
    uint8_t *ps = NULL;
    int ps_len = 0;

    ret = FMR_get_ps(g_idx, &ps, &ps_len);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
        return NULL;
    }
    PSname = env->NewByteArray(ps_len);
    env->SetByteArrayRegion(PSname, 0, ps_len, (const jbyte*)ps);
    //LOGD("%s, [ret=%d]\n", __func__, ret);
    return PSname;
}

jint nativeGetBler(JNIEnv *env, jobject thiz)
{
    (void) env;
	(void) thiz;
    int ret = 0;
    int bler = -1;

    ret = FMR_get_bler(g_idx, &bler);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
    }
    LOGD("%s, [bler=%d] [ret=%d]\n", __func__, bler, ret);
    return bler;
}

jint nativeGetRssi(JNIEnv *env, jobject thiz)
{
    (void) env;
	(void) thiz;
    int ret = 0;
    int rssi = -1;

    ret = FMR_get_rssi(g_idx, &rssi);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
    }
    LOGD("%s, [rssi=%d] [ret=%d]\n", __func__, rssi, ret);
    return rssi;
}

jint nativeGetSnr(JNIEnv *env, jobject thiz)
{
    (void) env;
	(void) thiz;
    int ret = 0;
    int snr = -1;

    ret = FMR_get_snr(g_idx, &snr);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
    }
    LOGD("%s, [snr=%d] [ret=%d]\n", __func__, snr, ret);
    return snr;
}

jint nativeReadRegParm(JNIEnv *env, jobject thiz, jobject para)
{
	(void) thiz;
    int ret = 0;
    fm_reg_ctl_parm fcp;
    JNIHelper helper(env); //////
    memset(&fcp, 0, sizeof(fcp));

    fcp.rw_flag = helper.getByteField(para,"rw_flag"); /////
    fcp.addr = helper.getIntField(para,"addr");  ///////
    fcp.err = helper.getByteField(para,"err"); ///////
    ret = FMR_rw_reg(g_idx, &fcp);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
        return -1;
    }

    helper.setByteField(para,"err",fcp.err); //////
    helper.setIntField(para,"addr",fcp.addr); ///////
    helper.setIntField(para,"val",fcp.val); ////////
    helper.setByteField(para,"rw_flag",fcp.rw_flag); //////
    LOGD("%s, [ret=%d][err=%d] [addr=0x%8x] [val=%d] [rw_flag=%d]\n", \
        __func__, ret, fcp.err,fcp.addr,fcp.val,fcp.rw_flag);

    return ret;
}

jint nativeWriteRegParm(JNIEnv *env, jobject thiz, jobject para)
{
	(void) thiz;
    int ret = 0;
    fm_reg_ctl_parm fcp;
    JNIHelper helper(env);
    memset(&fcp, 0, sizeof(fcp));

    fcp.err = helper.getByteField(para,"err");
    fcp.addr = helper.getIntField(para,"addr");
    fcp.val = helper.getIntField(para,"val");
    fcp.rw_flag = helper.getByteField(para,"rw_flag");

    LOGD("%s, [err=%d] [addr=0x%8x] [val=%d] [rw_flag=%d]\n",\
        __func__, fcp.err, fcp.addr, fcp.val, fcp.rw_flag);
    ret = FMR_rw_reg(g_idx, &fcp);

    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
    }
    LOGD("%s, [ret=%d]\n", __func__, ret);

    return ret;
}

jint nativeGetTuneParm(JNIEnv *env, jobject thiz, jobject para)
{
	(void) thiz;
    int ret = 0;
    fm_seek_criteria_parm parm;
    JNIHelper helper(env);
    memset(&parm, 0, sizeof(parm));

    ret = FMR_get_tune(g_idx, &parm);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
        return -1;
    }

    helper.setIntField(para,"rssi_th",parm.rssi_th);
    helper.setByteField(para,"snr_th",parm.snr_th);
    helper.setIntField(para,"freq_offset_th",parm.freq_offset_th);
    helper.setIntField(para,"pilot_power_th",parm.pilot_power_th);
    helper.setIntField(para,"noise_power_th",parm.noise_power_th);
    LOGD("%s, [ret=%d]\n", __func__, ret);

    return ret;
}

jint nativeSetTuneParm(JNIEnv *env, jobject thiz, jobject para)
{
	(void) thiz;
    int ret = 0;
    JNIHelper helper(env);
    fm_seek_criteria_parm parm;
    memset(&parm, 0, sizeof(parm));

    parm.rssi_th = (unsigned char)helper.getIntField(para,"rssi_th");
    parm.snr_th = helper.getByteField(para,"snr_th");
    parm.freq_offset_th = helper.getIntField(para,"freq_offset_th");
    parm.pilot_power_th = helper.getIntField(para,"pilot_power_th");
    parm.noise_power_th = helper.getIntField(para,"noise_power_th");
    ret = FMR_set_tune(g_idx, &parm);

    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
    }
    LOGD("%s, [ret=%d]\n", __func__, ret);

    return ret;
}

jint nativeGetAudioParm(JNIEnv *env, jobject thiz,jobject para)
{
	(void) thiz;
    int ret = 0;
    fm_audio_threshold_parm parm;
    JNIHelper helper(env);
    memset(&parm, 0, sizeof(parm));

    ret = FMR_get_audio(g_idx, &parm);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
        return -1;
    }

    helper.setIntField(para,"hbound",parm.hbound);
    helper.setIntField(para,"lbound",parm.lbound);
    helper.setIntField(para,"power_th",parm.power_th);
    helper.setByteField(para,"phyt",parm.phyt);
    helper.setByteField(para,"snr_th",parm.snr_th);
    LOGD("%s, [ret=%d]\n", __func__, ret);

    return ret;
}

jint nativeSetAudioParm(JNIEnv *env, jobject thiz, jobject para)
{
	(void) thiz;
    int ret = 0;
    JNIHelper helper(env);
    fm_audio_threshold_parm parm;
    memset(&parm, 0, sizeof(parm));

    parm.hbound = helper.getIntField(para,"hbound");
    parm.lbound = helper.getIntField(para,"lbound");
    parm.power_th = helper.getIntField(para,"power_th");
    parm.phyt = helper.getByteField(para,"phyt");
    parm.snr_th = helper.getByteField(para,"snr_th");

    ret = FMR_set_audio(g_idx, &parm);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
    }
    LOGD("%s, [ret=%d]\n", __func__, ret);

    return ret;
}

jbyteArray nativeGetLrText(JNIEnv *env, jobject thiz)
{
	(void) thiz;
    int ret = 0;
    jbyteArray LastRadioText;
    uint8_t *rt = NULL;
    int rt_len = 0;

    ret = FMR_get_rt(g_idx, &rt, &rt_len);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
        return NULL;
    }
    LastRadioText = env->NewByteArray(rt_len);
    env->SetByteArrayRegion(LastRadioText, 0, rt_len, (const jbyte*)rt);
    //LOGD("%s, [ret=%d]\n", __func__, ret);
    return LastRadioText;
}

jshort nativeActiveAf(JNIEnv *env, jobject thiz)
{
    (void) env;
	(void) thiz;
    int ret = 0;
    jshort ret_freq = 0;

    ret = FMR_active_af(g_idx, (uint16_t*)&ret_freq);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
        return 0;
    }
    LOGD("%s, [ret=%d]\n", __func__, ret);
    return ret_freq;
}

jshortArray nativeGetAFList(JNIEnv *env, jobject thiz)
{
	(void) thiz;
    int ret = 0;
    jshortArray AFList;
    char *af = NULL;
    int af_len = 0;

    //ret = FMR_get_af(g_idx, &af, &af_len); // If need, we should implemate this API
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
        return NULL;
    }
    AFList = env->NewShortArray(af_len);
    env->SetShortArrayRegion(AFList, 0, af_len, (const jshort*)af);
    LOGD("%s, [ret=%d]\n", __func__, ret);
    return AFList;
}

jint nativeSetRds(JNIEnv *env, jobject thiz, jboolean rdson)
{
    (void) env;
	(void) thiz;
    int ret = 0;
    int onoff = -1;

    if (rdson == JNI_TRUE) {
        onoff = FMR_RDS_ON;
    } else {
        onoff = FMR_RDS_OFF;
    }
    ret = FMR_turn_on_off_rds(g_idx, onoff);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
    }
    LOGD("%s, [onoff=%d] [ret=%d]\n", __func__, onoff, ret);
    return ret?JNI_FALSE:JNI_TRUE;
}

jboolean nativeStopScan(JNIEnv *env, jobject thiz)
{
    (void) env;
	(void) thiz;
    int ret = 0;

    ret = FMR_stop_scan(g_idx);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
    }
    LOGD("%s, [ret=%d]\n", __func__, ret);
    return ret?JNI_FALSE:JNI_TRUE;
}

jint nativeSetMute(JNIEnv *env, jobject thiz, jboolean mute)
{
    (void) env;
	(void) thiz;
    int ret = 0;

    ret = FMR_set_mute(g_idx, (int)mute);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
    }
    LOGD("%s, [mute=%d] [ret=%d]\n", __func__, (int)mute, ret);
    return ret?JNI_FALSE:JNI_TRUE;
}

/******************************************
 * Inquiry if RDS is support in driver.
 * Parameter:
 *      None
 *Return Value:
 *      1: support
 *      0: NOT support
 *      -1: error
 ******************************************/
jint nativeIsRdsSupport(JNIEnv *env, jobject thiz)
{
    (void) env;
	(void) thiz;
    int ret = 0;
    int supt = -1;

    ret = FMR_is_rdsrx_support(g_idx, &supt);
    if (ret) {
        LOGE("%s, error, [ret=%d]\n", __func__, ret);
    }
    LOGD("%s, [supt=%d] [ret=%d]\n", __func__, supt, ret);
    return supt;
}

/******************************************
 * SwitchAntenna
 * Parameter:
 *      antenna:
                0 : switch to long antenna
                1: switch to short antenna
 *Return Value:
 *          0: Success
 *          1: Failed
 *          2: Not support
 ******************************************/
jint nativeSwitchAntenna(JNIEnv *env, jobject thiz, jint antenna)
{
    (void) env;
	(void) thiz;
    int ret = 0;
    jint jret = 0;
    int ana = -1;

    if (0 == antenna) {
        ana = FM_LONG_ANA;
    } else if (1 == antenna) {
        ana = FM_SHORT_ANA;
    } else {
        LOGE("%s:fail, para error\n", __func__);
        jret = JNI_FALSE;
        goto out;
    }
    ret = FMR_ana_switch(g_idx, ana);
    if (ret == -ERR_UNSUPT_SHORTANA) {
        LOGW("Not support switchAntenna\n");
        jret = 2;
    } else if (ret) {
        LOGE("switchAntenna(), error\n");
        jret = 1;
    } else {
        jret = 0;
    }
out:
    LOGD("%s: [antenna=%d] [ret=%d]\n", __func__, ana, ret);
    return jret;
}

static const char *classPathNameRx = "com/android/fmradio/FmNative";

static JNINativeMethod methodsRx[] = {
    {"openDev", "()Z", (void*)nativeOpenDev },  //1
    {"closeDev", "()Z", (void*)nativeCloseDev }, //2
    {"powerUp", "(F)Z", (void*)nativePowerUp },  //3
    {"powerDown", "(I)Z", (void*)nativePowerDown }, //4
    {"tune", "(F)Z", (void*)nativeTune },          //5
    {"seek", "(FZ)F", (void*)nativeSeek },         //6
    {"autoScan",  "(I)[S", (void*)nativeAutoScan }, //7
    {"stopScan",  "()Z", (void*)nativeStopScan },  //8
    {"setRds",    "(Z)I", (void*)nativeSetRds  },  //10
    {"readRds",   "()S", (void*)nativeReadRds },  //11 will pending here for get event status
    {"getPs",     "()[B", (void*)nativeGetPs  },  //12
    {"getLrText", "()[B", (void*)nativeGetLrText}, //13
    {"activeAf",  "()S", (void*)nativeActiveAf},   //14
    {"setMute",	"(Z)I", (void*)nativeSetMute},  //15
    {"isRdsSupport",	"()I", (void*)nativeIsRdsSupport},  //16
    {"switchAntenna", "(I)I", (void*)nativeSwitchAntenna}, //17
    {"getBler",    "()I", (void*)nativeGetBler  },  //18
    {"getRssi",    "()I", (void*)nativeGetRssi  },
    {"getSnr",    "()I", (void*)nativeGetSnr  },
    {"getTuneParm",    "(Lcom/android/fmradio/FmNative$FmSeekCriteriaParms;)I", (void*)nativeGetTuneParm  },
    {"setTuneParm",    "(Lcom/android/fmradio/FmNative$FmSeekCriteriaParms;)I", (void*)nativeSetTuneParm  },
    {"getAudioParm",    "(Lcom/android/fmradio/FmNative$FmAudioThresholdParms;)I", (void*)nativeGetAudioParm  },
    {"setAudioParm",    "(Lcom/android/fmradio/FmNative$FmAudioThresholdParms;)I", (void*)nativeSetAudioParm  },
    {"readRegParm",     "(Lcom/android/fmradio/FmNative$FmRegCtlParms;)I", (void*)nativeReadRegParm  },
    {"writeRegParm",     "(Lcom/android/fmradio/FmNative$FmRegCtlParms;)I", (void*)nativeWriteRegParm  },
};

/*
 * Register several native methods for one class.
 */
static jint registerNativeMethods(JNIEnv* env, const char* className,
    JNINativeMethod* gMethods, int numMethods)
{
    jclass clazz;

    clazz = env->FindClass(className);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (clazz == NULL) {
        LOGE("Native registration unable to find class '%s'", className);
        return JNI_FALSE;
    }
    if (env->RegisterNatives(clazz, gMethods, numMethods) < 0) {
        LOGE("RegisterNatives failed for '%s'", className);
        return JNI_FALSE;
    }

    LOGD("%s, success\n", __func__);
    return JNI_TRUE;
}

/*
 * Register native methods for all classes we know about.
 *
 * returns JNI_TRUE on success.
 */
static jint registerNatives(JNIEnv* env)
{
    jint ret = JNI_FALSE;

    if (registerNativeMethods(env, classPathNameRx,methodsRx,
        sizeof(methodsRx) / sizeof(methodsRx[0]))) {
        ret = JNI_TRUE;
    }

    LOGD("%s, done\n", __func__);
    return ret;
}

// ----------------------------------------------------------------------------

/*
 * This is called by the VM when the shared library is first loaded.
 */

typedef union {
    JNIEnv* env;
    void* venv;
} UnionJNIEnvToVoid;

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    (void) reserved;
    UnionJNIEnvToVoid uenv;
    uenv.venv = NULL;
    jint result = -1;
    JNIEnv* env = NULL;

    LOGI("JNI_OnLoad");

    if (vm->GetEnv(&uenv.venv, JNI_VERSION_1_4) != JNI_OK) {
        LOGE("ERROR: GetEnv failed");
        goto fail;
    }
    env = uenv.env;

    if (registerNatives(env) != JNI_TRUE) {
        LOGE("ERROR: registerNatives failed");
        goto fail;
    }

    if ((g_idx = FMR_init()) < 0) {
        goto fail;
    }
    result = JNI_VERSION_1_4;

fail:
    return result;
}

