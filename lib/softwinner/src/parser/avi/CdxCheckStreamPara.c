/*
 * Copyright (c) 2008-2016 Allwinner Technology Co. Ltd.
 * All rights reserved.
 *
 * File : CdxCheckStreamPara.c
 * Description : CheckStreamPara
 * History :
 *
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "TEMP_TAG"
#include "CdxAviInclude.h"
#if 0
/*******************************************************************************
Function name: detect_audio_stream_info
Description:
    CDX_BOOL eLIBs_GetAudioDataInfo(const char *pFormat, __audio_file_info_t *AIF,
        CDX_S8* buf, CDX_S32 datalen);
    D:\al_audio\rel\audiodec\codec\audioinfo\AC320\GetAudio_format.h;
Parameters:

Return:
    AVI_SUCCESS;
    AVI_ERR_FAIL;
Time: 2010/1/22
*******************************************************************************/
CDX_S32 detect_audio_stream_info(CDX_S32 FormatTag, CDX_U8 *pbuf, CDX_U32 length,
                            __audio_stream_info_t *paudstrminfo, struct FILE_PARSER *p)
{
    CDX_S32   ret;
    CDX_S32   audio_codec_type;
    FILE_DEPACK *pAviDepack = (FILE_DEPACK*)p;
    memset(paudstrminfo, 0, sizeof(__audio_stream_info_t));
    
    switch(FormatTag)
    {
        case MP3_TAG1:
        case MP3_TAG2:
        {
            audio_codec_type = CDX_AUDIO_MP3;
            break;
        }
        case AAC_TAG:
        {
            audio_codec_type = CDX_AUDIO_MPEG_AAC_LC;
            break;
        }
        default:
        {
            ret = AVI_ERR_FAIL;
            goto _exit0;
        }
    }
    if(AVI_SUCCESS == pAviDepack->CbGetAudioDataInfo(audio_codec_type, pbuf, length, paudstrminfo))
    {
        ret = AVI_SUCCESS;
    }
    else
    {
        ret = AVI_ERR_FAIL;
    }
_exit0:
    return ret;
}

/*******************************************************************************
Function name: __modify_audio_stream_info_mp3
Description:
    1.�޸�mp3�����ʽ����Ƶ���������øú���ʱ��һ���д�
Parameters:

Return:
    AVI_SUCCESS: �޸ĳɹ�
    AVI_ERR_IGNORE : �޸�ʧ��

    < 0 :
    fatal error.
    AVI_ERR_READ_FILE_FAIL
    AVI_ERR_REQMEM_FAIL
Time: 2010/8/21
*******************************************************************************/
#define MP3_DATA_LENGTH (4096)
CDX_S32 __modify_audio_stream_info_mp3(AUDIO_STREAM_INFO *pAudStrmInfo,
AVI_FILE_IN *avi_in, struct FILE_PARSER *p)
{
//    CDX_S32   i;
    CDX_S32   stream_id;
    CDX_S32   ret = AVI_SUCCESS;
    CDX_U32   length;
    CDX_S64   cur_pos = 0;
    CDX_S32   chunk_num = 0;
    CDX_U8*   pAudBuf = NULL;
    CDX_S32   nAudBufLen = 0;
    __audio_stream_info_t   PrivAudStrmInfo;

    switch(pAudStrmInfo->avi_audio_tag)
    {
        case MP3_TAG1:
        case MP3_TAG2:
        {
            break;
        }
        default:
        {
            LOGV("TAG[%x] is not mp3\n", pAudStrmInfo->avi_audio_tag);
            return AVI_ERR_IGNORE;
        }
    }

    //�ȶ�2��chunk����Ƶ����,mp3��Ҫ2֡���ܽ���
    cur_pos = cdx_tell(avi_in->fp);   //���浱ǰ��fp��ֵ���������Ļָ���
    if(cdx_seek(avi_in->fp, avi_in->movi_start, CEDARLIB_SEEK_SET))
    {
        LOGV("Seek file failed!\n");
        ret = AVI_ERR_READ_FILE_FAIL;
        goto __err0;
    }
    pAudBuf = (CDX_U8*)malloc(MP3_DATA_LENGTH);  //�����ڴ�
    if(NULL == pAudBuf)
    {
        LOGV("malloc fail\n");
        ret = AVI_ERR_REQMEM_FAIL;
        goto __err0;
    }
    while(1)    //��ʼ��audio chunk,4096�ֽڻ�2��chunk�͹���
    {
        LOGV("read audio chunk for []times\n");
        ret = get_next_chunk_head(avi_in->fp, &avi_in->data_chunk, &length);
        if(ret == AVI_SUCCESS)
        {
            stream_id = CDX_STREAM_FROM_FOURCC(avi_in->data_chunk.fcc);
            if(stream_id == pAudStrmInfo->stream_idx)
            {
                if(nAudBufLen + avi_in->data_chunk.length> MP3_DATA_LENGTH)
                {   //����4096�ֽ�
                    LOGV("memory[4096] not enough,[%d],[%d]\n", nAudBufLen,
                        avi_in->data_chunk.length);
                    if(nAudBufLen >= MP3_DATA_LENGTH)
                    {
                        LOGV("fatal error!impossible!\n");
                        ret = AVI_ERR_READ_FILE_FAIL;
                        goto __err0;
                    }
                    if(cdx_read(pAudBuf+nAudBufLen, MP3_DATA_LENGTH - nAudBufLen,
                        1, avi_in->fp) != 1)
                    {
                        LOGV("read file fail\n");
                        ret = AVI_ERR_READ_FILE_FAIL;
                        goto __err0;
                    }
                    nAudBufLen = MP3_DATA_LENGTH;
                    break;
                }
                else
                {
                    if(cdx_read(pAudBuf+nAudBufLen, avi_in->data_chunk.length,
                        1, avi_in->fp) != 1)
                    {
                        ret = AVI_ERR_READ_FILE_FAIL;
                        goto __err0;
                    }
                    nAudBufLen += avi_in->data_chunk.length;
                    chunk_num++;
                    if(chunk_num >= 2)
                    {
                        break;
                    }
                }
            }
            else
            {
                cdx_seek(avi_in->fp, avi_in->data_chunk.length,
                    CEDARLIB_SEEK_CUR);//��ʱ���������Ӧ��ָ����һ��chunk�Ŀ�ʼ��
            }
        }
        else
        {
            goto __err0;
        }
    }
    //����2��audio_chunk //���룬�õ���ȷ�Ĳ���
    if(AVI_SUCCESS == detect_audio_stream_info(pAudStrmInfo->avi_audio_tag,
        pAudBuf, nAudBufLen, &PrivAudStrmInfo, p))
//    if(AVI_SUCCESS == pAviDepack->CbDetectAudioStreamInfo(pAudStrmInfo->avi_audio_tag,
//            pAudBuf, nAudBufLen, &PrivAudStrmInfo))
    {
        //�޸�strf����pabs_fmt
        //modify_strfh(pabs_hdr, pabs_fmt, &audstrminfo);
        pAudStrmInfo->AvgBytesPerSec = PrivAudStrmInfo.avg_bit_rate/8;
        ret = AVI_SUCCESS;
    }
    else
    {
        ret = AVI_ERR_IGNORE;
    }
__err0:
    if(pAudBuf)
    {
        free(pAudBuf);
    }
    cdx_seek(avi_in->fp, cur_pos, CEDARLIB_SEEK_SET);    //�ָ�fp
    return ret;
}

