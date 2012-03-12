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

#ifndef SPEAR_1340_HARDWARE_RENDERER_H_
#define SPEAR_1340_HARDWARE_RENDERER_H_

#include <media/stagefright/VideoRenderer.h>
#include <utils/RefBase.h>
#include <surfaceflinger/Surface.h>

namespace android
{
    class Surface;
    class MemoryHeapBase;

    class SPEAr1340HardwareRenderer:public VideoRenderer
    {
    public:
        SPEAr1340HardwareRenderer (const sp <Surface> &surface,
            OMX_COLOR_FORMATTYPE colorFormat,
            size_t displayWidth, size_t displayHeight,
            size_t decodedWidth, size_t decodedHeight);

        int init();
        virtual void render(const void *data, size_t size, void *platformPrivate);

        virtual ~SPEAr1340HardwareRenderer();
    private:
        OMX_COLOR_FORMATTYPE mColorFormat;
        sp<Surface> mSurface;
        sp<ANativeWindow> mNativeWindow;
        size_t mDisplayWidth, mDisplayHeight;
        size_t mDecodedWidth, mDecodedHeight;
        size_t mFrameSize;

        SPEAr1340HardwareRenderer (const SPEAr1340HardwareRenderer &);
        SPEAr1340HardwareRenderer & operator= (const SPEAr1340HardwareRenderer &);
    };

} // namespace android

#endif // SPEAR_1340_HARDWARE_RENDERER_H_
