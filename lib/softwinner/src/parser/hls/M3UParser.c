/*
* Copyright (c) 2008-2016 Allwinner Technology Co. Ltd.
* All rights reserved.
*
* File : CdxHlsParser.c
* Description :
* History :
*   Author  : Kewei Han
*   Date    : 2014/10/08
*   Comment : Http live streaming implementation.
*/

#include <stdint.h>
#include "M3UParser.h"
#include <log/log.h>
#include <ctype.h>
#include <stdlib.h>

//***********************************************************//
/* ���ַ���ת��ΪСд*/
//***********************************************************//
static void tolower_str(char *str)
{
    CDX_CHECK(str);
    while(*str != '\0')
    {
        *str = tolower(*str);
        str++;
    }
}

static void trim(AString *line)
{
    if(line->mSize <= 0)
    {
        LOGW("line->mSize <= 0");
        return ;
    }
    cdx_uint32 i = 0;
    while (i < line->mSize && isspace(line->mData[i]))
    {
        ++i;
    }
    cdx_uint32 j = line->mSize;
    while (j > i && isspace(line->mData[j - 1]))
    {
        --j;
    }

    memmove(line->mData, &line->mData[i], j - i);
    line->mSize = j - i;
    line->mData[line->mSize] = '\0';
    return ;
}
/*
void trim_char1(char *str, cdx_uint32 length)
{
    cdx_uint32 len = strlen(str);
    if(!len)
    {
        LOGW("strlen(str) == 0");
        return ;
    }
    cdx_uint32 i = 0;
    while (i < len && isspace(str[i]))
    {
        ++i;
    }
    cdx_uint32 j = len;
    while (j > i && isspace(str[j - 1]))
    {
        --j;
    }

    memmove(str, &str[i], j - i);
    str[j - i] = '\0';
    return ;
}
*/
static char * trim_char(char *str)
{
    CDX_CHECK(str);
    cdx_uint32 len = strlen(str);
    //CDX_CHECK(len); //len is possible to be 0, dont check.

    cdx_uint32 i = 0;
    while (i < len && isspace(str[i]))
    {
        ++i;
    }
    cdx_uint32 j = len;
    while (j > i && isspace(str[j - 1]))
    {
        --j;
    }
    str[j] = '\0';
    return str + i;
}

//***********************************************************//
/* Find the next occurence of the character "what" at or after "offset",*/
/* but ignore occurences between quotation marks.*/
/* Return the index of the occurrence or -1 if not found. */
/* �����Ҳ��ڡ��������е�what�ַ�������ʱһ��Ҫ��֤���(��offset)����಻���С�*/
/* what������'"'����һ������ԭ���ĺ���. */
//***********************************************************//
static ssize_t M3uFindNextUnquoted(const AString *line, char what, cdx_uint32 offset)
{
    bool quoted = false;/*��ʾ��û����"*/
    while (offset < line->mSize)
    {
        char c = line->mData[offset];

        if (c == what && !quoted)
            return offset;
        else if (c == '"')
            quoted = !quoted;
        ++offset;
    }
    return -1;
}

//***********************************************************//
/* ���ַ���str����AString�Ķ��󣬴�offset��ʼ���ַ�����Ϊlength*/
//***********************************************************//
static AString *creatAString(const char *str, cdx_uint32 offset, cdx_uint32 length)
{
/*
    if(str == NULL || length == 0 || offset + length > strlen(str))
    {
        LOGE("ERROR_MALFORMED");
        return NULL;
    }
*/
    AString *string = (AString *)malloc(sizeof(AString));
    CDX_FORCE_CHECK(string);
    string->mData = (char *)malloc((length + 1) * sizeof(char));
    CDX_FORCE_CHECK(string->mData);
    string->mSize = length;
    string->mAllocSize = length + 1;
    memcpy(string->mData, &str[offset], length);
    string->mData[length] = '\0';
    return string;
}

static inline void destroyAString(AString *string)
{
    if(string)
    {
        free(string->mData);
        free(string);
    }
    return ;
}

static inline status_t setInt32(AMessage *meta, const char *key, cdx_int32 int32Value)
{
    if (meta->mNumAtom < maxNumAtom)
    {
        meta->mAtom[meta->mNumAtom].u.int32Value = int32Value;
        meta->mAtom[meta->mNumAtom].mType = kTypeInt32;
        meta->mAtom[meta->mNumAtom].mName = key;
        (meta->mNumAtom)++;
    }
    else
        return ERROR_maxNumAtom_too_little;

    return OK;
}

static inline status_t setInt64(AMessage *meta, const char *key, cdx_int64 int64Value)
{
    if (meta->mNumAtom < maxNumAtom)
    {
        meta->mAtom[meta->mNumAtom].u.int64Value = int64Value;
        meta->mAtom[meta->mNumAtom].mType = kTypeInt64;
        meta->mAtom[meta->mNumAtom].mName = key;
        (meta->mNumAtom)++;
    }
    else
        return ERROR_maxNumAtom_too_little;

    return OK;
}

static inline status_t setString(AMessage *meta, const char *key, AString *stringValue)
{
    if (meta->mNumAtom < maxNumAtom)
    {
        meta->mAtom[meta->mNumAtom].u.stringValue = stringValue;
        meta->mAtom[meta->mNumAtom].mType = kTypeString;
        meta->mAtom[meta->mNumAtom].mName = key;
        (meta->mNumAtom)++;
    }
    else
        return ERROR_maxNumAtom_too_little;

    return OK;
}

