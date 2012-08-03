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

#define LOG_TAG "V4L2Camera"
#include <utils/Log.h>
#include <utils/threads.h>
#include <fcntl.h>

#include "V4L2Camera.h"

extern "C" { /* Android jpeglib.h missed extern "C" */
#include <jpeglib.h>
     void convertYUYVtoRGB565(unsigned char *buf, unsigned char *rgb, int width, int height);
}

namespace android {

V4L2Camera::V4L2Camera ()
    : nQueued(0), nDequeued(0)
{
    video = (struct Video *) calloc (1, sizeof (struct Video));
}

V4L2Camera::~V4L2Camera()
{
    free(video);
}

int V4L2Camera::Open(const char *device, int width, int height, int pixelformat)
{
    if ((fd = open(device, O_RDWR)) == -1) {
        LOGE("%s: Fails to open V4L device (%s)", __func__, strerror(errno));
        return -1;
    }

    if (ioctl(fd, VIDIOC_QUERYCAP, &video->cap) < 0) {
        LOGE("%s: ioctl VIDIOC_QUERYCAP failed.", __func__);
        return -1;
    }

    if ((video->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        LOGE("%s: Video capture not supported.", __func__);
        return -1;
    }

    if (!(video->cap.capabilities & V4L2_CAP_STREAMING)) {
        LOGE("%s: Capture device doesn't support streaming I/O", __func__);
        return -1;
    }

    video->width = width;
    video->height = height;
    video->framesizeIn = (width * height << 1);
    video->formatIn = pixelformat;

    video->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    video->format.fmt.pix.width = width;
    video->format.fmt.pix.height = height;
    video->format.fmt.pix.pixelformat = pixelformat;

    if (ioctl(fd, VIDIOC_S_FMT, &video->format) < 0) {
        LOGE("%s: ioctl VIDIOC_S_FMT failed (%s)", __func__, strerror(errno));
        return -1;
    }
    return 0;
}

void V4L2Camera::Close()
{
    close(fd);
}

int V4L2Camera::Init()
{
    video->rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    video->rb.memory = V4L2_MEMORY_MMAP;
    video->rb.count = NB_BUFFER;

    if (ioctl(fd, VIDIOC_REQBUFS, &video->rb) < 0) {
        LOGE("%s: ioctl VIDIOC_REQBUFS failed (%s)", __func__, strerror(errno));
        return -1;
    }

    for (int i = 0; i < NB_BUFFER; i++) {
        memset (&video->buf, 0, sizeof (struct v4l2_buffer));

        video->buf.index = i;
        video->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        video->buf.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd, VIDIOC_QUERYBUF, &video->buf) < 0) {
            LOGE("%s: ioctl VIDIOC_QUERYBUF failed (%s)",
                 __func__, strerror(errno));
            return -1;
        }

        video->mem[i] = mmap(0, video->buf.length,
                             PROT_READ | PROT_WRITE, MAP_SHARED,
                             fd, video->buf.m.offset);

        if (video->mem[i] == MAP_FAILED) {
            LOGE("%s: mmap failed (%s)", __func__, strerror(errno));
            return -1;
        }

        if (ioctl(fd, VIDIOC_QBUF, &video->buf) < 0) {
            LOGE("%s: ioctl VIDIOC_QBUF failed", __func__);
            return -1;
        }
        nQueued++;
    }

    return 0;
}

void V4L2Camera::Uninit()
{
    video->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    video->buf.memory = V4L2_MEMORY_MMAP;

    int DQcount = nQueued - nDequeued;

    for (int i = 0; i < DQcount-1; i++) {
        if (ioctl(fd, VIDIOC_DQBUF, &video->buf) < 0)
            LOGE("%s: ioctl VIDIOC_DQBUF failed", __func__);
    }
    nQueued = 0;
    nDequeued = 0;

    for (int i = 0; i < NB_BUFFER; i++)
        if (munmap(video->mem[i], video->buf.length) < 0)
            LOGE("%s: munmap failed", __func__);
}

int V4L2Camera::StartStreaming()
{
    enum v4l2_buf_type type;

    if (!video->isStreaming) {
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            LOGE("%s: Unable to start capture (%s)", __func__, strerror(errno));
            return -1;
        }

        video->isStreaming = true;
    }

    return 0;
}

int V4L2Camera::StopStreaming()
{
    enum v4l2_buf_type type;

    if (video->isStreaming) {
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
            LOGE("%s: Unable to stop capture (%s)", __func__, strerror(errno));
            return -1;
        }

        video->isStreaming = false;
    }

    return 0;
}

int V4L2Camera::width()
{
    return video->width;
}

int V4L2Camera::height()
{
    return video->height;
}

void * V4L2Camera::GrabPreviewFrame()
{
    video->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    video->buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_DQBUF, &video->buf) < 0) {
        LOGE("%s: VIDIOC_DQBUF Failed", __func__);
        return NULL;
    }
    nDequeued++;
    return video->mem[video->buf.index];
}

