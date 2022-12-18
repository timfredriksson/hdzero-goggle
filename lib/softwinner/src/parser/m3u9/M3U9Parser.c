/*
 * Copyright (c) 2008-2016 Allwinner Technology Co. Ltd.
 * All rights reserved.
 *
 * File : M3U9Parser.c
 * Description : M3U9Parser
 * History :
 *
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "M3U9Parser"
#include "M3U9Parser.h"
#include <log/log.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static inline int startsWith(const char* str, const char* prefix)
{
    return !strncmp(str, prefix, strlen(prefix));
}

PlaylistItem *findItemBySeqNumForM3u9(Playlist *playlist, int seqNum)
{
    PlaylistItem *item = playlist->mItems;
    while(item->seqNum != seqNum)
    {
        item = item->next;
    }
    return item;
}
PlaylistItem *findItemByIndexForM3u9(Playlist *playlist, int index)
{
    PlaylistItem *item = playlist->mItems;
    int j;
    for(j=0; j<index; j++)
    {
        if(!item)
        {
            break;
        }
        item = item->next;
    }
    return item;
}


//***********************************************************//
/* baseURL,url���Ǳ�׼���ַ������������ԡ�\0�����������������strstr�Ȳ����޷���ֹ*/
/* �������baseURL��urlǰ����һЩǰ���Ŀո���strncasecmp�����������*/
/* ���������һ��ʼ�ü�ǰ��Ŀո�*/
//***********************************************************//
static status_t MakeURL(const char *baseURL, const char *url, char **out)
{
    *out = NULL;
    if(strncasecmp("http://", baseURL, 7)
             && strncasecmp("https://", baseURL, 8)
             && strncasecmp("file://", baseURL, 7))
    {
        /*Base URL must be absolute*/
        return err_URL;
    }
    
    cdx_uint32 urlLen = strlen(url);
    if (!strncasecmp("http://", url, 7) || !strncasecmp("https://", url, 8))
    {
        /*"url" is already an absolute URL, ignore base URL.*/
        *out = malloc(urlLen + 1);
        if(!*out)
        {
            LOGE("err_no_memory");
            return err_no_memory;
        }
        memcpy(*out, url, urlLen + 1);
        return OK;
    }

    cdx_uint32 memSize = 0;
    char *temp;
    char *protocolEnd = strstr(baseURL, "//") + 2;/*Ϊ������http://��https://֮��Ĳ���*/

    if (url[0] == '/')
    {
         /*URL is an absolute path.*/
        char *pPathStart = strchr(protocolEnd, '/');

        if (pPathStart != NULL)
        {
            memSize = pPathStart - baseURL + urlLen + 1;
            temp = (char *)malloc(memSize);
            if (temp == NULL)
            {
                LOGE("err_no_memory");
                return err_no_memory;
            }
            memcpy(temp, baseURL, pPathStart - baseURL);
            memcpy(temp + (pPathStart - baseURL), url, urlLen + 1);/*url����'\0'��β��*/
        }
        else
        {
            cdx_uint32 baseLen = strlen(baseURL);
            memSize = baseLen + urlLen + 1;
            temp = (char *)malloc(memSize);
            if (temp == NULL)
            {
                LOGE("err_no_memory");
                return err_no_memory;
            }
            memcpy(temp, baseURL, baseLen);
            memcpy(temp + baseLen, url, urlLen+1);
        }
    }
    else
    {
         /* URL is a relative path*/
        cdx_uint32 n = strlen(baseURL);
        char *slashPos = strstr(protocolEnd, ".m3u");
        if(slashPos != NULL)
        {
            while(slashPos >= protocolEnd)
            {
                slashPos--;
                if(*slashPos == '/')
                    break;
            }
            if (slashPos >= protocolEnd)/*�ҵ�*/
            {
                memSize = slashPos - baseURL + urlLen + 2;
                temp = (char *)malloc(memSize);
                if (temp == NULL)
                {
                    LOGE("err_no_memory");
                    return err_no_memory;
                }
                memcpy(temp, baseURL, slashPos - baseURL);
                *(temp+(slashPos - baseURL))='/';
                memcpy(temp+(slashPos - baseURL)+1, url, urlLen + 1);
            }
            else
            {
                memSize= n + urlLen + 2;
                temp = (char *)malloc(memSize);
                if (temp == NULL)
                {
                    LOGE("err_no_memory");
                    return err_no_memory;
                }
                memcpy(temp, baseURL, n);
                *(temp + n)='/';
                memcpy(temp + n + 1, url, urlLen + 1);
            }
        }
        else if (baseURL[n - 1] == '/')
        {
            memSize = n + urlLen + 1;
            temp = (char *)malloc(memSize);
            if (temp == NULL)
            {
                LOGE("err_no_memory");
                return err_no_memory;
            }
            memcpy(temp, baseURL, n);
            memcpy(temp + n, url, urlLen + 1);
        }
        else
        {
            slashPos = strrchr(protocolEnd, '/');

            if (slashPos != NULL)/*�ҵ�*/
            {
                memSize = slashPos - baseURL + urlLen + 2;
                temp = (char *)malloc(memSize);
                if (temp == NULL)
                {
                    LOGE("err_no_memory");
                    return err_no_memory;
                }
                memcpy(temp, baseURL, slashPos - baseURL);
                *(temp+(slashPos - baseURL))='/';
                memcpy(temp+(slashPos - baseURL)+1, url, urlLen + 1);
            }
            else
            {
                memSize= n + urlLen + 2;
                temp = (char *)malloc(memSize);
                if (temp == NULL)
                {
                    LOGE("err_no_memory");
                    return err_no_memory;
                }
                memcpy(temp, baseURL, n);
                *(temp + n)='/';
                memcpy(temp + n + 1, url, urlLen + 1);
            }
        }
    }
    *out = temp;
    return OK;
}