int findInt32(const AMessage *meta, const char *name, cdx_int32 *value)
{
    unsigned int i = 0;
    while ((i < meta->mNumAtom) && strcmp(meta->mAtom[i].mName, name))
        i++;
    if((i<meta->mNumAtom) && (meta->mAtom[i].mType == kTypeInt32))
    {
       *value = meta->mAtom[i].u.int32Value;
       return 1;
    }
    return 0;
}

int findInt64(const AMessage *meta, const char *name, cdx_int64 *value)
{
    unsigned int i = 0;
    while ((i < meta->mNumAtom) && strcmp(meta->mAtom[i].mName, name))
        i++;
    if((i<meta->mNumAtom) && (meta->mAtom[i].mType == kTypeInt64))
    {
       *value = meta->mAtom[i].u.int64Value;
       return 1;
    }
    return 0;
}

int findString(const AMessage *meta, const char *name, AString **value)
{
    unsigned int i = 0;
    while ((i < meta->mNumAtom) && strcmp(meta->mAtom[i].mName, name))
        i++;
    if((i<meta->mNumAtom) && (meta->mAtom[i].mType == kTypeString))
    {
       *value = meta->mAtom[i].u.stringValue;
       return 1;
    }
    *value = NULL;
    return 0;
}

PlaylistItem *findItemBySeqNum(Playlist *playlist, int seqNum)
{
    PlaylistItem *item = playlist->mItems;
    while(item && item->u.mediaItemAttribute.seqNum != seqNum)
    {
        item = item->next;
    }
    return item;
}

PlaylistItem *findItemByIndex(Playlist *playlist, int index)
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
/* �������baseURL��urlǰ����һЩǰ���Ŀո���strncasecmp��������������������һ��ʼ�ü�ǰ���
   �ո�*/
/* �����ڲ��ᴴ��AString�Ķ���*/
//***********************************************************//
static status_t MakeURL(const char *baseURL, const char *url, AString **out)
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
        *out = creatAString(url, 0, urlLen);
        if(!*out)
        {
            LOGE("ERROR_MALFORMED");
            return ERROR_MALFORMED;
        }
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
        char *slashPos = strstr(protocolEnd, ".m3u8");
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
    *out = (AString *)malloc(sizeof(AString));
    if (!*out)
    {
        LOGE("err_no_memory");
        free(temp);
        return err_no_memory;
    }
    (*out)->mData = temp;
    (*out)->mSize = memSize-1;
    (*out)->mAllocSize = memSize;
    return OK;
}

static inline void destroyMediaItem(MediaItem *mediaItem)
{
    if(mediaItem)
    {
        destroyAString(mediaItem->mName);
        destroyAString(mediaItem->mURI);
        destroyAString(mediaItem->mLanguage);
        free(mediaItem);
    }
    return ;
}

static void destroyMediaItems(MediaItem *mediaItems)
{
    MediaItem *p = mediaItems;
    MediaItem *e;
    while (p)
    {
        e = p->next;
        destroyMediaItem(p);
        p = e;
    }
    return ;
}

static inline void destroyMediaGroup(MediaGroup *mediaGroup)
{
    if(mediaGroup)
    {
        destroyAString(mediaGroup->groupID);
        destroyMediaItems(mediaGroup->mMediaItems);
        free(mediaGroup);
    }
    return ;
}

static void destroyMediaGroups(MediaGroup *mediaGroups)
{
    MediaGroup *p = mediaGroups;
    MediaGroup *e;
    while (p)
    {
        e = p->next;
        destroyMediaGroup(p);
        p = e;
    }
    return ;
}

static void destroyPlaylistItems(PlaylistItem *p)
{
    PlaylistItem *e;
    uint32_t i;
    while (p)
    {
        e = p->next;
        for(i = 0; i < p->itemMeta.mNumAtom; i++)
        {
            if (p->itemMeta.mAtom[i].mType == kTypeString)
            {
                destroyAString(p->itemMeta.mAtom[i].u.stringValue);
                /*p->itemMeta.mAtom[i].name�����ͷţ���Ϊ�����Ƕ�̬����ģ�*/
                /*err = parseMetaDataDuration(&line, &itemMeta, "durationUs");
                  ��meta->mAtom[meta->mNumAtom].mName=key;*/
            }
        }
        destroyAString(p->mURI);
        free(p);
        p=e;
    }
}

//***********************************************************//
/* �ͷ�p��ָ������Playlist��������������mBaseURI.mData��mItems���������ͷŵ���Playlist*/
//***********************************************************//

void destroyPlaylist(Playlist *playList)
{
    Playlist *curPlayList, *nextPlayList;
    uint32_t i;

    curPlayList = playList;
    while (curPlayList)
    {
        nextPlayList = curPlayList->next;
        if (curPlayList->mBaseURI.mData)
        {
            free(curPlayList->mBaseURI.mData);
            curPlayList->mBaseURI.mData = NULL;
        }

        for (i = 0; i < curPlayList->mMeta.mNumAtom; i++)
        {
            if (curPlayList->mMeta.mAtom[i].mType == kTypeString)
            {
                destroyAString(curPlayList->mMeta.mAtom[i].u.stringValue);
            }
        }
        if(curPlayList->mIsVariantPlaylist)
        {
            destroyMediaGroups(curPlayList->u.masterPlaylistPrivate.mMediaGroups);
            curPlayList->u.masterPlaylistPrivate.mMediaGroups = NULL;
        }

        destroyPlaylistItems(curPlayList->mItems);
        free(curPlayList);
        curPlayList = nextPlayList;
    }
    return ;
}

