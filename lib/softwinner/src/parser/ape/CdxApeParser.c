/*
* Copyright (c) 2008-2016 Allwinner Technology Co. Ltd.
* All rights reserved.
*
* File : CdxApeParser.c
* Description :
* History :
*   Author  : Wenju Lin <linwenju@allwinnertech.com>
*   Date    : 2014/08/08
*   Comment : ������ʼ�汾��ʵ�� APE �Ľ⸴�ù���
*
*   Author  : Khan <chengkan@allwinnertech.com>
*   Date    : 2015/08/08
*   Comment : �޸� APE ��������, APE PARSER �ȶ���
*/
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "_apepsr"

#include <CdxTypes.h>
#include <CdxParser.h>
#include <CdxStream.h>
#include <CdxBinary.h>
#include "CdxApeParser.h"
#include <string.h>

#include <limits.h>
#include <fcntl.h>
#define BYTEALIGN
//#define SENDTWOFRAME

#define APE_WL32(p, d) do { \
                    ((cdx_uint8*)(p))[0] = (d); \
                    ((cdx_uint8*)(p))[1] = (d)>>8; \
                    ((cdx_uint8*)(p))[2] = (d)>>16; \
                    ((cdx_uint8*)(p))[3] = (d)>>24; } while(0)

static void ApeDumpInfo(APEParserImpl *pApeCtx)
{
    LOGV("blocksperframe       = %d", pApeCtx->blocksperframe);
    LOGV("duration             = %lld", pApeCtx->duration);
#if ENABLE_INFO_DEBUG
    cdx_uint32 i;

    LOGV("magic                = \"%c%c%c%c\"",
              pApeCtx->magic[0], pApeCtx->magic[1], pApeCtx->magic[2], pApeCtx->magic[3]);
    LOGV("fileversion          = %d", pApeCtx->fileversion);
    LOGV("descriptorlength     = %d", pApeCtx->descriptorlength);
    LOGV("headerlength         = %d", pApeCtx->headerlength);
    LOGV("seektablelength      = %d", pApeCtx->seektablelength);
    LOGV("wavheaderlength      = %d", pApeCtx->wavheaderlength);
    LOGV("audiodatalength      = %d", pApeCtx->audiodatalength);
    LOGV("audiodatalength_high = %d", pApeCtx->audiodatalength_high);
    LOGV("wavtaillength        = %d", pApeCtx->wavtaillength);
    LOGV("md5                  = ");
    for(i = 0; i < 16; i++)
        LOGV("%02x", pApeCtx->md5[i]);

    LOGV("\nHeader Block:");

    LOGV("compressiontype      = %d", pApeCtx->compressiontype);
    LOGV("formatflags          = %d", pApeCtx->formatflags);
    LOGV("finalframeblocks     = %d", pApeCtx->finalframeblocks);
    LOGV("totalframes          = %d", pApeCtx->totalframes);
    LOGV("bps                  = %d", pApeCtx->bps);
    LOGV("channels             = %d", pApeCtx->channels);
    LOGV("samplerate           = %d", pApeCtx->samplerate);

    LOGV("\nSeektable");
    if((pApeCtx->seektablelength / sizeof(uint32_t)) != pApeCtx->totalframes)
    {
        LOGV("No seektable");
    }
    else
    {
        for(i = 0; i < pApeCtx->seektablelength / sizeof(uint32_t); i++)
        {
            if (i < pApeCtx->totalframes - 1)
            {
                LOGV("%8d   %d (%d bytes)",
                          i,
                          pApeCtx->seektable[i],
                          pApeCtx->seektable[i + 1] - pApeCtx->seektable[i]);
            }
            else
            {
                LOGV("%8d   %d", i, pApeCtx->seektable[i]);
            }
        }
    }

    LOGV("\nFrames");
    for(i = 0; i < pApeCtx->totalframes; i++)
    {
        LOGV("%8d, pos = %8lld, size = %8d, samples = %d, skips %d",
                  i, pApeCtx->frames[i].pos, pApeCtx->frames[i].size,
            pApeCtx->frames[i].nblocks, pApeCtx->frames[i].skip);
        LOGV("%d, perframespts      = %8lld ", i, pApeCtx->frames[i].pts);
    }
    LOGV("\nCalculated information:");
    LOGV("junklength           = %d", pApeCtx->junklength);
    LOGV("firstframe           = %d", pApeCtx->firstframe);
    LOGV("totalsamples         = %d", pApeCtx->totalsamples);
#endif
}