void V4L2Camera::ReleasePreviewFrame()
{
    if (ioctl(fd, VIDIOC_QBUF, &video->buf) < 0)
        LOGE("%s: VIDIOC_QBUF Failed", __func__);
    nQueued++;
}

sp<IMemory> V4L2Camera::GrabRawFrame ()
{
    int size = video->width * video->height * 2;
    sp<MemoryHeapBase> memHeap = new MemoryHeapBase(size);
    sp<MemoryBase> memBase = new MemoryBase(memHeap, 0, size);

    return memBase;
}

camera_memory_t* V4L2Camera::GrabJpegFrame(camera_request_memory mRequestMemory)
{
    LOGI("%s", __func__);

    video->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    video->buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd, VIDIOC_DQBUF, &video->buf) < 0) {
        LOGE("%s: VIDIOC_DQBUF Failed", __func__);
        return NULL;
    }
    nDequeued++;

    size_t bytesused = video->buf.bytesused;
    char *tempBuf = new char[bytesused];

    MemoryStream strm(tempBuf, bytesused);
    saveYUYV2JPEG((unsigned char *)video->mem[video->buf.index],
                   video->width, video->height, strm, 100);
    strm.closeStream();

    size_t fileSize = strm.getOffset();
    camera_memory_t* picture = mRequestMemory(-1,fileSize,1,NULL);
    memcpy(picture->data, tempBuf, fileSize);
    delete[] tempBuf;

    if (ioctl(fd, VIDIOC_QBUF, &video->buf) < 0) {
        LOGE("%s: VIDIOC_QBUF Failed", __func__);
        return NULL;
    }
    nQueued++;

    return picture;
}

int V4L2Camera::saveYUYV2JPEG(unsigned char *inputBuffer, int width,
                              int height, FILE *file, int quality)
{
    JSAMPROW row_pointer[1];
    struct jpeg_compress_struct jcs;
    struct jpeg_error_mgr jerr;
    unsigned char *lineBuffer, *yuyv;
    int z;
    int fileSize;

    LOGI("%s: width (%d), height (%d)", __func__, width, height);

    lineBuffer = (unsigned char *) calloc(width * 3, 1);
    yuyv = inputBuffer;

    jcs.err = jpeg_std_error (&jerr);
    jpeg_create_compress (&jcs);
    jpeg_stdio_dest (&jcs, file);

    jcs.image_width = width;
    jcs.image_height = height;
    jcs.input_components = 3;
    jcs.in_color_space = JCS_RGB;

    jpeg_set_defaults (&jcs);
    jpeg_set_quality (&jcs, quality, TRUE);
    jpeg_start_compress (&jcs, TRUE);

    z = 0;
    while (jcs.next_scanline < jcs.image_height) {
        int x;
        unsigned char *ptr = lineBuffer;

        for (x = 0; x < width; x++) {
            int r, g, b;
            int y, u, v;

            if (!z)
                y = yuyv[0] << 8;
            else
                y = yuyv[2] << 8;

            u = yuyv[1] - 128;
            v = yuyv[3] - 128;

            r = (y + (359 * v)) >> 8;
            g = (y - (88 * u) - (183 * v)) >> 8;
            b = (y + (454 * u)) >> 8;

            *(ptr++) = (r > 255) ? 255 : ((r < 0) ? 0 : r);
            *(ptr++) = (g > 255) ? 255 : ((g < 0) ? 0 : g);
            *(ptr++) = (b > 255) ? 255 : ((b < 0) ? 0 : b);

            if (z++) {
                z = 0;
                yuyv += 4;
            }
        }

        row_pointer[0] = lineBuffer;
        jpeg_write_scanlines (&jcs, row_pointer, 1);
    }

    jpeg_finish_compress (&jcs);
    fileSize = ftell(file);
    jpeg_destroy_compress (&jcs);

    free (lineBuffer);

    return fileSize;
}


MemoryStream::MemoryStream(char* buf, size_t bufSize)
    : buffer(buf), bufferSize(bufSize), bytesWritten(0)
{
    if (pipe(pipeFd)) {
        file = NULL;
        return;
    }
    file = fdopen(pipeFd[1], "w");
    createThread(run, this);
}

void MemoryStream::closeStream()
{
    if (file == NULL)
        return;
    AutoMutex l(lock);
    fclose(file);
    file = NULL;
    exitedCondition.wait(lock);
}

int MemoryStream::run(void *self)
{
    return static_cast<MemoryStream *>(self)->readPipe();
}

int MemoryStream::readPipe()
{
    char *buf = buffer;
    ssize_t readSize;
    while ((readSize = read(pipeFd[0], buf, bufferSize - bytesWritten)) > 0) {
        bytesWritten += readSize;
        buf += readSize;
    }
    close(pipeFd[0]);
    AutoMutex l(lock);
    exitedCondition.signal();
    return 0;
}

}; // namespace android
