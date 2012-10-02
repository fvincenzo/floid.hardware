/*
**
** Copyright 2009, The Android-x86 Open Source Project
** Copyright (C) 2012 Wind River Systems, Inc.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
**
** Author: Niels Keeman <nielskeeman@gmail.com>
**
*/

#define LOG_TAG "CameraHardware"
#include <utils/Log.h>

#include "CameraHardware.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <cutils/native_handle.h>
#include <ui/GraphicBufferMapper.h>
#include <gui/ISurfaceTexture.h>
#include "converter.h"

#define MIN_WIDTH           640
#define MIN_HEIGHT          480
#define CAM_SIZE            "640x480"
#define PIXEL_FORMAT        V4L2_PIX_FMT_YUYV
#define CAMHAL_GRALLOC_USAGE GRALLOC_USAGE_HW_TEXTURE | \
    GRALLOC_USAGE_HW_RENDER | \
    GRALLOC_USAGE_SW_READ_RARELY | \
    GRALLOC_USAGE_SW_WRITE_NEVER

extern "C" {
    // void yuyv422_to_yuv420sp(unsigned char*,unsigned char*,int,int);
    void convertYUYVtoRGB565(unsigned char *buf, unsigned char *rgb, int width, int height);
}

namespace android {

    CameraHardware::CameraHardware(int cameraId)
        : mCameraId(cameraId),
        mParameters(),
        mPreviewHeap(0),
        mPreviewRunning(false),
        mRecordRunning(false),
        mCurrentPreviewFrame(0),
        nQueued(0),
        nDequeued(0),
        mNotifyFn(NULL),
        mDataFn(NULL),
        mTimestampFn(NULL),
        mUser(NULL),
        mMsgEnabled(0)
    {
        mNativeWindow=NULL;
        initDefaultParameters();
    }

    void CameraHardware::initDefaultParameters()
    {
        CameraParameters p;

        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "15,30");
        p.setPreviewFrameRate(30);

