/******************************************************************************
  Copyright (C), 2001-2016, Allwinner Tech. Co., Ltd.
 ******************************************************************************
  File Name     : mpp_version.h
  Version       : Initial Draft
  Author        : Allwinner BU3-PD2 Team
  Created       : 2017/07/25
  Last Modified :
  Description   : mpi functions declaration
  Function List :
  History       :
******************************************************************************/

#ifndef _MPP_VERSION_H_
#define _MPP_VERSION_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REPO_MODULE_NAME "MPP-Platform"
#define REPO_TAG "V1.0 Release"
#define REPO_BRANCH "v5-dev"
#define REPO_COMMIT "4eed413532ad87725895a8cb1ba75e22fc328233"
#define REPO_DATE "20170725"
#define RELEASE_AUTHOR "jenkins"

static inline void MPPLogVersionInfo(void)
{
    LOGI(">>>>>>>>>>>>>>>>>>>>>>>>>>>>> Media Process Platform<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
    LOGI("modName : %s", REPO_MODULE_NAME);
    LOGI("tag   : %s", REPO_TAG);
    LOGI("branch: %s", REPO_BRANCH);
    LOGI("commit: %s", REPO_COMMIT);
    LOGI("date  : %s", REPO_DATE);
    LOGI("author: %s", RELEASE_AUTHOR);
}

#ifdef __cplusplus
}
#endif

#endif  /* _MPP_VERSION_H_ */