/*******************************************************************************
Function name: adjust_audio_stream_info
Description:
    1.cbr:���ʵ���MIN_BYTERATE����Ϊ������
    2.vbr:һ����Ϊ���������ټ��
    3. ��ĳЩ�����ʽ�����ټ�������ʵĲ�������Ŀǰ��MP3��aac����֧�֡�
Parameters:

Return:
    AVI_SUCCESS;
    AVI_ERR_CORRECT_STREAM_INFO; �������������Ѹ���
    AVI_ERR_IGNORE:  �����������󣬵�������

    < 0 :
    fatal error,������ļ���seek�ļ�ʧ�ܵ�
Time: 2010/8/21
*******************************************************************************/
CDX_S32 adjust_audio_stream_info(AUDIO_STREAM_INFO *pAudStrmInfo,
AVI_FILE_IN *avi_in, struct FILE_PARSER *p)
{
    CDX_S32   ret;
    //1. ���
    if(pAudStrmInfo->cbr_flg)
    {
        if(pAudStrmInfo->AvgBytesPerSec >= MIN_BYTERATE)
        {
            ret = AVI_SUCCESS;
        }
        else
        {
            ret = AVI_ERR_IGNORE;
        }
    }
    else
    {
        ret = AVI_SUCCESS;
    }
    //2. ���Ծ���
    if(AVI_ERR_IGNORE == ret) //���Ƿ��ܹ�����
    {
        switch(pAudStrmInfo->avi_audio_tag) //Ŀǰֻ��MP3����
        {
            case MP3_TAG1:
            case MP3_TAG2:
            {
                return __modify_audio_stream_info_mp3(pAudStrmInfo, avi_in, p);
            }
            default:
            {
                LOGV("other audio tag[%x], not correct\n", pAudStrmInfo->avi_audio_tag);
                return ret;
            }
        }
    }
    else
    {
        return ret;
    }
    
}

/*******************************************************************************
Function name: CheckAudioStreamInfo2
Description:
    1.�����û���index����ȡ������˳���ȡ2��ͬid��audiochunk�İ취��
    2.�ȼ������Ƿ������⣬�پ����Ƿ������㡣
    3.���øú���ʱ���Ѿ�avi->open()���ˣ�movi_start���Ѿ�֪���ˡ�
    4.Ŀǰ��MP3��ʽ�����ĵ��ý��뺯��ȥ�����㣬����ֻ���������,�Ժ���չ������ʽ
    5.ע��fp�ı��ݺͻ�ԭ
    6.�޸ĵĳ�Ա������:AUDIO_STREAM_INFO   AudInfoArray[MAX_AUDIO_STREAM]
Parameters:

Return:

Time: 2010/8/19
*******************************************************************************/
CDX_S32 CheckAudioStreamInfo2(struct FILE_PARSER *pAviPsr)
{
    //ע���ļ�ָ��Ҫ�ָ���
    CDX_S32   i;
    AVI_FILE_IN *avi_in = (AVI_FILE_IN*)pAviPsr->priv_data;
    //AVI_FILE_IN *avi_in = (AVI_FILE_IN*)pAviPsr;
    //�����ж���Ƶ���Ĳ����Ƿ�������������
    for(i=0; i<pAviPsr->hasAudio; i++)
    {
        adjust_audio_stream_info(&avi_in->AudInfoArray[i], avi_in, pAviPsr);
    }
    return AVI_SUCCESS;
}
#endif
