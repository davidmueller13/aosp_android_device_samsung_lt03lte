/*
 * Copyright (C) 2012, The CyanogenMod Project
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

/**
* @file CameraWrapper.cpp
*
* This file wraps a vendor camera module.
*
*/

#define LOG_NDEBUG 1
#define LOG_PARAMETERS

#define LOG_TAG "CameraWrapper"
#include <cutils/log.h>

#include <utils/threads.h>
#include <utils/String8.h>
#include <hardware/hardware.h>
#include <hardware/camera.h>
#include <camera/Camera.h>
#include <camera/CameraParameters.h>

static android::Mutex gCameraWrapperLock;
static camera_module_t *gVendorModule = 0;

static char **fixed_set_params = NULL;

static int camera_device_open(const hw_module_t* module, const char* name,
                hw_device_t** device);
static int camera_device_close(hw_device_t* device);
static int camera_get_number_of_cameras(void);
static int camera_get_camera_info(int camera_id, struct camera_info *info);
static int camera_send_command(struct camera_device * device, int32_t cmd,
                int32_t arg1, int32_t arg2);
static void camera_get_vendor_tag_ops(vendor_tag_ops_t* ops);
static int camera_open_legacy(const struct hw_module_t* module,
            const char* id, uint32_t halVersion, struct hw_device_t** device);

static struct hw_module_methods_t camera_module_methods = {
        open: camera_device_open
};

camera_module_t HAL_MODULE_INFO_SYM = {
    common: {
         tag: HARDWARE_MODULE_TAG,
         module_api_version: CAMERA_MODULE_API_VERSION_1_0,
         hal_api_version: HARDWARE_HAL_API_VERSION, 
         id: CAMERA_HARDWARE_MODULE_ID,
         name: "MSM8974 Camera Wrapper",
         author: "The CyanogenMod Project",
         methods: &camera_module_methods,
         dso: NULL, /* remove compilation warnings */
         reserved: {0}, /* remove compilation warnings */
    },
    get_number_of_cameras: camera_get_number_of_cameras,
    get_camera_info: camera_get_camera_info,
    set_callbacks: NULL,
    get_vendor_tag_ops: NULL,
    open_legacy: camera_open_legacy,
    reserved: {0}, /* remove compilation warnings */
};

typedef struct wrapper_camera_device {
    camera_device_t base;
    int id;
    camera_device_t *vendor;
} wrapper_camera_device_t;

#define VENDOR_CALL(device, func, ...) ({ \
    wrapper_camera_device_t *__wrapper_dev = (wrapper_camera_device_t*) device; \
    __wrapper_dev->vendor->ops->func(__wrapper_dev->vendor, ##__VA_ARGS__); \
})

#define CAMERA_ID(device) (((wrapper_camera_device_t *)(device))->id)

static int check_vendor_module()
{
    int rv = 0;
    ALOGE("%s:%u ->", __func__,__LINE__);

    if(gVendorModule)
    {
        ALOGE("%s:%u <- %d", __func__,__LINE__, 0);
        return 0;
    }

    rv = hw_get_module_by_class("camera", "vendor",
            (const hw_module_t **)&gVendorModule);

    if (rv)
        ALOGE("failed to open vendor camera module");
    
    ALOGE("%s:%u <- %d", __func__,__LINE__, rv);
    return rv;
}

static void camera_get_vendor_tag_ops(vendor_tag_ops_t* ops)
{
    ALOGE("%s:%u", __func__,__LINE__);
    return;
}

static int camera_open_legacy(const struct hw_module_t* module,
            const char* id, uint32_t halVersion, struct hw_device_t** device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    return -ENOSYS;
}

#define KEY_VIDEO_HFR_VALUES "video-hfr-values"

