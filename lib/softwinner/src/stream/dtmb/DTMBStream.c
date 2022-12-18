/*
 * Copyright (c) 2008-2016 Allwinner Technology Co. Ltd.
 * All rights reserved.
 *
 * File : DTMBStream.c
 * Description : Stream to read from TSC
 * History :
 *
 */

#include <CdxStream.h>
#include <CdxAtomic.h>
#include <CdxMemory.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "consumer.h"
#include <pthread.h>
#include <sys/un.h>
#include <sys/socket.h>


#define DTMB_SELECT_TIMEOUT 100000L /*100 ms*/

#define DEFAULT_PROBE_DATA_LEN (1024 * 128)

/*fmt: "dtmb://xxx" */
struct CdxDTMBStreamImplS
{
    //fixme,add structure needed here
    CdxStreamT base;
    CdxStreamProbeDataT probeData;
    int fd;

    //url created by dvb core
    char *streamPath;
    int ioErr;
    int forceStop;
};

static CdxStreamProbeDataT *__DTMBStreamGetProbeData(CdxStreamT *stream)
{
    LOGD("enter:%s %d",__FUNCTION__,__LINE__);

    struct CdxDTMBStreamImplS *impl;

    CDX_CHECK(stream);
    impl = CdxContainerOf(stream, struct CdxDTMBStreamImplS, base);

    return &impl->probeData;
}

cdx_int32 CdxPipeIsBlocking(cdx_int32 pipefd)
{
    cdx_int32 flags = fcntl(pipefd, F_GETFL, 0);
    return !(flags & O_NONBLOCK);
}

cdx_ssize CdxPipeAsynRecv(cdx_int32 pipefd, void *buf, cdx_size len,
                        cdx_long timeoutUs, cdx_int32 *pForceStop)
{
    fd_set rs;
    fd_set errs;
    struct timeval tv;
    cdx_ssize ret = 0, recvSize = 0;
    cdx_long loopTimes = 0, i = 0;
    cdx_int32 ioErr;

    if (CdxPipeIsBlocking(pipefd))
    {
        LOGE("<%s,%d>err, blocking pipe", __FUNCTION__, __LINE__);
        return -1;
    }
    
    if (timeoutUs == 0)
    {
        loopTimes = ((unsigned long)(-1L))>> 1;
    }
    else
    {
        loopTimes = timeoutUs/DTMB_SELECT_TIMEOUT;
    }

    for (i = 0; i < loopTimes; i++)
    {
        if (pForceStop && *pForceStop)
        {
            LOGE("<%s,%d>force stop", __FUNCTION__, __LINE__);
            return recvSize>0 ? recvSize : -2;
        }

        FD_ZERO(&rs);
        FD_SET(pipefd, &rs);
        FD_ZERO(&errs);
        FD_SET(pipefd, &errs);
        tv.tv_sec = 0;
        tv.tv_usec = DTMB_SELECT_TIMEOUT;
        ret = select(pipefd + 1, &rs, NULL, &errs, &tv);
        if (ret < 0)
        {
            ioErr = errno;
            if (EINTR == ioErr)
            {
                continue;
            }
            LOGE("<%s,%d>select err(%d)", __FUNCTION__, __LINE__, ioErr);
            return -1;
        }
        else if (ret == 0)
        {
            //("timeout\n");
            LOGD("xxx timeout, select again...");
            continue;
        }

        while (1)
        {
            if (pForceStop && *pForceStop)
            {
                LOGE("<%s,%d>force stop.recvSize(%ld)", __FUNCTION__, __LINE__, recvSize);
                return recvSize>0 ? recvSize : -2;
            }
            if(FD_ISSET(pipefd,&errs))
            {
                LOGE("<%s,%d>errs ", __FUNCTION__, __LINE__);
                break;
            }
            if(!FD_ISSET(pipefd, &rs))
            {
                LOGV("select > 0, but sockfd is not ready?");
                break;
            }

            ret = read(pipefd, ((char *)buf) + recvSize, len - recvSize);
            if (ret < 0)
            {
                ioErr = errno;
                if (EAGAIN == ioErr)
                {
                    break;
                }
                else
                {
                    LOGE("<%s,%d>pipe read err(%d)", __FUNCTION__, __LINE__, errno);
                    return -1;
                }
            }
            else if (ret == 0)
            {
                return recvSize;
                //goto select;
            }
            else // ret > 0
            {
                recvSize += ret;
                
                if ((cdx_size)recvSize == len)
                {
                    return recvSize;
                }
                
            }
        }

    }

    return recvSize;
}


static cdx_int32 __DTMBStreamRead(CdxStreamT *stream, void *buf, cdx_uint32 len)
{
    struct CdxDTMBStreamImplS *impl;
    cdx_int32 ret = 0;
    cdx_int64 nHadReadLen;
    int i;
    
    CDX_CHECK(stream);
    impl = CdxContainerOf(stream, struct CdxDTMBStreamImplS, base);
    
    //read data
    //set io state
    #if 0
    ret = fifoRead(impl->fd,buf,len);

    if(ret == 0)
    {
        LOGD("no bit stream,force read sync!");
        ret = fifoReadSync(impl->fd,buf,len);
    }
    #else
    ret = CdxPipeAsynRecv(impl->fd,buf,len,0,&impl->forceStop);
    #endif
    //LOGD("recv size:%d",ret);
    
    return ret;
}

