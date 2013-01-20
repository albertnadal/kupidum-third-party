#include <pjsua-lib/pjsua.h>
#include <pjsua-lib/pjsua_internal.h>
#include <pjmedia/sdp.h>
#include <media_manager_api/spirit_media_manager.h>

/* from session.c */
static const pj_str_t ID_AUDIO = { "audio", 5};
static const pj_str_t ID_VIDEO = { "video", 5};
static const pj_str_t ID_APPLICATION = { "application", 11};
static const pj_str_t ID_IN = { "IN", 2 };
static const pj_str_t ID_IP4 = { "IP4", 3};
static const pj_str_t ID_IP6 = { "IP6", 3};
static const pj_str_t ID_RTP_AVP = { "RTP/AVP", 7 };
static const pj_str_t ID_RTP_SAVP = { "RTP/SAVP", 8 };
//static const pj_str_t ID_SDP_NAME = { "pjmedia", 7 };
static const pj_str_t ID_RTPMAP = { "rtpmap", 6 };
static const pj_str_t ID_TELEPHONE_EVENT = { "telephone-event", 15 };

static const pj_str_t STR_INACTIVE = { "inactive", 8 };
static const pj_str_t STR_SENDRECV = { "sendrecv", 8 };
static const pj_str_t STR_SENDONLY = { "sendonly", 8 };
static const pj_str_t STR_RECVONLY = { "recvonly", 8 };

/* from ... */
static const pj_str_t STR_AUDIO = { "audio", 5};
static const pj_str_t STR_VIDEO = { "video", 5};
static const pj_str_t STR_IN = { "IN", 2 };
static const pj_str_t STR_IP4 = { "IP4", 3};
static const pj_str_t STR_IP6 = { "IP6", 3};
static const pj_str_t STR_RTP_AVP = { "RTP/AVP", 7 };
static const pj_str_t STR_SDP_NAME = { "session", 7 };

static const pj_str_t ANY_ADDR = { "0.0.0.0", 7 };

#define THIS_FILE		"pjsua_media_spirit.c"

#ifdef ANDROID
  const char* LOG_PATH = "/mnt/sdcard/spirit.log";
#else
  const char* LOG_PATH = "spirit.log";
#endif

#define PJMEDIA_ADD_RTPMAP_FOR_STATIC_PT 1
#define DEFAULT_RTP_PORT	4000
#define CODECS_COUNT           3
#define MAX_CODECS_COUNT      16
#define TELEPHONE_EVENT        1
#define CREATE_MAX_RETRIES    10

static tMMHandle   g_hMM = 0;
static tSpiritLog *g_hLog = 0;
static tAudioCodecDescSettings g_pAudioCodecsList[CODECS_COUNT];

static int find_audio_index(const pjmedia_sdp_session *sdp, pj_bool_t prefer_srtp);
static pj_status_t spirit_create_sdp(pj_pool_t *pool,
					      unsigned stream_cnt,
					      const pjmedia_sock_info sock_info[],
					      pjmedia_sdp_session **p_sdp);
static pj_status_t spirit_session_info_from_sdp( pj_pool_t *pool,
			       unsigned max_streams,
			       pjmedia_session_info *si,
			       const pjmedia_sdp_session *local,
			       const pjmedia_sdp_session *remote);
static pj_status_t spirit_stream_info_from_sdp(
					   pjmedia_stream_info *si,
					   pj_pool_t *pool,
					   const pjmedia_sdp_session *local,
					   const pjmedia_sdp_session *remote,
					   unsigned stream_idx);

#ifdef SPIRIT_FAKE

tResult MM_Create( tMMHandle* phModule, tSpiritLog* pLog ) {
    return SPIRIT_RESULT_OK;
}

tResult MM_Destroy( tMMHandle hModule ) {
    return SPIRIT_RESULT_OK;
}

tResult MM_SetLog( void* pModule, tSpiritLog* pLog ) {
    return SPIRIT_RESULT_OK;
}

tResult MM_Voice_Channel_Create( tMMHandle hModule, uint32* pChannelId, const tCallParam* pParams ) {
    *pChannelId = 0;
    return SPIRIT_RESULT_OK;
}

tResult MM_Voice_Channel_StartRx( tMMHandle hModule, uint32 ChannelId) {
    return SPIRIT_RESULT_OK;
}

tResult MM_Voice_Channel_StopRx( tMMHandle hModule, uint32 ChannelId) {
    return SPIRIT_RESULT_OK;
}

tResult MM_Voice_Channel_StartTx( tMMHandle hModule, uint32 ChannelId) {
    return SPIRIT_RESULT_OK;
}

tResult MM_Voice_Channel_StopTx( tMMHandle hModule, uint32 ChannelId) {
    return SPIRIT_RESULT_OK;
}

tResult MM_Voice_Channel_Destroy( tMMHandle hModule, uint32 ChannelId) {
    return SPIRIT_RESULT_OK;
}

tResult MM_Voice_Channel_SetDestAddr( tMMHandle hModule, uint32 ChannelId, const tIpAddr* pRtpDest, const tIpAddr* pRtcpDest ) {
    return SPIRIT_RESULT_OK;
}

static tAudioCodecDescription fakeCodec;
static tResult fake_tAudioCodec_GetParam(void* pObject, tAudioCodecParam *pParam) {
    pParam->u.value = 0;
    if (pParam->key == cpk_sdp_name) {
        pParam->u.p = "PCMU";
    }
    if (pParam->key == cpk_samplerate) {
        pParam->u.value = 8000;
    }
    return SPIRIT_RESULT_OK;
}

const tAudioCodecDescription*  MM_Voice_GetCodec( const char* szCodec ) {
    fakeCodec.encoder.fnGetParam = fake_tAudioCodec_GetParam;
    return &fakeCodec;
}

tResult MM_Voice_SetEncoder( tMMHandle hModule, uint32 ChannelId, const tAudioCodecDescSettings* pDescr ) {
    return SPIRIT_RESULT_OK;
}

tResult MM_Voice_SetRtpPayloadMapping( tMMHandle hModule, uint32 ChannelId, const tCodecPayload* pInRtpMap, const tCodecPayload* pOutRtpMap ) {
    return SPIRIT_RESULT_OK;
}

uint32 SpiritLogCreate(tSpiritLog **ppLog, const char *pFileName, tSpiritLogType LogType, size_t LogMaxSize) {
    return SPIRIT_RESULT_OK;
}

uint32 SpiritLogDestroy(tSpiritLog *pLog) {
    return SPIRIT_RESULT_OK;
}

#endif

static tAudioCodecDescSettings spirit_get_codec(const char const * name)
{
    tAudioCodecDescSettings codec;
    codec.pVoiceCodec = MM_Voice_GetCodec(name);
    codec.count = 0;
    return codec;
}

pj_status_t pjsua_media_subsys_init(const pjsua_media_config *cfg)
{
    tResult res; 

    PJ_LOG(5,(THIS_FILE, "MM_SpiritLogCreate"));
    res = SpiritLogCreate(&g_hLog, LOG_PATH, eSPIRIT_LOG_FAST, 256*1024);
    if (SPIRIT_RESULT_OK != res) {
        PJ_LOG(4,(THIS_FILE, "MM_SpiritLogCreate error [%d]", res));
    }

    PJ_LOG(5,(THIS_FILE, "MM_Create"));
    res = MM_Create(&g_hMM, g_hLog);
    if (SPIRIT_RESULT_OK != res) {
        PJ_LOG(4,(THIS_FILE, "MM_Create error [%d]", res));
        return PJ_EINVAL;
    }

    g_pAudioCodecsList[0] = spirit_get_codec("G.711 u-law");
    g_pAudioCodecsList[1] = spirit_get_codec("G.711 A-law");
    g_pAudioCodecsList[2] = spirit_get_codec("IP-MR");

    return PJ_SUCCESS;
}