void destoryM3u9Playlist(Playlist *playList)
{
    if(playList)
    {
        if(playList->mBaseURI)
        {
            free(playList->mBaseURI);
        }

        PlaylistItem *e, *p;
        p = playList->mItems;
        while (p)
        {
            e = p->next;
            
            if (p->mURI)
            {
                free(p->mURI);
            }
            free(p);
            p = e;
        }
        
        free(playList);
    }
    return ;
}

static inline status_t ParseDouble(const char *s, double *x)
{
    char *end;
    double dVal = strtod(s, &end);

    if (!strcmp(end, s) || (*end != '\0' && *end != ','))
        return ERROR_MALFORMED;

    *x = dVal;
    return OK;
}

static status_t parseDuration(const char *line, int64_t *durationUs)
{
    char *colonPos = strstr(line, ":");
    
    if (colonPos == NULL)
        return ERROR_MALFORMED;

    double x;
    status_t err = ParseDouble(colonPos + 1, &x);

    if (err != OK)
        return err;

    *durationUs = (int64_t)(x * 1E6);
    return OK;
}

/*�ж��Ƿ�m3u9�ļ�*/
bool M3u9Probe(const char *data, cdx_uint32 size)
{
    cdx_uint32 offset = 0;
    while (offset < size && isspace(data[offset])) //isspace�а�����\n��\r,�����Ѿ��ų��˿��е����
    {
        offset++;
    }
    if(offset >= size)
    {
        return false;
    }
    cdx_uint32 nOffsetLF = offset;
    while (nOffsetLF < size && data[nOffsetLF] != '\n')
    {
        ++nOffsetLF;
    }
    if(nOffsetLF >= size)
    {
        return false;
    }
    cdx_uint32 offsetData = nOffsetLF;
    
    while(isspace(data[offsetData-1]))
    {
        --offsetData;
    }/*offsetData��ǰһ��λ��data[offsetData-1]����Ч�ַ���������'\r'��'\n'��
    data[offsetData]��'\r'��'\n'��offsetData - offset����Ч�ַ��ĸ���*/
    if(offsetData - offset != 9)
    {
        return false;
    }
    
    return !strncmp(data + offset, "#HISIPLAY", 9);
}