static char * camera_fixup_getparams(int id, const char * settings)
{
    android::CameraParameters params;
    
    ALOGE("%s:%u", __func__,__LINE__);
    
    params.unflatten(android::String8(settings));

#ifdef LOG_PARAMETERS
    ALOGV("%s: original parameters:", __FUNCTION__);
    params.dump();
#endif

    /* If the vendor has HFR values but doesn't also expose that
     * this can be turned off, fixup the params to tell the Camera
     * that it really is okay to turn it off.
     */

    const char* hfrValues = params.get(KEY_VIDEO_HFR_VALUES);
    if (hfrValues && *hfrValues && ! strstr(hfrValues, "off")) {
        char tmp[strlen(hfrValues) + 4 + 1];
        sprintf(tmp, "%s,off", hfrValues);
        params.set(KEY_VIDEO_HFR_VALUES, tmp);
    }

    android::String8 strParams = params.flatten();
    char *ret = strdup(strParams.string());

#ifdef LOG_PARAMETERS
    ALOGV("%s: fixed parameters:", __FUNCTION__);
    params.dump();
#endif
    return ret;
}

char * camera_fixup_setparams(struct camera_device * device, const char * settings)
{
    int id = CAMERA_ID(device);
    android::CameraParameters params;
    
    ALOGE("%s:%u", __func__,__LINE__);
    
    params.unflatten(android::String8(settings));

#ifdef LOG_PARAMETERS
    ALOGV("%s: original parameters:", __FUNCTION__);
    params.dump();
#endif

    const char* recordingHint = params.get(android::CameraParameters::KEY_RECORDING_HINT);
    bool isVideo = recordingHint && !strcmp(recordingHint, "true");

#if 0
    if (isVideo) {
        params.set("dis", "disable");
        params.set(android::CameraParameters::KEY_ZSL, "off");
    } else {
        params.set(android::CameraParameters::KEY_ZSL, "on");
    }
#endif

    android::String8 strParams = params.flatten();

    if (fixed_set_params[id])
        free(fixed_set_params[id]);
    fixed_set_params[id] = strdup(strParams.string());
    char *ret = fixed_set_params[id];

#ifdef LOG_PARAMETERS
    ALOGV("%s: fixed parameters:", __FUNCTION__);
    params.dump();
#endif

    return ret;
}

/*******************************************************************
 * implementation of camera_device_ops functions
 *******************************************************************/

int camera_set_preview_window(struct camera_device * device,
        struct preview_stream_ops *window)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return -EINVAL;

    return VENDOR_CALL(device, set_preview_window, window);
}

void camera_set_callbacks(struct camera_device * device,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void *user)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return;

    VENDOR_CALL(device, set_callbacks, notify_cb, data_cb, data_cb_timestamp, get_memory, user);
}

void camera_enable_msg_type(struct camera_device * device, int32_t msg_type)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return;

    VENDOR_CALL(device, enable_msg_type, msg_type);
}

void camera_disable_msg_type(struct camera_device * device, int32_t msg_type)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return;

    VENDOR_CALL(device, disable_msg_type, msg_type);
}

int camera_msg_type_enabled(struct camera_device * device, int32_t msg_type)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return 0;

    return VENDOR_CALL(device, msg_type_enabled, msg_type);
}

int camera_start_preview(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return -EINVAL;

    return VENDOR_CALL(device, start_preview);
}

void camera_stop_preview(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return;

    VENDOR_CALL(device, stop_preview);
}

int camera_preview_enabled(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return -EINVAL;

    return VENDOR_CALL(device, preview_enabled);
}

int camera_store_meta_data_in_buffers(struct camera_device * device, int enable)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return -EINVAL;

    return VENDOR_CALL(device, store_meta_data_in_buffers, enable);
}

int camera_start_recording(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return EINVAL;

    return VENDOR_CALL(device, start_recording);
}

void camera_stop_recording(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return;


    VENDOR_CALL(device, stop_recording);
}

int camera_recording_enabled(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return -EINVAL;

    return VENDOR_CALL(device, recording_enabled);
}