pj_status_t pjsua_media_subsys_start(void)
{
    return PJ_SUCCESS;
}

pj_status_t pjsua_media_subsys_destroy(void)
{
    if (g_hMM)
    {
        PJ_LOG(5,(THIS_FILE, "MM_Destroy"));
        MM_Destroy(g_hMM);
        g_hMM = 0;
    }

    if (g_hLog)
    {
        PJ_LOG(5,(THIS_FILE, "MM_SpiritLogDestroy"));
        SpiritLogDestroy(g_hLog);
        g_hLog = 0;
    }

    return PJ_SUCCESS;
}

pj_status_t pjsua_media_channel_init(pjsua_call_id call_id,
				     pjsip_role_e role,
				     int security_level,
				     pj_pool_t *tmp_pool,
				     const pjmedia_sdp_session *rem_sdp,
				     int *sip_err_code)
{
    pjsua_call *call = &pjsua_var.calls[call_id];

    PJ_UNUSED_ARG(role);

    call->med_channel_id = -1;
    PJ_UNUSED_ARG(security_level);

    /* Find out which media line in SDP that we support. If we are offerer,
     * audio will be initialized at index 0 in SDP. 
     */
    if (rem_sdp == NULL) {
	    call->audio_idx = 0;
    } 
    /* Otherwise find out the candidate audio media line in SDP */
    else {
	    pj_bool_t srtp_active = PJ_FALSE;

	    /* Media count must have been checked */
	    pj_assert(rem_sdp->media_count != 0);

	    call->audio_idx = find_audio_index(rem_sdp, srtp_active);
    }

    /* Reject offer if we couldn't find a good m=audio line in offer */
    if (call->audio_idx < 0) {
	    if (sip_err_code) *sip_err_code = PJSIP_SC_NOT_ACCEPTABLE_HERE;
	    pjsua_media_channel_deinit(call_id);
	    return PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_NOT_ACCEPTABLE_HERE);
    }

    PJ_LOG(4,(THIS_FILE, "Media index %d selected for call %d",
	      call->audio_idx, call->index));

    /* Create the Spirit channel */
    tIpAddr addr;
    addr.ip.i32 = 0; //0.0.0.0
    addr.port = DEFAULT_RTP_PORT;

    tCallParam call_param;
    call_param.transport_type = tr_internal;
    call_param.transport.intrnl.RtpSrc =
    call_param.transport.intrnl.RtcpSrc = addr;
    call_param.transport.intrnl.RtcpSrc.port++;

    int retries = 0;
    while(1) {
        tResult res;// = SPIRIT_RESULT_OK; 
        PJ_LOG(5,(THIS_FILE, "MM_Voice_Channel_Create"));
        res = MM_Voice_Channel_Create(g_hMM, &call->med_channel_id, &call_param);
        if (SPIRIT_RESULT_OK != res) {
            PJ_LOG(4,(THIS_FILE,
	              "Error creating voice channel for call %d [%i]",
	              call_id, res));
            if (++retries >= CREATE_MAX_RETRIES) {
                pjsua_media_channel_deinit(call_id);
                return PJ_EINVAL;   
            }
        }
        else {
            break;
        }
    }

    call->med_tp_st = PJSUA_MED_TP_INIT;

    return PJ_SUCCESS;
}

pj_status_t pjsua_media_channel_deinit(pjsua_call_id call_id)
{
    pjsua_call *call = &pjsua_var.calls[call_id];

    if (call->med_channel_id != -1) {
        tResult res; //= SPIRIT_RESULT_OK;

        PJ_LOG(5,(THIS_FILE, "MM_Voice_Channel_StopRx"));
        res = MM_Voice_Channel_StopRx(g_hMM, call->med_channel_id);
        if (SPIRIT_RESULT_OK != res) {
            PJ_LOG(4,(THIS_FILE,
		              "Error stoping rx voice channel for call %d [%i]",
		              call_id, res));
        }

        PJ_LOG(5,(THIS_FILE, "MM_Voice_Channel_StopTx"));
        res = MM_Voice_Channel_StopTx(g_hMM, call->med_channel_id);
        if (SPIRIT_RESULT_OK != res) {
            PJ_LOG(4,(THIS_FILE,
		              "Error stoping tx voice channel for call %d [%i]",
		              call_id, res));
        }

        PJ_LOG(5,(THIS_FILE, "MM_Voice_Channel_Destroy"));
        res = MM_Voice_Channel_Destroy(g_hMM, call->med_channel_id);
        if (SPIRIT_RESULT_OK != res) {
            PJ_LOG(4,(THIS_FILE,
		              "Error destroying voice channel for call %d [%i]",
		              call_id, res));
        }
        call->med_channel_id = -1;
    }

    call->med_tp_st = PJSUA_MED_TP_IDLE;

    return PJ_SUCCESS;
}

/*
 * i ) Make outgoing call to the specified URI using the specified account.
 * ii) Handle incoming INVITE request.
 */

pj_status_t pjsua_media_channel_create_sdp(pjsua_call_id call_id, 
					   pj_pool_t *pool,
					   const pjmedia_sdp_session *rem_sdp,
					   pjmedia_sdp_session **p_sdp,
					   int *sip_err_code)
{
    enum { MAX_MEDIA = 1 };
    pjmedia_sdp_session *sdp;
    pjmedia_transport_info tpinfo;
    pjsua_call *call = &pjsua_var.calls[call_id];
    pjmedia_sdp_neg_state sdp_neg_state = PJMEDIA_SDP_NEG_STATE_NULL;
    pj_status_t status;

    pj_assert(call->med_channel_id != -1);

    if (rem_sdp && rem_sdp->media_count != 0) {
    	pj_bool_t srtp_active = PJ_FALSE;

        call->audio_idx = find_audio_index(rem_sdp, srtp_active);
	    if (call->audio_idx == -1) {
	        /* No audio in the offer. We can't accept this */
	        PJ_LOG(4,(THIS_FILE,
		          "Unable to accept SDP offer without audio for call %d",
		          call_id));
	        return PJMEDIA_SDP_EINMEDIA;
	    }
    }

    /* Media index must have been determined before */
    pj_assert(call->audio_idx != -1);

    /* Get SDP negotiator state */
    if (call->inv && call->inv->neg)
    	sdp_neg_state = pjmedia_sdp_neg_get_state(call->inv->neg);

    /* Get media socket info */
    unsigned short port = DEFAULT_RTP_PORT;
    pj_sockaddr hostip;
    pj_gethostip(pj_AF_INET(), &hostip);
    
    pj_sockaddr_init(pj_AF_INET(), &tpinfo.sock_info.rtp_addr_name,
                             NULL,
                             (unsigned short)(port));
    pj_sockaddr_init(pj_AF_INET(), &tpinfo.sock_info.rtcp_addr_name,
                             NULL,
                             (unsigned short)(port + 1));
    pj_memcpy(pj_sockaddr_get_addr(&tpinfo.sock_info.rtp_addr_name),
              pj_sockaddr_get_addr(&hostip),
              pj_sockaddr_get_addr_len(&hostip));
    pj_memcpy(pj_sockaddr_get_addr(&tpinfo.sock_info.rtcp_addr_name),
              pj_sockaddr_get_addr(&hostip),
              pj_sockaddr_get_addr_len(&hostip));

    /* Create SDP */
    status = spirit_create_sdp(pool,
                       MAX_MEDIA, &tpinfo.sock_info, &sdp);
    if (status != PJ_SUCCESS) {
        if (sip_err_code) *sip_err_code = 500;
            return status;
    }

    /* If we're answering or updating the session with a new offer,
     * and the selected media is not the first media
     * in SDP, then fill in the unselected media with with zero port. 
     * Otherwise we'll crash in transport_encode_sdp() because the media
     * lines are not aligned between offer and answer.
     */
    if (call->audio_idx != 0 && 
         (rem_sdp || sdp_neg_state==PJMEDIA_SDP_NEG_STATE_DONE))
    {
        unsigned i;
        const pjmedia_sdp_session *ref_sdp = rem_sdp;

        if (!ref_sdp) {
            /* We are updating session with a new offer */
            status = pjmedia_sdp_neg_get_active_local(call->inv->neg,
					              &ref_sdp);
            pj_assert(status == PJ_SUCCESS);
        }

        for (i=0; i<ref_sdp->media_count; ++i) {
            const pjmedia_sdp_media *ref_m = ref_sdp->media[i];
            pjmedia_sdp_media *m;

            if ((int)i == call->audio_idx)
	            continue;

            m = pjmedia_sdp_media_clone_deactivate(pool, ref_m);
            if (i==sdp->media_count)
	            sdp->media[sdp->media_count++] = m;
            else {
	            pj_array_insert(sdp->media, sizeof(sdp->media[0]),
			            sdp->media_count, i, &m);
	            ++sdp->media_count;
            }
        }
    }

    /* Add NAT info in the SDP */
    if (pjsua_var.ua_cfg.nat_type_in_sdp) {
        pjmedia_sdp_attr *a;
        pj_str_t value;
        char nat_info[80];

        value.ptr = nat_info;
        if (pjsua_var.ua_cfg.nat_type_in_sdp == 1) {
            value.slen = pj_ansi_snprintf(nat_info, sizeof(nat_info),
				          "%d", pjsua_var.nat_type);
        } else {
            const char *type_name = pj_stun_get_nat_name(pjsua_var.nat_type);
            value.slen = pj_ansi_snprintf(nat_info, sizeof(nat_info),
				          "%d %s",
				          pjsua_var.nat_type,
				          type_name);
        }

        a = pjmedia_sdp_attr_create(pool, "X-nat", &value);

        pjmedia_sdp_attr_add(&sdp->attr_count, sdp->attr, a);
    }

    //TODO:
    /* Give the SDP to media transport */
    /*status = pjmedia_transport_encode_sdp(call->med_tp, pool, sdp,  
                    rem_sdp, call->audio_idx);
    if (status != PJ_SUCCESS) {
        if (sip_err_code) *sip_err_code = PJSIP_SC_NOT_ACCEPTABLE;
        return status;
    }*/

    /* Update currently advertised RTP source address */
    pj_memcpy(&call->med_rtp_addr, &tpinfo.sock_info.rtp_addr_name, 
	      sizeof(pj_sockaddr));

    *p_sdp = sdp;

    return PJ_SUCCESS;
}