static int ApeIndexSearch(CdxParserT *parser, cdx_int64 timeUs)
{
    APEParserImpl *pApe;
    int ret = 0;
    cdx_int32 frameindex;
    cdx_int32 samplesindex;
    cdx_int32 timeindex;
    cdx_int32 posdiff1;
    cdx_int32 posdiff2;
    cdx_int64 nseekpos = 0;

    pApe = (APEParserImpl *)parser;
    if(!pApe)
    {
        LOGE("pApe file parser lib has not been initiated!");
        ret = -1;
        goto Exit;
    }
    timeindex = timeUs / 1E6;
    LOGV("timeindex %d", timeindex);
    if(timeindex >= 0)
    {
        frameindex = (cdx_int32)timeindex * pApe->samplerate / pApe->blocksperframe;
        LOGV("frameindex %d", frameindex);
        if (frameindex > (cdx_int32)pApe->totalframes - 1)
        {
            frameindex = pApe->totalframes - 1;
        }
        else
        {
            samplesindex = (cdx_int32)timeindex * pApe->samplerate * pApe->nBlockAlign;
            posdiff1 = samplesindex - pApe->frames[frameindex].pos;
            posdiff2 = pApe->frames[frameindex + 1].pos - samplesindex;
            if(posdiff1 >= 0 && posdiff2 >= 0)
            {
                #if 0
                if(posdiff1 < posdiff2)
                    frameindex++;
                #endif
            }
        }
    }
    else
    {
        LOGW("pApe file seekUs negative");
        frameindex = 0;
    }
    LOGV("seek to frameindex : %d",frameindex);
    pApe->currentframe = frameindex;

    pApe->nseeksession = 1;
    LOGV("pApe file seekUs currentframe %d", pApe->currentframe);
Exit:
    return ret;

}

