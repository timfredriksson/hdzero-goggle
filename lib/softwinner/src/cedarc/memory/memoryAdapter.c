
/*
* Copyright (c) 2008-2016 Allwinner Technology Co. Ltd.
* All rights reserved.
*
* File : memoryAdapter.c
* Description :
* History :
*   Author  : xyliu <xyliu@allwinnertech.com>
*   Date    : 2016/04/13
*   Comment :
*
*
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <sys/mman.h>
#include <pthread.h>
#include "memoryAdapter.h"

#include "log.h"
#include "ionMemory/ionAllocEntry.h"

struct ScMemOpsS* MemAdapterGetOpsS()
{
    return __GetIonMemOpsS();
}

struct ScMemOpsS* SecureMemAdapterGetOpsS()
{
#if(PLATFORM_SURPPORT_SECURE_OS == 1)
    return __GetSecureMemOpsS();
#else
    loge("platform not surpport secure os, return null");
    return NULL;
#endif
}