static cdx_int32 __DTMBStreamClose(CdxStreamT *stream)
{
    LOGD("enter:%s %d",__FUNCTION__,__LINE__);

    struct CdxDTMBStreamImplS *impl;
    cdx_int32 ret;
    
    CDX_CHECK(stream);
    impl = CdxContainerOf(stream, struct CdxDTMBStreamImplS, base);

    //close stream
    //free filepath,probe buf
    //fixme,add it here
    ret = fifoClose(impl->fd);
    if(ret != 0)
    {
        LOGW(" close fd may be not normal, ret = %d, errno = %d",ret,errno);
    }
    
    if (impl->probeData.buf)
    {
        CdxFree(impl->probeData.buf);
    }

    if(impl->streamPath)
    {
        CdxFree(impl->streamPath);
    }

    if(impl)
    {
        CdxFree(impl);
    }

    return CDX_SUCCESS;
}

static cdx_int32 __DTMBStreamGetIoState(CdxStreamT *stream)
{
    LOGD("enter:%s %d",__FUNCTION__,__LINE__);

    struct CdxDTMBStreamImplS *impl;

    CDX_CHECK(stream);
    impl = CdxContainerOf(stream, struct CdxDTMBStreamImplS, base);
    
    return impl->ioErr;
}

static cdx_uint32 __DTMBStreamAttribute(CdxStreamT *stream)
{
    LOGD("enter:%s %d",__FUNCTION__,__LINE__);

    struct CdxDTMBStreamImplS *impl;
    cdx_uint32 flag = 0;
    
    impl = CdxContainerOf(stream, struct CdxDTMBStreamImplS, base);
    
    //LOGD("Attribute:%d",flag|CDX_STREAM_FLAG_NET);
    
    //fixme,whether NET stream?
    return flag|CDX_STREAM_FLAG_DTMB;
}

static cdx_int32 __DTMBStreamControl(CdxStreamT *stream, cdx_int32 cmd, void *param)
{
    LOGV("enter:%s %d cmd:%d",__FUNCTION__,__LINE__,cmd);

    struct CdxDTMBStreamImplS *impl;

    CDX_UNUSE(param);

    CDX_CHECK(stream);
    impl = CdxContainerOf(stream, struct CdxDTMBStreamImplS, base);

    switch (cmd)
    {
        case STREAM_CMD_SET_FORCESTOP:
               LOGD("DTMB STREAM_CMD_SET_FORCESTOP");
            impl->forceStop = 1;
            break;

        case STREAM_CMD_CLR_FORCESTOP:
               LOGD("DTMB STREAM_CMD_CLR_FORCESTOP 0");
            impl->forceStop = 0;
            break;
        default :
            break;
    }
    
    return CDX_SUCCESS;
}

static cdx_int64 __DTMBStreamTell(CdxStreamT *stream)
{
    LOGD("enter:%s %d",__FUNCTION__,__LINE__);

    struct CdxDTMBStreamImplS *impl;
    cdx_int64 pos;
    
    CDX_CHECK(stream);
    impl = CdxContainerOf(stream, struct CdxDTMBStreamImplS, base);

    //fixme,change if needed
    return -1;
}

static cdx_bool __DTMBStreamEos(CdxStreamT *stream)
{
    LOGD("enter:%s %d",__FUNCTION__,__LINE__);

    struct CdxDTMBStreamImplS *impl;
    cdx_int64 pos = -1;
    
    CDX_CHECK(stream);
    impl = CdxContainerOf(stream, struct CdxDTMBStreamImplS, base);

    //fixme,change if needed
    return CDX_FALSE;
}

static cdx_int64 __DTMBStreamSize(CdxStreamT *stream)
{
    LOGD("enter:%s %d",__FUNCTION__,__LINE__);

    struct CdxDTMBStreamImplS *impl;
    
    CDX_CHECK(stream);
    impl = CdxContainerOf(stream, struct CdxDTMBStreamImplS, base);

    //fixme,change if needed
    return -1;
}

static cdx_int32 __DTMBStreamGetMetaData(CdxStreamT *stream, const cdx_char *key, void **pVal)
{
    LOGD("enter:%s %d",__FUNCTION__,__LINE__);

    struct CdxDTMBStreamImplS *impl;
    
    CDX_CHECK(stream);
    impl = CdxContainerOf(stream, struct CdxDTMBStreamImplS, base);

    if (strcmp(key, "uri") == 0)
    {
        LOGW("key(%s)found:%s ...", key,impl->streamPath);
        *pVal = impl->streamPath;
        return 0;
    }
    
    LOGW("key(%s) not found...", key);
    return -1;
}

#define READ_ONCE_SIZE 188*100