static int CdxApeInit(CdxParserT* Parameter)
{
    APEParserImpl *pApe;
    cdx_uint32 tag;
    cdx_uint32 i;
    cdx_int64 pts = 0;

    pApe = (APEParserImpl *)Parameter;

    pApe->extradata = malloc(APE_EXTRADATA_SIZE);
    if(pApe->extradata == NULL)
    {
        LOGE("No mem for ape extradata!");
        goto Exit;
    }
    memset(pApe->extradata, 0x00, APE_EXTRADATA_SIZE);
    pApe->extrasize = APE_EXTRADATA_SIZE;

    pApe->junklength = 0;
    tag = CdxStreamGetLE32(pApe->stream);
    if(tag != MKTAG('M', 'A', 'C', ' '))
    {
        pApe->mErrno = PSR_OPEN_FAIL;
        goto Exit;
    }

    pApe->fileversion = CdxStreamGetLE16(pApe->stream);
    if(pApe->fileversion >= 3980)
    {
        pApe->padding1             = CdxStreamGetLE16(pApe->stream);
        pApe->descriptorlength     = CdxStreamGetLE32(pApe->stream);
        pApe->headerlength         = CdxStreamGetLE32(pApe->stream);
        pApe->seektablelength      = CdxStreamGetLE32(pApe->stream);
        pApe->wavheaderlength      = CdxStreamGetLE32(pApe->stream);
        pApe->audiodatalength      = CdxStreamGetLE32(pApe->stream);
        pApe->audiodatalength_high = CdxStreamGetLE32(pApe->stream);
        pApe->wavtaillength        = CdxStreamGetLE32(pApe->stream);
        CdxStreamRead(pApe->stream, pApe->md5, 16);

        /* Skip any unknown bytes at the end of the descriptor.
           This is for future compatibility */
        if (pApe->descriptorlength > 52)
            CdxStreamSeek(pApe->stream, pApe->descriptorlength - 52, SEEK_CUR);

        /* Read header data */
        pApe->compressiontype      = CdxStreamGetLE16(pApe->stream);
        pApe->formatflags          = CdxStreamGetLE16(pApe->stream);
        pApe->blocksperframe       = CdxStreamGetLE32(pApe->stream);
        pApe->finalframeblocks     = CdxStreamGetLE32(pApe->stream);
        pApe->totalframes          = CdxStreamGetLE32(pApe->stream);
        pApe->bps                  = CdxStreamGetLE16(pApe->stream);
        pApe->channels             = CdxStreamGetLE16(pApe->stream);
        pApe->samplerate           = CdxStreamGetLE32(pApe->stream);
    }
    else
    {
        pApe->descriptorlength = 0;
        pApe->headerlength = 32;
        pApe->compressiontype      = CdxStreamGetLE16(pApe->stream);
        pApe->formatflags          = CdxStreamGetLE16(pApe->stream);
        pApe->channels             = CdxStreamGetLE16(pApe->stream);
        pApe->samplerate           = CdxStreamGetLE32(pApe->stream);
        pApe->wavheaderlength      = CdxStreamGetLE32(pApe->stream);
        pApe->wavtaillength        = CdxStreamGetLE32(pApe->stream);
        pApe->totalframes          = CdxStreamGetLE32(pApe->stream);
        pApe->finalframeblocks     = CdxStreamGetLE32(pApe->stream);

        if(pApe->formatflags & APE_MAC_FORMAT_FLAG_HAS_PEAK_LEVEL)
        {
            CdxStreamSeek(pApe->stream, 4, SEEK_CUR); /* Skip the peak level */
            pApe->headerlength += 4;
        }

        if(pApe->formatflags & APE_MAC_FORMAT_FLAG_HAS_SEEK_ELEMENTS)
        {
            pApe->seektablelength = CdxStreamGetLE32(pApe->stream);
            pApe->headerlength += 4;
            pApe->seektablelength *= sizeof(int32_t);
        }
        else
        {
            pApe->seektablelength = pApe->totalframes * sizeof(int32_t);
        }

        if(pApe->formatflags & APE_MAC_FORMAT_FLAG_8_BIT)
            pApe->bps = 8;
        else if(pApe->formatflags & APE_MAC_FORMAT_FLAG_24_BIT)
            pApe->bps = 24;
        else
            pApe->bps = 16;

        if(pApe->fileversion >= 3950)
            pApe->blocksperframe = 73728 * 4;
        else if(pApe->fileversion >= 3900 ||
                (pApe->fileversion >= 3800  && pApe->compressiontype >= 4000))
            pApe->blocksperframe = 73728;
        else
            pApe->blocksperframe = 9216;

        /* Skip any stored wav header */
        if (!(pApe->formatflags & APE_MAC_FORMAT_FLAG_CREATE_WAV_HEADER))
            CdxStreamSkip(pApe->stream, pApe->wavheaderlength);
    }

    pApe->nBlockAlign = pApe->bps / 8 * pApe->channels;

    if(pApe->totalframes > UINT_MAX / sizeof(APEFrame))
    {
        LOGW("Too many frames %d", pApe->totalframes);
        pApe->mErrno = PSR_OPEN_FAIL;
        goto Exit;
    }
    pApe->frames = malloc(pApe->totalframes * sizeof(APEFrame));

    if(!pApe->frames)
    {
        pApe->mErrno = PSR_OPEN_FAIL;
        goto Exit;
    }

    pApe->firstframe   = pApe->junklength + pApe->descriptorlength + pApe->headerlength +
                         pApe->seektablelength + pApe->wavheaderlength;
    pApe->currentframe = 0;
    pApe->nheadframe   = 1;

    pApe->totalsamples = pApe->finalframeblocks;
    if(pApe->totalframes > 1)
        pApe->totalsamples += pApe->blocksperframe * (pApe->totalframes - 1);

    if(pApe->seektablelength > 0)
    {
        pApe->seektable = malloc(pApe->seektablelength);
        for (i = 0; i < pApe->seektablelength / sizeof(cdx_uint32); i++)
            pApe->seektable[i] = CdxStreamGetLE32(pApe->stream);
    }

    pApe->frames[0].pos     = pApe->firstframe;
    pApe->frames[0].nblocks = pApe->blocksperframe;
    pApe->frames[0].skip    = 0;
    for(i = 1; i < pApe->totalframes; i++)
    {
        pApe->frames[i].pos      = pApe->seektable[i];
        pApe->frames[i].nblocks  = pApe->blocksperframe;
        pApe->frames[i - 1].size = pApe->frames[i].pos - pApe->frames[i - 1].pos;
        pApe->frames[i].skip     = (pApe->frames[i].pos - pApe->frames[0].pos) & 3;
    }

    pApe->frames[pApe->totalframes - 1].size    = pApe->finalframeblocks * 4;
    pApe->frames[pApe->totalframes - 1].nblocks = pApe->finalframeblocks;
#ifdef  BYTEALIGN
    for(i = 0; i < pApe->totalframes; i++)
    {
        if(pApe->frames[i].skip)
        {
            pApe->frames[i].pos  -= pApe->frames[i].skip;
            pApe->frames[i].size += pApe->frames[i].skip;
        }
        pApe->frames[i].size = (pApe->frames[i].size + 3) & ~3;
    }
#endif
    pApe->totalblock = (pApe->totalframes == 0) ? 0 :
                        (pApe->totalframes - 1) * pApe->blocksperframe + pApe->finalframeblocks;
    pApe->duration = (cdx_int64) pApe->totalblock * AV_TIME_BASE / 1000 / pApe->samplerate;
    for(i = 0; i < pApe->totalframes; i++)
    {
        pApe->frames[i].pts = pts;
        pts += (cdx_int64)pApe->blocksperframe * AV_TIME_BASE  / pApe->samplerate;
    }
    LOGV("pApe->fileversion : %d, pApe->compressiontype : %d, pApe->formatflags : %d",
            pApe->fileversion, pApe->compressiontype, pApe->formatflags);
    pApe->extradata[0] = pApe->fileversion & 0xff;
    pApe->extradata[1] = (pApe->fileversion>>8) & 0xff;
    pApe->extradata[2] = pApe->compressiontype & 0xff;
    pApe->extradata[3] = (pApe->compressiontype>>8) & 0xff;
    pApe->extradata[4] = pApe->formatflags & 0xff;
    pApe->extradata[5] = (pApe->formatflags>>8) & 0xff;
    pApe->extradata[6] = 0x00;
    pApe->extradata[7] = 0x00;
    //8,9wBitsPerSample
    pApe->extradata[8] = pApe->bps & 0xff;
    pApe->extradata[9] = (pApe->bps>>8) & 0xff;
    //10,11,nChannels
    pApe->extradata[10]= pApe->channels & 0xff;
    pApe->extradata[11]= (pApe->channels>>8) & 0xff;
    //12-15 nSamplesPerSec
    pApe->extradata[12]= pApe->samplerate & 0xff;
    pApe->extradata[13]= (pApe->samplerate>>8) & 0xff;
    pApe->extradata[14]= (pApe->samplerate>>16) & 0xff;
    pApe->extradata[15]= (pApe->samplerate>>24) & 0xff;

    pApe->mErrno = PSR_OK;
    pthread_cond_signal(&pApe->cond);
    return 0;
Exit:
    pthread_cond_signal(&pApe->cond);
    return -1;
}

