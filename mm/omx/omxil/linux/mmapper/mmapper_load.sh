#!/bin/sh
#-------------------------------------------------------------------------------
#-                                                                            --
#-       This software is confidential and proprietary and may be used        --
#-        only as expressly authorized by a licensing agreement from          --
#-                                                                            --
#-                            Hantro Products Oy.                             --
#-                                                                            --
#-                   (C) COPYRIGHT 2011 HANTRO PRODUCTS OY                    --
#-                            ALL RIGHTS RESERVED                             --
#-                                                                            --
#-                 The entire notice above must be reproduced                 --
#-                  on all copies and should not be removed.                  --
#-                                                                            --
#-------------------------------------------------------------------------------
#-
#--  Description : Load device driver
#--
#--------------------------------------------------------------------------------

module="mmapper"
device="/dev/mmapper"
mode="666"

echo

mkdir /tmp/dev/

#insert module
insmod $module.ko $* || exit 1

echo "module $module inserted"

#remove old nod
rm -f $device

#read the major asigned at loading time
major=`cat /proc/devices | grep $module | cut -c1-3`

echo "$module major = $major"

#create dev node
mknod $device c $major 0

echo "node $device created"

#give all 'rw' access
chmod $mode $device

echo "set node access to $mode"

#the end
echo