        String8 previewColorString;
        previewColorString = CameraParameters::PIXEL_FORMAT_YUV420SP;
        previewColorString.append(",");
        previewColorString.append(CameraParameters::PIXEL_FORMAT_YUV420P);
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, previewColorString.string());
        p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, "640x480,352x288,176x144");
        p.setPreviewSize(MIN_WIDTH, MIN_HEIGHT);
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "15,30");
        p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "15,30");

        p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT, CameraParameters::PIXEL_FORMAT_YUV420P);
        p.set(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES, "640x480,352x288,176x144");
        p.setVideoSize(MIN_WIDTH, MIN_HEIGHT);
        p.set(CameraParameters::KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO, "640x480");

        p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, CameraParameters::FOCUS_MODE_FIXED);
        p.set(CameraParameters::KEY_FOCUS_MODE,CameraParameters::FOCUS_MODE_FIXED);
	//p.set(CameraParameters::KEY_FOCUS_DISTANCES,"0.60,1.20,Infinity");

        p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, "640x480,352x288,176x144");
        p.setPictureSize(MIN_WIDTH, MIN_HEIGHT);
        p.set(CameraParameters::KEY_JPEG_QUALITY, "90");
        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,CameraParameters::PIXEL_FORMAT_JPEG);
        p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES, "320x240,0x0");
        p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "320");
        p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "240");
        p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "100");

	//p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "52.6");
        //p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "36.9");

        p.set(CameraParameters::KEY_FOCAL_LENGTH, "2.8"); // typical

        p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
        p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "3");
        p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-3");
        p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0.1");

        if (setParameters(p) != NO_ERROR) {
            LOGE("Failed to set default parameters?!");
        }
    }

    CameraHardware::~CameraHardware()
    {
    }

    // ---------------------------------------------------------------------------

    void CameraHardware::setCallbacks(camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void *arg)
    {
        Mutex::Autolock lock(mLock);
        mNotifyFn = notify_cb;
        mDataFn = data_cb;
        mRequestMemory = get_memory;
        mTimestampFn = data_cb_timestamp;
        mUser = arg;
    }

    int CameraHardware::setPreviewWindow( preview_stream_ops_t *window)
    {
        int err;
        Mutex::Autolock lock(mLock);
        if(mNativeWindow)
            mNativeWindow=NULL;
        if(window==NULL)
        {
            LOGW("Window is Null");
            return 0;
        }
        int width, height;
        mParameters.getPreviewSize(&width, &height);
        mNativeWindow=window;
        mNativeWindow->set_usage(mNativeWindow,CAMHAL_GRALLOC_USAGE);
        mNativeWindow->set_buffers_geometry(
            mNativeWindow,
            width,
            height,
            HAL_PIXEL_FORMAT_YV12);
        err = mNativeWindow->set_buffer_count(mNativeWindow, kBufferCount);
        if (err != 0) {
            LOGE("native_window_set_buffer_count failed: %s (%d)", strerror(-err), -err);

            if ( ENODEV == err ) {
                LOGE("Preview surface abandoned!");
                mNativeWindow = NULL;
            }
        }

        return 0;
    }

    void CameraHardware::enableMsgType(int32_t msgType)
    {
        // Mutex::Autolock lock(mLock);
        mMsgEnabled |= msgType;
    }

    void CameraHardware::disableMsgType(int32_t msgType)
    {
        // Mutex::Autolock lock(mLock);
        mMsgEnabled &= ~msgType;
    }

    bool CameraHardware::msgTypeEnabled(int32_t msgType)
    {
        // Mutex::Autolock lock(mLock);
        return (mMsgEnabled & msgType);
    }


    //-------------------------------------------------------------
    int CameraHardware::previewThread()
    {
        int err;
        int stride;
        int width = camera.width();
        int height = camera.height();

        if (mPreviewRunning) {
            mLock.lock();
            if (mNativeWindow != NULL)
            {
                buffer_handle_t *buf_handle;
                if ((err = mNativeWindow->dequeue_buffer(mNativeWindow,&buf_handle,&stride)) != 0) {
                    LOGW("Surface::dequeueBuffer returned error %d", err);
                    return -1;
                }
                mNativeWindow->lock_buffer(mNativeWindow, buf_handle);
                GraphicBufferMapper &mapper = GraphicBufferMapper::get();

                Rect bounds(width, height);
                void *tempbuf;
                void *dst;
                if(0 == mapper.lock(*buf_handle,CAMHAL_GRALLOC_USAGE, bounds, &dst))
                {
                    // Get preview frame
                    tempbuf=camera.GrabPreviewFrame();

                    yuy2_to_yuv420p((unsigned char *)tempbuf,(unsigned char *)mPreviewHeap->data, width, height);
                    memcpy(dst, (uint8_t*)mPreviewHeap->data, width * height);
                    memcpy((uint8_t*)dst + width * height, (uint8_t*)mPreviewHeap->data + width * height * 5 / 4, width * height / 4);
                    memcpy((uint8_t*)dst + width * height * 5 / 4, (uint8_t*)mPreviewHeap->data + width * height, width * height / 4);
                    mapper.unlock(*buf_handle);
                    mNativeWindow->enqueue_buffer(mNativeWindow,buf_handle);
                    if ((mMsgEnabled & CAMERA_MSG_VIDEO_FRAME ) && mRecordRunning ) {
                        nsecs_t timeStamp = systemTime(SYSTEM_TIME_MONOTONIC);
                        mTimestampFn(timeStamp, CAMERA_MSG_VIDEO_FRAME, mPreviewHeap, 0, mUser);
                    }
                    if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
                        const char * preview_format = mParameters.getPreviewFormat();
                        if (!strcmp(preview_format, CameraParameters::PIXEL_FORMAT_YUV420SP)) {
                            yuy2_to_yuv420sp((unsigned char *)tempbuf,(unsigned char *)mPreviewHeap->data, width, height);
                        }
                        mDataFn(CAMERA_MSG_PREVIEW_FRAME,mPreviewHeap,0,NULL,mUser);
			mPreviewHeap->release(mPreviewHeap);
                    }
                    camera.ReleasePreviewFrame();
                }
            }
            mLock.unlock();
        }

        return NO_ERROR;
    }

    status_t CameraHardware::startPreview()
    {
        int width, height;
        mParameters.getPreviewSize(&width, &height);
        return startPreview(width,height);
    }

    status_t CameraHardware::startPreview(int width, int height)
    {
        int ret = 0;
        int i;
        int stride;
        char devnode[12];
        Mutex::Autolock lock(mLock);
        if (mPreviewThread != 0) {
            //already running
            return INVALID_OPERATION;
        }

        LOGI("CameraHardware::startPreview");
        int framesize= width * height * 1.5 ; //yuv420sp
        if (mPreviewHeap) {
            mPreviewHeap->release(mPreviewHeap);
            mPreviewHeap = 0;
        }
        mPreviewHeap = mRequestMemory(-1, framesize, 1, NULL);

        for( i=0; i<10; i++) {
            sprintf(devnode,"/dev/video%d",i);
            LOGI("trying the node %s width=%d height=%d \n",devnode,width,height);
            ret = camera.Open(devnode, width, height, PIXEL_FORMAT);
            if( ret >= 0)
                break;
        }

        if( ret < 0)
            return -1;

        ret = camera.Init();
        if (ret != 0) {
            LOGI("startPreview: Camera.Init failed\n");
            camera.Close();
            return ret;
        }

        ret = camera.StartStreaming();
        if (ret != 0) {
            LOGI("startPreview: Camera.StartStreaming failed\n");
            camera.Uninit();
            camera.Close();
            return ret;
        }

        mPreviewRunning = true;
        mPreviewThread = new PreviewThread(this);
        return NO_ERROR;
    }

    void CameraHardware::stopPreview()
    {
        sp<PreviewThread> previewThread;

        { // scope for the lock
            Mutex::Autolock lock(mLock);
            mPreviewRunning = false;
            previewThread = mPreviewThread;
        }

        if (previewThread != 0) {
            previewThread->requestExitAndWait();
        }

        if (mPreviewThread != 0) {
            camera.Uninit();
            camera.StopStreaming();
            camera.Close();
        }

        if (mPreviewHeap) {
            mPreviewHeap->release(mPreviewHeap);
            mPreviewHeap = 0;
        }

        Mutex::Autolock lock(mLock);
        mPreviewThread.clear();
    }

    bool CameraHardware::previewEnabled()
    {
        Mutex::Autolock lock(mLock);
        return ((mPreviewThread != 0) );
    }

    status_t CameraHardware::startRecording()
    {
        stopPreview();

        int width, height;
        mParameters.getVideoSize(&width, &height);
        startPreview(width, height);

        Mutex::Autolock lock(mLock);
        mRecordRunning = true;

        return NO_ERROR;
    }

    void CameraHardware::stopRecording()
    {
        {
            Mutex::Autolock lock(mLock);
            mRecordRunning = false;
        }
        stopPreview();

        int width, height;
        mParameters.getPreviewSize(&width, &height);
        startPreview(width, height);
    }

    bool CameraHardware::recordingEnabled()
    {
        return mRecordRunning;
    }

    void CameraHardware::releaseRecordingFrame(const void *opaque)
    {

    }

    // ---------------------------------------------------------------------------

    int CameraHardware::beginAutoFocusThread(void *cookie)
    {
        CameraHardware *c = (CameraHardware *)cookie;
        return c->autoFocusThread();
    }

    int CameraHardware::autoFocusThread()
    {
        if (mMsgEnabled & CAMERA_MSG_FOCUS)
            mNotifyFn(CAMERA_MSG_FOCUS, true, 0, mUser);
        return NO_ERROR;
    }

    status_t CameraHardware::autoFocus()
    {
        Mutex::Autolock lock(mLock);
        if (createThread(beginAutoFocusThread, this) == false)
            return UNKNOWN_ERROR;
        return NO_ERROR;
    }

    status_t CameraHardware::cancelAutoFocus()
    {
        return NO_ERROR;
    }

    int CameraHardware::pictureThread()
    {
        unsigned char *frame;
        int bufferSize;
        int ret = 0;
        struct v4l2_buffer buffer;
        struct v4l2_format format;
        struct v4l2_buffer cfilledbuffer;
        struct v4l2_requestbuffers creqbuf;
        struct v4l2_capability cap;
        int i;
        char devnode[12];
        camera_memory_t* picture = NULL;

        Mutex::Autolock lock(mLock);
        if (mMsgEnabled & CAMERA_MSG_SHUTTER)
            mNotifyFn(CAMERA_MSG_SHUTTER, 0, 0, mUser);

        int width, height;
        mParameters.getPictureSize(&width, &height);
        LOGD("Picture Size: Width = %d \t Height = %d", width, height);

        for(i=0; i<10; i++) {
            sprintf(devnode,"/dev/video%d",i);
            LOGI("trying the node %s \n",devnode);
            ret = camera.Open(devnode, width, height, PIXEL_FORMAT);
            if( ret >= 0)
                break;
        }

        if( ret < 0)
            return -1;

        camera.Init();
        camera.StartStreaming();
        if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
            int framesize= width * height * 2; // yuv422i

            // Get picture frame
            void *tempbuf=camera.GrabPreviewFrame();
            camera_memory_t* picture = mRequestMemory(-1, framesize, 1, NULL);
            memcpy((unsigned char *)picture->data,(unsigned char *)tempbuf,framesize);
            mDataFn(CAMERA_MSG_RAW_IMAGE, picture, 0, NULL, mUser);
            picture->release(picture);
        } else if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE_NOTIFY) {
            mNotifyFn(CAMERA_MSG_RAW_IMAGE_NOTIFY, 0, 0, mUser);
        }

        //TODO xxx : Optimize the memory capture call. Too many memcpy
        if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
            LOGD ("mJpegPictureCallback");
            picture = camera.GrabJpegFrame(mRequestMemory);
            mDataFn(CAMERA_MSG_COMPRESSED_IMAGE,picture,0,NULL ,mUser);
            picture->release(picture);
        }

        camera.Uninit();
        camera.StopStreaming();
        camera.Close();

        return NO_ERROR;
    }

    status_t CameraHardware::takePicture()
    {
        LOGD ("takepicture");
        stopPreview();

        pictureThread();

        return NO_ERROR;
    }

    status_t CameraHardware::cancelPicture()
    {

        return NO_ERROR;
    }

    status_t CameraHardware::dump(int fd, const Vector<String16>& args) const
    {
        return NO_ERROR;
    }

    status_t CameraHardware::setParameters(const CameraParameters& params)
    {
        Mutex::Autolock lock(mLock);

        if (strcmp(params.getPictureFormat(), CameraParameters::PIXEL_FORMAT_JPEG) != 0) {
            LOGE("Only jpeg still pictures are supported");
            return BAD_VALUE;
        }

        int new_preview_width, new_preview_height;
        params.getPreviewSize(&new_preview_width, &new_preview_height);
        if (0 > new_preview_width || 0 > new_preview_height){
            LOGE("Unsupported preview size: %d %d", new_preview_width, new_preview_height);
            return BAD_VALUE;
        }

        const char *new_str_preview_format = params.getPreviewFormat();
        if (strcmp(new_str_preview_format, CameraParameters::PIXEL_FORMAT_YUV420SP) &&
            strcmp(new_str_preview_format, CameraParameters::PIXEL_FORMAT_YUV420P)) {
            LOGE("Unsupported preview color format: %s", new_str_preview_format);
            return BAD_VALUE;
        }

        const char *new_focus_mode_str = params.get(CameraParameters::KEY_FOCUS_MODE);
        if (strcmp(new_focus_mode_str, CameraParameters::FOCUS_MODE_FIXED)) {
            LOGE("Unsupported preview focus mode: %s", new_focus_mode_str);
            return BAD_VALUE;
        }

        int new_min_fps, new_max_fps;
        params.getPreviewFpsRange(&new_min_fps, &new_max_fps);
        if (0 > new_min_fps || 0 > new_max_fps || new_min_fps > new_max_fps){
            LOGE("Unsupported fps range: %d %d", new_min_fps, new_max_fps);
            return BAD_VALUE;
        }

        int width, height;
        int framerate;
        params.getVideoSize(&width, &height);
        LOGD("VIDEO SIZE: width=%d h=%d", width, height);
        params.getPictureSize(&width, &height);
        LOGD("PICTURE SIZE: width=%d h=%d", width, height);
        params.getPreviewSize(&width, &height);
        framerate = params.getPreviewFrameRate();
        LOGD("PREVIEW SIZE: width=%d h=%d framerate=%d", width, height, framerate);
        mParameters = params;

        if(mNativeWindow){
            mNativeWindow->set_buffers_geometry(
            mNativeWindow,
            width,
            height,
            HAL_PIXEL_FORMAT_YV12);
        }
        return NO_ERROR;
    }

    status_t CameraHardware::sendCommand(int32_t command, int32_t arg1, int32_t arg2)
    {
        return BAD_VALUE;
    }

    CameraParameters CameraHardware::getParameters() const
    {
        CameraParameters params;

        Mutex::Autolock lock(mLock);
        params = mParameters;

        return params;
    }

    void CameraHardware::release()
    {
        close(camera_device);
    }

}; // namespace android