static int CdxApeParserGetMediaInfo(CdxParserT *parser, CdxMediaInfoT *mediaInfo)
{
    APEParserImpl *pApe;
    int ret = 0;
    AudioStreamInfo *audio = NULL;

    pApe = (APEParserImpl *)parser;
    if(!pApe)
    {
        LOGE("pApe file parser lib has not been initiated!");
        ret = -1;
        goto Exit;
    }

    mediaInfo->fileSize = CdxStreamSize(pApe->stream);
    if(pApe->seektablelength > 0 && CdxStreamSeekAble(pApe->stream))
        mediaInfo->bSeekable = CDX_TRUE;
    else
        LOGW("pApe file Unable To Seek");

    ApeDumpInfo(pApe);

    audio                   = &mediaInfo->program[0].audio[mediaInfo->program[0].audioNum];
    audio->eCodecFormat     = AUDIO_CODEC_FORMAT_APE;
    audio->nChannelNum      = pApe->channels;
    audio->nSampleRate      = pApe->samplerate;
    audio->nAvgBitrate      = pApe->bitrate;
    audio->nMaxBitRate      = pApe->bitrate;
    audio->nBitsPerSample   = pApe->bps;
    audio->nCodecSpecificDataLen = pApe->extrasize;
    audio->pCodecSpecificData = (char *)pApe->extradata;

    mediaInfo->program[0].audioNum++;
    mediaInfo->program[0].duration  = pApe->duration;
    mediaInfo->bSeekable            = 1;
    /*for the request from ericwang, for */
    mediaInfo->programNum = 1;
    mediaInfo->programIndex = 0;
    /**/
Exit:
    return ret;
}

