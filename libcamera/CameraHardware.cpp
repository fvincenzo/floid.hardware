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
#define LOG_NDEBUG 0
#define LOG_TAG "CameraHardware"
#include <utils/Log.h>

#include "CameraHardware.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <cutils/native_handle.h>
#include <ui/GraphicBufferMapper.h>
#include <gui/ISurfaceTexture.h>
#include "converter.h"

#define MIN_WIDTH           320
#define MIN_HEIGHT          240
#define CAM_SIZE            "640x480"
#define PIXEL_FORMAT        V4L2_PIX_FMT_YUYV
#define CAMHAL_GRALLOC_USAGE GRALLOC_USAGE_HW_TEXTURE | \
    GRALLOC_USAGE_HW_RENDER | \
    GRALLOC_USAGE_SW_READ_RARELY | \
    GRALLOC_USAGE_SW_WRITE_NEVER

#define EXIF_FILE_SIZE 28800

namespace android {

    CameraHardware::CameraHardware(int cameraId)
        : mCameraId(cameraId),
        mParameters(),
        mPreviewHeap(0),
        mPreviewRunning(false),
        mRecordRunning(false),
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
        p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(15000,30000)");
        p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "15000,30000");

        p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT, CameraParameters::PIXEL_FORMAT_YUV420P);
        p.set(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES, "640x480,352x288,176x144");
        p.setVideoSize(MIN_WIDTH, MIN_HEIGHT);
        p.set(CameraParameters::KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO, "640x480");

        p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, CameraParameters::FOCUS_MODE_FIXED);
        p.set(CameraParameters::KEY_FOCUS_MODE,CameraParameters::FOCUS_MODE_FIXED);
	p.set(CameraParameters::KEY_FOCUS_DISTANCES,"0.60,1.20,Infinity");

        p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, "640x480,352x288,176x144");
        p.setPictureSize(MIN_WIDTH, MIN_HEIGHT);
        p.set(CameraParameters::KEY_JPEG_QUALITY, "90");
        p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,CameraParameters::PIXEL_FORMAT_JPEG);
        p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES, "160x120,0x0");
        p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, "160");
        p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, "120");
        p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "100");

	p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "52.6");
        p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "36.9");

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
        //Mutex::Autolock lock(mLock);
        mNotifyFn = notify_cb;
        mDataFn = data_cb;
        mRequestMemory = get_memory;
        mTimestampFn = data_cb_timestamp;
        mUser = arg;
    }

    int CameraHardware::setPreviewWindow( preview_stream_ops_t *window)
    {
        int err = 0;
        bool previewRunning;

        // XXX Should probably hold the lock until the preview has stopped.
        mLock.lock();
        previewRunning = mPreviewRunning;
        mLock.unlock();
        if (previewRunning) {
            LOGI("stop preview (window change)");
            stopPreview();
        }

        mNativeWindow=window;
        if(!window)
	{
            LOGW("Window is Null");
            return err;
        }

        int width, height;
        mParameters.getPreviewSize(&width, &height);
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
        if (previewRunning) {
            LOGI("start/resume preview");
            startPreview(width, height);
        }

        return err;
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
            if (mNativeWindow != NULL)
            {
                buffer_handle_t *buf_handle;
                if ((err = mNativeWindow->dequeue_buffer(mNativeWindow,&buf_handle,&stride)) != 0) {
                    LOGW("Surface::dequeueBuffer returned error %d", err);
                    return err;
                }
                mNativeWindow->lock_buffer(mNativeWindow, buf_handle);
                GraphicBufferMapper &mapper = GraphicBufferMapper::get();

                Rect bounds(width, height);
                void *tempbuf;
                void *dst;
                if(0 == mapper.lock(*buf_handle, CAMHAL_GRALLOC_USAGE, bounds, &dst))
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
        { // scope for the lock
            Mutex::Autolock lock(mLock);
            mPreviewRunning = false;
        }
        if (mPreviewThread != 0) {
            mPreviewThread->requestExitAndWait();
            mPreviewThread.clear();
            camera.Uninit();
            camera.StopStreaming();
            camera.Close();
        }
        if (mPreviewHeap) {
            mPreviewHeap->release(mPreviewHeap);
            mPreviewHeap = 0;
        }
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
        {
            Mutex::Autolock lock(mLock);
            mRecordRunning = true;
        }
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
	camera_memory_t* thumbnail = NULL;
	camera_memory_t* ExifHeap = NULL;
	int filesize = 0, thumb_size = 0;
        int JpegExifSize = 0;
	int picture_quality = atoi(mParameters.get(CameraParameters::KEY_JPEG_QUALITY));
	int thumb_quality = atoi(mParameters.get(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY));

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
	    mThumbnailWidth = atof(mParameters.get(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH));
	    mThumbnailHeight = atof(mParameters.get(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT));

            LOGD ("mJpegPictureCallback");
	    if (mThumbnailWidth > 0 && mThumbnailHeight > 0) {
		LOGD("Thumbnail is enabled!");
                picture = camera.GrabJpegFrame(mRequestMemory, filesize, picture_quality, mThumbnailWidth, 
					       mThumbnailHeight, &thumbnail, thumb_size, thumb_quality);

	        ExifHeap = mRequestMemory(-1, EXIF_FILE_SIZE + thumb_size, 1, 0);
            	CreateExif((unsigned char *)thumbnail->data, thumb_size, (unsigned char *)ExifHeap->data, JpegExifSize);
		thumbnail->release(thumbnail);
	    } else {
		LOGD("Thumbnail is disabled");
		picture = camera.GrabJpegFrame(mRequestMemory, filesize, picture_quality);

                ExifHeap = mRequestMemory(-1, EXIF_FILE_SIZE, 1, 0);
                CreateExif(NULL, NULL, (unsigned char *)ExifHeap->data, JpegExifSize);
	    } 

	    camera_memory_t *mem = mRequestMemory(-1, filesize + JpegExifSize, 1, 0);
	    uint8_t *ptr = (uint8_t *) mem->data;

	    memcpy(ptr, picture->data, 2);
	    ptr += 2;

	    memcpy(ptr, ExifHeap->data, JpegExifSize);
	    ptr += JpegExifSize;

	    memcpy(ptr, (uint8_t *) picture->data + 2, filesize - 2);

            mDataFn(CAMERA_MSG_COMPRESSED_IMAGE, mem, 0, NULL, mUser);
	    mem->release(mem);
	    ExifHeap->release(ExifHeap);
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
	const char *valstr = NULL;
	int width, height;
        int framerate;

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

	if ((valstr = params.get(CameraParameters::KEY_GPS_PROCESSING_METHOD)) != NULL)
        {
            LOGD("Set GPS_PROCESSING_METHOD %s", params.get(CameraParameters::KEY_GPS_PROCESSING_METHOD));
	    strcpy(mPreviousGPSProcessingMethod, params.get(CameraParameters::KEY_GPS_PROCESSING_METHOD));
        }

	if ((valstr = params.get(CameraParameters::KEY_GPS_TIMESTAMP)) != NULL)
        {
	    long gpsTimestamp = strtol(valstr, NULL, 10);
	    struct tm *timeinfo = gmtime((time_t *) & (gpsTimestamp));

            LOGD("Set GPS_TIMESTAMP %s", params.get(CameraParameters::KEY_GPS_TIMESTAMP));

	    if (timeinfo != NULL) {
		strftime(m_gps_date, 11, "%Y:%m:%d", timeinfo);
		m_gpsHour = timeinfo->tm_hour;
		m_gpsMin  = timeinfo->tm_min;
		m_gpsSec  = timeinfo->tm_sec;
		LOGD("Convert timestamp %s %d:%d:%d", m_gps_date, m_gpsHour, m_gpsMin, m_gpsSec);
	    }
	    
        }


        params.getVideoSize(&width, &height);
        LOGD("VIDEO SIZE: width=%d h=%d", width, height);
        params.getPictureSize(&width, &height);
        LOGD("PICTURE SIZE: width=%d h=%d", width, height);
        params.getPreviewSize(&width, &height);
        framerate = params.getPreviewFrameRate();
        LOGD("PREVIEW SIZE: width=%d h=%d framerate=%d", width, height, framerate);
        mParameters = params;

        if(mNativeWindow){
	    if (!mPreviewRunning){
                LOGD("CameraHardware::setParameters preview stopped");
                mNativeWindow->set_buffers_geometry(
                mNativeWindow,
                width,
                height,
                HAL_PIXEL_FORMAT_YV12);
            }
            else{
                mParameters.setPreviewSize(camera.width(), camera.height());
            }
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

    void CameraHardware::CreateExif(unsigned char* pInThumbnailData,int Inthumbsize,unsigned char* pOutExifBuf,int& OutExifSize)
    {
	int w =0, h = 0;
	int orientationValue = camera.getOrientation();
	ExifCreator* mExifCreator = new ExifCreator();
	unsigned int ExifSize = 0;
	ExifInfoStructure ExifInfo;
	char ver_date[5] = {NULL,};
	unsigned short tempISO = 0;
        struct tm *t = NULL;
        time_t nTime;
	double arg0,arg3;
        int arg1,arg2;
		
	memset(&ExifInfo, NULL, sizeof(ExifInfoStructure));

	strcpy( (char *)&ExifInfo.maker, "STMicroelectronics");
	strcpy( (char *)&ExifInfo.model, "SPEAr1340-Cam");

	mParameters.getPictureSize(&w, &h);
	ExifInfo.imageWidth = ExifInfo.pixelXDimension = w;
	ExifInfo.imageHeight = ExifInfo.pixelYDimension = h;

	mParameters.getPreviewSize(&w, &h);

	time(&nTime);
	t = localtime(&nTime);

	if(t != NULL) {
	    sprintf((char *)&ExifInfo.dateTimeOriginal, "%4d:%02d:%02d %02d:%02d:%02d", 
			t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	    sprintf((char *)&ExifInfo.dateTimeDigitized, "%4d:%02d:%02d %02d:%02d:%02d", 
			t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);						
	    sprintf((char *)&ExifInfo.dateTime, "%4d:%02d:%02d %02d:%02d:%02d", 
			t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec); 					
	}

  	int cam_ver = camera.getCamera_version();

	ExifInfo.Camversion[0] = (cam_ver & 0xFF);
	ExifInfo.Camversion[1] = ((cam_ver >> 8) & 0xFF);
	ExifInfo.Camversion[2] = ((cam_ver >> 16) & 0xFF);
	ExifInfo.Camversion[3] = ((cam_ver >> 24) & 0xFF);
		
	sprintf((char *)&ExifInfo.software, "fw %02d.%02d prm %02d.%02d", 
			ExifInfo.Camversion[2],ExifInfo.Camversion[3],ExifInfo.Camversion[0],ExifInfo.Camversion[1]); 	
	if(mThumbnailWidth > 0 && mThumbnailHeight > 0) {
	    LOGD("has thumbnail! %d x %d", mThumbnailWidth, mThumbnailHeight);
	    ExifInfo.hasThumbnail = true;
	    ExifInfo.thumbStream = pInThumbnailData;
	    ExifInfo.thumbSize = Inthumbsize;
	    ExifInfo.thumbImageWidth = mThumbnailWidth;
	    ExifInfo.thumbImageHeight = mThumbnailHeight;
	} else {
	    ExifInfo.hasThumbnail = false;
	}

	ExifInfo.exposureProgram 	    = 1;
	ExifInfo.exposureMode 		    = 0;
	ExifInfo.contrast                   = convertToExifLMH(camera.getContrast(), 2);
	ExifInfo.fNumber.numerator          = 28;
	ExifInfo.fNumber.denominator        = 10;
	ExifInfo.aperture.numerator         = 28;
	ExifInfo.aperture.denominator       = 10;
	ExifInfo.maxAperture.numerator      = 26;
	ExifInfo.maxAperture.denominator    = 10;
	ExifInfo.focalLength.numerator      = 2800;
	ExifInfo.focalLength.denominator    = 1000;
		//]
	ExifInfo.brightness.numerator       = 5;
	ExifInfo.brightness.denominator     = 9;
	ExifInfo.iso                        = 1;
	ExifInfo.flash                 	    = 0;	// default value

	switch(orientationValue)
	{            		
	    case 0:
		ExifInfo.orientation                = 1 ;
		break;
	    case 90:
		ExifInfo.orientation                = 6 ;
		break;
	    case 180:
		ExifInfo.orientation                = 3 ;
		break;
	    case 270:
		ExifInfo.orientation                = 8 ;
		break;
	    default:
		ExifInfo.orientation                = 1 ;
		break;
	}

	ExifInfo.meteringMode = camera.getMetering();
	ExifInfo.whiteBalance = 0;
	ExifInfo.saturation = convertToExifLMH(camera.getSaturation(), 2);
	ExifInfo.sharpness = convertToExifLMH(camera.getSharpness(), 2);
	switch(camera.getISO()) {
	    case 2:
		ExifInfo.isoSpeedRating             = 50;
	        break;
	    case 3:
		ExifInfo.isoSpeedRating             = 100;
		break;
	    case 4:
		ExifInfo.isoSpeedRating             = 200;
		break;
	    case 5:
		ExifInfo.isoSpeedRating             = 400;
		break;
	    case 6:
		ExifInfo.isoSpeedRating             = 800;
		break;
	    default:
		ExifInfo.isoSpeedRating             = 100;
	        break;
	}               

	switch(camera.getBrightness()) {
	    case 0:
		ExifInfo.exposureBias.numerator = -20;
		break;
	    case 1:
		ExifInfo.exposureBias.numerator = -15;
		break;
	    case 2:
	        ExifInfo.exposureBias.numerator = -10;
		break;
	    case 3:
		ExifInfo.exposureBias.numerator =  -5;
		break;
	    case 4:
		ExifInfo.exposureBias.numerator =   0;
		break;
	    case 5:
		ExifInfo.exposureBias.numerator =   5;
		break;
	    case 6:
		ExifInfo.exposureBias.numerator =  10;
		break;
	    case 7:
		ExifInfo.exposureBias.numerator =  15;
		break;
	    case 8:
		ExifInfo.exposureBias.numerator =  20;
		break;
	    default:
		ExifInfo.exposureBias.numerator = 0;
		break;
	}
	ExifInfo.exposureBias.denominator       = 10;
	ExifInfo.sceneCaptureType               = 0;
	ExifInfo.meteringMode               = 2;
	ExifInfo.whiteBalance               = 1;
	ExifInfo.saturation                 = 0;
	ExifInfo.sharpness                  = 0;
	ExifInfo.isoSpeedRating             = 100;
	ExifInfo.exposureBias.numerator     = 0;
	ExifInfo.exposureBias.denominator   = 10;
	ExifInfo.sceneCaptureType           = 4;

	if (mParameters.get(mParameters.KEY_GPS_LATITUDE) != 0 && mParameters.get(mParameters.KEY_GPS_LONGITUDE) != 0)
	{		
	    arg0 = getGPSLatitude();

	    if (arg0 > 0)
	        ExifInfo.GPSLatitudeRef[0] = 'N'; 
	    else
	        ExifInfo.GPSLatitudeRef[0] = 'S';

	    convertFromDecimalToGPSFormat(fabs(arg0),arg1,arg2,arg3);

	    ExifInfo.GPSLatitude[0].numerator = arg1;
	    ExifInfo.GPSLatitude[0].denominator = 1;
  	    ExifInfo.GPSLatitude[1].numerator = arg2; 
	    ExifInfo.GPSLatitude[1].denominator = 1;
	    ExifInfo.GPSLatitude[2].numerator = arg3; 
	    ExifInfo.GPSLatitude[2].denominator = 60;

	    arg0 = getGPSLongitude();

	    if (arg0 > 0)
		ExifInfo.GPSLongitudeRef[0] = 'E';
	    else
		ExifInfo.GPSLongitudeRef[0] = 'W';

	    convertFromDecimalToGPSFormat(fabs(arg0),arg1,arg2,arg3);

	    ExifInfo.GPSLongitude[0].numerator = arg1; 
	    ExifInfo.GPSLongitude[0].denominator = 1;
	    ExifInfo.GPSLongitude[1].numerator = arg2; 
	    ExifInfo.GPSLongitude[1].denominator = 1;
	    ExifInfo.GPSLongitude[2].numerator = arg3; 
	    ExifInfo.GPSLongitude[2].denominator = 60;

	    arg0 = getGPSAltitude();

	    if (arg0 > 0)	
		ExifInfo.GPSAltitudeRef = 0;
	    else
		ExifInfo.GPSAltitudeRef = 1;

	    ExifInfo.GPSAltitude[0].numerator = fabs(arg0) ; 
	    ExifInfo.GPSAltitude[0].denominator = 1;

	    //GPS_Time_Stamp
	    ExifInfo.GPSTimestamp[0].numerator = (uint32_t)m_gpsHour;
	    ExifInfo.GPSTimestamp[0].denominator = 1; 
	    ExifInfo.GPSTimestamp[1].numerator = (uint32_t)m_gpsMin;
	    ExifInfo.GPSTimestamp[1].denominator = 1;               
	    ExifInfo.GPSTimestamp[2].numerator = (uint32_t)m_gpsSec;
	    ExifInfo.GPSTimestamp[2].denominator = 1;

	    //GPS_ProcessingMethod
	    strcpy((char *)ExifInfo.GPSProcessingMethod, mPreviousGPSProcessingMethod);

	    //GPS_Date_Stamp
	    strcpy((char *)ExifInfo.GPSDatestamp, m_gps_date);

	    ExifInfo.hasGPS = true;	
	    LOGD(" With GPS!\n");	
	} else {
	    ExifInfo.hasGPS = false;
	    LOGD(" No GPS! latitude=%s\n", mParameters.get(mParameters.KEY_GPS_LATITUDE));
	}		
	
	ExifSize = mExifCreator->ExifCreate( (unsigned char *)pOutExifBuf, &ExifInfo);
	OutExifSize = ExifSize;
	delete mExifCreator; 
    }


    void CameraHardware::convertFromDecimalToGPSFormat(double coord, int& deg, int& min, double& sec)
    {
	double tmp = 0;

	deg  = (int)floor(coord);
	tmp = (coord - floor(coord)) * 60;
	min = (int)floor(tmp);
	tmp = (tmp - floor(tmp)) * 3600;
	sec  = (int)floor(tmp);
	if ( sec >= 3600) {
	    sec = 0;
	    min += 1;
	}
	if (min >= 60) {
	    min = 0;
	    deg += 1;
	}
	LOGD("convertFromDecimalToGPSFormat: coord = %f\tdeg = %d\tmin = %d\tsec = %f", coord, deg, min, sec);
    }

    int CameraHardware::convertToExifLMH(int value, int key)
    {
	const int NORMAL = 0;
	const int LOW    = 1;
	const int HIGH   = 2;

	value -= key;
	if(value == 0) return NORMAL;
	if(value < 0) return LOW;
	else return HIGH;
    }

    double CameraHardware::getGPSLatitude() const
    {
	double gpsLatitudeValue = 0;
	if( mParameters.get(mParameters.KEY_GPS_LATITUDE)) {
 	    gpsLatitudeValue = atof(mParameters.get(mParameters.KEY_GPS_LATITUDE));
	    if(gpsLatitudeValue != 0) {
		LOGD("getGPSLatitude = %2.6f \n", gpsLatitudeValue);
	    }
	    return gpsLatitudeValue;
	} else {
	    LOGD("getGPSLatitude null \n");
	    return 0;
	}
    }

    double CameraHardware::getGPSLongitude() const
    {
	double gpsLongitudeValue = 0;
	if( mParameters.get(mParameters.KEY_GPS_LONGITUDE)) {
	    gpsLongitudeValue = atof(mParameters.get(mParameters.KEY_GPS_LONGITUDE));
	    if(gpsLongitudeValue != 0) {
		LOGD("getGPSLongitude = %2.6f \n", gpsLongitudeValue);
	    }
	    return gpsLongitudeValue;
	} else {
	    LOGD("getGPSLongitude null \n");
	    return 0;
	}
    }

    double CameraHardware::getGPSAltitude() const
    {
	double gpsAltitudeValue = 0;
	if( mParameters.get(mParameters.KEY_GPS_ALTITUDE)) {
	    gpsAltitudeValue = atof(mParameters.get(mParameters.KEY_GPS_ALTITUDE));
	    if(gpsAltitudeValue != 0) {
		LOGD("getGPSAltitude = %2.2f \n", gpsAltitudeValue);
	    }
	    return gpsAltitudeValue;
	} else {
	    LOGD("getGPSAltitude null \n");
	    return 0;
	}
    }

}; // namespace android