/*
 * Callback to be called when SDP offer/answer negotiation has just completed
 * in the session. This function will start/update media if negotiation
 * has succeeded.
 */
pj_status_t pjsua_media_channel_update(pjsua_call_id call_id,
				       const pjmedia_sdp_session *local_sdp,
				       const pjmedia_sdp_session *remote_sdp)
{
    int prev_media_st = 0;
    pjsua_call *call = &pjsua_var.calls[call_id];
    pjmedia_session_info sess_info;
    pjmedia_stream_info *si = NULL;
    pj_status_t status;

    /* Destroy existing media session, if any. */
    prev_media_st = call->media_st;
    //TODO: stop_media_session(call->index);

    /* Create media session info based on SDP parameters. 
     */    
    status = spirit_session_info_from_sdp(call->inv->pool_prov, 
					    PJMEDIA_MAX_SDP_MEDIA, &sess_info,
					    local_sdp, remote_sdp);
    if (status != PJ_SUCCESS)
	    return status;

    /* Update audio index from the negotiated SDP */
    call->audio_idx = find_audio_index(local_sdp, PJ_TRUE);

    /* Find which session is audio */
    PJ_ASSERT_RETURN(call->audio_idx != -1, PJ_EBUG);
    PJ_ASSERT_RETURN(call->audio_idx < (int)sess_info.stream_cnt, PJ_EBUG);
    si = &sess_info.stream_info[call->audio_idx];
    
    /* Reset session info with only one media stream */
    sess_info.stream_cnt = 1;
    if (si != &sess_info.stream_info[0]) {
	pj_memcpy(&sess_info.stream_info[0], si, sizeof(pjmedia_stream_info));
	si = &sess_info.stream_info[0];
    }

    /* Check if no media is active */
    if (sess_info.stream_cnt == 0 || si->dir == PJMEDIA_DIR_NONE)
    {
	/* Call media state */
	call->media_st = PJSUA_CALL_MEDIA_NONE;

	/* Call media direction */
	call->media_dir = PJMEDIA_DIR_NONE;

	/* Don't stop transport because we need to transmit keep-alives, and
	 * also to prevent restarting ICE negotiation. See
	 *  http://trac.pjsip.org/repos/ticket/1094
	 */
#if 0
	/* Shutdown transport's session */
	pjmedia_transport_media_stop(call->med_tp);
	call->med_tp_st = PJSUA_MED_TP_IDLE;

	/* No need because we need keepalive? */

	/* Close upper entry of transport stack */
	if (call->med_orig && (call->med_tp != call->med_orig)) {
	    pjmedia_transport_close(call->med_tp);
	    call->med_tp = call->med_orig;
	}
#endif

    } else {

    if (sess_info.stream_cnt <= call->audio_idx) {
        PJ_LOG(4,(THIS_FILE, "sess_info.stream_cnt < call->audio_idx"));
        return PJ_EINVAL;
    }

    pjmedia_stream_info *strm_info = &sess_info.stream_info[call->audio_idx];
    pjmedia_codec_info * strm_fmt = &strm_info->fmt;
    int pt = strm_fmt->pt;

    tCodecPayload rtp_map_in[MAX_CODECS_COUNT];
    memset(rtp_map_in, 0, sizeof(rtp_map_in));
    tCodecPayload rtp_map_out[MAX_CODECS_COUNT];
    memset(rtp_map_out, 0, sizeof(rtp_map_out));

    tAudioCodecDescSettings codec;
    int i;
    for (i=0; i<CODECS_COUNT; i++) {
        tAudioCodecParam cp_def_payload, cp_sdp_name;
        cp_def_payload.key = cpk_def_payload;
        cp_sdp_name.key = cpk_sdp_name;
        tVoiceCodec_GetParam fnGetParam = g_pAudioCodecsList[i].pVoiceCodec->encoder.fnGetParam;
        PJ_ASSERT_RETURN(fnGetParam, PJ_EINVAL);

        /* Assume all these params are available */
        fnGetParam( 0, &cp_def_payload );
        fnGetParam( 0, &cp_sdp_name );

        if (cp_def_payload.u.value == pt ||
            (strm_fmt->encoding_name.slen && strncmp(cp_sdp_name.u.p, strm_fmt->encoding_name.ptr, strm_fmt->encoding_name.slen) == 0)) {
            codec = g_pAudioCodecsList[i];
            break;
        }
    }

    rtp_map_in[0].Payload = rtp_map_out[0].Payload = pt;
    rtp_map_in[0].codec.audio = rtp_map_out[0].codec.audio = codec;

    tResult res;// = SPIRIT_RESULT_OK; 
    PJ_LOG(5,(THIS_FILE, "MM_Voice_SetRtpPayloadMapping"));
    res = MM_Voice_SetRtpPayloadMapping(g_hMM, call->med_channel_id, rtp_map_in, rtp_map_out);
    if (SPIRIT_RESULT_OK != res) {
        PJ_LOG(4,(THIS_FILE, "MM_SetRtpPayloadMapping error [%d]", res));
    }
    
    PJ_LOG(5,(THIS_FILE, "MM_Voice_SetEncoder %d", pt));
    res = MM_Voice_SetEncoder(g_hMM, call->med_channel_id, &codec);
    if (SPIRIT_RESULT_OK != res) {
        PJ_LOG(4,(THIS_FILE, "MM_Voice_SetEncoder error [%d]", res));
    }

    tIpAddr rtcp, rtp;
    rtp.ip.i32 = pj_sockaddr_in_get_addr((pj_sockaddr_in*)&strm_info->rem_addr).s_addr;
    rtp.port = pj_sockaddr_get_port(&strm_info->rem_addr);
    rtcp.ip.i32  = rtp.ip.i32;
    rtcp.port = rtp.port + 1;

    PJ_LOG(5,(THIS_FILE, "MM_Voice_Channel_SetDestAddr %d:%d", rtp.ip.i32, rtp.port));
    res = MM_Voice_Channel_SetDestAddr(g_hMM, call->med_channel_id, &rtp, &rtcp);
    if (SPIRIT_RESULT_OK != res) {
        PJ_LOG(4,(THIS_FILE, "MM_Voice_Channel_SetDestAddr error [%d]", res));
    }

    PJ_LOG(5,(THIS_FILE, "MM_Voice_StartTx"));
    res = MM_Voice_Channel_StartTx(g_hMM, call->med_channel_id);
    if (SPIRIT_RESULT_OK != res) {
        PJ_LOG(4,(THIS_FILE, "MM_Voice_StartTx error [%d]", res));
    }

    PJ_LOG(5,(THIS_FILE, "MM_Voice_StartRx"));
    res = MM_Voice_Channel_StartRx(g_hMM, call->med_channel_id);
    if (SPIRIT_RESULT_OK != res) {
        PJ_LOG(4,(THIS_FILE, "MM_Voice_StartRx error [%d]", res));
    }

	call->med_tp_st = PJSUA_MED_TP_RUNNING;

	/* Call media direction */
	call->media_dir = si->dir;

	/* Call media state */
	if (call->local_hold)
	    call->media_st = PJSUA_CALL_MEDIA_LOCAL_HOLD;
	else if (call->media_dir == PJMEDIA_DIR_DECODING)
	    call->media_st = PJSUA_CALL_MEDIA_REMOTE_HOLD;
	else
	    call->media_st = PJSUA_CALL_MEDIA_ACTIVE;
    }

    /* Print info. */
    {
	char info[80];
	int info_len = 0;
	unsigned i;

	for (i=0; i<sess_info.stream_cnt; ++i) {
	    int len;
	    const char *dir;
	    pjmedia_stream_info *strm_info = &sess_info.stream_info[i];

	    switch (strm_info->dir) {
	    case PJMEDIA_DIR_NONE:
		dir = "inactive";
		break;
	    case PJMEDIA_DIR_ENCODING:
		dir = "sendonly";
		break;
	    case PJMEDIA_DIR_DECODING:
		dir = "recvonly";
		break;
	    case PJMEDIA_DIR_ENCODING_DECODING:
		dir = "sendrecv";
		break;
	    default:
		dir = "unknown";
		break;
	    }
	    len = pj_ansi_sprintf( info+info_len,
				   ", stream #%d: %.*s (%s)", i,
				   (int)strm_info->fmt.encoding_name.slen,
				   strm_info->fmt.encoding_name.ptr,
				   dir);
	    if (len > 0)
		info_len += len;
	}
	PJ_LOG(4,(THIS_FILE,"Media updates%s", info));
    }

    return PJ_SUCCESS;
}