static int CdxApeParserControl(CdxParserT *parser, cdx_int32 cmd, void *param)
{
    APEParserImpl *pApe;
    (void *)param;
    pApe = (APEParserImpl *)parser;
    if(!pApe)
    {
        LOGE("Ape Parser Control failed for NULL ptr!");
        return -1;
    }

    switch(cmd)
    {
        case CDX_PSR_CMD_DISABLE_AUDIO:
        case CDX_PSR_CMD_DISABLE_VIDEO:
        case CDX_PSR_CMD_SWITCH_AUDIO:
            break;
        case CDX_PSR_CMD_SET_FORCESTOP:
            CdxStreamForceStop(pApe->stream);
            break;
        case CDX_PSR_CMD_CLR_FORCESTOP:
            CdxStreamClrForceStop(pApe->stream);
            break;
        default:
            LOGW("not implement...(%d)", cmd);
            break;
    }

    return 0;
}

static int CdxApeParserPrefetch(CdxParserT *parser, CdxPacketT *pkt)
{
    APEParserImpl *pApe;

    pApe = (APEParserImpl *)parser;
    if(!pApe)
    {
        LOGE("pApe file parser prefetch failed!");
        return -1;
    }

    if(pApe->currentframe >= pApe->totalframes)
    {
        LOGD("pApe file is eos");
        return -1;
    }

    pkt->type = CDX_MEDIA_AUDIO;
#ifdef SENDTWOFRAME
    if(pApe->currentframe == 0 || pApe->seek_flag)
    {
        if(pApe->currentframe < pApe->totalframes - 1)
        {
            pkt->length = pApe->frames[pApe->currentframe].size +
                        pApe->frames[pApe->currentframe+1].size;
        }
        else
            pkt->length = pApe->frames[pApe->currentframe].size;
    }
    else
#endif
    pkt->length = pApe->frames[pApe->currentframe].size;
    pkt->pts = pApe->frames[pApe->currentframe].pts;
    pkt->flags |= (FIRST_PART|LAST_PART);

    // First Frame
    pkt->length += 8;
    LOGV("pkt length %d, pkt pts %lld", pkt->length, pkt->pts);
    return 0;
}

