
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <log/log.h>

#include <ChannelRegionInfo.h>

ChannelRegionInfo* ChannelRegionInfo_Construct()
{
    ChannelRegionInfo *pRegion = (ChannelRegionInfo*)malloc(sizeof(ChannelRegionInfo));
    if(NULL == pRegion)
    {
        LOGE("fatal error! malloc fail[%s]!", strerror(errno));
        return NULL;
    }
    memset(pRegion, 0, sizeof(ChannelRegionInfo));
    return pRegion;
}

void ChannelRegionInfo_Destruct(ChannelRegionInfo *pRegion)
{
    if(pRegion->mBmp.mpData)
    {
        free(pRegion->mBmp.mpData);
        pRegion->mBmp.mpData = NULL;
    }
    free(pRegion);
}