static int find_audio_index(const pjmedia_sdp_session *sdp, 
			    pj_bool_t prefer_srtp)
{
    unsigned i;
    int audio_idx = -1;

    for (i=0; i<sdp->media_count; ++i) {
	const pjmedia_sdp_media *m = sdp->media[i];

	/* Skip if media is not audio */
	if (pj_stricmp2(&m->desc.media, "audio") != 0)
	    continue;

	/* Skip if media is disabled */
	if (m->desc.port == 0)
	    continue;

	/* Skip if transport is not supported */
	if (pj_stricmp2(&m->desc.transport, "RTP/AVP") != 0 &&
	    pj_stricmp2(&m->desc.transport, "RTP/SAVP") != 0)
	{
	    continue;
	}

	if (audio_idx == -1) {
	    audio_idx = i;
	} else {
	    /* We've found multiple candidates. This could happen
	     * e.g. when remote is offering both RTP/SAVP and RTP/AVP,
	     * or when remote for some reason offers two audio.
	     */

	    if (prefer_srtp &&
		pj_stricmp2(&m->desc.transport, "RTP/SAVP")==0)
	    {
		/* Prefer RTP/SAVP when our media transport is SRTP */
		audio_idx = i;
		break;
	    } else if (!prefer_srtp &&
		       pj_stricmp2(&m->desc.transport, "RTP/AVP")==0)
	    {
		/* Prefer RTP/AVP when our media transport is NOT SRTP */
		audio_idx = i;
	    }
	}
    }

    return audio_idx;
}

static pj_status_t spirit_create_sdp(pj_pool_t *pool,
					      unsigned stream_cnt,
					      const pjmedia_sock_info sock_info[],
					      pjmedia_sdp_session **p_sdp)
{
    pj_time_val tv;
    unsigned i;
    const pj_sockaddr *addr0;
    pjmedia_sdp_session *sdp;
    pjmedia_sdp_media *m;
    pjmedia_sdp_attr *attr;

    /* Sanity check arguments */
    PJ_ASSERT_RETURN(pool && p_sdp && stream_cnt, PJ_EINVAL);

    /* Check that there are not too many codecs */
    PJ_ASSERT_RETURN(CODECS_COUNT <= PJMEDIA_MAX_SDP_FMT,
		     PJ_ETOOMANY);

    /* Create and initialize basic SDP session */
    sdp = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_session);

    addr0 = &sock_info[0].rtp_addr_name;

    pj_gettimeofday(&tv);
    sdp->origin.user = pj_str("-");
    sdp->origin.version = sdp->origin.id = tv.sec + 2208988800UL;
    sdp->origin.net_type = STR_IN;

    if (addr0->addr.sa_family == pj_AF_INET()) {
	    sdp->origin.addr_type = STR_IP4;
	    pj_strdup2(pool, &sdp->origin.addr, 
		       pj_inet_ntoa(addr0->ipv4.sin_addr));
    } else if (addr0->addr.sa_family == pj_AF_INET6()) {
	    char tmp_addr[PJ_INET6_ADDRSTRLEN];

	    sdp->origin.addr_type = STR_IP6;
	    pj_strdup2(pool, &sdp->origin.addr, 
		       pj_sockaddr_print(addr0, tmp_addr, sizeof(tmp_addr), 0));

    } else {
	    pj_assert(!"Invalid address family");
	    return PJ_EAFNOTSUP;
    }

    sdp->name = STR_SDP_NAME;

    /* Since we only support one media stream at present, put the
     * SDP connection line in the session level.
     */
    sdp->conn = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_conn);
    sdp->conn->net_type = sdp->origin.net_type;
    sdp->conn->addr_type = sdp->origin.addr_type;
    sdp->conn->addr = sdp->origin.addr;


    /* SDP time and attributes. */
    sdp->time.start = sdp->time.stop = 0;
    sdp->attr_count = 0;

    /* Create media stream 0: */

    sdp->media_count = 1;
    m = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_media);
    sdp->media[0] = m;

    /* Standard media info: */
    pj_strdup(pool, &m->desc.media, &STR_AUDIO);
    m->desc.port = pj_sockaddr_get_port(addr0);
    m->desc.port_count = 1;
    pj_strdup (pool, &m->desc.transport, &STR_RTP_AVP);

    /* Init media line and attribute list. */
    m->desc.fmt_count = 0;
    m->attr_count = 0;

    /* Add "rtcp" attribute */