static status_t ParseInt32(const char *s, int32_t *x)
{
    char *pEnd;
    long lVal = strtol(s, &pEnd, 10);

    if (!strcmp(pEnd, s) || (*pEnd != '\0' && *pEnd != ','))
        return ERROR_MALFORMED;

    *x = (int32_t)lVal;
    return OK;
}

static status_t ParseInt64(const char *s, int64_t *x)
{
    char *pEnd;
    long long lval = strtoll(s, &pEnd, 10);

    if (!strcmp(pEnd, s) || (*pEnd != '\0' && *pEnd != ','))
        return ERROR_MALFORMED;

    *x = (int64_t)lval;
    return OK;
}

static status_t ParseDouble(const char *s, double *x)
{
    char *pEnd;
    double dVal = strtod(s, &pEnd);

    if (!strcmp(pEnd, s) || (*pEnd != '\0' && *pEnd != ','))
        return ERROR_MALFORMED;

    *x = dVal;
    return OK;
}

static status_t parseMetaData(const AString *line, AMessage *meta, const char *key)
{
    char *colonPos = strstr(line->mData, ":");

    if (colonPos == NULL)
        return ERROR_MALFORMED;

    int64_t x;
    status_t err = ParseInt64(colonPos + 1, &x);

    int32_t y = (int32_t)(x & 0x000000007fffffff);

    if (err != OK)
        return err;
    return setInt32(meta, key, y);
}

static status_t parseMetaDataDuration( const AString *line, AMessage *meta, const char *key)
{
    char *colonPos = strstr(line->mData, ":");

    if (colonPos == NULL)
        return ERROR_MALFORMED;

    double x;
    status_t err = ParseDouble(colonPos + 1, &x);

    if (err != OK)
        return err;
    return setInt64(meta, key, (int64_t)(x * 1E6));
}

/*�緵��ֵ����OK��length, offset�������ֵ��������ȷ���壬��Ӧʹ��*/
static status_t parseByteRange(const AString *line, uint64_t curOffset,
                                uint64_t *length, uint64_t *offset)
{
    char *colonPos = strstr(line->mData, ":");
    if (colonPos == NULL)
        return ERROR_MALFORMED;

    char *atPos = strstr(colonPos + 1, "@");
    if (atPos == NULL)
    {
        char *pEnd;
        *length = strtoull(colonPos + 1, &pEnd, 10);
        if (!strcmp(pEnd, colonPos + 1) || (*pEnd != '\0' && !isspace(*pEnd)))
            return ERROR_MALFORMED;
        *offset = curOffset;
    }
    else
    {
        char *pEnd;
        *length = strtoull(colonPos + 1, &pEnd, 10);
        if (!strcmp(pEnd, colonPos + 1) || (*pEnd != '@' && *pEnd != '\0' && !isspace(*pEnd)))
            return ERROR_MALFORMED;
        *offset = strtoull(atPos + 1, &pEnd, 10);
        if (!strcmp(pEnd,colonPos + 1) || (*pEnd != '\0' && !isspace(*pEnd)))
            return ERROR_MALFORMED;
    }
    return OK;
}

static status_t parseCipherInfo(AString *line, AMessage *meta, const AString *baseURI,
                                int *methodIsNone)
{
    *methodIsNone = 0;
    char *colonPos = strstr(line->mData, ":");
    if(colonPos == NULL)
    {
        LOGE("ERROR_MALFORMED");
        return ERROR_MALFORMED;
    }

    status_t err = OK;
    AString *value = NULL;
    cdx_uint32 offset = colonPos - line->mData + 1;

    while (offset < line->mSize)
    {
        ssize_t end = M3uFindNextUnquoted(line, ',', offset);
        if (end < 0)
        {
            end = line->mSize;
        }
        line->mData[end] = '\0';

        char *attr = trim_char(line->mData + offset);
        offset = end + 1;

        char *equalPos = strstr(attr, "=");
        if (equalPos == NULL)
        {
            continue;
        }
        *equalPos = '\0';

        char *key = trim_char(attr);
        char *val = trim_char(equalPos + 1);
        LOGV("key=%s value=%s", key, val);
        tolower_str(key);
        cdx_uint32 len = strlen(val);
        if(!strcmp("uri", key))
        {
            if (len < 2
                    || val[0] != '"'
                    || val[len - 1] != '"')
            {
                  LOGE("Expected quoted string for URI, got '%s' instead.", val);
                err = ERROR_MALFORMED;
                goto _err;
            }
            val[len - 1] = '\0';
            val = trim_char(++val);
            err = MakeURL(baseURI->mData, val, &value);
            if (err != OK)
            {
                LOGE("Failed to make absolute URI from '%s'", val);
                goto _err;
            }
            setString(meta, "cipher-uri", value);
        }
        else if(!strcmp("method", key))
        {
            value = creatAString(val, 0, len);
            if (!value)
            {
                LOGE("Failed to creatAString from '%s'", val);
                err = ERROR_MALFORMED;
                goto _err;
            }
            setString(meta, "cipher-method", value);
            if(0 == strcasecmp(val, "NONE"))
            {
                LOGD("cipher-method = NONE");
                *methodIsNone = 1;
            }
        }
        else if(!strcmp("iv", key))
        {
            value = creatAString(val, 0, len);
            if (!value)
            {
                LOGE("Failed to creatAString from '%s'", val);
                err = ERROR_MALFORMED;
                goto _err;
            }
            setString(meta, "cipher-iv", value);
        }
        value = NULL;/*��Ҫ*/
    }
    return OK;
_err:
    destroyAString(value);
    return err;
}