void camera_release_recording_frame(struct camera_device * device,
                const void *opaque)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return;

    VENDOR_CALL(device, release_recording_frame, opaque);
}

int camera_auto_focus(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return -EINVAL;


    return VENDOR_CALL(device, auto_focus);
}

int camera_cancel_auto_focus(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return -EINVAL;

    return VENDOR_CALL(device, cancel_auto_focus);
}

int camera_take_picture(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return -EINVAL;

    return VENDOR_CALL(device, take_picture);
}

int camera_cancel_picture(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return -EINVAL;

    return VENDOR_CALL(device, cancel_picture);
}

int camera_set_parameters(struct camera_device * device, const char *params)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return -EINVAL;

    char *tmp = NULL;
    tmp = camera_fixup_setparams(device, params);

    int ret = VENDOR_CALL(device, set_parameters, tmp);
    return ret;
}

char* camera_get_parameters(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return NULL;

    char* params = VENDOR_CALL(device, get_parameters);

    char * tmp = camera_fixup_getparams(CAMERA_ID(device), params);
    VENDOR_CALL(device, put_parameters, params);
    params = tmp;

    return params;
}

static void camera_put_parameters(struct camera_device *device, char *params)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(params)
        free(params);
}

int camera_send_command(struct camera_device * device,
            int32_t cmd, int32_t arg1, int32_t arg2)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return -EINVAL;

    return VENDOR_CALL(device, send_command, cmd, arg1, arg2);
}