cdx_int32 __DTMBStreamConnect(CdxStreamT *stream)
{
    LOGD("enter:%s %d",__FUNCTION__,__LINE__);

    cdx_int32 ret = 0;
    int i = 0;
    struct CdxDTMBStreamImplS *impl;

    int hdlStream;
    int hdlPipe;
    char *pipePath = NULL;
    char *socketPathServer = NULL;
    char *socketPathClient = NULL;
    int fd;
    struct sockaddr_un un;
    int addr_len = sizeof(struct sockaddr_un);
    
    
    CDX_CHECK(stream);
    impl = CdxContainerOf(stream, struct CdxDTMBStreamImplS, base);
    sscanf(impl->streamPath,"dtmb://%d?%d?test.ts",&hdlStream,&hdlPipe);
#ifdef USE_SOCKET
    socketPathServer = (char *)CdxMalloc(strlen(DTMB_SERVER_SOCKET) + 3);
    if(socketPathServer != NULL)
    {
        sprintf(socketPathServer,"%s%d",DTMB_SERVER_SOCKET,hdlPipe);
        LOGD("server path:%s",socketPathServer);
    }
    else
    {
        LOGE("memory full!!");
        goto failure;
    }

    socketPathClient = (char *)CdxMalloc(strlen(DTMB_CLIENT_SOCKET) + 3);
    if(socketPathClient != NULL)
    {
        sprintf(socketPathClient,"%s%d",DTMB_CLIENT_SOCKET,hdlPipe);
        LOGD("client path:%s",socketPathClient);
    }
    else
    {
        LOGE("memory full!!");
        goto failure;
    }

    if((fd = socket(AF_UNIX,SOCK_STREAM,0))<0)
    {
        LOGE("socket cdx errno:%d",errno);
        goto failure;
    }

    memset(&un,0,sizeof(un));
    un.sun_family = AF_UNIX;
    strncpy(un.sun_path,socketPathClient,strlen(socketPathClient)+1);
    unlink(socketPathClient);

    if(bind(fd,(struct sockaddr*)&un,addr_len)<0)
    {
        LOGE("bind cdx errno:%d",errno);
        goto failure;
    }

    //fill socket adress structure with server's address
    memset(&un,0,sizeof(un));
    un.sun_family = AF_UNIX;
    strncpy(un.sun_path,socketPathServer,strlen(socketPathServer)+1);
    
    if(connect(fd,(struct sockaddr*)&un,addr_len) < 0)
    {
        LOGE("connect cdx errno:%d",errno);
        goto failure;
    }
#else
    pipePath = (char *)CdxMalloc(strlen(DTMB) + 3);
    if(pipePath != NULL)
    {
        sprintf(pipePath,"%s%d",DTMB,hdlPipe);
        LOGD("pipe path:%s",pipePath);
    }
    else
    {
        LOGE("memory full!!");
        goto failure;
    }
#endif

#ifdef USE_SOCKET
    impl->fd = fd;
    LOGD("socket fd:%d",impl->fd);
    if (impl->fd <= 0)
    {
        LOGE("socket connect failure!");
        goto failure;
    }
    
    CdxFree(socketPathServer);
    CdxFree(socketPathClient);
#else
    impl->fd = open(pipePath,O_RDONLY|O_NONBLOCK);//
    LOGD("dup fd:%d",impl->fd);
    if (impl->fd <= 0)
    {
        LOGE("open dtmb file failure, errno(%d)", errno);
        goto failure;
    }
    CdxFree(pipePath);
#endif

    impl->probeData.buf = CdxMalloc(188);
    impl->probeData.len = 188;
    impl->ioErr = CDX_SUCCESS;
    memset(impl->probeData.buf,0x47,188);
    CDX_BUF_DUMP(impl->probeData.buf, 16);

    impl->ioErr = 0;
    return ret;
    
failure:
#ifdef USE_SOCKET
    CdxFree(socketPathServer);
    CdxFree(socketPathClient);
#else
    CdxFree(pipePath);
#endif
    return -1;
    
}

static struct CdxStreamOpsS DTMBStreamOps =
{
    .connect = __DTMBStreamConnect,
    .getProbeData = __DTMBStreamGetProbeData,
    .read = __DTMBStreamRead,
    .write = NULL,
    .close = __DTMBStreamClose,
    .getIOState = __DTMBStreamGetIoState,
    .attribute = __DTMBStreamAttribute,
    .control = __DTMBStreamControl,
    .getMetaData = __DTMBStreamGetMetaData,
    .seek = NULL,
    .seekToTime = NULL,
    .eos = __DTMBStreamEos,
    .tell = __DTMBStreamTell,
    .size = __DTMBStreamSize,
};

static CdxStreamT *__DTMBStreamCreate(CdxDataSourceT *source)
{
    LOGD("enter:%s %d",__FUNCTION__,__LINE__);

    struct CdxDTMBStreamImplS *impl;
    
    impl = CdxMalloc(sizeof(*impl));
    CDX_FORCE_CHECK(impl);
    memset(impl, 0x00, sizeof(*impl));
    
    impl->base.ops = &DTMBStreamOps;
    impl->streamPath = CdxStrdup(source->uri);

    LOGD("dtmb file '%s'", source->uri);

    return &impl->base;
}

CdxStreamCreatorT DTMBStreamCtor =
{
    .create = __DTMBStreamCreate
};