#if defined(PJMEDIA_HAS_RTCP_IN_SDP) && PJMEDIA_HAS_RTCP_IN_SDP!=0
    if (sock_info->rtcp_addr_name.addr.sa_family != 0) {
	attr = pjmedia_sdp_attr_create_rtcp(pool, &sock_info->rtcp_addr_name);
	if (attr)
	    pjmedia_sdp_attr_add(&m->attr_count, m->attr, attr);
    }
#endif

    /* Add format, rtpmap, and fmtp (when applicable) for each codec */
    for (i=0; i<CODECS_COUNT; ++i) {

	    pjmedia_codec_info codec_info;
	    pjmedia_sdp_rtpmap rtpmap;
	    char tmp_param[3];
	    pjmedia_sdp_attr *attr;
	    pjmedia_codec_param codec_param;
	    pj_str_t *fmt;

        tAudioCodecParam cp_def_payload, cp_sdp_name, cp_samplerate;
        cp_def_payload.key = cpk_def_payload;
        cp_sdp_name.key = cpk_sdp_name;
        cp_samplerate.key = cpk_samplerate;
        tVoiceCodec_GetParam fnGetParam = g_pAudioCodecsList[i].pVoiceCodec->encoder.fnGetParam;
        PJ_ASSERT_RETURN(fnGetParam, PJ_EINVAL);

        /* Assume all these params are available */
        fnGetParam( 0, &cp_def_payload );
        fnGetParam( 0, &cp_sdp_name );
        fnGetParam( 0, &cp_samplerate );

        codec_info.type = PJMEDIA_TYPE_AUDIO;
        codec_info.channel_cnt = 1;
	    codec_info.pt = cp_def_payload.u.value;
        codec_info.encoding_name = pj_str(cp_sdp_name.u.p);
        codec_info.clock_rate = cp_samplerate.u.value;

	    fmt = &m->desc.fmt[m->desc.fmt_count++];

	    fmt->ptr = (char*) pj_pool_alloc(pool, 8);
	    fmt->slen = pj_utoa(codec_info.pt, fmt->ptr);

	    rtpmap.pt = *fmt;
	    rtpmap.enc_name = codec_info.encoding_name;

    #if defined(PJMEDIA_HANDLE_G722_MPEG_BUG) && (PJMEDIA_HANDLE_G722_MPEG_BUG != 0)
	    if (codec_info.pt == PJMEDIA_RTP_PT_G722)
	        rtpmap.clock_rate = 8000;
	    else
	        rtpmap.clock_rate = codec_info.clock_rate;
    #else
	    rtpmap.clock_rate = codec_info.clock_rate;
    #endif

	    /* For audio codecs, rtpmap parameters denotes the number
	     * of channels, which can be omited if the value is 1.
	     */
	    if (codec_info.type == PJMEDIA_TYPE_AUDIO &&
	        codec_info.channel_cnt > 1)
	    {
	        /* Can only support one digit channel count */
	        pj_assert(codec_info.channel_cnt < 10);

	        tmp_param[0] = (char)('0' + codec_info.channel_cnt);

	        rtpmap.param.ptr = tmp_param;
	        rtpmap.param.slen = 1;

	    } else {
	        rtpmap.param.ptr = NULL;
	        rtpmap.param.slen = 0;
	    }

	    if (codec_info.pt >= 96 || PJMEDIA_ADD_RTPMAP_FOR_STATIC_PT) {
	        pjmedia_sdp_rtpmap_to_attr(pool, &rtpmap, &attr);
	        m->attr[m->attr_count++] = attr;
	    }

	    /* Add fmtp params */
	    if (codec_param.setting.dec_fmtp.cnt > 0) {
	        enum { MAX_FMTP_STR_LEN = 160 };
	        char buf[MAX_FMTP_STR_LEN];
	        unsigned buf_len = 0, i;
	        pjmedia_codec_fmtp *dec_fmtp = &codec_param.setting.dec_fmtp;

	        /* Print codec PT */
	        buf_len += pj_ansi_snprintf(buf, 
					    MAX_FMTP_STR_LEN - buf_len, 
					    "%d", 
					    codec_info.pt);

	        for (i = 0; i < dec_fmtp->cnt; ++i) {
		    unsigned test_len = 2;

		    /* Check if buf still available */
		    test_len = dec_fmtp->param[i].val.slen + 
			       dec_fmtp->param[i].name.slen;
		    if (test_len + buf_len >= MAX_FMTP_STR_LEN)
		        return PJ_ETOOBIG;

		    /* Print delimiter */
		    buf_len += pj_ansi_snprintf(&buf[buf_len], 
					        MAX_FMTP_STR_LEN - buf_len,
					        (i == 0?" ":";"));

		    /* Print an fmtp param */
		    if (dec_fmtp->param[i].name.slen)
		        buf_len += pj_ansi_snprintf(
					        &buf[buf_len],
					        MAX_FMTP_STR_LEN - buf_len,
					        "%.*s=%.*s",
					        (int)dec_fmtp->param[i].name.slen,
					        dec_fmtp->param[i].name.ptr,
					        (int)dec_fmtp->param[i].val.slen,
					        dec_fmtp->param[i].val.ptr);
		    else
		        buf_len += pj_ansi_snprintf(&buf[buf_len], 
					        MAX_FMTP_STR_LEN - buf_len,
					        "%.*s", 
					        (int)dec_fmtp->param[i].val.slen,
					        dec_fmtp->param[i].val.ptr);
	        }

	        attr = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_attr);

	        attr->name = pj_str("fmtp");
	        attr->value = pj_strdup3(pool, buf);
	        m->attr[m->attr_count++] = attr;
	    }
    }

    /* Add sendrecv attribute. */
    attr = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_attr);
    attr->name = STR_SENDRECV;
    m->attr[m->attr_count++] = attr;

#if defined(PJMEDIA_RTP_PT_TELEPHONE_EVENTS) && \
    PJMEDIA_RTP_PT_TELEPHONE_EVENTS != 0
    /*
     * Add support telephony event
     */
    if (TELEPHONE_EVENT) {
	    m->desc.fmt[m->desc.fmt_count++] =
	        pj_str(PJMEDIA_RTP_PT_TELEPHONE_EVENTS_STR);

	    /* Add rtpmap. */
	    attr = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_attr);
	    attr->name = pj_str("rtpmap");
	    attr->value = pj_str(PJMEDIA_RTP_PT_TELEPHONE_EVENTS_STR
			         " telephone-event/8000");
	    m->attr[m->attr_count++] = attr;

	    /* Add fmtp */
	    attr = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_attr);
	    attr->name = pj_str("fmtp");
	    attr->value = pj_str(PJMEDIA_RTP_PT_TELEPHONE_EVENTS_STR " 0-15");
	    m->attr[m->attr_count++] = attr;
    }
#endif

    /* Done */
    *p_sdp = sdp;

    return PJ_SUCCESS;
}

static pj_status_t spirit_session_info_from_sdp( pj_pool_t *pool,
			       unsigned max_streams,
			       pjmedia_session_info *si,
			       const pjmedia_sdp_session *local,
			       const pjmedia_sdp_session *remote)
{
    unsigned i;

    PJ_ASSERT_RETURN(pool && si && local && remote, PJ_EINVAL);

    si->stream_cnt = max_streams;
    if (si->stream_cnt > local->media_count)
    	si->stream_cnt = local->media_count;

    for (i=0; i<si->stream_cnt; ++i) {
	    pj_status_t status;

	    status = spirit_stream_info_from_sdp(&si->stream_info[i], pool,
					           local, remote, i);
	    if (status != PJ_SUCCESS)
	        return status;
    }

    return PJ_SUCCESS;
}

