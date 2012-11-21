/*
 * Copyright (C) 2012 ST Microelectronics.
 * Author Giuseppe Barba <giuseppe.barba@st.com>
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

#ifndef ANDROID_EXIF_H
#define ANDROID_EXIF_H

#define MAKER_NAME_LENGTH	20
#define MODEL_NAME_LENGTH	20
#define SOFTWARE_VERSION_LENGTH 5
#define DATE_TIME_FIELD_LENGTH  50

typedef struct
{
    unsigned int numerator;
    unsigned int denominator;
} Rational;

typedef struct
{
    int numerator;
    int denominator;
} SRational;

typedef struct
{
    /* Capability */
    bool hasGPS;
    bool hasThumbnail;

    /* Hardware name */
    unsigned char maker[MAKER_NAME_LENGTH];
    unsigned char model[MODEL_NAME_LENGTH];
    unsigned char software[SOFTWARE_VERSION_LENGTH];
    
    /* Image parameters */
    unsigned int imageWidth;
    unsigned int imageHeight;
 
    unsigned int pixelXDimension;
    unsigned int pixelYDimension;

    unsigned char dateTimeOriginal[DATE_TIME_FIELD_LENGTH];
    unsigned char dateTimeDigitized[DATE_TIME_FIELD_LENGTH];
    unsigned char dateTime[DATE_TIME_FIELD_LENGTH];

    unsigned int thumbImageWidth;
    unsigned int thumbImageHeight;	
    unsigned char *thumbStream;    
    unsigned int thumbSize;    

    unsigned short exposureProgram;
    unsigned short meteringMode;	
    unsigned short exposureMode;
    unsigned short whiteBalance;
    unsigned short saturation;
    unsigned short sharpness;
    unsigned short contrast;
    unsigned short isoSpeedRating;
    unsigned short iso;
    unsigned short flash;



    Rational fNumber;
    Rational maxAperture;
    Rational focalLength;
    Rational exposureTime;
    Rational aperture;

    SRational exposureBias;
    SRational brightness;
    SRational shutterSpeed;

    int orientation;

    unsigned short sceneCaptureType;
    unsigned char Camversion[4];

    unsigned char GPSLatitudeRef[2];
    Rational GPSLatitude[3];	
    unsigned char GPSLongitudeRef[2];
    Rational GPSLongitude[3];	
    unsigned char GPSAltitudeRef;
    Rational GPSAltitude[1];    
    Rational GPSTimestamp[3];
    unsigned char GPSProcessingMethod[150];
    unsigned char GPSDatestamp[11];    
} ExifInfoStructure;

#endif
