/*
 * Copyright (C) 2012 Wind River Systems, Inc.
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

#ifndef _V4L2CAMERA_H
#define _V4L2CAMERA_H

#include <hardware/camera.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <linux/videodev.h>

#define NB_BUFFER 4

namespace android {

struct Video {
    struct v4l2_buffer buf;
    struct v4l2_capability cap;
    struct v4l2_format format;
    struct v4l2_requestbuffers rb;
    bool isStreaming;
    void *mem[NB_BUFFER];
    int width;
    int height;
    int formatIn;
    int framesizeIn;
};

class V4L2Camera {

public:
    V4L2Camera();
    ~V4L2Camera();

    int  Open(const char *device, int width, int height, int pixelformat);
    void Close();

    int  Init();
    void Uninit();

    int StartStreaming();
    int StopStreaming();

    int width();
    int height();

    int setAutofocus();
    int getAutoFocusResult();
    int cancelAutofocus();
    int setZoom(int zoom_level);
    int setWhiteBalance(int white_balance);
    int setFramerate(int framerate,int cam_mode);
    int setBrightness(int brightness);
    int getBrightness(void);
    int setSceneMode(int scene_mode);
    int getSceneMode();
    int setFocusMode(int focus_mode);
    int SetCameraFlip(bool isCapture);
    int GetJpegImageSize();
    int GetThumbNailOffset();
    int GetYUVOffset();
    int GetThumbNailDataSize();
    int setExifOrientationInfo(int orientationInfo);
    int getOrientation();
    void stopPreview();
    int GetJPEG_Capture_Width();
    int GetJPEG_Capture_Height();
    int getCamera_version();
    int getWhiteBalance();
    int setMetering(int metering_value);
    int setISO(int iso_value);
    int getISO(void);
    int getMetering(void);
    int setContrast(int contrast_value);
    int getContrast(void);
    int getSharpness(void);
    int setSharpness(int sharpness_value);
    int setSaturation(int saturation_value);
    int getSaturation(void);
    
    void *GrabPreviewFrame();
    void *GrabPreviewFrame(size_t &bytesused);
    void ReleasePreviewFrame();
    sp<IMemory> GrabRawFrame();
    camera_memory_t *GrabJpegFrame(camera_request_memory mRequestMemory, 
				   int& filesize, int picture_quality);
    camera_memory_t *GrabJpegFrame(camera_request_memory mRequestMemory, int& mfilesize,
                                   int picture_quality, int thumbWidth, int thumbHeight, 
				   camera_memory_t **thumb, int &thumb_size, 
				   int thumb_quality);
    int getCameraFWVer();
    int saveYUYV2JPEG(unsigned char *inputBuffer, int width,
                      int height, FILE *file, int quality);
private:
    struct Video *video;

    int fd;
    int nQueued;
    int nDequeued;

    int mZoomLevel;
    int mWhiteBalance;
    int mFocusMode;
    int mSceneMode;
    int m_exif_orientation;
    int mISO;
    int mMetering;
    int mContrast;
    int mSharpness;
    int mSaturation;
    int mBrightness;

    void GrabRawFrame(void* pRawBuffer);
    void yuv_to_rgb16(unsigned char y, unsigned char u,
                      unsigned char v, unsigned char *rgb);
    void convert(unsigned char *buf, unsigned char *rgb,
                 int width, int height);
};

class MemoryStream {
public:
    MemoryStream(char *buf, size_t bufSize);
    ~MemoryStream() { closeStream(); }

    void closeStream();
    size_t getOffset() const { return bytesWritten; }
    operator FILE *() { return file; }

private:
    static int run(void *);
    int readPipe();

    char *buffer;
    size_t bufferSize;
    size_t bytesWritten;
    int pipeFd[2];
    FILE *file;
    Mutex lock;
    Condition exitedCondition;
};

}; // namespace android

#endif
