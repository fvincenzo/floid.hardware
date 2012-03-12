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

#define LOG_TAG "SPEAr1340VideoRenderer"
#include <utils/Log.h>

#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferMapper.h>
#include <binder/MemoryHeapBase.h>
#include <media/stagefright/MediaDebug.h>
#include <OMX_IVCommon.h>

#include "SPEAr1340HardwareRenderer.h"

android::VideoRenderer * createRenderer (const android::sp <
    android::Surface > &surface,
    const char *componentName,
    OMX_COLOR_FORMATTYPE colorFormat,
    size_t displayWidth,
    size_t displayHeight,
    size_t decodedWidth,
    size_t decodedHeight)
{
    android::SPEAr1340HardwareRenderer* pRenderer = 0;
    if(colorFormat==OMX_COLOR_Format32bitARGB8888||colorFormat==OMX_COLOR_Format16bitRGB565||colorFormat==OMX_COLOR_Format16bitARGB4444||colorFormat==OMX_COLOR_Format16bitBGR565){
        pRenderer = new android::SPEAr1340HardwareRenderer(surface, colorFormat, displayWidth, displayHeight, decodedWidth, decodedHeight);
        if(pRenderer->init()){
            delete pRenderer;
            pRenderer = 0;
        }
    }
    return pRenderer;
}

namespace android
{
    SPEAr1340HardwareRenderer::SPEAr1340HardwareRenderer (const sp <Surface> &surface,
        OMX_COLOR_FORMATTYPE colorFormat,
        size_t displayWidth,
        size_t displayHeight,
        size_t decodedWidth,
        size_t decodedHeight)
        : mColorFormat(colorFormat), mSurface(surface),
        mDisplayWidth(displayWidth), mDisplayHeight(displayHeight),
        mDecodedWidth(decodedWidth), mDecodedHeight(decodedHeight),
        mFrameSize(0)
    {

        CHECK(mSurface != NULL);
        CHECK(mDecodedWidth > 0);
        CHECK(mDecodedHeight > 0);
        mNativeWindow = mSurface;
    }

    SPEAr1340HardwareRenderer::~SPEAr1340HardwareRenderer()
    {

    }

    int SPEAr1340HardwareRenderer::init()
    {
        int pixelFormat;
        if(mColorFormat==OMX_COLOR_Format32bitARGB8888){
            pixelFormat=HAL_PIXEL_FORMAT_BGRA_8888;
            mFrameSize = mDecodedWidth * mDecodedHeight * 4;
        }
        else if(mColorFormat==OMX_COLOR_Format16bitARGB4444){
            pixelFormat=HAL_PIXEL_FORMAT_RGBA_4444;
            mFrameSize = mDecodedWidth * mDecodedHeight * 2;
        }
        else if(mColorFormat==OMX_COLOR_Format16bitBGR565){
            pixelFormat=HAL_PIXEL_FORMAT_RGB_565;
            mFrameSize = mDecodedWidth * mDecodedHeight * 2;
        }
        else{
            pixelFormat=HAL_PIXEL_FORMAT_RGB_565;
            mFrameSize = mDecodedWidth * mDecodedHeight * 2;
        }
        CHECK_EQ(0, native_window_set_usage(mNativeWindow.get(), GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_HW_TEXTURE));
        CHECK_EQ(0, native_window_set_buffer_count(mNativeWindow.get(),3));
        CHECK_EQ(0, native_window_set_buffers_geometry(mNativeWindow.get(),mDecodedWidth,mDecodedHeight,pixelFormat));

        return 0;
    }

    void SPEAr1340HardwareRenderer::render(const void *data, size_t size, void *platformPrivate)
    {
        android_native_buffer_t *buf;
        int err;
        if ((err = mNativeWindow->dequeueBuffer(mNativeWindow.get(), &buf)) != 0) {
            LOGW("SPEAr1340HardwareRenderer::render Surface::dequeueBuffer returned error %d", err);
            return;
        }

        mNativeWindow->lockBuffer(mNativeWindow.get(), buf);
        GraphicBufferMapper &mapper = GraphicBufferMapper::get();
        Rect bounds(mDecodedWidth, mDecodedHeight);
        void *dst;
        if ((err = mapper.lock(buf->handle, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst)) == 0) {
            memcpy(dst, data, size);
            mapper.unlock(buf->handle);
        } else {
            LOGW("SPEAr1340HardwareRenderer::render %s lock returned error %d", __FUNCTION__, err);
        }
        if ((err = mNativeWindow->queueBuffer(mNativeWindow.get(), buf)) != 0) {
            LOGW("SPEAr1340HardwareRenderer::render Surface::queueBuffer returned error %d", err);
        }
    }

} // namespace android