static int CdxApeParserRead(CdxParserT *parser, CdxPacketT *pkt)
{
    APEParserImpl *pApe;
    int ret = 0;
    int nblocks = 0;
    cdx_int64 nreadpos = 0;
    int nreadsize = 0;
    int nretsize  = 0;
    cdx_uint8* ptr = (cdx_uint8*)pkt->buf;

    pApe = (APEParserImpl *)parser;
    if(!pApe)
    {
        LOGE("pApe file parser read failed!");
        ret = -1;
        goto Exit;
    }

    if(pApe->currentframe >= pApe->totalframes)
    {
        LOGW("pApe file is eos");
    }

    //nreadsize = pApe->frames[pApe->currentframe].size;
#ifdef SENDTWOFRAME
    if(pApe->currentframe == 0 || pApe->seek_flag)
        nreadsize = pApe->frames[pApe->currentframe].size +pApe->frames[pApe->currentframe+1].size;
    else
#endif
        nreadsize = pApe->frames[pApe->currentframe].size;

    if (pApe->currentframe == (pApe->totalframes - 1))
        nblocks = pApe->finalframeblocks;
    else
        nblocks = pApe->blocksperframe;

    if (pApe->frames[pApe->currentframe].size <= 0 ||
        pApe->frames[pApe->currentframe].size > INT_MAX - pApe->extrasize) {
        LOGW("invalid packet size: %d",
            pApe->frames[pApe->currentframe].size);
        pApe->currentframe++;
        goto Exit;
    }

    APE_WL32(ptr    , nblocks);
    ptr += 4;
    APE_WL32(ptr, pApe->frames[pApe->currentframe].skip);
    ptr += 4;

    // using for headframe
    if (0){//pApe->nheadframe == 1) {
        nreadpos = pApe->frames[0].pos;
        pApe->nheadframe = 0;
    } else {
        nreadpos = pApe->frames[pApe->currentframe].pos;
    }

    ret = CdxStreamSeek(pApe->stream, nreadpos, SEEK_SET);
    if (ret < 0) {
        LOGE("CdxApeParserRead Failed to seek");
        ret = -1;
        goto Exit;
    }

    nretsize = CdxStreamRead(pApe->stream, ptr, nreadsize);
    if(nretsize <= 0)
    {
        LOGW("CdxApeParserRead Overflow");
        ret = -1;
        goto Exit;
    }

#if ENABLE_FILE_DEBUG
    LOGV("nreadpos %lld, nreadsize %d", nreadpos, nretsize);
    if(pApe->teeFd >= 0)
    {
        write(pApe->teeFd, pkt->buf, nretsize);
    }
#endif

    pkt->pts = pApe->frames[pApe->currentframe].pts;
    //pApe->currentframe++;
#ifdef SENDTWOFRAME
    if(pApe->currentframe == 0|| pApe->seek_flag)
        pApe->currentframe+=2;
    else
#endif
        pApe->currentframe++;

    pApe->seek_flag = 0;
Exit:
    return ret;
}

static int CdxApeParserSeekTo(CdxParserT *parser, cdx_int64 timeUs, SeekModeType seekModeType)
{
    CDX_UNUSE(seekModeType);
    APEParserImpl *pApe;
    int ret = 0;

    pApe = (APEParserImpl *)parser;
    if(!pApe)
    {
        LOGE("pApe file parser seek failed!");
        ret = -1;
        goto Exit;
    }

    // Clear
    pApe->seek_flag = 1;
    ret = ApeIndexSearch(parser, timeUs);

Exit:
    return ret;
}

static cdx_uint32 CdxApeParserAttribute(CdxParserT *parser)
{
    APEParserImpl *pApe;
    int ret = 0;

    pApe = (APEParserImpl *)parser;
    if(!pApe)
    {
        LOGE("pApe file parser Attribute failed!");
        ret = -1;
        goto Exit;
    }
Exit:
    return ret;
}