static status_t parseMedia(AString *line, Playlist *playlist)
{
    char *colonPos = strstr(line->mData, ":");
    if(colonPos == NULL)
    {
        LOGE("ERROR_MALFORMED");
        return ERROR_MALFORMED;
    }
    MediaItem *mediaItem = NULL;
    status_t err = OK;
    MediaGroup *mediaGroup = NULL;

    bool bHaveGroupType = false;
    MediaType groupType = TYPE_AUDIO;

    bool bHaveGroupID = false;
    AString *groupID = NULL;

    bool bHaveGroupLanguage = false;
    AString *groupLanguage = NULL;

    bool bHaveGroupName = false;
    AString *groupName = NULL;

    bool bHaveGroupAutoselect = false;
    bool bGroupAutoselect = false;

    bool bHaveGroupDefault = false;
    bool bGroupDefault = false;

    bool bHaveGroupForced = false;
    bool bGroupForced = false;

    bool bHaveGroupURI = false;
    AString *groupURI = NULL;

    cdx_uint32 offset = colonPos - line->mData + 1;

    while (offset < line->mSize)
    {
        ssize_t end = M3uFindNextUnquoted(line, ',', offset);
        if (end < 0)
        {
            end = line->mSize;
        }
        line->mData[end] = '\0';

        char *attr = trim_char(line->mData + offset);
        offset = end + 1;

        char *equalPos = strstr(attr, "=");
        if (equalPos == NULL)
        {
            continue;
        }
        *equalPos = '\0';

        char *key = trim_char(attr);
        char *val = trim_char(equalPos + 1);
        LOGV("key=%s value=%s", key, val);

        if (!strcasecmp("type", key))
        {
            if (!strcasecmp("subtitles", val))
            {
                groupType = TYPE_SUBS;
            }
            else if (!strcasecmp("audio", val))
            {
                groupType = TYPE_AUDIO;
            }
            else if (!strcasecmp("video", val))
            {
                groupType = TYPE_VIDEO;
            }
            else
            {
                LOGW("Invalid media group type '%s'", val);
                goto _err;/*��Щtype���ǲ�ʶ����CLOSED-CAPTIONS����ʱ������Ϊ�ǳ���������ᵼ����
                            ��m3u8parser�����˳�*/
                            /*Ӧ����goto������return OK,��Ϊ�þ䴦��while�У����������ط���������Դ
                            */
            }
            bHaveGroupType = true;
        }
        else if (!strcasecmp("group-id", key))
        {
            cdx_uint32 len = strlen(val);
            if (len < 2
                    || val[0] != '"'
                    || val[len - 1] != '"')
            {
                LOGE("Invalid string for group-id: '%s'", val);
                err = ERROR_MALFORMED;
                goto _err;
            }

            groupID = creatAString(val, 1, len - 2);
            if(groupID)
            {
                bHaveGroupID = true;
            }
        }
        else if (!strcasecmp("language", key))
        {
            cdx_uint32 len = strlen(val);
            if (len < 2
                    || val[0] != '"'
                    || val[len - 1] != '"')
            {
                LOGE("Invalid quoted string for LANGUAGE: '%s'", val);
                err = ERROR_MALFORMED;
                goto _err;
            }
            groupLanguage = creatAString(val, 1, len - 2);

            if(groupLanguage)
            {
                bHaveGroupLanguage = true;
            }
        }
        else if (!strcasecmp("name", key))
        {
            cdx_uint32 len = strlen(val);
            if (len < 2
                    || val[0] != '"'
                    || val[len - 1] != '"')
            {
                LOGE("Expected quoted string for NAME, got '%s' instead.", val);
                err = ERROR_MALFORMED;
                goto _err;
            }
            groupName = creatAString(val, 1, len - 2);
            if(groupName)
            {
                bHaveGroupName = true;
            }
        }
        else if (!strcasecmp("autoselect", key))
        {
            if (!strcasecmp("YES", val))
            {
                bGroupAutoselect = true;
            }
            else if (!strcasecmp("NO", val))
            {
                bGroupAutoselect = false;
            }
            else
            {
                LOGE("Expected YES or NO for AUTOSELECT attribute, got '%s' instead.", val);
                err = ERROR_MALFORMED;
                goto _err;
            }
            bHaveGroupAutoselect = true;
        }
        else if (!strcasecmp("default", key))
        {
            if (!strcasecmp("YES", val))
            {
                bGroupDefault = true;
            }
            else if (!strcasecmp("NO", val))
            {
                bGroupDefault = false;
            }
            else
            {
                LOGE("Expected YES or NO for DEFAULT attribute, got '%s' instead.", val);
                err = ERROR_MALFORMED;
                goto _err;
            }
            bHaveGroupDefault = true;
        }
        else if (!strcasecmp("forced", key))
        {
            if (!strcasecmp("YES", val))
            {
                bGroupForced = true;
            }
            else if (!strcasecmp("NO", val))
            {
                bGroupForced = false;
            }
            else
            {
                LOGE("Expected YES or NO for FORCED attribute, got '%s' instead.", val);
                err = ERROR_MALFORMED;
                goto _err;
            }

            bHaveGroupForced = true;
        }
        else if (!strcasecmp("uri", key))
        {
            cdx_uint32 len = strlen(val);
            if (len < 2
                    || val[0] != '"'
                    || val[len - 1] != '"')
            {
                LOGE("Expected quoted string for URI, got '%s' instead.", val);
                err = ERROR_MALFORMED;
                goto _err;
            }
            val[len - 1] = '\0';
            val = trim_char(++val);
            err = MakeURL(playlist->mBaseURI.mData, val, &groupURI);
            if (err != OK)
            {
                LOGE("Failed to make absolute URI from '%s'", val);
                goto _err;
            }
            if(groupURI)
            {
                bHaveGroupURI = true;
            }
        }
    }

    if (!bHaveGroupType || !bHaveGroupID || !bHaveGroupName)
    {
        LOGE("Incomplete EXT-X-MEDIA element.");
        err = ERROR_MALFORMED;
        goto _err;
    }

    uint32_t flags = 0;
    if (bHaveGroupAutoselect && bGroupAutoselect)
    {
        flags |= CDX_FLAG_AUTOSELECT;
    }
    if (bHaveGroupDefault && bGroupDefault)
    {
        flags |= CDX_FLAG_DEFAULT;
    }
    if (bHaveGroupForced)
    {
        if (groupType != TYPE_SUBS)
        {
            LOGE("The FORCED attribute MUST not be present on anything but SUBS media.");
            err = ERROR_MALFORMED;
            goto _err;
        }

        if (bGroupForced)
        {
            flags |= CDX_FLAG_FORCED;
        }
    }
    if (bHaveGroupLanguage)
    {
        flags |= CDX_FLAG_HAS_LANGUAGE;
    }
    if (bHaveGroupURI)
    {
        flags |= CDX_FLAG_HAS_URI;
    }

    mediaItem = (MediaItem *)malloc(sizeof(MediaItem));
    if(mediaItem == NULL)
    {
        LOGE("err_no_memory");
        err = err_no_memory;
        goto _err;
    }
    //memset(mediaItem, 0, sizeof(MediaItem));

    mediaItem->mName = groupName;
    mediaItem->mURI = groupURI;
    mediaItem->mLanguage = groupLanguage;
    mediaItem->mFlags = flags;
    mediaItem->next = NULL;

    mediaGroup = playlist->u.masterPlaylistPrivate.mMediaGroups;
    MediaGroup *mediaGroupPre = NULL;
    while(mediaGroup != NULL && strcmp(mediaGroup->groupID->mData, groupID->mData))
    {
        mediaGroupPre = mediaGroup;
        mediaGroup = mediaGroup->next;
    }
    if(mediaGroup == NULL)
    {
        mediaGroup = (MediaGroup *)malloc(sizeof(MediaGroup));
        if(mediaGroup == NULL)
        {
            LOGE("err_no_memory");
            err = err_no_memory;
            goto _err;
        }
        mediaGroup->groupID = groupID;
        mediaGroup->mType = groupType;
        mediaGroup->mMediaItems = mediaItem;
        mediaGroup->mNumMediaItem = 1;
        mediaGroup->mSelectedIndex = -1;
        mediaGroup->next = NULL;

        if(!playlist->u.masterPlaylistPrivate.mMediaGroups)
        {
            playlist->u.masterPlaylistPrivate.mMediaGroups = mediaGroup;
        }
        else
        {
            mediaGroupPre->next = mediaGroup;
        }
    }
    else
    {
        if(mediaGroup->mType != groupType)
        {
            LOGE("ERROR_MALFORMED.");
            err = ERROR_MALFORMED;
            goto _err;
        }

        MediaItem *tmp = mediaGroup->mMediaItems;/*mediaGroup->mMediaItems����Ϊ��*/
        while(tmp->next != NULL)
        {
            tmp = tmp->next;
        }
        tmp->next = mediaItem;
        mediaGroup->mNumMediaItem++;
    }
    mediaItem->father = mediaGroup;

    return OK;

_err:

/*mediaGroup�����ͷţ�Ҳ���ܼ��ͷš���ΪmediaGroup�ǿտ����Ǵ����е����ҵ��ģ��������½�����*/
/*��mediaGroup���½�����ʱ��Ψһ���ܵĴ�����err_no_memory��mediaGroupΪ�գ������ͷ�*/

    destroyMediaItem(mediaItem);
    destroyAString(groupID);
    destroyAString(groupLanguage);
    destroyAString(groupName);
    destroyAString(groupURI);

    return err;

}