static void parse_fmtp( pj_pool_t *pool,
			const pjmedia_sdp_media *m,
			unsigned pt,
			pjmedia_codec_fmtp *fmtp)
{
    const pjmedia_sdp_attr *attr;
    pjmedia_sdp_fmtp sdp_fmtp;
    char *p, *p_end, fmt_buf[8];
    pj_str_t fmt;

    pj_assert(m && fmtp);

    pj_bzero(fmtp, sizeof(pjmedia_codec_fmtp));

    /* Get "fmtp" attribute for the format */
    pj_ansi_sprintf(fmt_buf, "%d", pt);
    fmt = pj_str(fmt_buf);
    attr = pjmedia_sdp_media_find_attr2(m, "fmtp", &fmt);
    if (attr == NULL)
	return;

    /* Parse "fmtp" attribute */
    if (pjmedia_sdp_attr_get_fmtp(attr, &sdp_fmtp) != PJ_SUCCESS)
	return;

    /* Prepare parsing */
    p = sdp_fmtp.fmt_param.ptr;
    p_end = p + sdp_fmtp.fmt_param.slen;

    /* Parse */
    while (p < p_end) {
	char *token, *start, *end;

	/* Skip whitespaces */
	while (p < p_end && (*p == ' ' || *p == '\t')) ++p;
	if (p == p_end)
	    break;

	/* Get token */
	start = p;
	while (p < p_end && *p != ';' && *p != '=') ++p;
	end = p - 1;

	/* Right trim */
	while (end >= start && (*end == ' '  || *end == '\t' || 
				*end == '\r' || *end == '\n' ))
	    --end;

	/* Forward a char after trimming */
	++end;

	/* Store token */
	if (end > start) {
	    token = (char*)pj_pool_alloc(pool, end - start);
	    pj_ansi_strncpy(token, start, end - start);
	    if (*p == '=')
		/* Got param name */
		pj_strset(&fmtp->param[fmtp->cnt].name, token, end - start);
	    else
		/* Got param value */
		pj_strset(&fmtp->param[fmtp->cnt++].val, token, end - start);
	} else if (*p != '=') {
	    ++fmtp->cnt;
	}

	/* Next */
	++p;
    }
}