static int CdxApeParserGetStatus(CdxParserT *parser)
{
    APEParserImpl *pApe;

    pApe = (APEParserImpl *)parser;

    if(CdxStreamEos(pApe->stream))
    {
        LOGD("File EOS! ");
        return pApe->mErrno = PSR_EOS;
    }
    return pApe->mErrno;
}

static int CdxApeParserClose(CdxParserT *parser)
{
    APEParserImpl *pApe;
    int ret = 0;
    pApe = (APEParserImpl *)parser;
    if(!pApe)
    {
        LOGE("pApe file parser close failed!");
        ret = -1;
        goto Exit;
    }
    pApe->exitFlag = 1;
    //pthread_join(pApe->openTid, NULL);
#if ENABLE_FILE_DEBUG
    if(pApe->teeFd)
    {
        close(pApe->teeFd);
    }
#endif
    if(pApe->extradata)
    {
        free(pApe->extradata);
        pApe->extradata = NULL;
    }
    if(pApe->frames)
    {
        free(pApe->frames);
        pApe->frames = NULL;
    }
    if(pApe->seektable)
    {
        free(pApe->seektable);
        pApe->seektable = NULL;
    }
    if(pApe->stream)
    {
        CdxStreamClose(pApe->stream);
    }
    pthread_cond_destroy(&pApe->cond);
    if(pApe != NULL)
    {
        free(pApe);
        pApe = NULL;
    }

Exit:
    return ret;
}

static struct CdxParserOpsS ApeParserImpl =
{
    .control      = CdxApeParserControl,
    .prefetch     = CdxApeParserPrefetch,
    .read         = CdxApeParserRead,
    .getMediaInfo = CdxApeParserGetMediaInfo,
    .seekTo       = CdxApeParserSeekTo,
    .attribute    = CdxApeParserAttribute,
    .getStatus    = CdxApeParserGetStatus,
    .close        = CdxApeParserClose,
    .init         = CdxApeInit
};

CdxParserT *CdxApeParserOpen(CdxStreamT *stream, cdx_uint32 flags)
{
    APEParserImpl *ApeParserImple;
    //int ret = 0;
    if(flags > 0)
    {
        LOGI("Flag Not Zero");
    }
    ApeParserImple = (APEParserImpl *)malloc(sizeof(APEParserImpl));
    if(ApeParserImple == NULL)
    {
        LOGE("ApeParserOpen Failed for memory lackless!");
        CdxStreamClose(stream);
        return NULL;
    }
    memset(ApeParserImple, 0, sizeof(APEParserImpl));
    ApeParserImple->stream = stream;
    ApeParserImple->base.ops = &ApeParserImpl;
    ApeParserImple->mErrno = PSR_INVALID;
    pthread_cond_init(&ApeParserImple->cond, NULL);
    //ret = pthread_create(&ApeParserImple->openTid, NULL, ApeOpenThread, (void*)ApeParserImple);
   // CDX_FORCE_CHECK(!ret);

#if ENABLE_FILE_DEBUG
    char teePath[64];
    strcpy(teePath, "/data/camera/pApe.dat");
    ApeParserImple->teeFd = open(teePath, O_WRONLY | O_CREAT | O_EXCL, 0775);
#endif

    return &ApeParserImple->base;
}

static int ApeProbe(CdxStreamProbeDataT *pProbe)
{
    if(pProbe->buf[0] == 'M' && pProbe->buf[1] == 'A' && pProbe->buf[2] == 'C' &&
       pProbe->buf[3] == ' ')
        return CDX_TRUE;
    return CDX_FALSE;
}

static cdx_uint32 CdxApeParserProbe(CdxStreamProbeDataT *probeData)
{
    if(probeData->len < 4 || !ApeProbe(probeData))
    {
        LOGE("pApe Probe Failed");
        return 0;
    }

    return 100;
}

CdxParserCreatorT apeParserCtor =
{
    .create = CdxApeParserOpen,
    .probe = CdxApeParserProbe
};