static status_t parseStreamInf(AString *line, AMessage *meta, Playlist *playlist)
{
    char *colonPos = strstr(line->mData, ":");
    if(colonPos == NULL)
    {
        LOGE("ERROR_MALFORMED");
        return ERROR_MALFORMED;
    }
    status_t err = OK;
    AString *groupID = NULL;
    cdx_uint32 offset= colonPos - line->mData + 1;/*��ʱline->mData+offsetָ����ǡ�:������һ���ַ�*/

    while (offset < line->mSize)
    {
        ssize_t end = M3uFindNextUnquoted(line, ',', offset);
        if (end < 0)
        {
            end = line->mSize;
        }
        line->mData[end] = '\0';

        char *attr = trim_char(line->mData + offset);
        offset = end + 1;

        char *equalPos = strstr(attr, "=");
        if (equalPos == NULL)
        {
            continue;
        }
        *equalPos = '\0';
        char *key = trim_char(attr);
        char *val = trim_char(equalPos + 1);
        LOGV("key=%s value=%s", key, val);
        tolower_str(key);
        if (!strcmp("bandwidth", key))
        {
            const char *s = val;
            char *pEnd;
            unsigned long long x = strtoull(s, &pEnd, 10);

            if (!strcmp(pEnd,s) || *pEnd != '\0')
            {
                continue;/*malformed*/
            }
            if(setInt64(meta, "bandwidth", (cdx_int64)x) != OK)
            {
                LOGE("ERROR_maxNumAtom_too_little");
                err = ERROR_maxNumAtom_too_little;
                goto _err;
            }
        }
        else if (!strcmp("audio", key)
                || !strcmp("video", key)
                || !strcmp("subtitles", key))
        {
            cdx_uint32 len = strlen(val);
            if (len < 2
                    || val[0] != '"'
                    || val[len - 1] != '"')
            {
                LOGE("Expected quoted string for %s attribute, got '%s' instead.",
                      key, val);
                err = ERROR_MALFORMED;
                goto _err;
            }

            groupID = creatAString(val, 1, len - 2);
            if (!groupID)
            {
                LOGE("Failed to creatAString from '%s'", val);
                err = ERROR_MALFORMED;
                goto _err;
            }
            MediaGroup *mediaGroup = playlist->u.masterPlaylistPrivate.mMediaGroups;
            while(mediaGroup != NULL && strcmp(mediaGroup->groupID->mData, groupID->mData))
            {
                mediaGroup = mediaGroup->next;
            }
            if(mediaGroup == NULL)
            {
                LOGE("Undefined media group '%s' referenced in stream info.",
                      groupID->mData);
                err = ERROR_MALFORMED;
                goto _err;
            }
            setString(meta, key, groupID);
            groupID = NULL;/*��Ҫ!��ΪgroupID�Ǹ��õģ����û�������ܵ����ڴ���ظ��ͷ�*/
            /*���磬��һ��ѭ��ʱgroupID��д��meta�����ڶ���ѭ���г�������ʱdestroyAString(groupID);
              ��destroyPlaylist�ظ��ͷ��ڴ�*/
        }
    }

    return OK;
_err:

    destroyAString(groupID);
    /*��ʱ������һЩgroupID�Ѿ�д��meta���ô���δ�����ͷţ���ΪgroupID����while�С�û�й�ϵ�����ش�
    ��destroyPlaylist�Ὣ���ͷ�*/
    return err;
}

