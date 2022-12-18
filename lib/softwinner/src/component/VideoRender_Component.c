/*******************************************************************************
--                                                                            --
--                    CedarX Multimedia Framework                             --
--                                                                            --
--          the Multimedia Framework for Linux/Android System                 --
--                                                                            --
--       This software is confidential and proprietary and may be used        --
--        only as expressly authorized by a licensing agreement from          --
--                         Softwinner Products.                               --
--                                                                            --
--                   (C) COPYRIGHT 2011 SOFTWINNER PRODUCTS                   --
--                            ALL RIGHTS RESERVED                             --
--                                                                            --
--                 The entire notice above must be reproduced                 --
--                  on all copies and should not be removed.                  --
--                                                                            --
*******************************************************************************/

//#define LOG_NDEBUG 0
#define LOG_TAG "VideoRender_Component"
#include <log/log.h>

//#include <threads.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>

#include <mm_component.h>
//#include <CDX_FormatConvert.h>
#include <SystemBase.h>
#include <VIDEO_FRAME_INFO_S.h>

#include <tmessage.h>
#include <tsemaphore.h>

#include <video_render.h>
//#include <cdx_math.h>
#include <vdecoder.h>

#include <CDX_ErrorType.h>
//#include <cutils/properties.h>
#include <VdecCompStream.h>
#include "VideoRender_Component.h"
#include <mpi_videoformat_conversion.h>

#include <plat_systrace.h>

#include <cdx_list.h>
//#include <ConfigOption.h>

//----------------------------------------------------------------------------
#define MAX_VDRENDER_PORTS 2
typedef enum VIDEORENDER_PORT_SUFFIX_DEFINITION{
    VDR_PORT_SUFFIX_CLOCK = 0,
    VDR_PORT_SUFFIX_VIDEO = 1,
}VIDEORENDER_PORT_SUFFIX_DEFINITION;

#define VIDEO_RENDER_FIRST_FRAME_FLAG        1

static void* VideoRender_ComponentThread(void* pThreadData);

typedef struct
{
    ANativeWindowBufferCedarXWrapper mANWBuffer;
    struct list_head mList;
} VRANWBuffer;

typedef struct VOCompInputFrame
{
    VIDEO_FRAME_INFO_S mFrameInfo;
    struct list_head mList;
}VOCompInputFrame;

typedef struct VIDEORENDERDATATYPE {
    COMP_STATETYPE state;
    pthread_mutex_t mStateMutex;
    COMP_CALLBACKTYPE *pCallbacks;
    void* pAppData;
    COMP_HANDLETYPE hSelf;
    COMP_PORT_PARAM_TYPE sPortParam;
    COMP_PARAM_PORTDEFINITIONTYPE sInPortDef[MAX_VDRENDER_PORTS];
    COMP_INTERNAL_TUNNELINFOTYPE sInPortTunnelInfo[MAX_VDRENDER_PORTS];
    COMP_PARAM_BUFFERSUPPLIERTYPE sPortBufSupplier[MAX_VDRENDER_PORTS];
    BOOL mInputPortTunnelFlags[MAX_VDRENDER_PORTS];
    BOOL mOutputPortTunnelFlag;  //TRUE: tunnel mode; FALSE: non-tunnel mode.
    MPP_CHN_S mMppChnInfo;
    VO_CHN_ATTR_S mChnAttr;

    pthread_t thread_id;
    CompInternalMsgType eTCmd;
    message_queue_t  cmd_queue;
    BOOL start_to_play;
    int  wait_time_out;

    BOOL is_ref_clock;
    int  video_rend_flag;   //VIDEO_RENDER_FIRST_FRAME_FLAG
    BOOL mbRenderingStart;
    volatile int  priv_flag;
    char   av_sync;
    int64_t last_pts;   //last render frame pts, unit:us
    CDX_VideoRenderHAL *hnd_cdx_video_render;
    int hnd_cdx_video_render_init_flag;
    BOOL mbShowPicFlag; //should show picture.
    ANativeWindowBufferCedarXWrapper    mANativeWindowBuffer;

    BOOL    mResolutionChangeFlag;
    int render_seeking_flag;
    VideoRenderMode VRenderMode;    //cedarv_output_setting=CEDARX_OUTPUT_SETTING_MODE_PLANNER

    struct list_head        mVideoInputFrameIdleList;   // VOCompInputFrame
    struct list_head        mVideoInputFrameReadyList;
    struct list_head        mVideoInputFrameUsedList;
    BOOL                mWaitInputFrameFlag; //1: no input frame, wait!
    pthread_mutex_t         mVideoInputFrameListMutex;

    unsigned int mDispBufNum;    //cache display frameBuf number. scope:[0],[2,15]
    int mVideoDisplayTopX;     //record vdec frame displayTopX and displayTopY
    int mVideoDisplayTopY;
    int mVideoDisplayWidth;     //record vdec frame display_width and display_height
    int mVideoDisplayHeight;
    BOOL    mbNeedNotifyDisplaySize;

    int64_t mPlayFrameInterval;     //indicate two frame's interval which be used to display. unit:us
    int64_t mLastRenderFramePts;    //last inFrame's pts, unit:us

    int mStoreFrameCnt;

    struct list_head    mANWBuffersList;    //VRANWBuffer, for VideoRender_GUI mode

    //int subDelay;   //unit:ms
}VIDEORENDERDATATYPE;

static int convertColorPrimary2ColorSpace(int nColorPrimary)
{
    int cdxColorSpace;
    int nVideoFullRange = (nColorPrimary >> 8) & 0xFF;
    switch(nVideoFullRange)
    {
        case 0x0:
            cdxColorSpace = VENC_BT601;
            break;
        case 0x1:
            cdxColorSpace = VENC_YCC;
            break;
        default:
            LOGE("fatal error! unknown video full range[%d]", nVideoFullRange);
            cdxColorSpace = VENC_YCC;
            break;
    }
    return cdxColorSpace;
}

ERRORTYPE PropertyGet_PlayFrameInterval(int64_t *pInterval)
{
    LOGV("play frame interval use 40fps as default");
    *pInterval = 1000000/40;
    //return ERR_VO_NOT_SUPPORT;
    return SUCCESS;
    /*
    char prop_value[PROPERTY_VALUE_MAX];
    int len = property_get(PROP_KEY_PLAY_FRAME_INTERVAL, prop_value, NULL);
    if(len <= 0)
    {
        LOGD("key[%s] is not find", PROP_KEY_PLAY_FRAME_INTERVAL);
        return ERR_VO_NOT_SUPPORT;
    }
    LOGD("key[%s] value[%s]ms", PROP_KEY_PLAY_FRAME_INTERVAL, prop_value);
    *pInterval = (int64_t)atoi(prop_value)*1000;
    return SUCCESS;
    */
}

ERRORTYPE VideoRenderAddFrame_l(
        PARAM_IN VIDEORENDERDATATYPE *pVideoRenderData,
        PARAM_IN VIDEO_FRAME_INFO_S* pInFrame)
{
    VOCompInputFrame *pNode;
    if(list_empty(&pVideoRenderData->mVideoInputFrameIdleList))
    {
        LOGW("input frame list is empty, increase one");
        // return error if there is no more memory
        pNode = (VOCompInputFrame*)malloc(sizeof(VOCompInputFrame));
        if(NULL == pNode)
        {
            LOGE("fatal error! malloc fail!");
            return ERR_VO_NO_MEM;
        }
        memset(pNode, 0, sizeof(VOCompInputFrame));
        list_add_tail(&pNode->mList, &pVideoRenderData->mVideoInputFrameIdleList);
    }
    pNode = list_first_entry(&pVideoRenderData->mVideoInputFrameIdleList, VOCompInputFrame, mList);
    memcpy(&pNode->mFrameInfo, pInFrame, sizeof(VIDEO_FRAME_INFO_S));
    list_move_tail(&pNode->mList, &pVideoRenderData->mVideoInputFrameReadyList);
    return SUCCESS;
}

ERRORTYPE VideoRenderReleaseFrame_l(
        PARAM_IN VIDEORENDERDATATYPE *pVideoRenderData,
        PARAM_IN VOCompInputFrame* pFrame)
{
    ERRORTYPE omxRet = SUCCESS;
    VOCompInputFrame *pEntry;
    BOOL bFindFlag = FALSE;
    list_for_each_entry(pEntry, &pVideoRenderData->mVideoInputFrameUsedList, mList)
    {
        if(pEntry == pFrame)
        {
            bFindFlag = TRUE;
            break;
        }
    }
    if(FALSE == bFindFlag)
    {
        list_for_each_entry(pEntry, &pVideoRenderData->mVideoInputFrameReadyList, mList)
        {
            if(pEntry == pFrame)
            {
                bFindFlag = TRUE;
                break;
            }
        }
    }
    if(bFindFlag)
    {
        if(pVideoRenderData->mInputPortTunnelFlags[VDR_PORT_SUFFIX_VIDEO])
        {
            COMP_BUFFERHEADERTYPE    obh;
            obh.nOutputPortIndex = pVideoRenderData->sInPortTunnelInfo[VDR_PORT_SUFFIX_VIDEO].nTunnelPortIndex;
            obh.nInputPortIndex = pVideoRenderData->sInPortTunnelInfo[VDR_PORT_SUFFIX_VIDEO].nPortIndex;
            obh.pOutputPortPrivate = (void*)&pEntry->mFrameInfo;
            omxRet = COMP_FillThisBuffer(pVideoRenderData->sInPortTunnelInfo[VDR_PORT_SUFFIX_VIDEO].hTunnel, &obh);
            if(omxRet != SUCCESS)
            {
                LOGE("fatal error! fill this buffer fail[0x%x], nId=[%d], check code!", omxRet, pFrame->mFrameInfo.mId);
            }
            else
            {
                list_move_tail(&pEntry->mList, &pVideoRenderData->mVideoInputFrameIdleList);
            }
        }
        else
        {
            COMP_BUFFERHEADERTYPE obh;
            obh.pAppPrivate = (void*)&pEntry->mFrameInfo;
            pVideoRenderData->pCallbacks->EmptyBufferDone(
                    pVideoRenderData->hSelf, 
                    pVideoRenderData->pAppData, 
                    &obh);
            list_move_tail(&pEntry->mList, &pVideoRenderData->mVideoInputFrameIdleList);
        }
    }
    else
    {
        LOGE("fatal error! not find pFrame[%p], pictureId[%d] in used list.", pFrame, pFrame->mFrameInfo.mId);
        omxRet = ERR_VO_ILLEGAL_PARAM;
    }
    return omxRet;
}

ERRORTYPE VideoRenderReleaseFrame(
        PARAM_IN VIDEORENDERDATATYPE *pVideoRenderData,
        PARAM_IN VOCompInputFrame* pFrame)
{
    ERRORTYPE eError;
    pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
    eError = VideoRenderReleaseFrame_l(pVideoRenderData, pFrame);
    pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
    return eError;
}

/*******************************************************************************
Function name: VideoRenderANWBufferComeBack
Description: 
    find in pVideoRenderData->mANWBuffersList, and config it, mbOccupyFlag=1,
Parameters: 
    
Return: 
    
Time: 2015/9/12
*******************************************************************************/
ANativeWindowBufferCedarXWrapper* VideoRenderANWBufferComeBack(VIDEORENDERDATATYPE *pVideoRenderData, ANativeWindowBufferCedarXWrapper *pANWB)
{
    if(list_empty(&pVideoRenderData->mANWBuffersList))
    {
        LOGE("fatal error! ANWBuffersList is empty!");
        return NULL;
    }
    int num = 0;
    VRANWBuffer *pEntry;
    ANativeWindowBufferCedarXWrapper *pANWBuf = NULL;
    list_for_each_entry(pEntry, &pVideoRenderData->mANWBuffersList, mList)
    {
        if(pEntry->mANWBuffer.dst == pANWB->dst)
        {
            if(pEntry->mANWBuffer.width == pANWB->width
                && pEntry->mANWBuffer.height == pANWB->height
                && pEntry->mANWBuffer.stride == pANWB->stride
                && pEntry->mANWBuffer.format == pANWB->format
                && pEntry->mANWBuffer.usage == pANWB->usage
                //&& pEntry->mANWBuffer.dstPhy == pANWB->dstPhy
                && pEntry->mANWBuffer.pObjANativeWindowBuffer == pANWB->pObjANativeWindowBuffer
                //&& pEntry->mANWBuffer.mIonUserHandle == pANWB->mIonUserHandle
                )
            {
            }
            else
            {
                LOGE("fatal error! ANativeWindowBufferCedarXWrapper not match:"
                    "[%dx%d][%d][0x%x][0x%x][%p][%p][%p][%d]\n"
                    "[%dx%d][%d][0x%x][0x%x][%p][%p][%p][%d]\n", 
                    pEntry->mANWBuffer.width, pEntry->mANWBuffer.height, pEntry->mANWBuffer.stride, 
                    pEntry->mANWBuffer.format, pEntry->mANWBuffer.usage, 
                    pEntry->mANWBuffer.dst, pEntry->mANWBuffer.dstPhy, 
                    pEntry->mANWBuffer.pObjANativeWindowBuffer, pEntry->mANWBuffer.mIonUserHandle, 
                    pANWB->width, pANWB->height, pANWB->stride, 
                    pANWB->format, pANWB->usage, 
                    pANWB->dst, pANWB->dstPhy, 
                    pANWB->pObjANativeWindowBuffer, pANWB->mIonUserHandle);
                if(pEntry->mANWBuffer.pObjANativeWindowBuffer != pANWB->pObjANativeWindowBuffer)
                {
                    pEntry->mANWBuffer.pObjANativeWindowBuffer = pANWB->pObjANativeWindowBuffer;
                }
            }
            if(0 == pEntry->mANWBuffer.mbOccupyFlag)
            {
                pEntry->mANWBuffer.mbOccupyFlag = 1;
            }
            else
            {
                LOGE("fatal error! why this ANWB dst[%p] is not goto GUI?", pEntry->mANWBuffer.dst);
            }
            
            if(0 == num)
            {
                pANWBuf = &pEntry->mANWBuffer;
                num++;
            }
            else
            {
                LOGE("fatal error! more AndroidNativeWindowBuffer has buffer[%p]!", pEntry->mANWBuffer.dst);
                num++;
            }
        }
    }
    return pANWBuf;
    
}

