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

    void *GrabPreviewFrame();
    void ReleasePreviewFrame();
    sp<IMemory> GrabRawFrame();
    camera_memory_t *GrabJpegFrame(camera_request_memory mRequestMemory);

private:
    struct Video *video;

    int fd;
    int nQueued;
    int nDequeued;

    int saveYUYV2JPEG(unsigned char *inputBuffer, int width,
                      int height, FILE *file, int quality);
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