//***********************************************************//
/* ����_data��ָ��������ص���m3u9�ļ���������Ĵ�СΪsize*/
/* baseURI��ָ�ַ������ڴ������ⲿ���ٵ�,�����ԡ�\0������,�����в���ı�char *baseURI*/
/* ���parse��������Ӧ�ڴ��Ѿ����ͷ�*/
//***********************************************************//
status_t M3u9Parse(const void *_data, cdx_uint32 size, Playlist **P, const char *baseURI)
{
    const char *data = (const char *)_data;
    cdx_uint32 offset = 0;
    int seqNum = 0, lineNo = 0;
    status_t err = OK;
    int64_t durationUs = 0;
    char *line = NULL;
    PlaylistItem *item = NULL;
    
    Playlist *playList = malloc(sizeof(Playlist));
    if(!playList)
    {
        LOGE("err_no_memory");
        return err_no_memory;
    }
    memset(playList, 0x00, sizeof(Playlist));
    
    LOGV("baseURI=%s", baseURI);
    int baseLen = strlen(baseURI);
    playList->mBaseURI = malloc(baseLen+1);
    if(!playList->mBaseURI)
    {
        LOGE("err_no_memory");
        err = err_no_memory;
        goto _err;
    }
    memcpy(playList->mBaseURI, baseURI, baseLen+1);
    
    while(offset < size)
    {
        while(offset < size && isspace(data[offset]))
            //isspace�а�����\n��\r,�����Ѿ��ų��˿��е����
        {
            offset++;
        }
        if(offset >= size)
        {
            break;
        }
        cdx_uint32 nOffsetLF = offset;
        while (nOffsetLF < size && data[nOffsetLF] != '\n')
        {
            ++nOffsetLF;
        }
        /*ȥ�����code,�Լ������һ�в���'\n'���������
        if(offsetLF >= size)
        {
            break;
        }*/
        cdx_uint32 offsetData = nOffsetLF;
        
        while(isspace(data[offsetData-1]))
        {
            --offsetData;
        }/*offsetData��ǰһ��λ��data[offsetData-1]����Ч�ַ���������'\r'��'\n'��
        data[offsetData]��'\r'��'\n'��offsetData - offset����Ч�ַ��ĸ���*/
        if ((offsetData - offset)<=0)        /*˵���ǿ���*/
        {
            offset = nOffsetLF + 1;
            continue;
        }
        else
        {
            line = (char *)malloc(offsetData - offset + 1);
            if(!line)
            {
                LOGE("err_no_memory");
                err = err_no_memory;
                goto _err;
            }
            memcpy(line, &data[offset], offsetData - offset);
            /*��data[offset]����data[offsetData-1]����һ��*/
            line[offsetData - offset] = '\0';
            LOGI("#%s#", line);
        }

        if (lineNo == 0)
        {
            if (strcmp(line, "#HISIPLAY"))
            {
                LOGE("lineNo == 0, but line != #HISIPLAY");
                err = ERROR_MALFORMED;
                goto _err;
            }
        }
        else
        {
            if (startsWith(line,"#HISIPLAY_STREAM"))
            {
                err = parseDuration(line, &durationUs);
            }
            else if (startsWith(line,"#HISIPLAY_ENDLIST"))
            {
                //break;����lineû���ͷ�
            }

            if (err != OK)
            {
                LOGE("err = %d", err);
                goto _err;
            }
        }

        if (!startsWith(line,"#")) /*���ǿ��У����Ǳ�ǩ������ע�ͣ���URL*/
        {
            item= (PlaylistItem*)malloc(sizeof(PlaylistItem));
            if (!item)
            {
                LOGE("err_no_memory");
                err = err_no_memory;
                goto _err;
            }
            memset(item, 0, sizeof(PlaylistItem));
            if(durationUs > 0)
            {
                item->durationUs = durationUs;
                item->seqNum = seqNum;
                item->baseTimeUs = playList->durationUs;
                playList->durationUs += durationUs;/*����Ƭ��*/
                playList->lastSeqNum = seqNum;
            }
            else
            {
                LOGE("ERROR_MALFORMED");
                err = ERROR_MALFORMED;
                goto _err;
            }
            err = MakeURL(playList->mBaseURI, line, &item->mURI);
            if (err != OK)
            {
                LOGE("err = %d", err);
                goto _err;
            }
            //item->next = NULL;//memset�Ѿ�������
            if (playList->mItems == NULL)
            {
                playList->mItems = item;
            }
            else
            {
                PlaylistItem *item2 = playList->mItems;
                while(item2->next != NULL)
                    item2 = item2->next;
                item2->next = item;
            }
            (playList->mNumItems)++;

            seqNum++;
            durationUs = 0;
            item = NULL;
            
        }

        free(line);
        line = NULL;
        offset = nOffsetLF + 1;
        ++lineNo;
    }
    if(playList->mNumItems <= 0)
    {
        LOGE("playList->mNumItems <= 0");
        goto _err;
    }
    *P = playList;
    return OK;
    
_err:
    if(item != NULL)
    {
        free(item);
    }
    if(line != NULL)
    {
        free(line);
    }
    destoryM3u9Playlist(playList);
    return err;
    
}