static pj_status_t spirit_stream_info_from_sdp(
					   pjmedia_stream_info *si,
					   pj_pool_t *pool,
					   const pjmedia_sdp_session *local,
					   const pjmedia_sdp_session *remote,
					   unsigned stream_idx)
{
    const pjmedia_sdp_attr *attr;
    const pjmedia_sdp_media *local_m;
    const pjmedia_sdp_media *rem_m;
    const pjmedia_sdp_conn *local_conn;
    const pjmedia_sdp_conn *rem_conn;
    int rem_af, local_af;
    pj_sockaddr local_addr;
    pjmedia_sdp_rtpmap *rtpmap;
    unsigned i, pt, fmti;
    pj_status_t status;

    /* Validate arguments: */
    PJ_ASSERT_RETURN(pool && si && local && remote, PJ_EINVAL);
    PJ_ASSERT_RETURN(stream_idx < local->media_count, PJ_EINVAL);
    PJ_ASSERT_RETURN(stream_idx < remote->media_count, PJ_EINVAL);

    /* Keep SDP shortcuts */
    local_m = local->media[stream_idx];
    rem_m = remote->media[stream_idx];

    local_conn = local_m->conn ? local_m->conn : local->conn;
    if (local_conn == NULL)
	    return PJMEDIA_SDP_EMISSINGCONN;

    rem_conn = rem_m->conn ? rem_m->conn : remote->conn;
    if (rem_conn == NULL)
	    return PJMEDIA_SDP_EMISSINGCONN;


    /* Reset: */
    pj_bzero(si, sizeof(*si));

#if PJMEDIA_HAS_RTCP_XR && PJMEDIA_STREAM_ENABLE_XR
    /* Set default RTCP XR enabled/disabled */
    si->rtcp_xr_enabled = PJ_TRUE;
#endif

    /* Media type: */

    if (pj_stricmp(&local_m->desc.media, &ID_AUDIO) == 0) {

	si->type = PJMEDIA_TYPE_AUDIO;

    } else if (pj_stricmp(&local_m->desc.media, &ID_VIDEO) == 0) {

	si->type = PJMEDIA_TYPE_VIDEO;

    } else {

	si->type = PJMEDIA_TYPE_UNKNOWN;

	/* Avoid rejecting call because of unrecognized media, 
	 * just return PJ_SUCCESS, this media will be deactivated later.
	 */
	//return PJMEDIA_EINVALIMEDIATYPE;
	return PJ_SUCCESS;

    }

    /* Transport protocol */

    /* At this point, transport type must be compatible, 
     * the transport instance will do more validation later.
     */
    status = pjmedia_sdp_transport_cmp(&rem_m->desc.transport, 
				       &local_m->desc.transport);
    if (status != PJ_SUCCESS)
	return PJMEDIA_SDPNEG_EINVANSTP;

    if (pj_stricmp(&local_m->desc.transport, &ID_RTP_AVP) == 0) {

	si->proto = PJMEDIA_TP_PROTO_RTP_AVP;

    } else if (pj_stricmp(&local_m->desc.transport, &ID_RTP_SAVP) == 0) {

	si->proto = PJMEDIA_TP_PROTO_RTP_SAVP;

    } else {

	si->proto = PJMEDIA_TP_PROTO_UNKNOWN;
	return PJ_SUCCESS;
    }


    /* Check address family in remote SDP */
    rem_af = pj_AF_UNSPEC();
    if (pj_stricmp(&rem_conn->net_type, &ID_IN)==0) {
	if (pj_stricmp(&rem_conn->addr_type, &ID_IP4)==0) {
	    rem_af = pj_AF_INET();
	} else if (pj_stricmp(&rem_conn->addr_type, &ID_IP6)==0) {
	    rem_af = pj_AF_INET6();
	}
    }

    if (rem_af==pj_AF_UNSPEC()) {
	/* Unsupported address family */
	return PJ_EAFNOTSUP;
    }

    /* Set remote address: */
    status = pj_sockaddr_init(rem_af, &si->rem_addr, &rem_conn->addr, 
			      rem_m->desc.port);
    if (status != PJ_SUCCESS) {
	/* Invalid IP address. */
	return PJMEDIA_EINVALIDIP;
    }

    /* Check address family of local info */
    local_af = pj_AF_UNSPEC();
    if (pj_stricmp(&local_conn->net_type, &ID_IN)==0) {
	if (pj_stricmp(&local_conn->addr_type, &ID_IP4)==0) {
	    local_af = pj_AF_INET();
	} else if (pj_stricmp(&local_conn->addr_type, &ID_IP6)==0) {
	    local_af = pj_AF_INET6();
	}
    }

    if (local_af==pj_AF_UNSPEC()) {
	/* Unsupported address family */
	return PJ_SUCCESS;
    }

    /* Set remote address: */
    status = pj_sockaddr_init(local_af, &local_addr, &local_conn->addr, 
			      local_m->desc.port);
    if (status != PJ_SUCCESS) {
	/* Invalid IP address. */
	return PJMEDIA_EINVALIDIP;
    }

    /* Local and remote address family must match */
    if (local_af != rem_af)
	return PJ_EAFNOTSUP;

    /* Media direction: */

    if (local_m->desc.port == 0 || 
	pj_sockaddr_has_addr(&local_addr)==PJ_FALSE ||
	pj_sockaddr_has_addr(&si->rem_addr)==PJ_FALSE ||
	pjmedia_sdp_media_find_attr(local_m, &STR_INACTIVE, NULL)!=NULL)
    {
	/* Inactive stream. */

	si->dir = PJMEDIA_DIR_NONE;

    } else if (pjmedia_sdp_media_find_attr(local_m, &STR_SENDONLY, NULL)!=NULL) {

	/* Send only stream. */

	si->dir = PJMEDIA_DIR_ENCODING;

    } else if (pjmedia_sdp_media_find_attr(local_m, &STR_RECVONLY, NULL)!=NULL) {

	/* Recv only stream. */

	si->dir = PJMEDIA_DIR_DECODING;

    } else {

	/* Send and receive stream. */

	si->dir = PJMEDIA_DIR_ENCODING_DECODING;

    }

    /* No need to do anything else if stream is rejected */
    if (local_m->desc.port == 0) {
	return PJ_SUCCESS;
    }

    /* If "rtcp" attribute is present in the SDP, set the RTCP address
     * from that attribute. Otherwise, calculate from RTP address.
     */
    attr = pjmedia_sdp_attr_find2(rem_m->attr_count, rem_m->attr,
				  "rtcp", NULL);
    if (attr) {
	pjmedia_sdp_rtcp_attr rtcp;
	status = pjmedia_sdp_attr_get_rtcp(attr, &rtcp);
	if (status == PJ_SUCCESS) {
	    if (rtcp.addr.slen) {
		status = pj_sockaddr_init(rem_af, &si->rem_rtcp, &rtcp.addr,
					  (pj_uint16_t)rtcp.port);
	    } else {
		pj_sockaddr_init(rem_af, &si->rem_rtcp, NULL, 
				 (pj_uint16_t)rtcp.port);
		pj_memcpy(pj_sockaddr_get_addr(&si->rem_rtcp),
		          pj_sockaddr_get_addr(&si->rem_addr),
			  pj_sockaddr_get_addr_len(&si->rem_addr));
	    }
	}
    }
    
    if (!pj_sockaddr_has_addr(&si->rem_rtcp)) {
	int rtcp_port;

	pj_memcpy(&si->rem_rtcp, &si->rem_addr, sizeof(pj_sockaddr));
	rtcp_port = pj_sockaddr_get_port(&si->rem_addr) + 1;
	pj_sockaddr_set_port(&si->rem_rtcp, (pj_uint16_t)rtcp_port);
    }


    /* Get the payload number for receive channel. */
    /*
       Previously we used to rely on fmt[0] being the selected codec,
       but some UA sends telephone-event as fmt[0] and this would
       cause assert failure below.

       Thanks Chris Hamilton <chamilton .at. cs.dal.ca> for this patch.

    // And codec must be numeric!
    if (!pj_isdigit(*local_m->desc.fmt[0].ptr) || 
	!pj_isdigit(*rem_m->desc.fmt[0].ptr))
    {
	return PJMEDIA_EINVALIDPT;
    }

    pt = pj_strtoul(&local_m->desc.fmt[0]);
    pj_assert(PJMEDIA_RTP_PT_TELEPHONE_EVENTS==0 ||
	      pt != PJMEDIA_RTP_PT_TELEPHONE_EVENTS);
    */

    /* This is to suppress MSVC warning about uninitialized var */
    pt = 0;

    /* Find the first codec which is not telephone-event */
    for ( fmti = 0; fmti < local_m->desc.fmt_count; ++fmti ) {
	if ( !pj_isdigit(*local_m->desc.fmt[fmti].ptr) )
	    return PJMEDIA_EINVALIDPT;
	pt = pj_strtoul(&local_m->desc.fmt[fmti]);
	if ( PJMEDIA_RTP_PT_TELEPHONE_EVENTS == 0 ||
		pt != PJMEDIA_RTP_PT_TELEPHONE_EVENTS )
		break;
    }
    if ( fmti >= local_m->desc.fmt_count )
	return PJMEDIA_EINVALIDPT;

    /* Get codec info.
     * For static payload types, get the info from codec manager.
     * For dynamic payload types, MUST get the rtpmap.
     */
    if (pt < 96) {
	pj_bool_t has_rtpmap;

	rtpmap = NULL;
	has_rtpmap = PJ_TRUE;

	attr = pjmedia_sdp_media_find_attr(local_m, &ID_RTPMAP, 
					   &local_m->desc.fmt[fmti]);
	if (attr == NULL) {
	    has_rtpmap = PJ_FALSE;
	}
	if (attr != NULL) {
	    status = pjmedia_sdp_attr_to_rtpmap(pool, attr, &rtpmap);
	    if (status != PJ_SUCCESS)
		has_rtpmap = PJ_FALSE;
	}

	/* Build codec format info: */
	if (has_rtpmap) {
	    si->fmt.type = si->type;
	    si->fmt.pt = pj_strtoul(&local_m->desc.fmt[fmti]);
	    pj_strdup(pool, &si->fmt.encoding_name, &rtpmap->enc_name);
	    si->fmt.clock_rate = rtpmap->clock_rate;
	    
#if defined(PJMEDIA_HANDLE_G722_MPEG_BUG) && (PJMEDIA_HANDLE_G722_MPEG_BUG != 0)
	    /* The session info should have the actual clock rate, because 
	     * this info is used for calculationg buffer size, etc in stream 
	     */
	    if (si->fmt.pt == PJMEDIA_RTP_PT_G722)
		si->fmt.clock_rate = 16000;
#endif

	    /* For audio codecs, rtpmap parameters denotes the number of
	     * channels.
	     */
	    if (si->type == PJMEDIA_TYPE_AUDIO && rtpmap->param.slen) {
		si->fmt.channel_cnt = (unsigned) pj_strtoul(&rtpmap->param);
	    } else {
		si->fmt.channel_cnt = 1;
	    }

	} else {	    
	    const pjmedia_codec_info *p_info;

        //TODO
	    /*status = pjmedia_codec_mgr_get_codec_info( mgr, pt, &p_info);
	    if (status != PJ_SUCCESS)
		return status;*/

	    pj_memcpy(&si->fmt, p_info, sizeof(pjmedia_codec_info));
	}

	/* For static payload type, pt's are symetric */
	si->tx_pt = pt;

    } else {

	attr = pjmedia_sdp_media_find_attr(local_m, &ID_RTPMAP, 
					   &local_m->desc.fmt[fmti]);
	if (attr == NULL)
	    return PJMEDIA_EMISSINGRTPMAP;

	status = pjmedia_sdp_attr_to_rtpmap(pool, attr, &rtpmap);
	if (status != PJ_SUCCESS)
	    return status;

	/* Build codec format info: */

	si->fmt.type = si->type;
	si->fmt.pt = pj_strtoul(&local_m->desc.fmt[fmti]);
	pj_strdup(pool, &si->fmt.encoding_name, &rtpmap->enc_name);
	si->fmt.clock_rate = rtpmap->clock_rate;

	/* For audio codecs, rtpmap parameters denotes the number of
	 * channels.
	 */
	if (si->type == PJMEDIA_TYPE_AUDIO && rtpmap->param.slen) {
	    si->fmt.channel_cnt = (unsigned) pj_strtoul(&rtpmap->param);
	} else {
	    si->fmt.channel_cnt = 1;
	}

	/* Determine payload type for outgoing channel, by finding
	 * dynamic payload type in remote SDP that matches the answer.
	 */
	si->tx_pt = 0xFFFF;
	for (i=0; i<rem_m->desc.fmt_count; ++i) {
	    unsigned rpt;
	    pjmedia_sdp_attr *r_attr;
	    pjmedia_sdp_rtpmap r_rtpmap;

	    rpt = pj_strtoul(&rem_m->desc.fmt[i]);
	    if (rpt < 96)
		continue;

	    r_attr = pjmedia_sdp_media_find_attr(rem_m, &ID_RTPMAP,
						 &rem_m->desc.fmt[i]);
	    if (!r_attr)
		continue;

	    if (pjmedia_sdp_attr_get_rtpmap(r_attr, &r_rtpmap) != PJ_SUCCESS)
		continue;

	    if (!pj_stricmp(&rtpmap->enc_name, &r_rtpmap.enc_name) &&
		rtpmap->clock_rate == r_rtpmap.clock_rate)
	    {
		/* Found matched codec. */
		si->tx_pt = rpt;

		break;
	    }
	}

	if (si->tx_pt == 0xFFFF)
	    return PJMEDIA_EMISSINGRTPMAP;
    }

  
    /* Now that we have codec info, get the codec param. */
    si->param = PJ_POOL_ALLOC_T(pool, pjmedia_codec_param);
    //TODO: status = pjmedia_codec_mgr_get_default_param(mgr, &si->fmt, si->param);

    /* Get remote fmtp for our encoder. */
    parse_fmtp(pool, rem_m, si->tx_pt, &si->param->setting.enc_fmtp);

    /* Get local fmtp for our decoder. */
    parse_fmtp(pool, local_m, si->fmt.pt, &si->param->setting.dec_fmtp);

    /* Get the remote ptime for our encoder. */
    attr = pjmedia_sdp_attr_find2(rem_m->attr_count, rem_m->attr,
				  "ptime", NULL);
    if (attr) {
	pj_str_t tmp_val = attr->value;
	unsigned frm_per_pkt;
 
	pj_strltrim(&tmp_val);

	/* Round up ptime when the specified is not multiple of frm_ptime */
	frm_per_pkt = (pj_strtoul(&tmp_val) + si->param->info.frm_ptime/2) /
		      si->param->info.frm_ptime;
	if (frm_per_pkt != 0) {
            si->param->setting.frm_per_pkt = (pj_uint8_t)frm_per_pkt;
        }
    }

    /* Get remote maxptime for our encoder. */
    attr = pjmedia_sdp_attr_find2(rem_m->attr_count, rem_m->attr,
				  "maxptime", NULL);
    if (attr) {
	pj_str_t tmp_val = attr->value;

	pj_strltrim(&tmp_val);
	si->tx_maxptime = pj_strtoul(&tmp_val);
    }

    /* When direction is NONE (it means SDP negotiation has failed) we don't
     * need to return a failure here, as returning failure will cause
     * the whole SDP to be rejected. See ticket #:
     *	http://
     *
     * Thanks Alain Totouom 
     */
    if (status != PJ_SUCCESS && si->dir != PJMEDIA_DIR_NONE)
	return status;


    /* Get incomming payload type for telephone-events */
    si->rx_event_pt = -1;
    for (i=0; i<local_m->attr_count; ++i) {
	pjmedia_sdp_rtpmap r;

	attr = local_m->attr[i];
	if (pj_strcmp(&attr->name, &ID_RTPMAP) != 0)
	    continue;
	if (pjmedia_sdp_attr_get_rtpmap(attr, &r) != PJ_SUCCESS)
	    continue;
	if (pj_strcmp(&r.enc_name, &ID_TELEPHONE_EVENT) == 0) {
	    si->rx_event_pt = pj_strtoul(&r.pt);
	    break;
	}
    }

    /* Get outgoing payload type for telephone-events */
    si->tx_event_pt = -1;
    for (i=0; i<rem_m->attr_count; ++i) {
	pjmedia_sdp_rtpmap r;

	attr = rem_m->attr[i];
	if (pj_strcmp(&attr->name, &ID_RTPMAP) != 0)
	    continue;
	if (pjmedia_sdp_attr_get_rtpmap(attr, &r) != PJ_SUCCESS)
	    continue;
	if (pj_strcmp(&r.enc_name, &ID_TELEPHONE_EVENT) == 0) {
	    si->tx_event_pt = pj_strtoul(&r.pt);
	    break;
	}
    }

    /* Leave SSRC to random. */
    si->ssrc = pj_rand();

    /* Set default jitter buffer parameter. */
    si->jb_init = si->jb_max = si->jb_min_pre = si->jb_max_pre = -1;

    return PJ_SUCCESS;
}