void camera_release(struct camera_device * device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device, (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if(!device)
        return;

    VENDOR_CALL(device, release);
}

int camera_dump(struct camera_device * device, int fd)
{
    ALOGE("%s:%u", __func__,__LINE__);
    if(!device)
        return -EINVAL;

    return VENDOR_CALL(device, dump, fd);
}

extern "C" void heaptracker_free_leaked_memory(void);

int camera_device_close(hw_device_t* device)
{
    ALOGE("%s:%u", __func__,__LINE__);
    int ret = 0;
    wrapper_camera_device_t *wrapper_dev = NULL;

    ALOGI("%s", __FUNCTION__);

    android::Mutex::Autolock lock(gCameraWrapperLock);

    if (!device) {
        ret = -EINVAL;
        goto done;
    }

    for (int i = 0; i < camera_get_number_of_cameras(); i++) {
        if (fixed_set_params[i])
            free(fixed_set_params[i]);
    }

    wrapper_dev = (wrapper_camera_device_t*) device;

    wrapper_dev->vendor->common.close((hw_device_t*)wrapper_dev->vendor);
    if (wrapper_dev->base.ops)
        free(wrapper_dev->base.ops);
    free(wrapper_dev);
done:
#ifdef HEAPTRACKER
    heaptracker_free_leaked_memory();
#endif
    return ret;
}

/*******************************************************************
 * implementation of camera_module functions
 *******************************************************************/

/* open device handle to one of the cameras
 *
 * assume camera service will keep singleton of each camera
 * so this function will always only be called once per camera instance
 */

int camera_device_open(const hw_module_t* module, const char* name,
                hw_device_t** device)
{
    int rv = 0;
    int num_cameras = 0;
    int cameraid;
    wrapper_camera_device_t* camera_device = NULL;
    camera_device_ops_t* camera_ops = NULL;

    android::Mutex::Autolock lock(gCameraWrapperLock);

    ALOGE("%s:%u", __func__,__LINE__);
    ALOGI("camera_device open");

    if (name != NULL) {
        if (check_vendor_module())
            return -EINVAL;

        cameraid = atoi(name);
        num_cameras = gVendorModule->get_number_of_cameras();

        fixed_set_params = (char **) malloc(sizeof(char *) * num_cameras);
        if (!fixed_set_params) {
            ALOGE("parameter memory allocation fail");
            rv = -ENOMEM;
            goto fail;
        }
        memset(fixed_set_params, 0, sizeof(char *) * num_cameras);

        if(cameraid > num_cameras)
        {
            ALOGE("camera service provided cameraid out of bounds, "
                    "cameraid = %d, num supported = %d",
                    cameraid, num_cameras);
            rv = -EINVAL;
            goto fail;
        }

        camera_device = (wrapper_camera_device_t*)malloc(sizeof(*camera_device));
        if(!camera_device)
        {
            ALOGE("camera_device allocation fail");
            rv = -ENOMEM;
            goto fail;
        }
        memset(camera_device, 0, sizeof(*camera_device));
        camera_device->id = cameraid;

        rv = gVendorModule->common.methods->open((const hw_module_t*)gVendorModule, name, (hw_device_t**)&(camera_device->vendor));
        if (rv) {
            ALOGE("vendor camera open fail");
            goto fail;
        }
        ALOGI("%s: got vendor camera device 0x%08X", __FUNCTION__, (uintptr_t)(camera_device->vendor));

        camera_ops = (camera_device_ops_t*)malloc(sizeof(*camera_ops));
        if(!camera_ops)
        {
            ALOGE("camera_ops allocation fail");
            rv = -ENOMEM;
            goto fail;
        }

        memset(camera_ops, 0, sizeof(*camera_ops));

        camera_device->base.common.tag = HARDWARE_DEVICE_TAG;
        camera_device->base.common.version = 0;
        camera_device->base.common.module = (hw_module_t *)(module);
        camera_device->base.common.close = camera_device_close;
        camera_device->base.ops = camera_ops;

        camera_ops->set_preview_window = camera_set_preview_window;
        camera_ops->set_callbacks = camera_set_callbacks;
        camera_ops->enable_msg_type = camera_enable_msg_type;
        camera_ops->disable_msg_type = camera_disable_msg_type;
        camera_ops->msg_type_enabled = camera_msg_type_enabled;
        camera_ops->start_preview = camera_start_preview;
        camera_ops->stop_preview = camera_stop_preview;
        camera_ops->preview_enabled = camera_preview_enabled;
        camera_ops->store_meta_data_in_buffers = camera_store_meta_data_in_buffers;
        camera_ops->start_recording = camera_start_recording;
        camera_ops->stop_recording = camera_stop_recording;
        camera_ops->recording_enabled = camera_recording_enabled;
        camera_ops->release_recording_frame = camera_release_recording_frame;
        camera_ops->auto_focus = camera_auto_focus;
        camera_ops->cancel_auto_focus = camera_cancel_auto_focus;
        camera_ops->take_picture = camera_take_picture;
        camera_ops->cancel_picture = camera_cancel_picture;
        camera_ops->set_parameters = camera_set_parameters;
        camera_ops->get_parameters = camera_get_parameters;
        camera_ops->put_parameters = camera_put_parameters;
        camera_ops->send_command = camera_send_command;
        camera_ops->release = camera_release;
        camera_ops->dump = camera_dump;

        *device = &camera_device->base.common;
    }

    return rv;

fail:
    if(camera_device) {
        free(camera_device);
        camera_device = NULL;
    }
    if(camera_ops) {
        free(camera_ops);
        camera_ops = NULL;
    }
    *device = NULL;
    return rv;
}

int camera_get_number_of_cameras(void)
{
    int numberOfCameras = 0;
    ALOGE("%s:%u ->", __func__,__LINE__);
    
    if (check_vendor_module())
        return 0;
        
    numberOfCameras = gVendorModule->get_number_of_cameras();
    ALOGE("%s:%u <- %d", __func__,__LINE__, numberOfCameras);
    return numberOfCameras;
}

int camera_get_camera_info(int camera_id, struct camera_info *info)
{
    ALOGE("%s:%u", __func__,__LINE__);
    if (check_vendor_module())
        return 0;
    return gVendorModule->get_camera_info(camera_id, info);
}