VOCompInputFrame* VideoRenderFindUsedFrameByANWBuffer(VIDEORENDERDATATYPE *pVideoRenderData, ANativeWindowBufferCedarXWrapper *pANWB)
{
    ERRORTYPE omxRet = SUCCESS;
    VOCompInputFrame *pEntry;
    BOOL bFindFlag = FALSE;
    list_for_each_entry(pEntry, &pVideoRenderData->mVideoInputFrameUsedList, mList)
    {
        if(pEntry->mFrameInfo.VFrame.mpVirAddr[0] == pANWB->dst)
        {
            bFindFlag = TRUE;
            break;
        }
    }
    if(bFindFlag)
    {
        return pEntry;
    }
    else
    {
        LOGE("fatal error! not find frame[%p] in used list.", pANWB->dst);
        return NULL;
    }
}


static ANativeWindowBufferCedarXWrapper* VideoRenderFindANWBufferByFrame(VIDEORENDERDATATYPE* pVideoRenderData, VOCompInputFrame *pFrame)
{
    if(list_empty(&pVideoRenderData->mANWBuffersList))
    {
        LOGE("fatal error! ANWBuffersList is empty!");
        return NULL;
    }
    int num = 0;
    VRANWBuffer *pEntry;
    ANativeWindowBufferCedarXWrapper *pANWBuf = NULL;
    list_for_each_entry(pEntry, &pVideoRenderData->mANWBuffersList, mList)
    {
        if(pEntry->mANWBuffer.dst == (void*)pFrame->mFrameInfo.VFrame.mpVirAddr[0])
        {
            if(0 == num)
            {
                pANWBuf = &pEntry->mANWBuffer;
                num++;
            }
            else
            {
                LOGE("fatal error! more AndroidNativeWindowBuffer has buffer[%p]!", pEntry->mANWBuffer.dst);
                num++;
            }
        }
    }
    return pANWBuf;
}
static ERRORTYPE VideoRenderDestroyANWBuffersInfo(VIDEORENDERDATATYPE *pVideoRenderData)
{
    if(list_empty(&pVideoRenderData->mANWBuffersList))
    {
        return SUCCESS;
    }
    int ret;
    //cancel frame to gpu
    VRANWBuffer *pEntry, *pTmp;
    ANativeWindowBufferCedarXWrapper *pANWBuf = NULL;
    list_for_each_entry(pEntry, &pVideoRenderData->mANWBuffersList, mList)
    {
        if(pEntry->mANWBuffer.mbOccupyFlag)
        {
            ret = pVideoRenderData->hnd_cdx_video_render->cancel_frame(pVideoRenderData->hnd_cdx_video_render, &pEntry->mANWBuffer);
            if(SUCCESS == ret)
            {
                pEntry->mANWBuffer.mbOccupyFlag = 0;
            }
            else
            {
                LOGW("fatal error! CancelFrame fail[%d]", ret);
            }
        }
    }
    //free list
    list_for_each_entry_safe(pEntry, pTmp, &pVideoRenderData->mANWBuffersList, mList)
    {
        list_del(&pEntry->mList);
        free(pEntry);
    }
    return SUCCESS;
}

static ERRORTYPE VideoRenderCreateANWBuffersInfo(VIDEORENDERDATATYPE *pVideoRenderData, CdxANWBuffersInfo *pANWBuffersInfo)
{
    int i;
    VRANWBuffer *pBuf;
    if(!list_empty(&pVideoRenderData->mANWBuffersList))
    {
        LOGE("fatal error! why ANWBuffersList is not empty!");
    }
    for(i=0; i<pANWBuffersInfo->mnBufNum; i++)
    {
        pBuf = (VRANWBuffer*)malloc(sizeof(VRANWBuffer));
        if(NULL == pBuf)
        {
            LOGE("fatal error! malloc fail!");
            return ERR_VO_NO_MEM;
        }
        memset(pBuf, 0, sizeof(VRANWBuffer));
        memcpy(&pBuf->mANWBuffer, &pANWBuffersInfo->mANWBuffers[i], sizeof(ANativeWindowBufferCedarXWrapper));
        list_add_tail(&pBuf->mList, &pVideoRenderData->mANWBuffersList);
    }

    return SUCCESS;
}

ERRORTYPE VideoRenderGetMPPChannelInfo(
        PARAM_IN COMP_HANDLETYPE hComponent,
        PARAM_OUT MPP_CHN_S *pChn)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    ERRORTYPE eError = SUCCESS;
    copy_MPP_CHN_S(pChn, &pVideoRenderData->mMppChnInfo);
    return eError;
}

ERRORTYPE VideoRenderSetMPPChannelInfo(
        PARAM_IN COMP_HANDLETYPE hComponent,
        PARAM_IN MPP_CHN_S *pChn)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    copy_MPP_CHN_S(&pVideoRenderData->mMppChnInfo, pChn);
    return SUCCESS;
}

ERRORTYPE VideoRenderSetChnAttr(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_IN VO_CHN_ATTR_S *pChnAttr)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    ERRORTYPE eError = SUCCESS;
    memcpy(&pVideoRenderData->mChnAttr, pChnAttr, sizeof(VO_CHN_ATTR_S));
    return eError;
}

ERRORTYPE VideoRenderGetChnAttr(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_OUT VO_CHN_ATTR_S* pChnAttr)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    memcpy(pChnAttr, &pVideoRenderData->mChnAttr, sizeof(VO_CHN_ATTR_S));
    return SUCCESS;
}

ERRORTYPE VideoRenderGetDisplaySize(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_OUT SIZE_S *pDisplaySize)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    ERRORTYPE eError = SUCCESS;
    pDisplaySize->Width = pVideoRenderData->mVideoDisplayWidth;
    pDisplaySize->Height = pVideoRenderData->mVideoDisplayHeight;
    return eError;
}

ERRORTYPE VideoRenderSetDispBufNum(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_OUT unsigned int nDispBufNum)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    ERRORTYPE eError = SUCCESS;
    pVideoRenderData->mDispBufNum = nDispBufNum;
    return eError;
}

ERRORTYPE VideoRenderGetDispBufNum(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_OUT unsigned int *pDispBufNum)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    ERRORTYPE eError = SUCCESS;
    *pDispBufNum = pVideoRenderData->mDispBufNum;
    return eError;
}

ERRORTYPE VideoRenderStoreFrame(
    PARAM_IN COMP_HANDLETYPE hComponent, 
    PARAM_IN uint64_t framePts)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    message_t   msg;
    memset(&msg, 0, sizeof(message_t));
    msg.command = VRenderComp_StoreFrame;
    msg.mpData = (void*)&framePts;
    msg.mDataSize = sizeof(framePts);
    putMessageWithData(&pVideoRenderData->cmd_queue, &msg);
    return eError;
}

ERRORTYPE VideoRenderGetPortDefinition(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_INOUT COMP_PARAM_PORTDEFINITIONTYPE *pPortDef)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    int i;
    ERRORTYPE eError = SUCCESS;
    for(i=0; i<pVideoRenderData->sPortParam.nPorts; i++)
    {
        if(pPortDef->nPortIndex == pVideoRenderData->sInPortDef[i].nPortIndex)
        {
            memcpy(pPortDef, &pVideoRenderData->sInPortDef[i], sizeof(COMP_PARAM_PORTDEFINITIONTYPE));
        }
    }
    if(i == pVideoRenderData->sPortParam.nPorts)
    {
        eError = ERR_VO_ILLEGAL_PARAM;
    }
    return eError;
}

ERRORTYPE VideoRenderGetCompBufferSupplier(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_INOUT COMP_PARAM_BUFFERSUPPLIERTYPE *pPortBufSupplier)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    ERRORTYPE eError = SUCCESS;
    //find nPortIndex
    BOOL bFindFlag = FALSE;
    int i;
    for(i=0; i<MAX_VDRENDER_PORTS; i++)
    {
        if(pVideoRenderData->sPortBufSupplier[i].nPortIndex == pPortBufSupplier->nPortIndex)
        {
            bFindFlag = TRUE;
            memcpy(pPortBufSupplier, &pVideoRenderData->sPortBufSupplier[i], sizeof(COMP_PARAM_BUFFERSUPPLIERTYPE));
            break;
        }
    }
    if(bFindFlag)
    {
        eError = SUCCESS;
    }
    else
    {
        eError = ERR_VO_ILLEGAL_PARAM;
    }
    return eError;
}

ERRORTYPE VideoRenderGetPortParam(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_OUT COMP_PORT_PARAM_TYPE *pPortParam)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    memcpy(pPortParam, &pVideoRenderData->sPortParam, sizeof(COMP_PORT_PARAM_TYPE));
    return SUCCESS;
}

ERRORTYPE VideoRenderGetTimeCurrentMediaTime(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_OUT COMP_TIME_CONFIG_TIMESTAMPTYPE *pComponentConfigStructure)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    COMP_TIME_CONFIG_TIMESTAMPTYPE *timestamp = (COMP_TIME_CONFIG_TIMESTAMPTYPE*)pComponentConfigStructure;
    timestamp->nTimestamp = pVideoRenderData->last_pts;
    return SUCCESS;
}

ERRORTYPE VideoRenderSetPortDefinition(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_IN COMP_PARAM_PORTDEFINITIONTYPE *pPortDef)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    int i;
    ERRORTYPE eError = SUCCESS;
    for(i=0; i<pVideoRenderData->sPortParam.nPorts; i++)
    {
        if(pPortDef->nPortIndex == pVideoRenderData->sInPortDef[i].nPortIndex)
        {
            memcpy(&pVideoRenderData->sInPortDef[i], pPortDef, sizeof(COMP_PARAM_PORTDEFINITIONTYPE));
            break;
        }
    }
    if(i == pVideoRenderData->sPortParam.nPorts)
    {
        eError = ERR_VO_ILLEGAL_PARAM;
    }
    return eError;
}

ERRORTYPE VideoRenderSetCompBufferSupplier(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_IN COMP_PARAM_BUFFERSUPPLIERTYPE *pPortBufSupplier)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError;
    //find nPortIndex
    BOOL bFindFlag = FALSE;
    int i;
    for(i=0; i<MAX_VDRENDER_PORTS; i++)
    {
        if(pVideoRenderData->sPortBufSupplier[i].nPortIndex == pPortBufSupplier->nPortIndex)
        {
            bFindFlag = TRUE;
            memcpy(&pVideoRenderData->sPortBufSupplier[i], pPortBufSupplier, sizeof(COMP_PARAM_BUFFERSUPPLIERTYPE));
            break;
        }
    }
    if(bFindFlag)
    {
        eError = SUCCESS;
    }
    else
    {
        eError = ERR_VO_ILLEGAL_PARAM;
    }
    return eError;
}

ERRORTYPE VideoRenderSetTimeActiveRefClock(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_IN COMP_TIME_CONFIG_ACTIVEREFCLOCKTYPE *pRefClock)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    if(pRefClock->eClock == COMP_TIME_RefClockVideo)
    {
        pVideoRenderData->is_ref_clock = TRUE;
    }
    else
    {
        LOGE("fatal error! check RefClock[0x%x]", pRefClock->eClock);
        pVideoRenderData->is_ref_clock = FALSE;
    }
    return eError;
}

ERRORTYPE VideoRenderSeek(PARAM_IN COMP_HANDLETYPE hComponent)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    pVideoRenderData->render_seeking_flag   = 1;
    //keep used frames, return other frames. vdeclib permit return frame out-of-order.
    pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
    int cnt = 0;
    struct list_head *pList;
    list_for_each(pList, &pVideoRenderData->mVideoInputFrameUsedList)
    {
        cnt++;
    }
    if(cnt > 2)
    {
        LOGE("fatal error! why VR_HW used frame[%d]>2? check code!", cnt);
    }
    VOCompInputFrame    *pEntry, *pTmp;
    COMP_BUFFERHEADERTYPE    obh;
    ERRORTYPE   omxRet;
    cnt = 0;
    list_for_each_entry_safe(pEntry, pTmp, &pVideoRenderData->mVideoInputFrameReadyList, mList)
    {
        VideoRenderReleaseFrame_l(pVideoRenderData, pEntry);
        cnt++;
    }
    LOGD("VR seek, release [%d]input video Frame!", cnt);
    pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
    return eError;
}
    
ERRORTYPE VideoRenderSwitchAudio(PARAM_IN COMP_HANDLETYPE hComponent)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    LOGD("need start to play again after switch audio!");
    pVideoRenderData->render_seeking_flag = 1;
    return eError;
}