PJ_DECL(unsigned) pjsua_conf_get_max_ports(void)
{
    return PJ_SUCCESS;
}

PJ_DECL(unsigned) pjsua_conf_get_active_ports(void)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_enum_conf_ports(pjsua_conf_port_id id[],
					   unsigned *count)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_conf_get_port_info( pjsua_conf_port_id port_id,
					       pjsua_conf_port_info *info)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_conf_add_port(pj_pool_t *pool,
					 pjmedia_port *port,
					 pjsua_conf_port_id *p_id)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_conf_remove_port(pjsua_conf_port_id port_id)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_conf_connect(pjsua_conf_port_id source,
					pjsua_conf_port_id sink)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_conf_disconnect(pjsua_conf_port_id source,
					   pjsua_conf_port_id sink)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_conf_adjust_tx_level(pjsua_conf_port_id slot,
						float level)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_conf_adjust_rx_level(pjsua_conf_port_id slot,
						float level)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_conf_get_signal_level(pjsua_conf_port_id slot,
						 unsigned *tx_level,
						 unsigned *rx_level)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_player_create(const pj_str_t *filename,
					 unsigned options,
					 pjsua_player_id *p_id)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_playlist_create(const pj_str_t file_names[],
					   unsigned file_count,
					   const pj_str_t *label,
					   unsigned options,
					   pjsua_player_id *p_id)
{
    return PJ_SUCCESS;
}

PJ_DECL(pjsua_conf_port_id) pjsua_player_get_conf_port(pjsua_player_id id)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_player_get_port(pjsua_player_id id,
					   pjmedia_port **p_port)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_player_set_pos(pjsua_player_id id,
					  pj_uint32_t samples)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_player_destroy(pjsua_player_id id)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_recorder_create(const pj_str_t *filename,
					   unsigned enc_type,
					   void *enc_param,
					   pj_ssize_t max_size,
					   unsigned options,
					   pjsua_recorder_id *p_id)
{
    return PJ_SUCCESS;
}

PJ_DECL(pjsua_conf_port_id) pjsua_recorder_get_conf_port(pjsua_recorder_id id)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_recorder_get_port(pjsua_recorder_id id,
					     pjmedia_port **p_port)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_recorder_destroy(pjsua_recorder_id id)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_enum_aud_devs(pjmedia_aud_dev_info info[],
					 unsigned *count)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_enum_snd_devs(pjmedia_snd_dev_info info[],
					 unsigned *count)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_get_snd_dev(int *capture_dev,
				       int *playback_dev)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_set_snd_dev(int capture_dev,
				       int playback_dev)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_set_null_snd_dev(void)
{
    return PJ_SUCCESS;
}

PJ_DECL(pjmedia_port*) pjsua_set_no_snd_dev(void)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_set_ec(unsigned tail_ms, unsigned options)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_get_ec_tail(unsigned *p_tail_ms)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_bool_t) pjsua_snd_is_active(void)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_snd_set_setting(pjmedia_aud_dev_cap cap,
					   const void *pval,
					   pj_bool_t keep)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_snd_get_setting(pjmedia_aud_dev_cap cap,
					   void *pval)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_enum_codecs( pjsua_codec_info id[],
				        unsigned *count )
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_codec_set_priority( const pj_str_t *codec_id,
					       pj_uint8_t priority )
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_codec_get_param( const pj_str_t *codec_id,
					    pjmedia_codec_param *param )
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) pjsua_codec_set_param( const pj_str_t *codec_id,
					    const pjmedia_codec_param *param)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) 
pjsua_media_transports_create(const pjsua_transport_config *cfg)
{
    return PJ_SUCCESS;
}

PJ_DECL(pj_status_t) 
pjsua_media_transports_attach( pjsua_media_transport tp[],
			       unsigned count,
			       pj_bool_t auto_delete)
{
    return PJ_SUCCESS;
}
