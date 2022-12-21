#ifndef CEDAR_AW_DEMUX_H_
#define CEDAR_AW_DEMUX_H_

#include <log/log.h>

#include "cedarx_demux.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct AwDemuxInfo 
{
    CEDARX_SOURCETYPE   mSourceType;
    int (*mpEventHandler)(void* cookie, int event, void *data);
    void* mpCookie;
} AwDemuxInfo;

extern CedarXDemuxerAPI cdx_dmxs_aw;
int AwDemuxerSetDataSource(/*out*/CdxDataSourceT *pDataSource, /*in*/CedarXDataSourceDesc *datasrc_desc);
void clearDataSourceFields(CdxDataSourceT* source);
void clearCdxMediaInfoT(CdxMediaInfoT *pMediaInfo, int nParserType);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CEDAR_AW_DEMUX_H_ */