ERRORTYPE VideoRenderSetStreamEof(PARAM_IN COMP_HANDLETYPE hComponent)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    LOGV("videoRender end flag is set");
    pVideoRenderData->priv_flag |= CDX_comp_PRIV_FLAGS_STREAMEOF;
    message_t   msg;
    msg.command = VRenderComp_InputFrameAvailable;
    put_message(&pVideoRenderData->cmd_queue, &msg);
    return eError;
}

ERRORTYPE VideoRenderClearStreamEof(PARAM_IN COMP_HANDLETYPE hComponent)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    pVideoRenderData->priv_flag &= ~CDX_comp_PRIV_FLAGS_STREAMEOF;
    return eError;
}

ERRORTYPE VideoRenderShow(PARAM_IN COMP_HANDLETYPE hComponent, BOOL bShowFlag)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    if(pVideoRenderData->mbShowPicFlag == bShowFlag)
    {
        LOGD("vo already [%s]", bShowFlag?"show":"hide");
        return eError;
    }
    pVideoRenderData->mbShowPicFlag = bShowFlag;
    if(pVideoRenderData->hnd_cdx_video_render_init_flag)
    {
        pVideoRenderData->hnd_cdx_video_render->set_showflag(pVideoRenderData->hnd_cdx_video_render, bShowFlag);
    }
    return eError;
}

ERRORTYPE VideoRenderInitVideoRenderHAL(PARAM_IN COMP_HANDLETYPE hComponent, void *callback_info)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    pVideoRenderData->hnd_cdx_video_render = cedarx_video_render_create(callback_info);
    return eError;
}

ERRORTYPE VideoRenderSetAVSync(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_IN char avSyncFlag)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    pVideoRenderData->av_sync = avSyncFlag;
    return eError;
}

ERRORTYPE VideoRenderSetVRenderMode(
        PARAM_IN COMP_HANDLETYPE hComponent, 
        PARAM_IN VideoRenderMode mode)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    pVideoRenderData->VRenderMode = mode;
    LOGD("CDX use VideoRenderMode = %d", pVideoRenderData->VRenderMode);
    return eError;
}

ERRORTYPE VideoRenderNotifyStartToRun(PARAM_IN COMP_HANDLETYPE hComponent)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    pVideoRenderData->start_to_play = TRUE;
    return eError;
}

ERRORTYPE VideoRenderResolutionChange(PARAM_IN COMP_HANDLETYPE hComponent)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    message_t   msg;
    msg.command = VRenderComp_ResolutionChange;
    put_message(&pVideoRenderData->cmd_queue, &msg);
    return eError;
}

ERRORTYPE VideoRenderSendCommand(PARAM_IN COMP_HANDLETYPE hComponent,
        PARAM_IN COMP_COMMANDTYPE Cmd, PARAM_IN unsigned int nParam1,
        PARAM_IN void* pCmdData) {
    VIDEORENDERDATATYPE *pVideoRenderData;
    CompInternalMsgType eCmd;
    ERRORTYPE eError = SUCCESS;
    message_t msg;

    pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);

    if (pVideoRenderData->state == COMP_StateInvalid)
    {
        eError = ERR_VO_CHN_INVALIDSTATE;
        goto OMX_CONF_CMD_BAIL;
    }

    switch (Cmd) 
    {
    case COMP_CommandStateSet:
        eCmd = SetState;
        break;
    case COMP_CommandFlush:
        eCmd = Flush;
        break;
    case COMP_CommandPortDisable:
        eCmd = StopPort;
        break;
    case COMP_CommandPortEnable:
        eCmd = RestartPort;
        break;
    case COMP_CommandMarkBuffer:
        eCmd = MarkBuf;
        if (nParam1 > 0)
            eError = ERR_VO_ILLEGAL_PARAM;
            goto OMX_CONF_CMD_BAIL;
        ;
        break;
    case COMP_CommandVendorChangeANativeWindow:
        eCmd = VRenderComp_ChangeANativeWindow;
        break;
    default:
        eCmd = -1;
        break;
    }

    msg.command = eCmd;
    msg.para0 = nParam1;
    put_message(&pVideoRenderData->cmd_queue, &msg);

    OMX_CONF_CMD_BAIL: return eError;
}

/*****************************************************************************/
ERRORTYPE VideoRenderGetState(PARAM_IN COMP_HANDLETYPE hComponent,
        PARAM_OUT COMP_STATETYPE* pState) {
    VIDEORENDERDATATYPE *pVideoRenderData;
    ERRORTYPE eError = SUCCESS;

    pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);

    *pState = pVideoRenderData->state;

    OMX_CONF_CMD_BAIL: return eError;
}

/*****************************************************************************/
ERRORTYPE VideoRenderSetCallbacks(PARAM_IN COMP_HANDLETYPE hComponent,
        PARAM_IN COMP_CALLBACKTYPE* pCallbacks, PARAM_IN void* pAppData) {
    VIDEORENDERDATATYPE *pVideoRenderData;
    ERRORTYPE eError = SUCCESS;

    pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);

    pVideoRenderData->pCallbacks = pCallbacks;
    pVideoRenderData->pAppData = pAppData;

    OMX_CONF_CMD_BAIL: return eError;
}