/*�ж��Ƿ�m3u8�ļ�*/
bool M3uProbe(const char *data, cdx_uint32 size)
{
    cdx_uint32 offset = 0;
    while (offset < size && isspace(data[offset])) //isspace�а�����\n��\r,�����Ѿ��ų��˿��е����
    {
        offset++;
    }
    if(offset + 7 >= size)
    {
        return false;
    }

    if (strncmp(data + offset, "#EXTM3U", 7))
        return false;

    offset += 7;
    if (offset >= size)
        return false;

    char *needle[] = {"#EXT-X-TARGETDURATION:", "#EXT-X-MEDIA-SEQUENCE:", "#EXT-X-STREAM-INF:",};
    int i;
    for (i = 0; i < sizeof(needle) / sizeof(needle[0]); i++)
    {
        if (memmem(data + offset, size - offset, needle[i], strlen(needle[i])))
            return true;
    }

    LOGW("start with #EXTM3U but don't have some #EXT tags, don't treat it as HLS");
    return false;
}

//***********************************************************//
/* ����_data��ָ��������ص���m3u8�ļ���������Ĵ�СΪsize*/
/* baseURI��ָ�ַ������ڴ������ⲿ���ٵ�,�����ԡ�\0������,�����в���ı�char *baseURI*/
/* ���parse��������Ӧ�ڴ��Ѿ����ͷ�*/
//***********************************************************//
status_t M3u8Parse(const void *_data, cdx_uint32 size, Playlist **P, const char *baseURI)
{
    *P = NULL;
    int32_t lineNo = 0;
    const char *data = (const char *)_data;
    cdx_uint32 offset = 0;
    uint64_t segmentRangeOffset = 0;
    bool hasEncrypteInf = false;
    AMessage itemMeta;
    memset(&itemMeta, 0, sizeof(AMessage));
    AString line;
    line.mData = NULL;
    int seqNum = 0;
    status_t err = OK;
    PlaylistItem *cipherReference = NULL;

    Playlist *playList = (Playlist*)malloc(sizeof(Playlist));
    if(playList == NULL)
    {
        LOGE("err_no_memory");
        return err_no_memory;
    }
    memset(playList, 0x00, sizeof(Playlist));
    int mIsVariantPlaylist = -1;/*�������������ԭ����playList->mIsVariantPlaylist��bool����δ��ȷ
                                  �ж�֮ǰ�Ѿ���ֵ0*/

    LOGV("baseURI=%s", baseURI);
    int baseLen = strlen(baseURI);
    playList->mBaseURI.mData = (char *)malloc(baseLen+1);
    if(playList->mBaseURI.mData == NULL)
    {
        LOGE("err_no_memory");
        err = err_no_memory;
        goto _err;
    }
    memcpy(playList->mBaseURI.mData, baseURI, baseLen+1);
    playList->mBaseURI.mSize = baseLen;
    playList->mBaseURI.mAllocSize = baseLen+1;

    while (offset < size)
    {
        while(offset < size && isspace(data[offset]))//isspace�а�����\n��\r,�����Ѿ��ų��˿��е����
        {
            offset++;
        }
        if(offset >= size)
        {
            break;
        }
        cdx_uint32 uOffsetLF = offset;
        while (uOffsetLF < size && data[uOffsetLF] != '\n')
        {
            ++uOffsetLF;
        }
        /*ȥ�����code,�Լ������һ�в���'\n'���������
        if(uOffsetLF >= size)
        {
            break;
        }*/
        cdx_uint32 offsetData = uOffsetLF;

        while(isspace(data[offsetData-1]))
        {
            --offsetData;
        }/*offsetData��ǰһ��λ��data[offsetData-1]����Ч�ַ���������'\r'��'\n'��data[offsetData]��
           '\r'��'\n'��offsetData - offset����Ч�ַ��ĸ���*/
        if ((offsetData - offset)<=0)        /*˵���ǿ���*/
        {
            offset = uOffsetLF + 1;
            continue;
        }
        else
        {
            line.mData = (char *)malloc((offsetData - offset+1)*sizeof(char));
            if(line.mData == NULL)
            {
                LOGE("err_no_memory");
                err = err_no_memory;
                goto _err;
            }
            line.mSize = offsetData - offset;
            line.mAllocSize = offsetData - offset+1;
            /*��data[offset]����data[offsetData-1]����һ��*/
            memcpy(line.mData, &data[offset], offsetData - offset);
            line.mData[offsetData - offset] = '\0';
            LOGV("#%s#", line.mData);
        }

        if (lineNo == 0)
        {
            if (!strcmp(line.mData, "#EXTM3U"))
            {
                playList->mIsExtM3U = true;
            }
            else
            {
                LOGE("lineNo == 0, but line != EXTM3U");
                err = ERROR_MALFORMED;
                goto _err;
            }
        }
        else
        {
            if (startsWith(line.mData,"#EXT-X-TARGETDURATION"))
            {
                if (playList->mIsVariantPlaylist)
                {
                    LOGE("ERROR_MALFORMED");
                    err = ERROR_MALFORMED;/*��Щtag���������master playlist ��
                                            ��ֻ������media playlisy��*/
                    goto _err;
                }
                err = parseMetaData(&line, &(playList->mMeta), "target-duration");
            /*mMeta�¹�playlisy��ȫ�֣��������һ��URL,����ֻ����һ�Σ���itemMeta�����һ��url*/
                mIsVariantPlaylist = 0;

            }
            else if (startsWith(line.mData,"#EXT-X-MEDIA-SEQUENCE"))
            {
                if (playList->mIsVariantPlaylist)
                {
                    LOGE("ERROR_MALFORMED");
                    err = ERROR_MALFORMED;
                    goto _err;
                }
                err = parseMetaData(&line, &(playList->mMeta), "media-sequence");
                findInt32(&playList->mMeta,"media-sequence", &seqNum);
                mIsVariantPlaylist = 0;
            }
            else if (startsWith(line.mData,"#EXT-X-KEY"))
            {
                if (playList->mIsVariantPlaylist)
                {
                    LOGE("ERROR_MALFORMED");
                    err = ERROR_MALFORMED;
                    goto _err;
                }
                int methodIsNone;
                err = parseCipherInfo(&line, &itemMeta, &playList->mBaseURI, &methodIsNone);
                if(!methodIsNone)
                {
                    hasEncrypteInf = true;
                    playList->u.mediaPlaylistPrivate.hasEncrypte = true;
                }
            }

            else if (startsWith(line.mData,"#EXT-X-ENDLIST"))
            {
                //LOGV("#EXT-X-ENDLIST");

                if (playList->mIsVariantPlaylist)
                {
                    LOGE("ERROR_MALFORMED");
                    //err = ERROR_MALFORMED;
                    //goto _err;
                }
                playList->u.mediaPlaylistPrivate.mIsComplete = true;
            }
            else if (startsWith(line.mData,"#EXT-X-PLAYLIST-TYPE:EVENT"))
            {
                if (playList->mIsVariantPlaylist)
                {
                    LOGE("ERROR_MALFORMED");
                    err = ERROR_MALFORMED;
                    goto _err;
                }
                playList->u.mediaPlaylistPrivate.mIsEvent = true;
            }
            else if (startsWith(line.mData,"#EXTINF"))
            {
                if (playList->mIsVariantPlaylist)
                {
                    LOGE("ERROR_MALFORMED");
                    err = ERROR_MALFORMED;
                    goto _err;
                }
                err = parseMetaDataDuration(&line, &itemMeta, "durationUs");
                mIsVariantPlaylist = 0;
            }
            else if (startsWith(line.mData,"#EXT-X-DISCONTINUITY"))
            {
                if (playList->mIsVariantPlaylist)
                {
                    LOGE("ERROR_MALFORMED");
                    err = ERROR_MALFORMED;
                    goto _err;
                }
                setInt32(&itemMeta, "discontinuity", 1);
                playList->u.mediaPlaylistPrivate.discontinueTimeshift = 1;
            }
            else if (startsWith(line.mData,"#EXT-X-STREAM-INF"))
            {
                if (!mIsVariantPlaylist)
                {
                    LOGE("ERROR_MALFORMED");
                    err = ERROR_MALFORMED;
                    goto _err;
                }
                playList->mIsVariantPlaylist = true;
                mIsVariantPlaylist = 1;
                err = parseStreamInf(&line, &itemMeta, playList);
            }
            else if (startsWith(line.mData,"#EXT-X-BYTERANGE"))
            {
                if (playList->mIsVariantPlaylist)
                {
                    LOGE("ERROR_MALFORMED");
                    err = ERROR_MALFORMED;
                    goto _err;
                }

                uint64_t length, offset;
                err = parseByteRange(&line, segmentRangeOffset, &length, &offset);
                if (err != OK)
                {
                    LOGE("ERROR_MALFORMED");
                    goto _err;
                }

                status_t err1 = setInt64(&itemMeta, "range-offset", offset);
                status_t err2 = setInt64(&itemMeta, "range-length", length);
                if (err1 !=OK || err2 != OK)
                {
                    LOGE("ERROR_maxNumAtom_too_little");
                    err = ERROR_maxNumAtom_too_little;
                    goto _err;
                }

                segmentRangeOffset = offset + length;
            }
            else if (startsWith(line.mData,"#EXT-X-MEDIA"))
            {
                err = parseMedia(&line, playList);
            }

            if (err != OK)
            {
                LOGE("err = %d", err);
                goto _err;
            }
        }

        if (!startsWith(line.mData,"#")) /*���ǿ��У����Ǳ�ǩ������ע�ͣ���URL*/
        {
            cdx_int64 durationUs = 0;
            cdx_int64 bandwidth = 0;

            if (!playList->mIsVariantPlaylist)
            {
                if (itemMeta.mNumAtom == 0 || !findInt64(&itemMeta,"durationUs", &durationUs))
                {
                    LOGE("ERROR_MALFORMED");
                    goto freeAndContinue;
                }
            }

            PlaylistItem *item= (PlaylistItem*)malloc(sizeof(PlaylistItem));
            if (item == NULL)
            {
                LOGE("err_no_memory");
                err = err_no_memory;
                goto _err;
            }
            memset(item, 0, sizeof(PlaylistItem));
            //Amessage�е�atom������ָ�룬����memcpy��free��ҪС��
            memcpy(&(item->itemMeta),&itemMeta,sizeof(AMessage));
            if(durationUs >= 0)
            {
                item->u.mediaItemAttribute.durationUs = durationUs;
                item->u.mediaItemAttribute.seqNum = seqNum;
                if(playList->u.mediaPlaylistPrivate.hasEncrypte)
                {
                    if(hasEncrypteInf)
                    {
                        item->u.mediaItemAttribute.cipherReference = item;
                        cipherReference = item;
                    }
                    else
                    {
                        item->u.mediaItemAttribute.cipherReference = cipherReference;
                    }
                }
                item->u.mediaItemAttribute.baseTimeUs=playList->u.mediaPlaylistPrivate.mDurationUs;
                hasEncrypteInf = false;
                playList->u.mediaPlaylistPrivate.mDurationUs += durationUs;/*����Ƭ��*/
                playList->u.mediaPlaylistPrivate.lastSeqNum = seqNum;
            }
            if (findInt64(&itemMeta,"bandwidth", &bandwidth) && (bandwidth > 0))
            {
                item->u.masterItemAttribute.bandwidth = bandwidth;
                item->u.masterItemAttribute.bandwidthNum = seqNum;
            }

            if (findInt32(&itemMeta, "discontinuity", &item->discontinue) == 0)
            {
                item->discontinue = 0;
            }

            seqNum++;
            MakeURL(playList->mBaseURI.mData, line.mData, &item->mURI);
            item->next = NULL;
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
            memset(&itemMeta, 0, sizeof(AMessage)); /*��ΪitemMeta��Ӧ��һ��URL*/
        }

freeAndContinue:
        free(line.mData);
        line.mData = NULL;
        offset = uOffsetLF + 1;
        ++lineNo;
    }
    if (mIsVariantPlaylist == -1)/*��ʱ��δ��������*/
    {
        LOGE("ERROR_MALFORMED");
        err = ERROR_MALFORMED;
        goto _err;
    }
    if(playList->mNumItems <= 0)
    {
        LOGE("playList->mNumItems <= 0");
        err = ERROR_MALFORMED;
        goto _err;
    }
    *P = playList;
    return OK;

_err:
    if(line.mData != NULL)
    {
        free(line.mData);
    }
    destroyPlaylist(playList);
    return err;
}
