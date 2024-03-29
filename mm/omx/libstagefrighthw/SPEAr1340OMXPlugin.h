/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) STmicroelectronics 2011. All rights reserved.
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

#ifndef SPEAR_1340_OMX_PLUGIN_H_
#define SPEAR_1340_OMX_PLUGIN_H_

#include <media/stagefright/OMXPluginBase.h>

namespace android
{

    struct SPEAr1340OMXPlugin:public OMXPluginBase
    {
        SPEAr1340OMXPlugin ();
        virtual ~SPEAr1340OMXPlugin ();

        virtual OMX_ERRORTYPE makeComponentInstance (const char *name,
            const OMX_CALLBACKTYPE *
            callbacks,
            OMX_PTR appData,
            OMX_COMPONENTTYPE **
            component);

        virtual OMX_ERRORTYPE destroyComponentInstance (OMX_COMPONENTTYPE *
            component);

        virtual OMX_ERRORTYPE enumerateComponents (OMX_STRING name,
            size_t size,
            OMX_U32 index);

        virtual OMX_ERRORTYPE getRolesOfComponent (const char *name,
            Vector < String8 > *roles);

    private:
        void *mLibHandle;

        typedef OMX_ERRORTYPE (*InitFunc) ();
        typedef OMX_ERRORTYPE (*DeinitFunc) ();
        typedef OMX_ERRORTYPE (*ComponentNameEnumFunc) (OMX_STRING, OMX_U32,
            OMX_U32);

        typedef OMX_ERRORTYPE (*GetHandleFunc) (OMX_HANDLETYPE *, OMX_STRING,
            OMX_PTR, OMX_CALLBACKTYPE *);

        typedef OMX_ERRORTYPE (*FreeHandleFunc) (OMX_HANDLETYPE *);

        typedef OMX_ERRORTYPE (*GetRolesOfComponentFunc) (OMX_STRING,
            OMX_U32 *,
            OMX_U8 **);

        InitFunc mInit;
        DeinitFunc mDeinit;
        ComponentNameEnumFunc mComponentNameEnum;
        GetHandleFunc mGetHandle;
        FreeHandleFunc mFreeHandle;
        GetRolesOfComponentFunc mGetRolesOfComponentHandle;

        SPEAr1340OMXPlugin (const SPEAr1340OMXPlugin &);
        SPEAr1340OMXPlugin & operator= (const SPEAr1340OMXPlugin &);
    };

} // namespace android

#endif // SPEAR_1340_OMX_PLUGIN_H_