ERRORTYPE VideoRenderGetConfig(
        PARAM_IN COMP_HANDLETYPE hComponent,
        PARAM_IN COMP_INDEXTYPE nIndex,
        PARAM_INOUT void* pComponentConfigStructure)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;
    if (pVideoRenderData->state == COMP_StateInvalid)
    {
        eError = ERR_VO_CHN_INCORRECT_STATE_OPERATION;
        goto OMX_CONF_CMD_BAIL;
    }
    
    switch (nIndex)
    {
        case COMP_IndexParamPortDefinition:
        {
            eError = VideoRenderGetPortDefinition(hComponent, (COMP_PARAM_PORTDEFINITIONTYPE*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexParamCompBufferSupplier:
        {
            eError = VideoRenderGetCompBufferSupplier(hComponent, (COMP_PARAM_BUFFERSUPPLIERTYPE*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorGetPortParam:
        {
            eError = VideoRenderGetPortParam(hComponent, (COMP_PORT_PARAM_TYPE*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexConfigTimeCurrentMediaTime:
        {
            eError = VideoRenderGetTimeCurrentMediaTime(hComponent, (COMP_TIME_CONFIG_TIMESTAMPTYPE*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorMPPChannelInfo:
        {
            eError = VideoRenderGetMPPChannelInfo(hComponent, (MPP_CHN_S*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorVOChnAttr:
        {
            eError = VideoRenderGetChnAttr(hComponent, (VO_CHN_ATTR_S*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorVOChnDisplaySize:
        {
            eError = VideoRenderGetDisplaySize(hComponent, (SIZE_S*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorVODispBufNum:
        {
            eError = VideoRenderGetDispBufNum(hComponent, (unsigned int*)pComponentConfigStructure);
            break;
        }
        default:
            LOGE("fatal error! unknown index[0x%x]", nIndex);
            eError = ERR_VO_ILLEGAL_PARAM;
            break;
    }

OMX_CONF_CMD_BAIL:
    return eError;
}

ERRORTYPE VideoRenderSetConfig(
        PARAM_IN COMP_HANDLETYPE hComponent,
        PARAM_IN COMP_INDEXTYPE nIndex,
        PARAM_IN void* pComponentConfigStructure)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE*)((MM_COMPONENTTYPE*)hComponent)->pComponentPrivate;
    ERRORTYPE eError = SUCCESS;

    switch (nIndex)
    {
        case COMP_IndexParamPortDefinition:
        {
            eError = VideoRenderSetPortDefinition(hComponent, (COMP_PARAM_PORTDEFINITIONTYPE*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexParamCompBufferSupplier:
        {
            eError = VideoRenderSetCompBufferSupplier(hComponent, (COMP_PARAM_BUFFERSUPPLIERTYPE*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexConfigTimeActiveRefClock:
        {
            eError = VideoRenderSetTimeActiveRefClock(hComponent, (COMP_TIME_CONFIG_ACTIVEREFCLOCKTYPE*)pComponentConfigStructure);
    		break;
    	}
        case COMP_IndexVendorSeekToPosition:
        {
            eError = VideoRenderSeek(hComponent);
    		break;
        }
        case COMP_IndexVendorSwitchAudio:
        {
            eError = VideoRenderSwitchAudio(hComponent);
            break;
        }
        case COMP_IndexVendorSetStreamEof:
        {
            eError = VideoRenderSetStreamEof(hComponent);
            break;
        }
        case COMP_IndexVendorClearStreamEof:
        {
            eError = VideoRenderClearStreamEof(hComponent);
            break;
        }
        case COMP_IndexVendorInitInstance:
        {
            eError = VideoRenderInitVideoRenderHAL(hComponent, pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorSetAVSync:
        {
            eError = VideoRenderSetAVSync(hComponent, *(char*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorVRenderMode:
        {
            eError = VideoRenderSetVRenderMode(hComponent, *(VideoRenderMode*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorNotifyStartToRun:
        {
            eError = VideoRenderNotifyStartToRun(hComponent);
            break;
        }
        case COMP_IndexVendorResolutionChange:
        {
            eError = VideoRenderResolutionChange(hComponent);
            break;
        }
        case COMP_IndexVendorMPPChannelInfo:
        {
            eError = VideoRenderSetMPPChannelInfo(hComponent, (MPP_CHN_S*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorVOChnAttr:
        {
            eError = VideoRenderSetChnAttr(hComponent, (VO_CHN_ATTR_S*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorShow:
        {
            eError = VideoRenderShow(hComponent, *(BOOL*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorVODispBufNum:
        {
            eError = VideoRenderSetDispBufNum(hComponent, *(unsigned int*)pComponentConfigStructure);
            break;
        }
        case COMP_IndexVendorStoreFrame:
        {
            eError = VideoRenderStoreFrame(hComponent, *(uint64_t*)pComponentConfigStructure);
            break;
        }
        default:
        {
            LOGE("fatal error! unknown nIndex[0x%x] in state[%d]", nIndex, pVideoRenderData->state);
            eError = ERR_VO_ILLEGAL_PARAM;
            break;
        }
    }

    return eError;
}

ERRORTYPE VideoRenderComponentTunnelRequest(
    PARAM_IN  COMP_HANDLETYPE hComponent,
    PARAM_IN  unsigned int nPort,
    PARAM_IN  COMP_HANDLETYPE hTunneledComp,
    PARAM_IN  unsigned int nTunneledPort,
    PARAM_INOUT  COMP_TUNNELSETUPTYPE* pTunnelSetup)
{
    ERRORTYPE eError = SUCCESS;
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    if (pVideoRenderData->state == COMP_StateExecuting)
    {
        LOGW("Be careful! tunnel request may be some danger in StateExecuting");
    }
    else if(pVideoRenderData->state != COMP_StateIdle)
    {
        LOGE("fatal error! tunnel request can't be in state[0x%x]", pVideoRenderData->state);
        eError = ERR_VO_CHN_INCORRECT_STATE_OPERATION;
        goto COMP_CMD_FAIL;
    }
    COMP_PARAM_PORTDEFINITIONTYPE    *pPortDef;
    COMP_INTERNAL_TUNNELINFOTYPE              *pPortTunnelInfo;
    COMP_PARAM_BUFFERSUPPLIERTYPE    *pPortBufSupplier;
    int nInPortSuffix = -1;
    BOOL bFindFlag;
    int i;
    bFindFlag = FALSE;
    for(i=0; i<MAX_VDRENDER_PORTS; i++)
    {
        if(pVideoRenderData->sInPortDef[i].nPortIndex == nPort)
        {
            pPortDef = &pVideoRenderData->sInPortDef[i];
            bFindFlag = TRUE;
            nInPortSuffix =i;
            break;
        }
    }
    if(FALSE == bFindFlag)
    {
        LOGE("fatal error! portIndex[%d] wrong!", nPort);
        eError = ERR_VO_ILLEGAL_PARAM;
        goto COMP_CMD_FAIL;
    }

    bFindFlag = FALSE;
    for(i=0; i<MAX_VDRENDER_PORTS; i++)
    {
        if(pVideoRenderData->sInPortTunnelInfo[i].nPortIndex == nPort)
        {
            pPortTunnelInfo = &pVideoRenderData->sInPortTunnelInfo[i];
            bFindFlag = TRUE;
            break;
        }
    }
    if(FALSE == bFindFlag)
    {
        LOGE("fatal error! portIndex[%d] wrong!", nPort);
        eError = ERR_VO_ILLEGAL_PARAM;
        goto COMP_CMD_FAIL;
    }

    bFindFlag = FALSE;
    for(i=0; i<MAX_VDRENDER_PORTS; i++)
    {
        if(pVideoRenderData->sPortBufSupplier[i].nPortIndex == nPort)
        {
            pPortBufSupplier = &pVideoRenderData->sPortBufSupplier[i];
            bFindFlag = TRUE;
            break;
        }
    }
    if(FALSE == bFindFlag)
    {
        LOGE("fatal error! portIndex[%d] wrong!", nPort);
        eError = ERR_VO_ILLEGAL_PARAM;
        goto COMP_CMD_FAIL;
    }
    
    pPortTunnelInfo->nPortIndex = nPort;
    pPortTunnelInfo->hTunnel = hTunneledComp;
    pPortTunnelInfo->nTunnelPortIndex = nTunneledPort;
    pPortTunnelInfo->eTunnelType = (pPortDef->eDomain == COMP_PortDomainOther) ? TUNNEL_TYPE_CLOCK : TUNNEL_TYPE_COMMON;
    if(NULL==hTunneledComp && 0==nTunneledPort && NULL==pTunnelSetup)
    {
        LOGD("omx_core cancel setup tunnel on port[%d]", nPort);
        eError = SUCCESS;
        goto COMP_CMD_FAIL;
    }
    if (pPortTunnelInfo->eTunnelType == TUNNEL_TYPE_CLOCK) 
    {
        CDX_NotifyStartToRunTYPE NotifyStartToRunInfo;
        NotifyStartToRunInfo.nPortIndex = pPortTunnelInfo->nTunnelPortIndex;
        NotifyStartToRunInfo.mbNotify = TRUE;
        COMP_SetConfig(
                pPortTunnelInfo->hTunnel,
                COMP_IndexVendorNotifyStartToRunInfo,
                (void*)&NotifyStartToRunInfo);
    }
    if(pPortDef->eDir == COMP_DirOutput)
    {
        if (pVideoRenderData->mOutputPortTunnelFlag) {
            LOGE("VO_Comp outport already bind, why bind again?!");
            eError = FAILURE;
            goto COMP_CMD_FAIL;
        }
        pTunnelSetup->nTunnelFlags = 0;
        pTunnelSetup->eSupplier = pPortBufSupplier->eBufferSupplier;
        pVideoRenderData->mOutputPortTunnelFlag = TRUE;
    }
    else
    {
        if (pVideoRenderData->mInputPortTunnelFlags[VDR_PORT_SUFFIX_CLOCK] && pVideoRenderData->mInputPortTunnelFlags[VDR_PORT_SUFFIX_VIDEO]) {
            LOGE("VO_Comp inport already bind, why bind again?!");
            eError = FAILURE;
            goto COMP_CMD_FAIL;
        }
        //Check the data compatibility between the ports using one or more GetParameter calls.
        //B checks if its input port is compatible with the output port of component A.
        COMP_PARAM_PORTDEFINITIONTYPE out_port_def;
        out_port_def.nPortIndex = nTunneledPort;
        COMP_GetConfig(hTunneledComp, COMP_IndexParamPortDefinition, &out_port_def);
        if(out_port_def.eDir != COMP_DirOutput)
        {
            LOGE("fatal error! tunnel port index[%d] direction is not output!", nTunneledPort);
            eError = ERR_VO_ILLEGAL_PARAM;
            goto COMP_CMD_FAIL;
        }
        pPortDef->format = out_port_def.format;
    
        //The component B informs component A about the final result of negotiation.
        if(pTunnelSetup->eSupplier != pPortBufSupplier->eBufferSupplier)
        {
            LOGW("Low probability! use input portIndex[%d] buffer supplier[%d] as final!", nPort, pPortBufSupplier->eBufferSupplier);
            pTunnelSetup->eSupplier = pPortBufSupplier->eBufferSupplier;
        }
        COMP_PARAM_BUFFERSUPPLIERTYPE oSupplier;
        oSupplier.nPortIndex = nTunneledPort;
        COMP_GetConfig(hTunneledComp, COMP_IndexParamCompBufferSupplier, &oSupplier);
        oSupplier.eBufferSupplier = pTunnelSetup->eSupplier;
        COMP_SetConfig(hTunneledComp, COMP_IndexParamCompBufferSupplier, &oSupplier);
        pVideoRenderData->mInputPortTunnelFlags[nInPortSuffix] = TRUE;
    }

COMP_CMD_FAIL:
    return eError;
}

ERRORTYPE VideoRenderEmptyThisBuffer(
            PARAM_IN  COMP_HANDLETYPE hComponent,
            PARAM_IN  COMP_BUFFERHEADERTYPE* pBuffer)
{
    VIDEORENDERDATATYPE *pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);
    ERRORTYPE eError = SUCCESS;
    VIDEO_FRAME_INFO_S *pInFrame;

    pthread_mutex_lock(&pVideoRenderData->mStateMutex);
    if(pVideoRenderData->state!=COMP_StateIdle && pVideoRenderData->state!=COMP_StateExecuting && pVideoRenderData->state!=COMP_StatePause)
    {
        LOGD("call function in invalid state[0x%x]!", pVideoRenderData->state);
        pthread_mutex_unlock(&pVideoRenderData->mStateMutex);
        return ERR_VO_CHN_INCORRECT_STATE_OPERATION;
    }
    if(pVideoRenderData->mInputPortTunnelFlags[VDR_PORT_SUFFIX_VIDEO])
    {
        pInFrame = (VIDEO_FRAME_INFO_S*)pBuffer->pOutputPortPrivate;
    }
    else
    {
        pInFrame = (VIDEO_FRAME_INFO_S*)pBuffer->pAppPrivate;
    }
    
    /*
    LOGD("inPts[%lld]ms", (int64_t)pInFrame->VFrame.mpts/1000);
    if((int64_t)pInFrame->VFrame.mpts - pVideoRenderData->mLastRenderFramePts > 60*1000)
    {
        LOGW("Be careful! pts jump[%lld]ms, [%lld]ms-[%lldms]", ((int64_t)pInFrame->VFrame.mpts - pVideoRenderData->mLastRenderFramePts)/1000, (int64_t)pInFrame->VFrame.mpts/1000, pVideoRenderData->mLastRenderFramePts/1000);
    }
    */

    if(pVideoRenderData->mLastRenderFramePts >=0)
    {
        if ((int64_t)pInFrame->VFrame.mpts < pVideoRenderData->mLastRenderFramePts) 
        {
            pVideoRenderData->mLastRenderFramePts = pInFrame->VFrame.mpts;
        }
        if ((int64_t)pInFrame->VFrame.mpts - pVideoRenderData->mLastRenderFramePts < pVideoRenderData->mPlayFrameInterval) 
        {
            pthread_mutex_unlock(&pVideoRenderData->mStateMutex);
            return ERR_VO_SYS_NOTREADY;
        }
    }
    
    pVideoRenderData->mLastRenderFramePts = pInFrame->VFrame.mpts;

    if(pVideoRenderData->mInputPortTunnelFlags[VDR_PORT_SUFFIX_VIDEO])
    {
        if(pBuffer->nInputPortIndex == pVideoRenderData->sInPortDef[VDR_PORT_SUFFIX_VIDEO].nPortIndex)
        {
            pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
            eError = VideoRenderAddFrame_l(pVideoRenderData, pInFrame);
            if (eError != SUCCESS)
            {
                pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
                goto ADD_FRM_ERR;
            }
            if(pVideoRenderData->mWaitInputFrameFlag)
            {
                pVideoRenderData->mWaitInputFrameFlag = FALSE;
                message_t msg;
                msg.command = VRenderComp_InputFrameAvailable;
                put_message(&pVideoRenderData->cmd_queue, &msg);
            }
            pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
        }
        else
        {
            LOGE("fatal error! inputPortIndex[%u] match nothing!", pBuffer->nInputPortIndex);
            eError = ERR_VO_ILLEGAL_PARAM;
        }
    }
    else
    {
        pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
        eError = VideoRenderAddFrame_l(pVideoRenderData, pInFrame);
        if (eError != SUCCESS)
        {
            pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
            goto ADD_FRM_ERR;
        }
        if(pVideoRenderData->mWaitInputFrameFlag)
        {
            pVideoRenderData->mWaitInputFrameFlag = FALSE;
            message_t msg;
            msg.command = VRenderComp_InputFrameAvailable;
            put_message(&pVideoRenderData->cmd_queue, &msg);
        }
        pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
    }

ADD_FRM_ERR:
    pthread_mutex_unlock(&pVideoRenderData->mStateMutex);

    return eError;
}

ERRORTYPE VideoRenderComponentDeInit(PARAM_IN COMP_HANDLETYPE hComponent) {
    VIDEORENDERDATATYPE *pVideoRenderData;
    ERRORTYPE eError = SUCCESS;
    CompInternalMsgType eCmd = Stop;
    message_t msg;

    pVideoRenderData = (VIDEORENDERDATATYPE *) (((MM_COMPONENTTYPE*) hComponent)->pComponentPrivate);

    msg.command = eCmd;
    put_message(&pVideoRenderData->cmd_queue, &msg);

    LOGV("wait video render component exit!...");
    // Wait for thread to exit so we can get the status into "error"
    pthread_join(pVideoRenderData->thread_id, (void*) &eError);

    LOGV("video render component exited 0!");

    message_destroy(&pVideoRenderData->cmd_queue);

    if(pVideoRenderData->hnd_cdx_video_render != NULL) 
    {
        pVideoRenderData->hnd_cdx_video_render->exit(pVideoRenderData->hnd_cdx_video_render);
        cedarx_video_render_destroy(pVideoRenderData->hnd_cdx_video_render);
    }

    pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
    if (!list_empty(&pVideoRenderData->mVideoInputFrameUsedList)) 
    {
        LOGE("fatal error! inputUsedFrame must be 0!");
    }
    if (!list_empty(&pVideoRenderData->mVideoInputFrameReadyList)) 
    {
        LOGE("fatal error! inputReadyFrame must be 0!");
    }
    if (!list_empty(&pVideoRenderData->mVideoInputFrameIdleList)) 
    {
        VOCompInputFrame *pEntry, *pTmp;
        list_for_each_entry_safe(pEntry, pTmp, &pVideoRenderData->mVideoInputFrameIdleList, mList)
        {
            list_del(&pEntry->mList);
            free(pEntry);
        }
    }
    pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
    
    pthread_mutex_destroy(&pVideoRenderData->mStateMutex);
    pthread_mutex_destroy(&pVideoRenderData->mVideoInputFrameListMutex);
    if(pVideoRenderData)
    {
        free(pVideoRenderData);
        pVideoRenderData = NULL;
    }

    LOGV("video render component exited 1!");

    return eError;
}

/*****************************************************************************/
ERRORTYPE VideoRenderComponentInit(PARAM_IN COMP_HANDLETYPE hComponent) 
{
    MM_COMPONENTTYPE *pComp;
    VIDEORENDERDATATYPE *pVideoRenderData;
    ERRORTYPE eError = SUCCESS;
    unsigned int err;
    int i;

    pComp = (MM_COMPONENTTYPE *) hComponent;

    // Create private data
    pVideoRenderData = (VIDEORENDERDATATYPE *) malloc(sizeof(VIDEORENDERDATATYPE));
    memset(pVideoRenderData, 0x0, sizeof(VIDEORENDERDATATYPE));
    LOGV("hComponent %p, pVideoRenderData %p", hComponent, pVideoRenderData);
    pComp->pComponentPrivate = (void*) pVideoRenderData;
    pVideoRenderData->state = COMP_StateLoaded;
    pVideoRenderData->hSelf = hComponent;
    pVideoRenderData->mVideoDisplayWidth = -1;
    pVideoRenderData->mVideoDisplayHeight = -1;
    pVideoRenderData->mbShowPicFlag = TRUE;
    pVideoRenderData->mLastRenderFramePts = -1;
    PropertyGet_PlayFrameInterval(&pVideoRenderData->mPlayFrameInterval);

    err = pthread_mutex_init(&pVideoRenderData->mStateMutex, NULL);
    if(err!=0)
    {
        LOGE("fatal error! pthread mutex init fail!");
        eError = ERR_VO_SYS_NOTREADY;
        goto EXIT0;
    }
    INIT_LIST_HEAD(&pVideoRenderData->mVideoInputFrameIdleList);
    INIT_LIST_HEAD(&pVideoRenderData->mVideoInputFrameReadyList);
    INIT_LIST_HEAD(&pVideoRenderData->mVideoInputFrameUsedList);
    for (i = 0; i < 16; i++) 
    {
        VOCompInputFrame *pNode = (VOCompInputFrame*)malloc(sizeof(VOCompInputFrame));
        if (NULL == pNode) 
        {
            LOGE("fatal error! malloc fail[%s]!", strerror(errno));
            break;
        }
        memset(pNode, 0, sizeof(VOCompInputFrame));
        list_add_tail(&pNode->mList, &pVideoRenderData->mVideoInputFrameIdleList);
    }
    INIT_LIST_HEAD(&pVideoRenderData->mANWBuffersList);
    err = pthread_mutex_init(&pVideoRenderData->mVideoInputFrameListMutex, NULL);
    if(err!=0)
    {
        LOGE("fatal error! pthread mutex init fail!");
        eError = ERR_VO_NO_MEM;
        goto EXIT2;
    }
    // Fill in function pointers
    pComp->SetCallbacks = VideoRenderSetCallbacks;
    pComp->SendCommand = VideoRenderSendCommand;
    pComp->GetConfig = VideoRenderGetConfig;
    pComp->SetConfig = VideoRenderSetConfig;
    pComp->GetState = VideoRenderGetState;
    pComp->ComponentTunnelRequest = VideoRenderComponentTunnelRequest;
    pComp->EmptyThisBuffer = VideoRenderEmptyThisBuffer;
    pComp->ComponentDeInit = VideoRenderComponentDeInit;

    // Initialize component data structures to default values
    pVideoRenderData->sPortParam.nPorts = 0;
    pVideoRenderData->sPortParam.nStartPortNumber = 0x0;

    pVideoRenderData->sPortBufSupplier[VDR_PORT_SUFFIX_CLOCK].nPortIndex = 0x0;
    pVideoRenderData->sPortBufSupplier[VDR_PORT_SUFFIX_CLOCK].eBufferSupplier = COMP_BufferSupplyOutput;
    pVideoRenderData->sPortBufSupplier[VDR_PORT_SUFFIX_VIDEO].nPortIndex = 0x1;
    pVideoRenderData->sPortBufSupplier[VDR_PORT_SUFFIX_VIDEO].eBufferSupplier = COMP_BufferSupplyOutput;

    pVideoRenderData->sInPortTunnelInfo[VDR_PORT_SUFFIX_CLOCK].nPortIndex = 0x0;
    pVideoRenderData->sInPortTunnelInfo[VDR_PORT_SUFFIX_CLOCK].eTunnelType = TUNNEL_TYPE_CLOCK;
    pVideoRenderData->sInPortTunnelInfo[VDR_PORT_SUFFIX_VIDEO].nPortIndex = 0x1;
    pVideoRenderData->sInPortTunnelInfo[VDR_PORT_SUFFIX_VIDEO].eTunnelType = TUNNEL_TYPE_COMMON;

    pVideoRenderData->sInPortDef[VDR_PORT_SUFFIX_CLOCK].nPortIndex = pVideoRenderData->sPortParam.nPorts;
    pVideoRenderData->sInPortDef[VDR_PORT_SUFFIX_CLOCK].bEnabled = TRUE;
    pVideoRenderData->sInPortDef[VDR_PORT_SUFFIX_CLOCK].eDomain = COMP_PortDomainOther;
    pVideoRenderData->sInPortDef[VDR_PORT_SUFFIX_CLOCK].eDir = COMP_DirInput;
    pVideoRenderData->sPortParam.nPorts++;

    pVideoRenderData->sInPortDef[VDR_PORT_SUFFIX_VIDEO].nPortIndex = pVideoRenderData->sPortParam.nPorts;
    pVideoRenderData->sInPortDef[VDR_PORT_SUFFIX_VIDEO].bEnabled = TRUE;
    pVideoRenderData->sInPortDef[VDR_PORT_SUFFIX_VIDEO].eDomain = COMP_PortDomainVideo;
    pVideoRenderData->sInPortDef[VDR_PORT_SUFFIX_VIDEO].eDir = COMP_DirInput;
    pVideoRenderData->sPortParam.nPorts++;

    if(message_create(&pVideoRenderData->cmd_queue) < 0)
    {
        LOGE("message error!");
        eError = ERR_VO_SYS_NOTREADY;
        goto EXIT3;
    }
    // Create the component thread
    err = pthread_create(&pVideoRenderData->thread_id, NULL, VideoRender_ComponentThread, pVideoRenderData);
    if (err || !pVideoRenderData->thread_id) 
    {
        eError = ERR_VO_SYS_NOTREADY;
        goto EXIT4;
    }
    return eError;
EXIT4:
//    free(pVideoRenderData->pANativeWindowBuffer);
//    pVideoRenderData->pANativeWindowBuffer = NULL;
EXIT3:
    pthread_mutex_destroy(&pVideoRenderData->mVideoInputFrameListMutex);
EXIT2:
EXIT1:
    pthread_mutex_destroy(&pVideoRenderData->mStateMutex);
EXIT0: 
    return eError;
}

ERRORTYPE VideoRenderGetFrame_l(
        PARAM_IN VIDEORENDERDATATYPE *pVideoRenderData,
        PARAM_OUT VOCompInputFrame** ppFrame)
{
    ERRORTYPE eError;
    if(!list_empty(&pVideoRenderData->mVideoInputFrameReadyList))
    {
        VOCompInputFrame *pOutFrame = list_first_entry(&pVideoRenderData->mVideoInputFrameReadyList, VOCompInputFrame, mList);
        list_move_tail(&pOutFrame->mList, &pVideoRenderData->mVideoInputFrameUsedList);
        *ppFrame = pOutFrame;
        eError = SUCCESS;
    }
    else
    {
        eError = ERR_VO_NO_MEM;
    }
    return eError;
}
ERRORTYPE VideoRenderGetFrame(
        PARAM_IN VIDEORENDERDATATYPE *pVideoRenderData,
        PARAM_OUT VOCompInputFrame** ppFrame)
{
    ERRORTYPE eError;
    pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
    eError = VideoRenderGetFrame_l(pVideoRenderData, ppFrame);
    pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
    return eError;
}

/*****************************************************************************/
static void* VideoRender_ComponentThread(void* pThreadData)
{
    unsigned int              cmddata;
    CompInternalMsgType           cmd;
    VIDEORENDERDATATYPE* pVideoRenderData;
    COMP_BUFFERHEADERTYPE omx_buffer_header;
    message_t            cmd_msg;
    int              i;
    COMP_HANDLETYPE   hnd_vdec_comp;
    COMP_HANDLETYPE     hnd_clock_comp;
    MM_COMPONENTTYPE*   hnd_sub_comp;
    COMP_INTERNAL_TUNNELINFOTYPE*  p_vdec_tunnel;
    COMP_INTERNAL_TUNNELINFOTYPE*  p_clock_tunnel;
    COMP_BUFFERHEADERTYPE omx_sub_header;

    prctl(PR_SET_NAME, (unsigned long)"CDX_VRender", 0, 0, 0);

    int64_t total_interval = 0;
    int64_t dec_cnt = 0;
                    
    hnd_vdec_comp    = NULL;
    hnd_clock_comp   = NULL;
    hnd_sub_comp     = NULL;
    p_vdec_tunnel    = NULL;
    p_clock_tunnel   = NULL;
    pVideoRenderData = (VIDEORENDERDATATYPE*) pThreadData;

    pVideoRenderData->video_rend_flag = VIDEO_RENDER_FIRST_FRAME_FLAG;
    
    //omx_buffer_header.pBuffer = pVideoRenderData->p_frame_info;
    while (1)
    {
PROCESS_MESSAGE:
        if(get_message(&pVideoRenderData->cmd_queue, &cmd_msg) == 0)
        {       
            cmd = cmd_msg.command;
            cmddata = (unsigned int)cmd_msg.para0;

            // State transition command
            if (cmd == SetState)
            {
                pthread_mutex_lock(&pVideoRenderData->mStateMutex);
                if (pVideoRenderData->state == (COMP_STATETYPE) (cmddata))
                {
                    pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                               pVideoRenderData->pAppData,
                                                               COMP_EventError,
                                                               ERR_VO_CHN_SAMESTATE,
                                                               pVideoRenderData->state,
                                                               NULL);
                }
                else
                {
                    switch ((COMP_STATETYPE) (cmddata))
                    {
                    case COMP_StateInvalid:
                        pVideoRenderData->state = COMP_StateInvalid;
                        pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                                   pVideoRenderData->pAppData,
                                                                   COMP_EventError,
                                                                   ERR_VO_CHN_INVALIDSTATE,
                                                                   0,
                                                                   NULL);
                        pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                                   pVideoRenderData->pAppData,
                                                                   COMP_EventCmdComplete,
                                                                   COMP_CommandStateSet,
                                                                   pVideoRenderData->state,
                                                                   NULL);
                        break;

                    case COMP_StateLoaded:
                    {
                        if (pVideoRenderData->state != COMP_StateIdle)
                        {
                            LOGE("fatal error! VideoRender incorrect state transition [0x%x]->Loaded!", pVideoRenderData->state);
                            pVideoRenderData->pCallbacks->EventHandler(
                                    pVideoRenderData->hSelf, 
                                    pVideoRenderData->pAppData,
                                    COMP_EventError,
                                    ERR_VO_CHN_INCORRECT_STATE_TRANSITION, 
                                    0, 
                                    NULL);
                        }
                        ERRORTYPE   omxRet;
                            //release all frames.
                        LOGV("release all frames to VDec, when state[0x%x]->[0x%x]", pVideoRenderData->state, COMP_StateLoaded);
                        pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
                        if(!list_empty(&pVideoRenderData->mVideoInputFrameUsedList))
                        {
                            int cnt = 0;
                            struct list_head *pList;
                            list_for_each(pList, &pVideoRenderData->mVideoInputFrameUsedList)
                            {
                                cnt++;
                            }
                            LOGD("release [%d]used inputFrame! state[%d->loaded]", cnt, pVideoRenderData->state);
                            VOCompInputFrame *pEntry, *pTmp;
                            list_for_each_entry_safe(pEntry, pTmp, &pVideoRenderData->mVideoInputFrameUsedList, mList)
                            {
                                VideoRenderReleaseFrame_l(pVideoRenderData, pEntry);
                            }
                        }
                        if(!list_empty(&pVideoRenderData->mVideoInputFrameReadyList))
                        {
                            int cnt = 0;
                            struct list_head *pList;
                            list_for_each(pList, &pVideoRenderData->mVideoInputFrameReadyList)
                            {
                                cnt++;
                            }
                            LOGD("release [%d]unused inputFrame! state[%d->loaded]", cnt, pVideoRenderData->state);
                            VOCompInputFrame    *pEntry, *pTmp;
                            list_for_each_entry_safe(pEntry, pTmp, &pVideoRenderData->mVideoInputFrameReadyList, mList)
                            {
                                VideoRenderReleaseFrame_l(pVideoRenderData, pEntry);
                            }
                        }
                        pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
                        VideoRenderDestroyANWBuffersInfo(pVideoRenderData);
                        pVideoRenderData->state = COMP_StateLoaded;
                        pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                                   pVideoRenderData->pAppData,
                                                                   COMP_EventCmdComplete,
                                                                   COMP_CommandStateSet,
                                                                   pVideoRenderData->state,
                                                                   NULL);
                        break;
                    }
                    case COMP_StateIdle:
                        if (pVideoRenderData->state == COMP_StateInvalid)
                        {
                            pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                                       pVideoRenderData->pAppData,
                                                                       COMP_EventError,
                                                                       ERR_VO_CHN_INCORRECT_STATE_OPERATION,
                                                                       0,
                                                                       NULL);
                        }
                        else
                        {
                            LOGV("VideoRender state[0x%x]->Idle!", pVideoRenderData->state);
                            if ((pVideoRenderData->state == COMP_StateExecuting) || (pVideoRenderData->state == COMP_StatePause))
                            {
                                //release all frames.  //for vi comp will wait for all frame buffer release when stop chn(vi state to idle)
                                ERRORTYPE omxRet;
                                LOGV("release all frames");
                                pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
                                if(!list_empty(&pVideoRenderData->mVideoInputFrameUsedList))
                                {
                                    int cnt = 0;
                                    struct list_head *pList;
                                    list_for_each(pList, &pVideoRenderData->mVideoInputFrameUsedList)
                                    {
                                        cnt++;
                                    }
                                    LOGD("release [%d]used inputFrame!", cnt);
                                    VOCompInputFrame *pEntry, *pTmp;
                                    list_for_each_entry_safe(pEntry, pTmp, &pVideoRenderData->mVideoInputFrameUsedList, mList)
                                    {
                                        VideoRenderReleaseFrame_l(pVideoRenderData, pEntry);
                                    }
                                }
                                if(!list_empty(&pVideoRenderData->mVideoInputFrameReadyList))
                                {
                                    int cnt = 0;
                                    struct list_head *pList;
                                    list_for_each(pList, &pVideoRenderData->mVideoInputFrameReadyList)
                                    {
                                        cnt++;
                                    }
                                    LOGD("release [%d]unused inputFrame!", cnt);
                                    VOCompInputFrame    *pEntry, *pTmp;
                                    list_for_each_entry_safe(pEntry, pTmp, &pVideoRenderData->mVideoInputFrameReadyList, mList)
                                    {
                                        VideoRenderReleaseFrame_l(pVideoRenderData, pEntry);
                                    }
                                }
                                pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
                                VideoRenderDestroyANWBuffersInfo(pVideoRenderData);
                            }

                            pVideoRenderData->state = COMP_StateIdle;
                            pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf, 
                                                                    pVideoRenderData->pAppData,
                                                                    COMP_EventCmdComplete,
                                                                    COMP_CommandStateSet,
                                                                    pVideoRenderData->state,
                                                                    NULL);
                        }
                        break;

                    case COMP_StateExecuting:
                        // Transition can only happen from pause or idle state
                        if (pVideoRenderData->state == COMP_StateIdle || pVideoRenderData->state == COMP_StatePause)
                        {
                            hnd_vdec_comp  = (pVideoRenderData->sInPortTunnelInfo[VDR_PORT_SUFFIX_VIDEO].hTunnel);
                            p_vdec_tunnel  = &pVideoRenderData->sInPortTunnelInfo[VDR_PORT_SUFFIX_VIDEO];
                            hnd_clock_comp = (pVideoRenderData->sInPortTunnelInfo[VDR_PORT_SUFFIX_CLOCK].hTunnel);
                            p_clock_tunnel = &pVideoRenderData->sInPortTunnelInfo[VDR_PORT_SUFFIX_CLOCK];
                            #if 1
                            if(pVideoRenderData->state == COMP_StateIdle)
                            {
                                pVideoRenderData->video_rend_flag |= VIDEO_RENDER_FIRST_FRAME_FLAG;
                                if(!pVideoRenderData->av_sync)
                                    pVideoRenderData->start_to_play = TRUE;
                                else
                                    pVideoRenderData->start_to_play = FALSE;

                                pVideoRenderData->wait_time_out = 0;
                                if(pVideoRenderData->render_seeking_flag)
                                {
                                    LOGD("seek in idle state?");
                                    pVideoRenderData->render_seeking_flag = 0;
                                }
                            }
                            else if(pVideoRenderData->state == COMP_StatePause)
                            {
                                //if current Pause is caused by jump, we need set first frame flag. Because we need reset start_time to clock_component.
                                if(pVideoRenderData->render_seeking_flag)
                                {
                                    pVideoRenderData->video_rend_flag |= VIDEO_RENDER_FIRST_FRAME_FLAG;
                                    if(!pVideoRenderData->av_sync)
                                        pVideoRenderData->start_to_play = TRUE;
                                    else
                                        pVideoRenderData->start_to_play = FALSE;

                                    pVideoRenderData->wait_time_out = 0;
                                    pVideoRenderData->render_seeking_flag = 0;
                                }
                            }
                            #else
                            pVideoRenderData->video_rend_flag |= VIDEO_RENDER_FIRST_FRAME_FLAG;

                            if(!pVideoRenderData->av_sync)
                                pVideoRenderData->start_to_play = TRUE;
                            else
                                pVideoRenderData->start_to_play = FALSE;

                            pVideoRenderData->wait_time_out = 0;
                            #endif
                            pVideoRenderData->state = COMP_StateExecuting;

                            pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                                       pVideoRenderData->pAppData,
                                                                       COMP_EventCmdComplete,
                                                                       COMP_CommandStateSet,
                                                                       pVideoRenderData->state,
                                                                       NULL);
                        }
                        else
                        {
                            pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                                       pVideoRenderData->pAppData,
                                                                       COMP_EventError,
                                                                       ERR_VO_CHN_INCORRECT_STATE_OPERATION,
                                                                       0,
                                                                       NULL);
                        }
                        break;

                    case COMP_StatePause:
                        // Transition can only happen from idle or executing state
                        if (pVideoRenderData->state == COMP_StateIdle || pVideoRenderData->state == COMP_StateExecuting)
                        {
                            pVideoRenderData->state = COMP_StatePause;
                            pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                                       pVideoRenderData->pAppData,
                                                                       COMP_EventCmdComplete,
                                                                       COMP_CommandStateSet,
                                                                       pVideoRenderData->state,
                                                                       NULL);
                        }
                        else
                            pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                                       pVideoRenderData->pAppData,
                                                                       COMP_EventError,
                                                                       ERR_VO_CHN_INCORRECT_STATE_TRANSITION,
                                                                       0,
                                                                       NULL);
                        break;

                    default:
                        break;
                    }
                }
                pthread_mutex_unlock(&pVideoRenderData->mStateMutex);
            }
            else if (cmd == Stop)
            {
                // Kill thread
                goto EXIT;
            }
            else if (cmd == VRenderComp_InputFrameAvailable)
            {
                LOGV("inputFrameAvailable");
            }
            else if (cmd == VRenderComp_ChangeANativeWindow)
            {          
                if(pVideoRenderData->state!=COMP_StatePause)
                {
                    LOGE("fatal error! state[%d] is not pause when change window", pVideoRenderData->state);
                }
                LOGD("change Android Native Window");
                if(pVideoRenderData->hnd_cdx_video_render_init_flag)
                {
                    if(VideoRender_GUI == pVideoRenderData->VRenderMode)
                    {
                        ERRORTYPE omxRet;
                        //return gui occupied frames to vdec. mVideoInputFrameUsedList
                        pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
                        if(!list_empty(&pVideoRenderData->mVideoInputFrameUsedList))
                        {
                            LOGD("release used video frame!");
                            VOCompInputFrame *pEntry, *pTmp;
                            list_for_each_entry_safe(pEntry, pTmp, &pVideoRenderData->mVideoInputFrameUsedList, mList)
                            {
                                VideoRenderReleaseFrame_l(pVideoRenderData, pEntry);
                            }
                        }
                        //return frames to VDecComponent.
                        //cancel frames to ANativeWindow
                        int nCancelFrameRet;
                        if(!list_empty(&pVideoRenderData->mVideoInputFrameReadyList))
                        {
                            LOGD("release all inputVideoFrame!");
                            VOCompInputFrame    *pEntry, *pTmp;
                            list_for_each_entry_safe(pEntry, pTmp, &pVideoRenderData->mVideoInputFrameReadyList, mList)
                            {
                                VideoRenderReleaseFrame_l(pVideoRenderData, pEntry);
                                ANativeWindowBufferCedarXWrapper *pANWBuffer = VideoRenderFindANWBufferByFrame(pVideoRenderData, pEntry);
                                if(NULL == pANWBuffer)
                                {
                                    LOGW("fatal error! not find ANWBuffer for VideoPicture!");
                                    //pVideoRenderData->state = COMP_StateInvalid;
                                    //goto PROCESS_MESSAGE;
                                    continue;
                                }
                                nCancelFrameRet = pVideoRenderData->hnd_cdx_video_render->cancel_frame(pVideoRenderData->hnd_cdx_video_render, pANWBuffer);
                                if(SUCCESS == nCancelFrameRet)
                                {
                                    pANWBuffer->mbOccupyFlag = 0;
                                }
                                else
                                {
                                    LOGE("fatal error! CancelFrame fail[%d]", nCancelFrameRet);
                                }
                            }
                        }
                        pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
                        //clear all ANWBuffers
                        VideoRenderDestroyANWBuffersInfo(pVideoRenderData);
                    }
                    pVideoRenderData->hnd_cdx_video_render->exit(pVideoRenderData->hnd_cdx_video_render);
                    pVideoRenderData->hnd_cdx_video_render_init_flag = 0;
                }
                else
                {
                    LOGD("Be careful! not init video render when change Android Native Window in state[%d]", pVideoRenderData->state);
                }
                pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                           pVideoRenderData->pAppData,
                                                           COMP_EventCmdComplete,
                                                           COMP_CommandVendorChangeANativeWindow,
                                                           pVideoRenderData->state,
                                                           NULL);
            }
            else if (cmd == VRenderComp_ResolutionChange)
            {
                LOGD("Resolution Change");
                pVideoRenderData->mResolutionChangeFlag = TRUE;
            }
            else if(cmd == VRenderComp_StoreFrame)
            {
                uint64_t framePts = *(uint64_t*)cmd_msg.mpData;
                pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
                if(!list_empty(&pVideoRenderData->mVideoInputFrameUsedList) || !list_empty(&pVideoRenderData->mVideoInputFrameReadyList))
                {
                    //find nearest frame to store
                    int64_t minInterval = -1;
                    int64_t minAbsInterval = -1;
                    int64_t nInterval;
                    int64_t nAbsInterval;
                    VOCompInputFrame *pEntry;
                    VOCompInputFrame *pDstEntry;
                    list_for_each_entry(pEntry, &pVideoRenderData->mVideoInputFrameUsedList, mList)
                    {
                        if(pEntry->mFrameInfo.VFrame.mpts >= framePts)
                        {
                            nAbsInterval = (int64_t)(pEntry->mFrameInfo.VFrame.mpts - framePts);
                            nInterval = nAbsInterval;
                        }
                        else
                        {
                            nAbsInterval = (int64_t)(framePts - pEntry->mFrameInfo.VFrame.mpts);
                            nInterval = -nAbsInterval;
                        }
                        if(-1 == minAbsInterval || minAbsInterval > nAbsInterval)
                        {
                            minAbsInterval = nAbsInterval;
                            minInterval = nInterval;
                            pDstEntry = pEntry;
                        }
                    }
                    list_for_each_entry(pEntry, &pVideoRenderData->mVideoInputFrameReadyList, mList)
                    {
                        if(pEntry->mFrameInfo.VFrame.mpts >= framePts)
                        {
                            nAbsInterval = (int64_t)(pEntry->mFrameInfo.VFrame.mpts - framePts);
                            nInterval = nAbsInterval;
                        }
                        else
                        {
                            nAbsInterval = (int64_t)(framePts - pEntry->mFrameInfo.VFrame.mpts);
                            nInterval = -nAbsInterval;
                        }
                        if(-1 == minAbsInterval || minAbsInterval > nAbsInterval)
                        {
                            minAbsInterval = nAbsInterval;
                            minInterval = nInterval;
                            pDstEntry = pEntry;
                        }
                    }
                    LOGD("match voPts[%lld]-dstPts[%lld]=[%lld]us", pDstEntry->mFrameInfo.VFrame.mpts, framePts, minInterval);
                    if(MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420 == pDstEntry->mFrameInfo.VFrame.mPixelFormat)
                    {
                        char DbgStoreFilePath[256];
                        snprintf(DbgStoreFilePath, 256, "/tmp/pic[%d][%lldus].nv21", pVideoRenderData->mStoreFrameCnt++, pDstEntry->mFrameInfo.VFrame.mpts);
                        LOGW("prepare store frame in file[%s]", DbgStoreFilePath);
                        FILE *dbgFp = fopen(DbgStoreFilePath, "wb");
                        if(dbgFp != NULL)
                        {
                            VideoFrameBufferSizeInfo FrameSizeInfo;
                            getVideoFrameBufferSizeInfo(&pDstEntry->mFrameInfo, &FrameSizeInfo);
                            int yuvSize[3] = {FrameSizeInfo.mYSize, FrameSizeInfo.mUSize, FrameSizeInfo.mVSize};
                            for(int i=0; i<3; i++)
                            {
                                if(pDstEntry->mFrameInfo.VFrame.mpVirAddr[i] != NULL)
                                {
                                    fwrite(pDstEntry->mFrameInfo.VFrame.mpVirAddr[i], 1, yuvSize[i], dbgFp);
                                    LOGD("virAddr[%d]=[%p], length=[%d]", i, pDstEntry->mFrameInfo.VFrame.mpVirAddr[i], yuvSize[i]);
                                }
                            }
                            fclose(dbgFp);
                            LOGD("store frame in file[%s]", DbgStoreFilePath);
                        }
                    }
                    else
                    {
                        LOGE("other pixel format[0x%x], need support!", pDstEntry->mFrameInfo.VFrame.mPixelFormat);
                    }
                    
                }
                pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);

                /* mpData is malloced in TMessageDeepCopyMessage() */
                free(cmd_msg.mpData);
                cmd_msg.mpData = NULL;
                cmd_msg.mDataSize = 0;
            }
            //precede to process message
            goto PROCESS_MESSAGE;
        }

        if (pVideoRenderData->state == COMP_StateExecuting)
        {
            ERRORTYPE                 ret;
            COMP_TIME_CONFIG_TIMESTAMPTYPE time_stamp;
            int64_t                       lltime_stamp2;
            int64_t                       lltime_stamp3;
            int64_t                     media_clock_time;  //unit:us
            int64_t                     media_video_time; //unit:us
            VIDEO_FRAME_INFO_S    *pic=NULL;    //VideoPicture
            int nRenderRet;
            int nQueueFrameRet;
            int nDequeueFrameRet;

            if(VideoRender_GUI == pVideoRenderData->VRenderMode)
            {

                if(0==pVideoRenderData->hnd_cdx_video_render_init_flag)
                {
                    //try to get vdec fbm para.
                    CdxVRANWInitPara nativeWindowInitPara;
                    FbmBufInfo  vdecFbmBufInfo;
                    while(1)
                    {
                        if(SUCCESS == COMP_GetConfig(hnd_vdec_comp, COMP_IndexVendorFbmBufInfo, (void*)&vdecFbmBufInfo))
                        {
                            memset(&nativeWindowInitPara, 0, sizeof(CdxVRANWInitPara));
                            nativeWindowInitPara.mnBufNum = vdecFbmBufInfo.nBufNum;
                            nativeWindowInitPara.mnBufWidth = vdecFbmBufInfo.nBufWidth;
                            nativeWindowInitPara.mnBufHeight = vdecFbmBufInfo.nBufHeight;
                            nativeWindowInitPara.mePixelFormat = vdecFbmBufInfo.ePixelFormat;
                            nativeWindowInitPara.mnAlignValue = vdecFbmBufInfo.nAlignValue;
                            nativeWindowInitPara.mbProgressiveFlag = vdecFbmBufInfo.bProgressiveFlag;
                            break;
                        }
                        else
                        {
                            LOGD("get vdec FbmBufInfo fail, wait 50ms!");
                            if(TMessage_WaitQueueNotEmpty(&pVideoRenderData->cmd_queue, 50) > 0)
                            {
                                goto PROCESS_MESSAGE;
                            }
                        }
                    }
                    //init video render, get gpu buffers, set back to vdec.
                    CdxANWBuffersInfo anwBuffersInfo;
                    memset(&anwBuffersInfo, 0, sizeof(CdxANWBuffersInfo));
                    if(SUCCESS != pVideoRenderData->hnd_cdx_video_render->init(pVideoRenderData->hnd_cdx_video_render, 
                        pVideoRenderData->VRenderMode, (void*)&nativeWindowInitPara, (void*)&anwBuffersInfo))
                    {
                        LOGW("fatal error! init ANativeWindow fail! state to OMX StateInvalid");
                        pVideoRenderData->state = COMP_StateInvalid;
                        goto PROCESS_MESSAGE;
                    }
                    VideoRenderCreateANWBuffersInfo(pVideoRenderData, &anwBuffersInfo);
                    VDecCompFrameBuffersParam setFrameBuffersParam;
                    setFrameBuffersParam.mpANWBuffersInfo = &anwBuffersInfo;
                    INIT_LIST_HEAD(&setFrameBuffersParam.mFramesOwnedByANW);
                    if(SUCCESS != COMP_SetConfig(hnd_vdec_comp, COMP_IndexVendorFrameBuffers, (void*)&setFrameBuffersParam))
                    {
                        LOGW("fatal error! set gpu buffers to vdec fail! state to OMX StateInvalid");
                        pVideoRenderData->state = COMP_StateInvalid;
                        goto PROCESS_MESSAGE;
                    }
                    //put frames owned by ANW to videoInputFrameUsedList
                    pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
                    int cnt = 0;
                    struct list_head *pList;
                    list_for_each(pList, &pVideoRenderData->mVideoInputFrameUsedList)
                    {
                        cnt++;
                    }
                    if(cnt > 0)
                    {
                        LOGE("fatal error! check videoInputFrameUsedList, it has [%d] used frames.", cnt);
                    }
                    list_splice_tail(&setFrameBuffersParam.mFramesOwnedByANW, &pVideoRenderData->mVideoInputFrameUsedList);
                    pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
                    pVideoRenderData->hnd_cdx_video_render_init_flag = 1;
                }
            }
            
            //pict = (VideoPicture*)omx_buffer_header.pBuffer;
            VOCompInputFrame *pFrame = NULL;
            pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
            ret = VideoRenderGetFrame_l(pVideoRenderData, &pFrame);
            if(ret != SUCCESS)
            {
                if(pVideoRenderData->mResolutionChangeFlag)
                {
                    LOGD("Resolution Change, vrender display all old frames, vdec can ReopenVideoEngine() and continue to decode!");
                    //return usedInputframes because we will ReopenVideoEngine().
                    if(!list_empty(&pVideoRenderData->mVideoInputFrameUsedList))
                    {
                        LOGD("release used video frame!");
                        VOCompInputFrame *pEntry, *pTmp;
                        ERRORTYPE omxRet;
                        list_for_each_entry_safe(pEntry, pTmp, &pVideoRenderData->mVideoInputFrameUsedList, mList)
                        {
                            VideoRenderReleaseFrame_l(pVideoRenderData, pEntry);
                        }
                    }
                    VideoRenderDestroyANWBuffersInfo(pVideoRenderData);
                    COMP_SetConfig(hnd_vdec_comp, COMP_IndexVendorReopenVideoEngine, NULL);
                    if(pVideoRenderData->hnd_cdx_video_render_init_flag)
                    {
                        pVideoRenderData->hnd_cdx_video_render->exit(pVideoRenderData->hnd_cdx_video_render);
                        pVideoRenderData->hnd_cdx_video_render_init_flag = 0;
                    }
                    else
                    {
                        LOGD("Be careful! not init video render when resolution change in state[%d]", pVideoRenderData->state);
                    }
                    pVideoRenderData->mVideoDisplayWidth = -1;
                    pVideoRenderData->mVideoDisplayHeight = -1;
                    pVideoRenderData->mResolutionChangeFlag = FALSE;
                    pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
                    goto PROCESS_MESSAGE;
                }
                pVideoRenderData->mWaitInputFrameFlag = TRUE;
                pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
                if(pVideoRenderData->priv_flag & CDX_comp_PRIV_FLAGS_STREAMEOF)
                {
                    LOGD("videoRender notify EOF!");
                    pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf, pVideoRenderData->pAppData, COMP_EventBufferFlag, 0, 0, NULL);
                    pVideoRenderData->state = COMP_StateIdle;
                    goto PROCESS_MESSAGE;
                }
                TMessage_WaitQueueNotEmpty(&pVideoRenderData->cmd_queue, 0);
                if(pVideoRenderData->priv_flag & CDX_comp_PRIV_FLAGS_STREAMEOF)
                {
                    LOGD("videoRender notify EOF!");
                    pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf, pVideoRenderData->pAppData, COMP_EventBufferFlag, 0, 0, NULL);
                    pVideoRenderData->state = COMP_StateIdle;
                }
                goto PROCESS_MESSAGE;
            }
            else
            {
                pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
            }
            {
                pic = &pFrame->mFrameInfo;
                if(-1 == pVideoRenderData->mVideoDisplayWidth && -1 == pVideoRenderData->mVideoDisplayHeight)
                {
                    pVideoRenderData->mVideoDisplayTopX = pic->VFrame.mOffsetLeft;
                    pVideoRenderData->mVideoDisplayTopY = pic->VFrame.mOffsetTop;
                    pVideoRenderData->mVideoDisplayWidth = pic->VFrame.mOffsetRight - pic->VFrame.mOffsetLeft;
                    pVideoRenderData->mVideoDisplayHeight = pic->VFrame.mOffsetBottom - pic->VFrame.mOffsetTop;
                    pVideoRenderData->mbNeedNotifyDisplaySize = TRUE;
                }
                if(pVideoRenderData->mVideoDisplayWidth != pic->VFrame.mOffsetRight - pic->VFrame.mOffsetLeft
                    || pVideoRenderData->mVideoDisplayHeight != pic->VFrame.mOffsetBottom - pic->VFrame.mOffsetTop
                    || pVideoRenderData->mVideoDisplayTopX != pic->VFrame.mOffsetLeft
                    || pVideoRenderData->mVideoDisplayTopY != pic->VFrame.mOffsetTop)
                {
                    LOGW("Be careful! resolution has changed! oldSize[%d,%d][%dx%d],newSize[%d,%d][%dx%d]", 
                        pVideoRenderData->mVideoDisplayTopX, pVideoRenderData->mVideoDisplayTopY, pVideoRenderData->mVideoDisplayWidth, pVideoRenderData->mVideoDisplayHeight, 
                        pic->VFrame.mOffsetLeft, pic->VFrame.mOffsetTop, pic->VFrame.mOffsetRight-pic->VFrame.mOffsetLeft, pic->VFrame.mOffsetBottom-pic->VFrame.mOffsetTop);
                    pVideoRenderData->mVideoDisplayTopX = pic->VFrame.mOffsetLeft;
                    pVideoRenderData->mVideoDisplayTopY = pic->VFrame.mOffsetTop;
                    pVideoRenderData->mVideoDisplayWidth = pic->VFrame.mOffsetRight-pic->VFrame.mOffsetLeft;
                    pVideoRenderData->mVideoDisplayHeight = pic->VFrame.mOffsetBottom-pic->VFrame.mOffsetTop;
                    pVideoRenderData->mbNeedNotifyDisplaySize = TRUE;
                }
                if(0==pVideoRenderData->hnd_cdx_video_render_init_flag)
                {
                    //init video render
                    LOGV("request first frame, init video_render, param: display_width[%d], display_height[%d], colorFormat[0x%x]", 
                        pic->VFrame.mOffsetRight-pic->VFrame.mOffsetLeft, pic->VFrame.mOffsetBottom-pic->VFrame.mOffsetTop, pic->VFrame.mPixelFormat);
                    /*{
                        char DbgStoreFilePath[256];
                        snprintf(DbgStoreFilePath, 256, "/mnt/extsd/pic.nv21");
                        LOGW("prepare store frame in file[%s]", DbgStoreFilePath);
                        FILE *dbgFp = fopen(DbgStoreFilePath, "wb");
                        if(dbgFp != NULL)
                        {
                            VideoFrameBufferSizeInfo FrameSizeInfo;
                            getVideoFrameBufferSizeInfo(pic, &FrameSizeInfo);
                            int yuvSize[3] = {FrameSizeInfo.mYSize, FrameSizeInfo.mUSize, FrameSizeInfo.mVSize};
                            for(int i=0; i<3; i++)
                            {
                                if(pic->VFrame.mpVirAddr[i] != NULL)
                                {
                                    fwrite(pic->VFrame.mpVirAddr[i], 1, yuvSize[i], dbgFp);
                                    LOGD("virAddr[%d]=[%p], length=[%d]", i, pic->VFrame.mpVirAddr[i], yuvSize[i]);
                                }
                            }
                            fclose(dbgFp);
                            LOGD("store frame in file[%s]", DbgStoreFilePath);
                        }
                    }*/
                    if(VideoRender_HW == pVideoRenderData->VRenderMode)
                    {
                        CdxVRHwcLayerInitPara hwcInitPara;
                        memset(&hwcInitPara, 0, sizeof(CdxVRHwcLayerInitPara));
                        hwcInitPara.mnBufWidth = pic->VFrame.mWidth;
                        hwcInitPara.mnBufHeight = pic->VFrame.mHeight;
                        hwcInitPara.mnDisplayTopX = pic->VFrame.mOffsetLeft;
                        hwcInitPara.mnDisplayTopY = pic->VFrame.mOffsetTop;
                        hwcInitPara.mnDisplayWidth = pic->VFrame.mOffsetRight - pic->VFrame.mOffsetLeft;
                        hwcInitPara.mnDisplayHeight = pic->VFrame.mOffsetBottom - pic->VFrame.mOffsetTop;
                        hwcInitPara.mePixelFormat = map_PIXEL_FORMAT_E_to_EPIXELFORMAT(pic->VFrame.mPixelFormat);
                        hwcInitPara.mColorSpace = VENC_BT601;   //convertColorPrimary2ColorSpace(pic->nColorPrimary);
                        hwcInitPara.mVoLayer = pVideoRenderData->mMppChnInfo.mDevId;
                        pVideoRenderData->hnd_cdx_video_render->init(pVideoRenderData->hnd_cdx_video_render,
                                pVideoRenderData->VRenderMode, &hwcInitPara, NULL);
                        pVideoRenderData->hnd_cdx_video_render->set_showflag(pVideoRenderData->hnd_cdx_video_render,
                                (int)pVideoRenderData->mbShowPicFlag);
                        //notify vdec display width and height
                        pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                               pVideoRenderData->pAppData,
                                                               COMP_EventVideoDisplaySize,
                                                               pic->VFrame.mOffsetRight - pic->VFrame.mOffsetLeft,
                                                               pic->VFrame.mOffsetBottom - pic->VFrame.mOffsetTop,
                                                               NULL);
                        pVideoRenderData->hnd_cdx_video_render_init_flag = 1;
                        pVideoRenderData->mbNeedNotifyDisplaySize = FALSE;
                        LOGD("init video_render, param: displayRect[%d,%d][%dx%d], bufSize[%dx%d], vdecColorFormat[0x%x]", 
                            hwcInitPara.mnDisplayTopX, hwcInitPara.mnDisplayTopY, 
                            hwcInitPara.mnDisplayWidth, hwcInitPara.mnDisplayHeight,
                            hwcInitPara.mnBufWidth, hwcInitPara.mnBufHeight, 
                            hwcInitPara.mePixelFormat);
                        /*{
                            LOGD("store first frame,len[%d]", pic->VFrame.mStride[0]);
                            FILE* pFp = fopen("/mnt/extsd/pic[0].raw", "wb");
                            fwrite(pic->VFrame.mpVirAddr[0], 1, pic->VFrame.mStride[0], pFp);
                            fclose(pFp);
                        }*/
                    }
                    else if(VideoRender_SW == pVideoRenderData->VRenderMode)
                    {
                        CdxVRSWANWInitPara swANWInitPara;
                        memset(&swANWInitPara, 0, sizeof(CdxVRSWANWInitPara));
                        swANWInitPara.mnBufWidth = pic->VFrame.mWidth;
                        swANWInitPara.mnBufHeight = pic->VFrame.mHeight;
                        swANWInitPara.mnDisplayWidth = pic->VFrame.mOffsetRight - pic->VFrame.mOffsetLeft;
                        swANWInitPara.mnDisplayHeight = pic->VFrame.mOffsetBottom - pic->VFrame.mOffsetTop;
                        swANWInitPara.mePixelFormat = PIXEL_FORMAT_YV12;
                        pVideoRenderData->hnd_cdx_video_render->init(pVideoRenderData->hnd_cdx_video_render,
                                pVideoRenderData->VRenderMode, &swANWInitPara, NULL);
                        pVideoRenderData->hnd_cdx_video_render_init_flag = 1;
                        pVideoRenderData->mbNeedNotifyDisplaySize = FALSE;
                    }
                    else
                    {
                        LOGW("fatal error! videoRender GUI must init video render at begin!");
                        pVideoRenderData->state = COMP_StateInvalid;
                        goto PROCESS_MESSAGE;
                    }
                }
            }
            if(VideoRender_SW == pVideoRenderData->VRenderMode)
            {
                //begin dequeue frame, convert.
                nRenderRet = pVideoRenderData->hnd_cdx_video_render->dequeue_frame(pVideoRenderData->hnd_cdx_video_render, &pVideoRenderData->mANativeWindowBuffer);
                if(CDX_OK == nRenderRet)
                {
                    //lltime_stamp2 = CDX_GetNowUs();
                    LOGW("vdec output pixel_format[0x%x], not support!", pic->VFrame.mPixelFormat);
                    //need copy to gpu frame.
//                        lltime_stamp3 = CDX_GetNowUs();
//                        total_interval += (lltime_stamp3 - lltime_stamp2);
//                        dec_cnt++;
//                        LOGD("lltime_stamp2[%lld], lltime_stamp3[%lld], interval[%lld]", lltime_stamp2, lltime_stamp3, lltime_stamp3-lltime_stamp2);
//                        LOGD("average_interval=[%lld], [%lld]/[%lld]", total_interval/dec_cnt, total_interval, dec_cnt);
                }
                else if(CDX_NO_NATIVE_WINDOW == nRenderRet)
                {
                    // mNativeWindow == NULL, we process as dequeue frame success!
                    LOGD("ret[%d], pts[%lld]us, NativeWindow=NULL, consider dequeue frame success!", nRenderRet, pic->VFrame.mpts);
                }
                else
                {
                    LOGW("fatal error! ret[%d], pts[%lld]us, why dequeue frame fail?", nRenderRet, pic->VFrame.mpts);
                    //if dequeue frame from gui fail, we ReleaseBuffer immediately, consider as display successful.
                    VideoRenderReleaseFrame(pVideoRenderData, pFrame);
                    goto PROCESS_MESSAGE;
                }
            }
            
            media_video_time = pic->VFrame.mpts;
            //LOGD("newPicture.rotate_angle=%d", newPicture.rotate_angle);
            if(pVideoRenderData->render_seeking_flag || (pVideoRenderData->video_rend_flag & VIDEO_RENDER_FIRST_FRAME_FLAG))
            {
                pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf, pVideoRenderData->pAppData, COMP_EventKeyFrameDecoded, 0, 0, (void *)&media_video_time);
                pVideoRenderData->render_seeking_flag = 0;
            }

            if (!(pVideoRenderData->video_rend_flag & VIDEO_RENDER_FIRST_FRAME_FLAG))
            {
                //****************************************************************************************************//
                //* if it is not for wifi display, we need to wait some time for synchronization.
                //****************************************************************************************************//
                //LOGV("av_sync %d, stream_source_type %d", pVideoRenderData->av_sync, pVideoRenderData->stream_source_type);
                if(pVideoRenderData->av_sync && hnd_clock_comp)
                {
                    int vpsspeed = 0;
                    COMP_GetConfig(hnd_clock_comp, COMP_IndexConfigTimeCurrentMediaTime, &time_stamp);
                    media_clock_time = time_stamp.nTimestamp;
                    COMP_GetConfig(hnd_clock_comp, COMP_IndexVendorVps, &vpsspeed);
                    vpsspeed = -vpsspeed;
                    if (media_video_time > media_clock_time && media_clock_time > 0)
                    {
                        int sleep_time = (int)(media_video_time - media_clock_time);
                        if(sleep_time > 200*1000)
                        {
                            LOGW("videorender sleep too long[%d]us, media_video_time:%lld media_clock_time:%lld", sleep_time, media_video_time, media_clock_time);
                            int delayTime = (sleep_time *100/(100+vpsspeed))/1000;
                            if(delayTime > 3*1000)
                            {
                                delayTime = 3*1000;
                            }
                            //at least delay 200ms.
                            if(delayTime>200)
                            {
                                usleep(200*1000);
                                delayTime-=200;
                            }
                            TMessage_WaitQueueNotEmpty(&pVideoRenderData->cmd_queue, (unsigned int)delayTime);
                        }
                        else if(sleep_time > 0)
                        {
                            usleep(sleep_time *100/(100+vpsspeed));
                        }
                        else
                        {
                            LOGW("sleep none!!");
                        }
                    }
                    else if(media_video_time + 100000< media_clock_time && media_clock_time > 0)
                    {
                        //decide whether give up this frame. Commonly, we don't want to give up frame. But in vps play,
                        //when vps > 0, we must give up some frames to guarantee av sync.
                        if(vpsspeed > 0)
                        {
                            LOGD("vps[%d], V:[%lld]ms S:[%lld]ms DIFF:[%lld]ms, give up frame!",
                                vpsspeed, media_video_time/1000, time_stamp.nTimestamp/1000, media_video_time/1000-time_stamp.nTimestamp/1000);
                        }
                    }
                }
            }

            //****************************************************************************************************//
            //* send the new frame to display.
            //****************************************************************************************************//
//          hnd_clock_comp->GetConfig(hnd_clock_comp,OMX_IndexConfigTimeCurrentMediaTime, &time_stamp);
//          LOGV("     V:%lld S:%lld DIFF:%lld", media_video_time/1000, time_stamp.nTimestamp/1000, media_video_time/1000-time_stamp.nTimestamp/1000);

            if(VideoRender_HW == pVideoRenderData->VRenderMode)
            {
                if(pVideoRenderData->mbNeedNotifyDisplaySize)
                {
                    CdxVRFrameInfo  frameInfo;
                    memset(&frameInfo, 0, sizeof(CdxVRFrameInfo));
                    frameInfo.mnDisplayTopX = pVideoRenderData->mVideoDisplayTopX;
                    frameInfo.mnDisplayTopY = pVideoRenderData->mVideoDisplayTopY;
                    frameInfo.mnDisplayWidth = pVideoRenderData->mVideoDisplayWidth;
                    frameInfo.mnDisplayHeight = pVideoRenderData->mVideoDisplayHeight;
                    frameInfo.mnBufWidth = pic->VFrame.mWidth;
                    frameInfo.mnBufHeight = pic->VFrame.mHeight;
                    pVideoRenderData->hnd_cdx_video_render->update_display_size(pVideoRenderData->hnd_cdx_video_render, &frameInfo);
                    //notify vdec display width and height
                    pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                           pVideoRenderData->pAppData,
                                                           COMP_EventVideoDisplaySize,
                                                           pVideoRenderData->mVideoDisplayWidth,
                                                           pVideoRenderData->mVideoDisplayHeight,
                                                           NULL);
                    pVideoRenderData->mbNeedNotifyDisplaySize = FALSE;
                }
                MPP_AtraceBegin(ATRACE_TAG_MPP_VO, "ATRACE_TAG_MPP_VO");
                pVideoRenderData->hnd_cdx_video_render->render(pVideoRenderData->hnd_cdx_video_render, (void*)pic, pic->mId);
                MPP_AtraceEnd(ATRACE_TAG_MPP_VO);
                pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
                int cnt = 0;
                struct list_head *pList;
                list_for_each(pList, &pVideoRenderData->mVideoInputFrameUsedList)
                {
                    cnt++;
                }
                if(cnt > pVideoRenderData->mDispBufNum + 1)
                {
                    LOGE("fatal error! why VR_HW used frame[%d]>[%d]? check code!", cnt, pVideoRenderData->mDispBufNum + 1);
                }
                VOCompInputFrame *pEntry;
                while(cnt>pVideoRenderData->mDispBufNum)
                {
                    pEntry = list_first_entry(&pVideoRenderData->mVideoInputFrameUsedList, VOCompInputFrame, mList);
                    VideoRenderReleaseFrame_l(pVideoRenderData, pEntry);
                    cnt--;
                }
                pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
            }
            else if(VideoRender_SW == pVideoRenderData->VRenderMode)
            {
                nRenderRet = pVideoRenderData->hnd_cdx_video_render->enqueue_frame(pVideoRenderData->hnd_cdx_video_render, &pVideoRenderData->mANativeWindowBuffer);
                if(nRenderRet != SUCCESS)
                {
                    LOGE("fatal error! videoRender_SW queue frame fail!");
                }
                VideoRenderReleaseFrame(pVideoRenderData, pFrame);
            }
            else if(VideoRender_GUI == pVideoRenderData->VRenderMode)
            {
                //need set pFrame to pANativeWindowBuffer;
                ANativeWindowBufferCedarXWrapper *pANWBuffer = VideoRenderFindANWBufferByFrame(pVideoRenderData, pFrame);
                if(NULL == pANWBuffer)
                {
                    LOGW("fatal error! not find ANWBuffer for VideoPicture!");
                    pVideoRenderData->state = COMP_StateInvalid;
                    goto PROCESS_MESSAGE;
                }
                if(pVideoRenderData->mbNeedNotifyDisplaySize)
                {
                    CdxVRFrameInfo  frameInfo;
                    memset(&frameInfo, 0, sizeof(CdxVRFrameInfo));
                    frameInfo.mnDisplayTopX = pVideoRenderData->mVideoDisplayTopX; 
                    frameInfo.mnDisplayTopY = pVideoRenderData->mVideoDisplayTopY; 
                    frameInfo.mnDisplayWidth = pVideoRenderData->mVideoDisplayWidth;
                    frameInfo.mnDisplayHeight = pVideoRenderData->mVideoDisplayHeight;
                    frameInfo.mnBufWidth = pic->VFrame.mWidth; 
                    frameInfo.mnBufHeight = pic->VFrame.mHeight;
                    pVideoRenderData->hnd_cdx_video_render->update_display_size(pVideoRenderData->hnd_cdx_video_render, &frameInfo);
                    //notify vdec display width and height
                    pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                           pVideoRenderData->pAppData,
                                                           COMP_EventVideoDisplaySize,
                                                           pVideoRenderData->mVideoDisplayWidth,
                                                           pVideoRenderData->mVideoDisplayHeight,
                                                           NULL);
                    pVideoRenderData->mbNeedNotifyDisplaySize = FALSE;
                }
                nQueueFrameRet = pVideoRenderData->hnd_cdx_video_render->enqueue_frame(pVideoRenderData->hnd_cdx_video_render, pANWBuffer);
                if(CDX_OK == nQueueFrameRet)
                {
                    pANWBuffer->mbOccupyFlag = 0;
                }
                else
                {
                    LOGW("fatal error! QueueFrame fail[%d]", nQueueFrameRet);
                }
                nDequeueFrameRet = pVideoRenderData->hnd_cdx_video_render->dequeue_frame(pVideoRenderData->hnd_cdx_video_render, &pVideoRenderData->mANativeWindowBuffer);
                if(SUCCESS == nDequeueFrameRet)
                {
                    ANativeWindowBufferCedarXWrapper *pDequeueANWB = VideoRenderANWBufferComeBack(pVideoRenderData, &pVideoRenderData->mANativeWindowBuffer);
                    pthread_mutex_lock(&pVideoRenderData->mVideoInputFrameListMutex);
                    //find VDecCompOutputFrame in mVideoInputFrameUsedList accord to ANativeWindowBufferCedarXWrapper.
                    VOCompInputFrame *pReturnFrame = VideoRenderFindUsedFrameByANWBuffer(pVideoRenderData, pDequeueANWB);
                    //return VDecCompOutputFrame to VdecComponent.
                    VideoRenderReleaseFrame_l(pVideoRenderData, pReturnFrame);
                    pthread_mutex_unlock(&pVideoRenderData->mVideoInputFrameListMutex);
                }
                else if(CDX_NO_NATIVE_WINDOW == nDequeueFrameRet)
                {
                    // mNativeWindow == NULL, we process as dequeue frame success!
                    LOGD("NativeWindow=NULL, ret[%d], pts[%lld]us, consider dequeue frame success!", nDequeueFrameRet, pic->VFrame.mpts);
                }
                else
                {
                    LOGE("fatal error! ret[%d], pts[%lld], why dequeue frame fail?", nDequeueFrameRet, pic->VFrame.mpts);
                    //if dequeue frame from gui fail, we ReleaseBuffer immediately, consider as display successful.
                }
            }
            else
            {
                LOGW("fatal error! pVideoRenderData->VRenderMode[%d]", pVideoRenderData->VRenderMode);
            }
            if(!pVideoRenderData->mbRenderingStart)
            {
                //notify rendering start
                pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf,
                                                           pVideoRenderData->pAppData,
                                                           COMP_EventRenderingStart,
                                                           0,
                                                           0,
                                                           NULL);
                pVideoRenderData->mbRenderingStart = TRUE;
            }
            pVideoRenderData->last_pts = pic->VFrame.mpts;

            //****************************************************************************************************//
            //* wait some time for audio start, clear the first frame flag.
            //****************************************************************************************************//
            if (pVideoRenderData->video_rend_flag & VIDEO_RENDER_FIRST_FRAME_FLAG)
            {
                if(media_video_time >= 0)   //* only if the pts is valid.
                {
                    if(hnd_clock_comp)
                    {
                        int time_out = 0;
                        time_stamp.nPortIndex = p_clock_tunnel->nTunnelPortIndex;
                        time_stamp.nTimestamp = media_video_time;
                        LOGD("video set config start time to [%lld]us", time_stamp.nTimestamp);
                        COMP_SetConfig(hnd_clock_comp, COMP_IndexConfigTimeClientStartTime, &time_stamp);
                        while(pVideoRenderData->start_to_play != TRUE)
                        {
                            usleep(20*1000);
                            if(pVideoRenderData->wait_time_out > 150)
                            {
                                LOGD(" video wait too long, force clock component to start.");
                                COMP_SetConfig(hnd_clock_comp, COMP_IndexVendorConfigTimeClientForceStart, &time_stamp);
                                pVideoRenderData->wait_time_out = 0;
                            }
                            pVideoRenderData->wait_time_out++;

                            if(get_message_count(&pVideoRenderData->cmd_queue) > 0)
                            {
                                LOGD("video wait to start, meet a message come!");
                                goto PROCESS_MESSAGE;
                            }

                            if(pVideoRenderData->priv_flag & CDX_comp_PRIV_FLAGS_STREAMEOF)
                            {
                                LOGD("videoRender notify EOF!");
                                pVideoRenderData->pCallbacks->EventHandler(pVideoRenderData->hSelf, pVideoRenderData->pAppData, COMP_EventBufferFlag, 0, 0, NULL);
                                pVideoRenderData->state = COMP_StateIdle;
                                break;
                            }
                        }
                    }
                    else
                    {
                        pVideoRenderData->start_to_play = TRUE;
                    }
                    pVideoRenderData->video_rend_flag &= ~VIDEO_RENDER_FIRST_FRAME_FLAG;
                }
                else
                {
                    LOGW("fatal error! frame Pts[%lld]us is invalid!", media_video_time);
                }
            }
        }
        else
        {
            TMessage_WaitQueueNotEmpty(&pVideoRenderData->cmd_queue, 0);
        }
    }
EXIT:
    LOGV("Video Render ComponentThread stopped");
    return (void*) SUCCESS;

}

