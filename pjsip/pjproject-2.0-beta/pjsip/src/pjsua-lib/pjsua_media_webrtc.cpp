/* $Id: pjsua_media.c 3929 2011-12-28 09:52:07Z nanang $ */
/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pjsua-lib/pjsua.h>
#include <pjsua-lib/pjsua_internal.h>
#include <pjmedia/sdp.h>
#include <pjmedia/codec.h>
#include <pjmedia/format.h>
#include <voice_engine/main/interface/voe_base.h>
#include <voice_engine/main/interface/voe_rtp_rtcp.h>
#include <voice_engine/main/interface/voe_codec.h>
#include <voice_engine/main/interface/voe_audio_processing.h>
#include <voice_engine/main/interface/voe_volume_control.h>
#include <voice_engine/main/interface/voe_hardware.h>
#include <voice_engine/main/interface/voe_dtmf.h>
#include <common_types.h>
#include <system_wrappers/interface/trace.h>

#define THIS_FILE		"pjsua_media_webrtc.cpp"

#define DEFAULT_RTP_PORT	4000

#define NULL_SND_DEV_ID		-99

#define RTP_MAX_RETRIES 10
#define MIN_RTP_PORT 4000
#define MAX_RTP_PORT 4100

#ifndef PJSUA_REQUIRE_CONSECUTIVE_RTCP_PORT
#   define PJSUA_REQUIRE_CONSECUTIVE_RTCP_PORT	0
#endif

/* Next RTP port to be used */
static pj_uint16_t next_rtp_port;

/* Open sound dev */
static pj_status_t open_snd_dev(pjmedia_snd_port_param *param);
/* Close existing sound device */
static void close_snd_dev(void);
/* Create audio device param */
static pj_status_t create_aud_param(pjmedia_aud_param *param,
				    pjmedia_aud_dev_index capture_dev,
				    pjmedia_aud_dev_index playback_dev,
				    unsigned clock_rate,
				    unsigned channel_count,
				    unsigned samples_per_frame,
				    unsigned bits_per_sample);


static void pjsua_media_config_dup(pj_pool_t *pool,
				   pjsua_media_config *dst,
				   const pjsua_media_config *src)
{
    pj_memcpy(dst, src, sizeof(*src));
    pj_strdup(pool, &dst->turn_server, &src->turn_server);
    pj_stun_auth_cred_dup(pool, &dst->turn_auth_cred, &src->turn_auth_cred);
}

static struct codec_desc {
    int              enabled;           /* Is this codec enabled?           */
    const char      *name;              /* Codec name.                      */
    pj_uint8_t       pt;                /* Payload type.                    */
    pjmedia_format_id fmt_id;           /* Source format.                   */
    unsigned         clock_rate;        /* Codec's clock rate.              */
    unsigned         channel_count;     /* Codec's channel count.           */
    unsigned         samples_per_frame; /* Codec's samples count.           */
    unsigned         def_bitrate;       /* Default bitrate of this codec.   */
    unsigned         max_bitrate;       /* Maximum bitrate of this codec.   */
    pj_uint8_t       frm_per_pkt;       /* Default num of frames per packet.*/
    pj_uint8_t       vad;               /* VAD enabled/disabled.            */
    pj_uint8_t       plc;               /* PLC enabled/disabled.            */
    pjmedia_codec_fmtp dec_fmtp;        /* Decoder's fmtp params.           */
}
codec_desc[] =
{
    {1, "ISAC",     103,   (pjmedia_format_id)PJMEDIA_FORMAT_PACK('I', 'S', 'A', 'C'),
        16000, 1, 160,
        7400, 44000, 2, 1, 1
    },
    {1, "iLBC",     102,      PJMEDIA_FORMAT_ILBC,
        8000, 1,  240,
        13333, 15200, 1, 1, 1,
        {1, {{{"mode", 4}, {"30", 2}}} }
    },
    {1, "PCMU",     PJMEDIA_RTP_PT_PCMU,      PJMEDIA_FORMAT_PCMU,
        8000, 1,  80,
        64000, 64000, 2, 1, 1
    },
    {1, "PCMA",     PJMEDIA_RTP_PT_PCMA,      PJMEDIA_FORMAT_PCMA,
        8000, 1,  80,
        64000, 64000, 2, 1, 1
    },
};

static pj_status_t test_alloc( pjmedia_codec_factory *factory, 
                              const pjmedia_codec_info *info )
{
    return PJ_SUCCESS;
}

static pj_status_t default_attr ( pjmedia_codec_factory *factory, 
                                 const pjmedia_codec_info *id, 
                                 pjmedia_codec_param *attr )
{
    return PJ_SUCCESS;
}

static pj_status_t alloc_codec( pjmedia_codec_factory *factory, 
                               const pjmedia_codec_info *id,
                               pjmedia_codec **p_codec)
{
    return PJ_SUCCESS;
}

static pj_status_t dealloc_codec( pjmedia_codec_factory *factory, 
                                 pjmedia_codec *codec )
{
    return PJ_SUCCESS;
}

static pj_status_t enum_codecs( pjmedia_codec_factory *factory,
                               unsigned *count,
                               pjmedia_codec_info codecs[])
{
    unsigned max;
    unsigned i;
    
    PJ_UNUSED_ARG(factory);
    PJ_ASSERT_RETURN(codecs && *count > 0, PJ_EINVAL);
    
    max = *count;
    
    for (i = 0, *count = 0; i < PJ_ARRAY_SIZE(codec_desc) && *count < max; ++i)
    {
        pj_bzero(&codecs[*count], sizeof(pjmedia_codec_info));
        codecs[*count].encoding_name = pj_str((char*)codec_desc[i].name);
        codecs[*count].pt = codec_desc[i].pt;
        codecs[*count].type = PJMEDIA_TYPE_AUDIO;
        codecs[*count].clock_rate = codec_desc[i].clock_rate;
        codecs[*count].channel_cnt = codec_desc[i].channel_count;
        
        ++*count;
    }
    
    return PJ_SUCCESS;
}

static pj_status_t codec_factory_destroy()
{
    return PJ_SUCCESS;
}

/* Definition for passthrough codecs factory operations. */
static pjmedia_codec_factory_op codec_factory_op =
{
    &test_alloc,
    &default_attr,
    &enum_codecs,
    &alloc_codec,
    &dealloc_codec,
    &codec_factory_destroy
};

/* Passthrough codecs factory */
static struct codec_factory {
    pjmedia_codec_factory    base;
    pjmedia_endpt           *endpt;
    pj_pool_t               *pool;
    pj_mutex_t              *mutex;
} codec_factory;


        
using namespace webrtc;

static VoiceEngine* voe;

class NSLogTraceCallback: public TraceCallback
{
public:
    virtual void Print(const TraceLevel level,
                       const char *traceString,
                       const int length) {
        if (!pj_thread_is_registered()) {
            pj_thread_desc desc;
            pj_thread_t *this_thread;

            pj_bzero(desc, sizeof(desc));

            pj_thread_register("pjsua_media_webrtc thread", desc, &this_thread);
        }
        PJ_LOG(5,(THIS_FILE, traceString));
    }
public:
    virtual ~NSLogTraceCallback() {}
    NSLogTraceCallback() {}
};


/**
 * Init media subsystems.
 */
pj_status_t pjsua_media_subsys_init(const pjsua_media_config *cfg)
{
    PJ_LOG(3,(THIS_FILE, "pjsua_media_subsys_init"));
    
    pj_str_t codec_id = {NULL, 0};
    pj_status_t status;
    
    Trace::CreateTrace();
    Trace::SetLevelFilter(kTraceAll);
    Trace::SetTraceCallback(new NSLogTraceCallback());

    PJ_LOG(5,(THIS_FILE, "VoiceEngine::Create()"));
    voe = VoiceEngine::Create();
    
    VoEBase* base = VoEBase::GetInterface(voe);
    
    PJ_LOG(5,(THIS_FILE, "VoEBase::Init"));
    status = base->Init();
    base->Release();
    if (status != PJ_SUCCESS) {
        pjsua_perror(THIS_FILE, 
                     "Media stack initialization has returned error", 
                     status);
        goto on_error;
    }
        
    /* Create media endpoint. */
    status = pjmedia_endpt_create(&pjsua_var.cp.factory, 
                                  pjsua_var.media_cfg.has_ioqueue? NULL :
                                  pjsip_endpt_get_ioqueue(pjsua_var.endpt),
                                  pjsua_var.media_cfg.thread_cnt,
                                  &pjsua_var.med_endpt);
    if (status != PJ_SUCCESS) {
        pjsua_perror(THIS_FILE, 
                     "Media stack initialization has returned error", 
                     status);
        goto on_error;
    } 
    
    PJ_LOG(5,(THIS_FILE, "pjmedia_endpt_get_codec_mgr"));
    pjmedia_codec_mgr *codec_mgr;
    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);
    
    codec_factory.base.op = &codec_factory_op;
    codec_factory.base.factory_data = NULL;
    codec_factory.endpt = pjsua_var.med_endpt;
    
    codec_factory.pool = pjmedia_endpt_create_pool(pjsua_var.med_endpt, "webrtc codecs",
                                                   4000, 4000);
    if (!codec_factory.pool)
        return PJ_ENOMEM;
    
    /* Create mutex. */
    status = pj_mutex_create_simple(codec_factory.pool, "webrtc codecs",
                                    &codec_factory.mutex);
    if (status != PJ_SUCCESS)
        goto on_error;
    
    status = pjmedia_codec_mgr_register_factory(codec_mgr,
                                                &codec_factory.base);
    
    codec_id = pj_str("ISAC/16000");
    pjmedia_codec_mgr_set_codec_priority(
                                         codec_mgr,
                                         &codec_id, PJMEDIA_CODEC_PRIO_NORMAL+1);
    
    codec_id = pj_str("iLBC/8000");
    pjmedia_codec_mgr_set_codec_priority(
                                         codec_mgr,
                                         &codec_id, PJMEDIA_CODEC_PRIO_NORMAL+1);
    
    codec_id = pj_str("PCMU/8000");
    pjmedia_codec_mgr_set_codec_priority(
                                         codec_mgr,
                                         &codec_id, PJMEDIA_CODEC_PRIO_NORMAL+1);
    
    codec_id = pj_str("PCMA/8000");
    pjmedia_codec_mgr_set_codec_priority(
                                         codec_mgr,
                                         &codec_id, PJMEDIA_CODEC_PRIO_NORMAL+1);

    next_rtp_port = MIN_RTP_PORT;


    /* Video */
#if PJMEDIA_HAS_VIDEO
    PJ_LOG(2,(THIS_FILE, "Execució si pjmedia_has_video. Carregant pjsua_vid_subsys_init..."));
    status = pjsua_vid_subsys_init();
    if (status != PJ_SUCCESS)
        goto on_error;
#endif


    return PJ_SUCCESS;

on_error:
    return status;
}

/*
 * Start pjsua media subsystem.
 */
pj_status_t pjsua_media_subsys_start(void)
{
    PJ_LOG(2,(THIS_FILE, "pjsua_media_subsys_start"));

    pj_status_t status;

    /* Video */
#if PJMEDIA_HAS_VIDEO
    PJ_LOG(2,(THIS_FILE, "pjsua_media_subsys_start. Execució si pjmedia_has_video"));
    status = pjsua_vid_subsys_start();
    if (status != PJ_SUCCESS) {
        pj_log_pop_indent();
        return status;
    }
#endif

    return PJ_SUCCESS;
}


/*
 * Destroy pjsua media subsystem.
 */
pj_status_t pjsua_media_subsys_destroy(unsigned flags)
{
    PJ_LOG(3,(THIS_FILE, "pjsua_media_subsys_destroy"));
    
    VoEBase* base = VoEBase::GetInterface(voe);
    
    PJ_LOG(5,(THIS_FILE, "VoEBase::Terminate"));
    base->Terminate();
    
    base->Release();    

#	if PJMEDIA_HAS_VIDEO
    PJ_LOG(2,(THIS_FILE, "pjsua_media_subsys_destroy: Execució si pjmedia_has_video"));
    pjsua_vid_subsys_destroy();
#	endif

    return PJ_SUCCESS;
}

/*
 * Create RTP and RTCP socket pair, and possibly resolve their public
 * address via STUN.
 */
static pj_status_t create_rtp_rtcp_sock(const pjsua_transport_config *cfg,
                                        pjmedia_sock_info *skinfo)
{
    PJ_LOG(2,(THIS_FILE, "create_rtp_rtcp_sock"));
    
    enum {
        RTP_RETRY = 100
    };
    int i;
    pj_sockaddr_in bound_addr;
    pj_sockaddr_in mapped_addr[2];
    pj_status_t status = PJ_SUCCESS;
    char addr_buf[PJ_INET6_ADDRSTRLEN+2];
    pj_sock_t sock[2];
    
    /* Make sure STUN server resolution has completed */
    status = resolve_stun_server(PJ_TRUE);
    if (status != PJ_SUCCESS) {
        pjsua_perror(THIS_FILE, "Error resolving STUN server", status);
        return status;
    }
    
    if (next_rtp_port == 0)
        next_rtp_port = (pj_uint16_t)cfg->port;
    
    if (next_rtp_port == 0)
        next_rtp_port = (pj_uint16_t)40000;
    
    for (i=0; i<2; ++i)
        sock[i] = PJ_INVALID_SOCKET;
    
    bound_addr.sin_addr.s_addr = PJ_INADDR_ANY;
    if (cfg->bound_addr.slen) {
        status = pj_sockaddr_in_set_str_addr(&bound_addr, &cfg->bound_addr);
        if (status != PJ_SUCCESS) {
            pjsua_perror(THIS_FILE, "Unable to resolve transport bind address",
                         status);
            return status;
        }
    }
    
    /* Loop retry to bind RTP and RTCP sockets. */
    for (i=0; i<RTP_RETRY; ++i, next_rtp_port += 2) {
        
        /* Create RTP socket. */
        status = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &sock[0]);
        if (status != PJ_SUCCESS) {
            pjsua_perror(THIS_FILE, "socket() error", status);
            return status;
        }
        
        /* Apply QoS to RTP socket, if specified */
        status = pj_sock_apply_qos2(sock[0], cfg->qos_type,
                                    &cfg->qos_params,
                                    2, THIS_FILE, "RTP socket");
        
        /* Bind RTP socket */
        status=pj_sock_bind_in(sock[0], pj_ntohl(bound_addr.sin_addr.s_addr),
                               next_rtp_port);
        if (status != PJ_SUCCESS) {
            pj_sock_close(sock[0]);
            sock[0] = PJ_INVALID_SOCKET;
            continue;
        }
        
        /* Create RTCP socket. */
        status = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &sock[1]);
        if (status != PJ_SUCCESS) {
            pjsua_perror(THIS_FILE, "socket() error", status);
            pj_sock_close(sock[0]);
            return status;
        }
        
        /* Apply QoS to RTCP socket, if specified */
        status = pj_sock_apply_qos2(sock[1], cfg->qos_type,
                                    &cfg->qos_params,
                                    2, THIS_FILE, "RTCP socket");
        
        /* Bind RTCP socket */
        status=pj_sock_bind_in(sock[1], pj_ntohl(bound_addr.sin_addr.s_addr),
                               (pj_uint16_t)(next_rtp_port+1));
        if (status != PJ_SUCCESS) {
            pj_sock_close(sock[0]);
            sock[0] = PJ_INVALID_SOCKET;
            
            pj_sock_close(sock[1]);
            sock[1] = PJ_INVALID_SOCKET;
            continue;
        }
        
        /*
         * If we're configured to use STUN, then find out the mapped address,
         * and make sure that the mapped RTCP port is adjacent with the RTP.
         */
        if (pjsua_var.stun_srv.addr.sa_family != 0) {
            char ip_addr[32];
            pj_str_t stun_srv;
            
            pj_ansi_strcpy(ip_addr,
                           pj_inet_ntoa(pjsua_var.stun_srv.ipv4.sin_addr));
            stun_srv = pj_str(ip_addr);
            
            status=pjstun_get_mapped_addr(&pjsua_var.cp.factory, 2, sock,
                                          &stun_srv, pj_ntohs(pjsua_var.stun_srv.ipv4.sin_port),
                                          &stun_srv, pj_ntohs(pjsua_var.stun_srv.ipv4.sin_port),
                                          mapped_addr);
            if (status != PJ_SUCCESS) {
                pjsua_perror(THIS_FILE, "STUN resolve error", status);
                goto on_error;
            }
            
#if PJSUA_REQUIRE_CONSECUTIVE_RTCP_PORT
            if (pj_ntohs(mapped_addr[1].sin_port) ==
                pj_ntohs(mapped_addr[0].sin_port)+1)
            {
                /* Success! */
                break;
            }
            
            pj_sock_close(sock[0]);
            sock[0] = PJ_INVALID_SOCKET;
            
            pj_sock_close(sock[1]);
            sock[1] = PJ_INVALID_SOCKET;
#else
            if (pj_ntohs(mapped_addr[1].sin_port) !=
                pj_ntohs(mapped_addr[0].sin_port)+1)
            {
                PJ_LOG(4,(THIS_FILE,
                          "Note: STUN mapped RTCP port %d is not adjacent"
                          " to RTP port %d",
                          pj_ntohs(mapped_addr[1].sin_port),
                          pj_ntohs(mapped_addr[0].sin_port)));
            }
            /* Success! */
            break;
#endif
            
        } else if (cfg->public_addr.slen) {
            
            status = pj_sockaddr_in_init(&mapped_addr[0], &cfg->public_addr,
                                         (pj_uint16_t)next_rtp_port);
            if (status != PJ_SUCCESS)
                goto on_error;
            
            status = pj_sockaddr_in_init(&mapped_addr[1], &cfg->public_addr,
                                         (pj_uint16_t)(next_rtp_port+1));
            if (status != PJ_SUCCESS)
                goto on_error;
            
            break;
            
        } else {
            
            if (bound_addr.sin_addr.s_addr == 0) {
                pj_sockaddr addr;
                
                /* Get local IP address. */
                status = pj_gethostip(pj_AF_INET(), &addr);
                if (status != PJ_SUCCESS)
                    goto on_error;
                
                bound_addr.sin_addr.s_addr = addr.ipv4.sin_addr.s_addr;
            }
            
            for (i=0; i<2; ++i) {
                pj_sockaddr_in_init(&mapped_addr[i], NULL, 0);
                mapped_addr[i].sin_addr.s_addr = bound_addr.sin_addr.s_addr;
            }
            
            mapped_addr[0].sin_port=pj_htons((pj_uint16_t)next_rtp_port);
            mapped_addr[1].sin_port=pj_htons((pj_uint16_t)(next_rtp_port+1));
            break;
        }
    }
    
    if (sock[0] == PJ_INVALID_SOCKET) {
        PJ_LOG(1,(THIS_FILE,
                  "Unable to find appropriate RTP/RTCP ports combination"));
        goto on_error;
    }
    
    
    skinfo->rtp_sock = sock[0];
    pj_memcpy(&skinfo->rtp_addr_name,
              &mapped_addr[0], sizeof(pj_sockaddr_in));
    
    skinfo->rtcp_sock = sock[1];
    pj_memcpy(&skinfo->rtcp_addr_name,
              &mapped_addr[1], sizeof(pj_sockaddr_in));
    
    PJ_LOG(4,(THIS_FILE, "RTP socket reachable at %s",
              pj_sockaddr_print(&skinfo->rtp_addr_name, addr_buf,
                                sizeof(addr_buf), 3)));
    PJ_LOG(4,(THIS_FILE, "RTCP socket reachable at %s",
              pj_sockaddr_print(&skinfo->rtcp_addr_name, addr_buf,
                                sizeof(addr_buf), 3)));
    
    next_rtp_port += 2;
    return PJ_SUCCESS;
    
on_error:
    for (i=0; i<2; ++i) {
        if (sock[i] != PJ_INVALID_SOCKET)
            pj_sock_close(sock[i]);
    }
    return status;
}

/* Create normal UDP media transports */
static pj_status_t create_udp_media_transport(const pjsua_transport_config *cfg,
                                              pjsua_call_media *call_med)
{
    PJ_LOG(2,(THIS_FILE, "create_udp_media_transport"));
    
    pjmedia_sock_info skinfo;
    pj_status_t status;
    
    status = create_rtp_rtcp_sock(cfg, &skinfo);
    if (status != PJ_SUCCESS) {
        pjsua_perror(THIS_FILE, "Unable to create RTP/RTCP socket",
                     status);
        goto on_error;
    }
    
    status = pjmedia_transport_udp_attach(pjsua_var.med_endpt, NULL,
                                          &skinfo, 0, &call_med->tp);
    if (status != PJ_SUCCESS) {
        pjsua_perror(THIS_FILE, "Unable to create media transport",
                     status);
        goto on_error;
    }
    
    pjmedia_transport_simulate_lost(call_med->tp, PJMEDIA_DIR_ENCODING,
                                    pjsua_var.media_cfg.tx_drop_pct);
    
    pjmedia_transport_simulate_lost(call_med->tp, PJMEDIA_DIR_DECODING,
                                    pjsua_var.media_cfg.rx_drop_pct);
    
    call_med->tp_ready = PJ_SUCCESS;
    
    return PJ_SUCCESS;
    
on_error:
    if (call_med->tp)
        pjmedia_transport_close(call_med->tp);
    
    return status;
}

/* Go through the list of media in the SDP, find acceptable media, and
 * sort them based on the "quality" of the media, and store the indexes
 * in the specified array. Media with the best quality will be listed
 * first in the array. The quality factors considered currently is
 * encryption.
 */
static void sort_media(const pjmedia_sdp_session *sdp,
                       const pj_str_t *type,
                       pjmedia_srtp_use	use_srtp,
                       pj_uint8_t midx[],
                       unsigned *p_count,
                       unsigned *p_total_count)
{
    PJ_LOG(3,(THIS_FILE, "sort_media"));
    
    unsigned i;
    unsigned count = 0;
    int score[PJSUA_MAX_CALL_MEDIA];
    
    pj_assert(*p_count >= PJSUA_MAX_CALL_MEDIA);
    pj_assert(*p_total_count >= PJSUA_MAX_CALL_MEDIA);
    
    *p_count = 0;
    *p_total_count = 0;
    for (i=0; i<PJSUA_MAX_CALL_MEDIA; ++i)
        score[i] = 1;
    
    /* Score each media */
    for (i=0; i<sdp->media_count && count<PJSUA_MAX_CALL_MEDIA; ++i) {
        const pjmedia_sdp_media *m = sdp->media[i];
        const pjmedia_sdp_conn *c;
        
        /* Skip different media */
        if (pj_stricmp(&m->desc.media, type) != 0) {
            score[count++] = -22000;
            continue;
        }
        
        c = m->conn? m->conn : sdp->conn;
        
        /* Supported transports */
        if (pj_stricmp2(&m->desc.transport, "RTP/SAVP")==0) {
            switch (use_srtp) {
                case PJMEDIA_SRTP_MANDATORY:
                case PJMEDIA_SRTP_OPTIONAL:
                    ++score[i];
                    break;
                case PJMEDIA_SRTP_DISABLED:
                    //--score[i];
                    score[i] -= 5;
                    break;
            }
        } else if (pj_stricmp2(&m->desc.transport, "RTP/AVP")==0) {
            switch (use_srtp) {
                case PJMEDIA_SRTP_MANDATORY:
                    //--score[i];
                    score[i] -= 5;
                    break;
                case PJMEDIA_SRTP_OPTIONAL:
                    /* No change in score */
                    break;
                case PJMEDIA_SRTP_DISABLED:
                    ++score[i];
                    break;
            }
        } else {
            score[i] -= 10;
        }
        
        /* Is media disabled? */
        if (m->desc.port == 0)
            score[i] -= 10;
        
        /* Is media inactive? */
        if (pjmedia_sdp_media_find_attr2(m, "inactive", NULL) ||
            pj_strcmp2(&c->addr, "0.0.0.0") == 0)
        {
            //score[i] -= 10;
            score[i] -= 1;
        }
        
        ++count;
    }
    
    /* Created sorted list based on quality */
    for (i=0; i<count; ++i) {
        unsigned j;
        int best = 0;
        
        for (j=1; j<count; ++j) {
            if (score[j] > score[best])
                best = j;
        }
        /* Don't put media with negative score, that media is unacceptable
         * for us.
         */
        midx[i] = (pj_uint8_t)best;
        if (score[best] >= 0)
            (*p_count)++;
        if (score[best] > -22000)
            (*p_total_count)++;
        
        score[best] = -22000;
        
    }
}

/* Set media transport state and notify the application via the callback. */
void set_media_tp_state(pjsua_call_media *call_med,
                        pjsua_med_tp_st tp_st)
{
    PJ_LOG(3,(THIS_FILE, "set_media_tp_state"));
    
    if (pjsua_var.ua_cfg.cb.on_call_media_transport_state &&
        call_med->tp_st != tp_st)
    {
        pjsua_med_tp_state_info info;
        
        pj_bzero(&info, sizeof(info));
        info.med_idx = call_med->idx;
        info.state = tp_st;
        info.status = call_med->tp_ready;
        (*pjsua_var.ua_cfg.cb.on_call_media_transport_state)(
                                                             call_med->call->index, &info);
    }
    
    call_med->tp_st = tp_st;
}

/* Callback to resume pjsua_call_media_init() after media transport
 * creation is completed.
 */
static pj_status_t call_media_init_cb(pjsua_call_media *call_med,
                                      pj_status_t status,
                                      int security_level,
                                      int *sip_err_code)
{
    PJ_LOG(2,(THIS_FILE, "call_media_init_cb"));
    
    pjsua_acc *acc = &pjsua_var.acc[call_med->call->acc_id];
    pjmedia_transport_info tpinfo;
    int err_code = 0;
    
    if (status != PJ_SUCCESS)
        goto on_return;
    
    if (call_med->tp_st == PJSUA_MED_TP_CREATING)
        set_media_tp_state(call_med, PJSUA_MED_TP_IDLE);
    
    if (!call_med->tp_orig &&
        pjsua_var.ua_cfg.cb.on_create_media_transport)
    {
        call_med->use_custom_med_tp = PJ_TRUE;
    } else
        call_med->use_custom_med_tp = PJ_FALSE;
    
#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
    /* This function may be called when SRTP transport already exists
     * (e.g: in re-invite, update), don't need to destroy/re-create.
     */
    if (!call_med->tp_orig) {
        pjmedia_srtp_setting srtp_opt;
        pjmedia_transport *srtp = NULL;
        
        /* Check if SRTP requires secure signaling */
        if (acc->cfg.use_srtp != PJMEDIA_SRTP_DISABLED) {
            if (security_level < acc->cfg.srtp_secure_signaling) {
                err_code = PJSIP_SC_NOT_ACCEPTABLE;
                status = PJSIP_ESESSIONINSECURE;
                goto on_return;
            }
        }
        
        /* Always create SRTP adapter */
        pjmedia_srtp_setting_default(&srtp_opt);
        srtp_opt.close_member_tp = PJ_TRUE;
        /* If media session has been ever established, let's use remote's 
         * preference in SRTP usage policy, especially when it is stricter.
         */
        if (call_med->rem_srtp_use > acc->cfg.use_srtp)
            srtp_opt.use = call_med->rem_srtp_use;
        else
            srtp_opt.use = acc->cfg.use_srtp;
        
        status = pjmedia_transport_srtp_create(pjsua_var.med_endpt,
                                               call_med->tp,
                                               &srtp_opt, &srtp);
        if (status != PJ_SUCCESS) {
            err_code = PJSIP_SC_INTERNAL_SERVER_ERROR;
            goto on_return;
        }
        
        /* Set SRTP as current media transport */
        call_med->tp_orig = call_med->tp;
        call_med->tp = srtp;
    }
#else
    call_med->tp_orig = call_med->tp;
    PJ_UNUSED_ARG(security_level);
#endif
    
    
    pjmedia_transport_info_init(&tpinfo);
    pjmedia_transport_get_info(call_med->tp, &tpinfo);
    
    pj_sockaddr_cp(&call_med->rtp_addr, &tpinfo.sock_info.rtp_addr_name);
    
    
on_return:
    if (status != PJ_SUCCESS && call_med->tp) {
        pjmedia_transport_close(call_med->tp);
        call_med->tp = NULL;
    }
    
    if (sip_err_code)
        *sip_err_code = err_code;
    
    if (call_med->med_init_cb) {
        pjsua_med_tp_state_info info;
        
        pj_bzero(&info, sizeof(info));
        info.status = status;
        info.state = call_med->tp_st;
        info.med_idx = call_med->idx;
        info.sip_err_code = err_code;
        (*call_med->med_init_cb)(call_med->call->index, &info);
    }
    
    return status;
}

pj_status_t pjsua_media_channel_init(pjsua_call_id call_id,
				     pjsip_role_e role,
				     int security_level,
				     pj_pool_t *tmp_pool,
				     const pjmedia_sdp_session *rem_sdp,
				     int *sip_err_code,
                                     pj_bool_t async,
                                     pjsua_med_tp_state_cb cb)
{
    PJ_LOG(3,(THIS_FILE, "pjsua_media_channel_init"));
    
    const pj_str_t STR_AUDIO = { "audio", 5 };
    const pj_str_t STR_VIDEO = { "video", 5 };
    pjsua_call *call = &pjsua_var.calls[call_id];
    pjsua_acc *acc = &pjsua_var.acc[call->acc_id];
    pj_uint8_t maudidx[PJSUA_MAX_CALL_MEDIA];
    unsigned maudcnt = PJ_ARRAY_SIZE(maudidx);
    unsigned mtotaudcnt = PJ_ARRAY_SIZE(maudidx);
    pj_uint8_t mvididx[PJSUA_MAX_CALL_MEDIA];
    unsigned mvidcnt = PJ_ARRAY_SIZE(mvididx);
    unsigned mtotvidcnt = PJ_ARRAY_SIZE(mvididx);
    unsigned mi;
    pj_bool_t pending_med_tp = PJ_FALSE;
    pj_bool_t reinit = PJ_FALSE;
    pj_status_t status;
    
    PJ_UNUSED_ARG(role);

    /*
     * Note: this function may be called when the media already exists
     * (e.g. in reinvites, updates, etc).
     */

    if (pjsua_get_state() != PJSUA_STATE_RUNNING)
        return PJ_EBUSY;

    if (call->inv && call->inv->state == PJSIP_INV_STATE_CONFIRMED)
        reinit = PJ_TRUE;

    PJ_LOG(4,(THIS_FILE, "Call %d: %sinitializing media..",
			 call_id, (reinit?"re-":"") ));

    /* Get media count for each media type */
    if (rem_sdp) {
	sort_media(rem_sdp, &STR_AUDIO, acc->cfg.use_srtp,
		   maudidx, &maudcnt, &mtotaudcnt);
	if (maudcnt==0) {
	    /* Expecting audio in the offer */
	    if (sip_err_code) *sip_err_code = PJSIP_SC_NOT_ACCEPTABLE_HERE;
	    pjsua_media_channel_deinit(call_id);
	    status = PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_NOT_ACCEPTABLE_HERE);
	    goto on_error;
	}

#if PJMEDIA_HAS_VIDEO
	sort_media(rem_sdp, &STR_VIDEO, acc->cfg.use_srtp,
		   mvididx, &mvidcnt, &mtotvidcnt);
#else
	mvidcnt = mtotvidcnt = 0;
	PJ_UNUSED_ARG(STR_VIDEO);
#endif

	/* Update media count only when remote add any media, this media count
	 * must never decrease. Also note that we shouldn't apply the media
	 * count setting (of the call setting) before the SDP negotiation.
	 */
	if (call->med_cnt < rem_sdp->media_count)
	    call->med_cnt = PJ_MIN(rem_sdp->media_count, PJSUA_MAX_CALL_MEDIA);

	call->rem_offerer = PJ_TRUE;
	call->rem_aud_cnt = maudcnt;
	call->rem_vid_cnt = mvidcnt;

    } else {

	/* If call already established, calculate media count from current 
	 * local active SDP and call setting. Otherwise, calculate media
	 * count from the call setting only.
	 */
	if (reinit) {
	    const pjmedia_sdp_session *sdp;

	    status = pjmedia_sdp_neg_get_active_local(call->inv->neg, &sdp);
	    pj_assert(status == PJ_SUCCESS);

	    sort_media(sdp, &STR_AUDIO, acc->cfg.use_srtp,
		       maudidx, &maudcnt, &mtotaudcnt);
	    pj_assert(maudcnt > 0);

	    sort_media(sdp, &STR_VIDEO, acc->cfg.use_srtp,
		       mvididx, &mvidcnt, &mtotvidcnt);

	    /* Call setting may add or remove media. Adding media is done by
	     * enabling any disabled/port-zeroed media first, then adding new
	     * media whenever needed. Removing media is done by disabling
	     * media with the lowest 'quality'.
	     */

	    /* Check if we need to add new audio */
	    if (maudcnt < call->opt.aud_cnt &&
		mtotaudcnt < call->opt.aud_cnt)
	    {
		for (mi = 0; mi < call->opt.aud_cnt - mtotaudcnt; ++mi)
		    maudidx[maudcnt++] = (pj_uint8_t)call->med_cnt++;
		
		mtotaudcnt = call->opt.aud_cnt;
	    }
	    maudcnt = call->opt.aud_cnt;

	    /* Check if we need to add new video */
	    if (mvidcnt < call->opt.vid_cnt &&
		mtotvidcnt < call->opt.vid_cnt)
	    {
		for (mi = 0; mi < call->opt.vid_cnt - mtotvidcnt; ++mi)
		    mvididx[mvidcnt++] = (pj_uint8_t)call->med_cnt++;

		mtotvidcnt = call->opt.vid_cnt;
	    }
	    mvidcnt = call->opt.vid_cnt;

	} else {

	    maudcnt = mtotaudcnt = call->opt.aud_cnt;
	    for (mi=0; mi<maudcnt; ++mi) {
		maudidx[mi] = (pj_uint8_t)mi;
	    }
	    mvidcnt = mtotvidcnt = call->opt.vid_cnt;
	    for (mi=0; mi<mvidcnt; ++mi) {
		mvididx[mi] = (pj_uint8_t)(maudcnt + mi);
	    }
	    call->med_cnt = maudcnt + mvidcnt;

	    /* Need to publish supported media? */
	    if (call->opt.flag & PJSUA_CALL_INCLUDE_DISABLED_MEDIA) {
		if (mtotaudcnt == 0) {
		    mtotaudcnt = 1;
		    maudidx[0] = (pj_uint8_t)call->med_cnt++;
		}
#if PJMEDIA_HAS_VIDEO
		if (mtotvidcnt == 0) {
		    mtotvidcnt = 1;
		    mvididx[0] = (pj_uint8_t)call->med_cnt++;
		}
#endif
	    }
	}

	call->rem_offerer = PJ_FALSE;
    }

    if (call->med_cnt == 0) {
	/* Expecting at least one media */
	if (sip_err_code) *sip_err_code = PJSIP_SC_NOT_ACCEPTABLE_HERE;
	pjsua_media_channel_deinit(call_id);
	status = PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_NOT_ACCEPTABLE_HERE);
	goto on_error;
    }

    if (async) {
        call->med_ch_cb = cb;
    }

    if (rem_sdp) {
        call->async_call.rem_sdp =
            pjmedia_sdp_session_clone(call->inv->pool_prov, rem_sdp);
    } else {
	call->async_call.rem_sdp = NULL;
    }

    call->async_call.pool_prov = tmp_pool;

    /* Initialize each media line */
    for (mi=0; mi < call->med_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media[mi];
	pj_bool_t enabled = PJ_FALSE;
	pjmedia_type media_type = PJMEDIA_TYPE_UNKNOWN;
        
    call_med->channel_id = -1;

	if (pj_memchr(maudidx, mi, mtotaudcnt * sizeof(maudidx[0]))) {
	    media_type = PJMEDIA_TYPE_AUDIO;
	    if (call->opt.aud_cnt &&
		pj_memchr(maudidx, mi, maudcnt * sizeof(maudidx[0])))
	    {
		enabled = PJ_TRUE;
	    }
	} else if (pj_memchr(mvididx, mi, mtotvidcnt * sizeof(mvididx[0]))) {
	    media_type = PJMEDIA_TYPE_VIDEO;
	    if (call->opt.vid_cnt &&
		pj_memchr(mvididx, mi, mvidcnt * sizeof(mvididx[0])))
	    {
		enabled = PJ_TRUE;
	    }
	}

	if (enabled) {
        VoEBase* base = VoEBase::GetInterface(voe);
        
        int channel = base->CreateChannel();
        PJ_LOG(5,(THIS_FILE, "VoiceEngine::CreateChannel(%d)", channel));
        
        if (channel < 0) {
            pjsua_media_channel_deinit(call_id);
            status = PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_NOT_ACCEPTABLE_HERE);
            goto on_error;
        }
        
        call_med->channel_id = channel;
        int retries = 0;
        int port = next_rtp_port;
        while(1) {
            if (port > MAX_RTP_PORT) {
                port = MIN_RTP_PORT;
            }
            PJ_LOG(5,(THIS_FILE, "VoiceEngine::SetLocalReceiver(%d, %d)", channel, port));
            status = base->SetLocalReceiver(channel, port);
            if (status == PJ_SUCCESS) {
                call_med->channel_port = port;
                break;
            }
        
            if (++retries >= RTP_MAX_RETRIES) {
                pjsua_media_channel_deinit(call_id);
                status = PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_NOT_ACCEPTABLE_HERE);
                goto on_error;
            }
            port += 2;
        }
        base->Release();
        call_med->type = media_type;
	} else {
	    /* By convention, the media is disabled if transport is NULL 
	     * or transport state is PJSUA_MED_TP_DISABLED.
	     */
	    if (call_med->tp) {
		// Don't close transport here, as SDP negotiation has not been
		// done and stream may be still active.
		//pjmedia_transport_close(call_med->tp);
		//call_med->tp = NULL;
		pj_assert(call_med->tp_st == PJSUA_MED_TP_INIT || 
			  call_med->tp_st == PJSUA_MED_TP_RUNNING);
		set_media_tp_state(call_med, PJSUA_MED_TP_DISABLED);
	    }

	    /* Put media type just for info */
	    call_med->type = media_type;
	}
    }

    call->audio_idx = maudidx[0];

    PJ_LOG(4,(THIS_FILE, "Media index %d selected for audio call %d",
	      call->audio_idx, call->index));

    if (pending_med_tp) {
        /* We shouldn't use temporary pool anymore. */
        call->async_call.pool_prov = NULL;
        /* We have a pending media transport initialization. */
        pj_log_pop_indent();
        return PJ_EPENDING;
    }

    /* Media transport initialization completed immediately, so 
     * we don't need to call the callback.
     */
    call->med_ch_cb = NULL;

    pj_log_pop_indent();
    return status;

on_error:
    if (call->med_ch_mutex) {
        pj_mutex_destroy(call->med_ch_mutex);
        call->med_ch_mutex = NULL;
    }

    pj_log_pop_indent();
    return status;
}


/* Create SDP based on the current media channel. Note that, this function
 * will not modify the media channel, so when receiving new offer or
 * updating media count (via call setting), media channel must be reinit'd
 * (using pjsua_media_channel_init()) first before calling this function.
 */
pj_status_t pjsua_media_channel_create_sdp(pjsua_call_id call_id, 
					   pj_pool_t *pool,
					   const pjmedia_sdp_session *rem_sdp,
					   pjmedia_sdp_session **p_sdp,
					   int *sip_err_code)
{
    PJ_LOG(3,(THIS_FILE, "pjsua_media_channel_create_sdp"));
    
    enum { MAX_MEDIA = PJSUA_MAX_CALL_MEDIA };
    pjmedia_sdp_session *sdp;
    pj_sockaddr origin;
    pjsua_call *call = &pjsua_var.calls[call_id];
    pjmedia_sdp_neg_state sdp_neg_state = PJMEDIA_SDP_NEG_STATE_NULL;
    unsigned mi;
    pj_status_t status;

    if (pjsua_get_state() != PJSUA_STATE_RUNNING)
	return PJ_EBUSY;

    /* Get SDP negotiator state */
    if (call->inv && call->inv->neg)
	sdp_neg_state = pjmedia_sdp_neg_get_state(call->inv->neg);

    /* Get one address to use in the origin field */
    pj_bzero(&origin, sizeof(origin));
    pj_gethostip(pj_AF_INET(), &origin);

    /* Create the base (blank) SDP */
    status = pjmedia_endpt_create_base_sdp(pjsua_var.med_endpt, pool, NULL,
                                           &origin, &sdp);
    if (status != PJ_SUCCESS)
	return status;

    /* Process each media line */
    for (mi=0; mi<call->med_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media[mi];
	pjmedia_sdp_media *m = NULL;
	pjmedia_transport_info tpinfo;

	if (rem_sdp && mi >= rem_sdp->media_count) {
	    /* Remote might have removed some media lines. */
	    break;
	}

	if (call_med->channel_id == -1)
	{
	    /*
	     * This media is disabled. Just create a valid SDP with zero
	     * port.
	     */
	    if (rem_sdp) {
		/* Just clone the remote media and deactivate it */
		m = pjmedia_sdp_media_clone_deactivate(pool,
						       rem_sdp->media[mi]);
	    } else {
		m = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_media);
		m->desc.transport = pj_str("RTP/AVP");
		m->desc.fmt_count = 1;
		m->conn = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_conn);
		m->conn->net_type = pj_str("IN");
		m->conn->addr_type = pj_str("IP4");
		m->conn->addr = pj_str("127.0.0.1");

		switch (call_med->type) {
		case PJMEDIA_TYPE_AUDIO:
		    m->desc.media = pj_str("audio");
		    m->desc.fmt[0] = pj_str("0");
		    break;
		case PJMEDIA_TYPE_VIDEO:
		    m->desc.media = pj_str("video");
		    m->desc.fmt[0] = pj_str("31");
		    break;
		default:
		    /* This must be us generating re-offer, and some unknown
		     * media may exist, so just clone from active local SDP
		     * (and it should have been deactivated already).
		     */
		    pj_assert(call->inv && call->inv->neg &&
			      sdp_neg_state == PJMEDIA_SDP_NEG_STATE_DONE);
		    {
			const pjmedia_sdp_session *s_;
			pjmedia_sdp_neg_get_active_local(call->inv->neg, &s_);

			pj_assert(mi < s_->media_count);
			m = pjmedia_sdp_media_clone(pool, s_->media[mi]);
			m->desc.port = 0;
		    }
		    break;
		}
	    }

	    sdp->media[sdp->media_count++] = m;
	    continue;
	}

	/* Get transport address info */
	pjmedia_transport_info_init(&tpinfo);
    
    unsigned short port = call_med->channel_port;
    pj_sockaddr_init(pj_AF_INET(), &(tpinfo.sock_info.rtp_addr_name),
                    NULL,
                    (unsigned short)(port));
    pj_sockaddr_init(pj_AF_INET(), &(tpinfo.sock_info.rtcp_addr_name),
                    NULL,
                    (unsigned short)(port + 1));
    pj_memcpy(pj_sockaddr_get_addr(&tpinfo.sock_info.rtp_addr_name),
            pj_sockaddr_get_addr(&origin),
            pj_sockaddr_get_addr_len(&origin));
    pj_memcpy(pj_sockaddr_get_addr(&tpinfo.sock_info.rtcp_addr_name),
            pj_sockaddr_get_addr(&origin),
            pj_sockaddr_get_addr_len(&origin));
        
	/* Ask pjmedia endpoint to create SDP media line */
	switch (call_med->type) {
	case PJMEDIA_TYPE_AUDIO:
	    status = pjmedia_endpt_create_audio_sdp(pjsua_var.med_endpt, pool,
                                                    &tpinfo.sock_info, 0, &m);
	    break;
#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
	case PJMEDIA_TYPE_VIDEO:
	    status = pjmedia_endpt_create_video_sdp(pjsua_var.med_endpt, pool,
	                                            &tpinfo.sock_info, 0, &m);
	    break;
#endif
	default:
	    pj_assert(!"Invalid call_med media type");
	    return PJ_EBUG;
	}

	if (status != PJ_SUCCESS)
	    return status;

	sdp->media[sdp->media_count++] = m;

	/* Give to transport */
	/*status = pjmedia_transport_encode_sdp(call_med->tp, pool,
					      sdp, rem_sdp, mi);
	if (status != PJ_SUCCESS) {
	    if (sip_err_code) *sip_err_code = PJSIP_SC_NOT_ACCEPTABLE;
	    return status;
	}*/

	/* Copy c= line of the first media to session level,
	 * if there's none.
	 */
	if (sdp->conn == NULL) {
	    sdp->conn = pjmedia_sdp_conn_clone(pool, m->conn);
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


#if DISABLED_FOR_TICKET_1185 && defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
    /* Check if SRTP is in optional mode and configured to use duplicated
     * media, i.e: secured and unsecured version, in the SDP offer.
     */
    if (!rem_sdp &&
	pjsua_var.acc[call->acc_id].cfg.use_srtp == PJMEDIA_SRTP_OPTIONAL &&
	pjsua_var.acc[call->acc_id].cfg.srtp_optional_dup_offer)
    {
	unsigned i;

	for (i = 0; i < sdp->media_count; ++i) {
	    pjmedia_sdp_media *m = sdp->media[i];

	    /* Check if this media is unsecured but has SDP "crypto"
	     * attribute.
	     */
	    if (pj_stricmp2(&m->desc.transport, "RTP/AVP") == 0 &&
		pjmedia_sdp_media_find_attr2(m, "crypto", NULL) != NULL)
	    {
		if (i == (unsigned)call->audio_idx &&
		    sdp_neg_state == PJMEDIA_SDP_NEG_STATE_DONE)
		{
		    /* This is a session update, and peer has chosen the
		     * unsecured version, so let's make this unsecured too.
		     */
		    pjmedia_sdp_media_remove_all_attr(m, "crypto");
		} else {
		    /* This is new offer, duplicate media so we'll have
		     * secured (with "RTP/SAVP" transport) and and unsecured
		     * versions.
		     */
		    pjmedia_sdp_media *new_m;

		    /* Duplicate this media and apply secured transport */
		    new_m = pjmedia_sdp_media_clone(pool, m);
		    pj_strdup2(pool, &new_m->desc.transport, "RTP/SAVP");

		    /* Remove the "crypto" attribute in the unsecured media */
		    pjmedia_sdp_media_remove_all_attr(m, "crypto");

		    /* Insert the new media before the unsecured media */
		    if (sdp->media_count < PJMEDIA_MAX_SDP_MEDIA) {
			pj_array_insert(sdp->media, sizeof(new_m),
					sdp->media_count, i, &new_m);
			++sdp->media_count;
			++i;
		    }
		}
	    }
	}
    }
#endif

    call->rem_offerer = (rem_sdp != NULL);

    *p_sdp = sdp;
    return PJ_SUCCESS;
}

/* This callback is called when ICE negotiation completes */
static void on_ice_complete(pjmedia_transport *tp, 
                            pj_ice_strans_op op,
                            pj_status_t result)
{
    PJ_LOG(2,(THIS_FILE, "on_ice_complete"));
    
    pjsua_call_media *call_med = (pjsua_call_media*)tp->user_data;
    
    if (!call_med)
        return;
    
    switch (op) {
        case PJ_ICE_STRANS_OP_INIT:
            PJSUA_LOCK();
            call_med->tp_ready = result;
            if (call_med->med_create_cb)
                (*call_med->med_create_cb)(call_med, result,
                                           call_med->call->secure_level, NULL);
            PJSUA_UNLOCK();
            break;
        case PJ_ICE_STRANS_OP_NEGOTIATION:
            if (result != PJ_SUCCESS) {
                call_med->state = PJSUA_CALL_MEDIA_ERROR;
                call_med->dir = PJMEDIA_DIR_NONE;
                
                if (call_med->call && pjsua_var.ua_cfg.cb.on_call_media_state) {
                    pjsua_var.ua_cfg.cb.on_call_media_state(call_med->call->index);
                }
            } else if (call_med->call) {
                /* Send UPDATE if default transport address is different than
                 * what was advertised (ticket #881)
                 */
                pjmedia_transport_info tpinfo;
                pjmedia_ice_transport_info *ii = NULL;
                unsigned i;
                
                pjmedia_transport_info_init(&tpinfo);
                pjmedia_transport_get_info(tp, &tpinfo);
                for (i=0; i<tpinfo.specific_info_cnt; ++i) {
                    if (tpinfo.spc_info[i].type==PJMEDIA_TRANSPORT_TYPE_ICE) {
                        ii = (pjmedia_ice_transport_info*)
                        tpinfo.spc_info[i].buffer;
                        break;
                    }
                }
                
                if (ii && ii->role==PJ_ICE_SESS_ROLE_CONTROLLING &&
                    pj_sockaddr_cmp(&tpinfo.sock_info.rtp_addr_name,
                                    &call_med->rtp_addr))
                {
                    pj_bool_t use_update;
                    const pj_str_t STR_UPDATE = { "UPDATE", 6 };
                    pjsip_dialog_cap_status support_update;
                    pjsip_dialog *dlg;
                    
                    dlg = call_med->call->inv->dlg;
                    support_update = pjsip_dlg_remote_has_cap(dlg, PJSIP_H_ALLOW,
                                                              NULL, &STR_UPDATE);
                    use_update = (support_update == PJSIP_DIALOG_CAP_SUPPORTED);
                    
                    PJ_LOG(4,(THIS_FILE, 
                              "ICE default transport address has changed for "
                              "call %d, sending %s",
                              call_med->call->index,
                              (use_update ? "UPDATE" : "re-INVITE")));
                    
                    if (use_update)
                        pjsua_call_update(call_med->call->index, 0, NULL);
                    else
                        pjsua_call_reinvite(call_med->call->index, 0, NULL);
                }
            }
            break;
        case PJ_ICE_STRANS_OP_KEEP_ALIVE:
            if (result != PJ_SUCCESS) {
                PJ_PERROR(4,(THIS_FILE, result,
                             "ICE keep alive failure for transport %d:%d",
                             call_med->call->index, call_med->idx));
            }
            if (pjsua_var.ua_cfg.cb.on_call_media_transport_state) {
                pjsua_med_tp_state_info info;
                
                pj_bzero(&info, sizeof(info));
                info.med_idx = call_med->idx;
                info.state = call_med->tp_st;
                info.status = result;
                info.ext_info = &op;
                (*pjsua_var.ua_cfg.cb.on_call_media_transport_state)(
                                                                     call_med->call->index, &info);
            }
            if (pjsua_var.ua_cfg.cb.on_ice_transport_error) {
                pjsua_call_id id = call_med->call->index;
                (*pjsua_var.ua_cfg.cb.on_ice_transport_error)(id, op, result,
                                                              NULL);
            }
            break;
    }
}

/* Parse "HOST:PORT" format */
static pj_status_t parse_host_port(const pj_str_t *host_port,
                                   pj_str_t *host, pj_uint16_t *port)
{
    PJ_LOG(2,(THIS_FILE, "parse_host_port"));
    
    pj_str_t str_port;
    
    str_port.ptr = pj_strchr(host_port, ':');
    if (str_port.ptr != NULL) {
        int iport;
        
        host->ptr = host_port->ptr;
        host->slen = (str_port.ptr - host->ptr);
        str_port.ptr++;
        str_port.slen = host_port->slen - host->slen - 1;
        iport = (int)pj_strtoul(&str_port);
        if (iport < 1 || iport > 65535)
            return PJ_EINVAL;
        *port = (pj_uint16_t)iport;
    } else {
        *host = *host_port;
        *port = 0;
    }
    
    return PJ_SUCCESS;
}

/* Create ICE media transports (when ice is enabled) */
static pj_status_t create_ice_media_transport(
                                              const pjsua_transport_config *cfg,
                                              pjsua_call_media *call_med,
                                              pj_bool_t async)
{
    PJ_LOG(2,(THIS_FILE, "create_ice_media_transport"));
    
    char stunip[PJ_INET6_ADDRSTRLEN];
    pj_ice_strans_cfg ice_cfg;
    pjmedia_ice_cb ice_cb;
    char name[32];
    unsigned comp_cnt;
    pj_status_t status;
    
    /* Make sure STUN server resolution has completed */
    status = resolve_stun_server(PJ_TRUE);
    if (status != PJ_SUCCESS) {
        pjsua_perror(THIS_FILE, "Error resolving STUN server", status);
        return status;
    }
    
    /* Create ICE stream transport configuration */
    pj_ice_strans_cfg_default(&ice_cfg);
    pj_stun_config_init(&ice_cfg.stun_cfg, &pjsua_var.cp.factory, 0,
                        pjsip_endpt_get_ioqueue(pjsua_var.endpt),
                        pjsip_endpt_get_timer_heap(pjsua_var.endpt));
    
    ice_cfg.af = pj_AF_INET();
    ice_cfg.resolver = pjsua_var.resolver;
    
    ice_cfg.opt = pjsua_var.media_cfg.ice_opt;
    
    /* Configure STUN settings */
    if (pj_sockaddr_has_addr(&pjsua_var.stun_srv)) {
        pj_sockaddr_print(&pjsua_var.stun_srv, stunip, sizeof(stunip), 0);
        ice_cfg.stun.server = pj_str(stunip);
        ice_cfg.stun.port = pj_sockaddr_get_port(&pjsua_var.stun_srv);
    }
    if (pjsua_var.media_cfg.ice_max_host_cands >= 0)
        ice_cfg.stun.max_host_cands = pjsua_var.media_cfg.ice_max_host_cands;
    
    /* Copy QoS setting to STUN setting */
    ice_cfg.stun.cfg.qos_type = cfg->qos_type;
    pj_memcpy(&ice_cfg.stun.cfg.qos_params, &cfg->qos_params,
              sizeof(cfg->qos_params));
    
    /* Configure TURN settings */
    if (pjsua_var.media_cfg.enable_turn) {
        status = parse_host_port(&pjsua_var.media_cfg.turn_server,
                                 &ice_cfg.turn.server,
                                 &ice_cfg.turn.port);
        if (status != PJ_SUCCESS || ice_cfg.turn.server.slen == 0) {
            PJ_LOG(1,(THIS_FILE, "Invalid TURN server setting"));
            return PJ_EINVAL;
        }
        if (ice_cfg.turn.port == 0)
            ice_cfg.turn.port = 3479;
        ice_cfg.turn.conn_type = pjsua_var.media_cfg.turn_conn_type;
        pj_memcpy(&ice_cfg.turn.auth_cred, 
                  &pjsua_var.media_cfg.turn_auth_cred,
                  sizeof(ice_cfg.turn.auth_cred));
        
        /* Copy QoS setting to TURN setting */
        ice_cfg.turn.cfg.qos_type = cfg->qos_type;
        pj_memcpy(&ice_cfg.turn.cfg.qos_params, &cfg->qos_params,
                  sizeof(cfg->qos_params));
    }
    
    pj_bzero(&ice_cb, sizeof(pjmedia_ice_cb));
    ice_cb.on_ice_complete = &on_ice_complete;
    pj_ansi_snprintf(name, sizeof(name), "icetp%02d", call_med->idx);
    call_med->tp_ready = PJ_EPENDING;
    
    comp_cnt = 1;
    if (PJMEDIA_ADVERTISE_RTCP && !pjsua_var.media_cfg.ice_no_rtcp)
        ++comp_cnt;
    
    status = pjmedia_ice_create3(pjsua_var.med_endpt, name, comp_cnt,
                                 &ice_cfg, &ice_cb, 0, call_med,
                                 &call_med->tp);
    if (status != PJ_SUCCESS) {
        pjsua_perror(THIS_FILE, "Unable to create ICE media transport",
                     status);
        goto on_error;
    }
    
    /* Wait until transport is initialized, or time out */
    if (!async) {
        pj_bool_t has_pjsua_lock = PJSUA_LOCK_IS_LOCKED();
        if (has_pjsua_lock)
            PJSUA_UNLOCK();
        while (call_med->tp_ready == PJ_EPENDING) {
            pjsua_handle_events(100);
        }
        if (has_pjsua_lock)
            PJSUA_LOCK();
    }
    
    if (async && call_med->tp_ready == PJ_EPENDING) {
        return PJ_EPENDING;
    } else if (call_med->tp_ready != PJ_SUCCESS) {
        pjsua_perror(THIS_FILE, "Error initializing ICE media transport",
                     call_med->tp_ready);
        status = call_med->tp_ready;
        goto on_error;
    }
    
    pjmedia_transport_simulate_lost(call_med->tp, PJMEDIA_DIR_ENCODING,
                                    pjsua_var.media_cfg.tx_drop_pct);
    
    pjmedia_transport_simulate_lost(call_med->tp, PJMEDIA_DIR_DECODING,
                                    pjsua_var.media_cfg.rx_drop_pct);
    
    return PJ_SUCCESS;
    
on_error:
    if (call_med->tp != NULL) {
        pjmedia_transport_close(call_med->tp);
        call_med->tp = NULL;
    }
    
    return status;
}

static void stop_media_session(pjsua_call_id call_id)
{
    PJ_LOG(3,(THIS_FILE, "stop_media_session"));
    
    pjsua_call *call = &pjsua_var.calls[call_id];
    unsigned mi;

    pj_log_push_indent();

    for (mi=0; mi<call->med_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media[mi];

	if (call_med->type == PJMEDIA_TYPE_AUDIO) {
	    pjmedia_stream *strm = call_med->strm.a.stream;
	    pjmedia_rtcp_stat stat;

	    if (strm) {
		if (call_med->strm.a.conf_slot != PJSUA_INVALID_ID) {
		    if (pjsua_var.mconf) {
			pjsua_conf_remove_port(call_med->strm.a.conf_slot);
		    }
		    call_med->strm.a.conf_slot = PJSUA_INVALID_ID;
		}

		if ((call_med->dir & PJMEDIA_DIR_ENCODING) &&
		    (pjmedia_stream_get_stat(strm, &stat) == PJ_SUCCESS))
		{
		    /* Save RTP timestamp & sequence, so when media session is
		     * restarted, those values will be restored as the initial
		     * RTP timestamp & sequence of the new media session. So in
		     * the same call session, RTP timestamp and sequence are
		     * guaranteed to be contigue.
		     */
		    call_med->rtp_tx_seq_ts_set = 1 | (1 << 1);
		    call_med->rtp_tx_seq = stat.rtp_tx_last_seq;
		    call_med->rtp_tx_ts = stat.rtp_tx_last_ts;
		}

		if (pjsua_var.ua_cfg.cb.on_stream_destroyed) {
		    pjsua_var.ua_cfg.cb.on_stream_destroyed(call_id, strm, mi);
		}

		pjmedia_stream_destroy(strm);
		call_med->strm.a.stream = NULL;
	    }
	}

#if PJMEDIA_HAS_VIDEO
	else if (call_med->type == PJMEDIA_TYPE_VIDEO) {
	    stop_video_stream(call_med);
	}
#endif

	PJ_LOG(4,(THIS_FILE, "Media session call%02d:%d is destroyed",
			     call_id, mi));
        call_med->prev_state = call_med->state;
	call_med->state = PJSUA_CALL_MEDIA_NONE;
    }

    pj_log_pop_indent();
}

/* Initialize the media line */
pj_status_t pjsua_call_media_init(pjsua_call_media *call_med,
                                  pjmedia_type type,
                                  const pjsua_transport_config *tcfg,
                                  int security_level,
                                  int *sip_err_code,
                                  pj_bool_t async,
                                  pjsua_med_tp_state_cb cb)
{
    PJ_LOG(2,(THIS_FILE, "pjsua_call_media_init"));
    
    pj_status_t status = PJ_SUCCESS;
    
    /*
     * Note: this function may be called when the media already exists
     * (e.g. in reinvites, updates, etc.)
     */
    call_med->type = type;
    
    /* Create the media transport for initial call. */
    if (call_med->tp == NULL) {
#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
        PJ_LOG(2,(THIS_FILE, "Execució si pjmedia_has_video"));
        pjsua_acc *acc = &pjsua_var.acc[call_med->call->acc_id];
        
        /* While in initial call, set default video devices */
        if (type == PJMEDIA_TYPE_VIDEO) {
            call_med->strm.v.rdr_dev = acc->cfg.vid_rend_dev;
            call_med->strm.v.cap_dev = acc->cfg.vid_cap_dev;
            if (call_med->strm.v.rdr_dev == PJMEDIA_VID_DEFAULT_RENDER_DEV) {
                pjmedia_vid_dev_info info;
                pjmedia_vid_dev_get_info(call_med->strm.v.rdr_dev, &info);
                call_med->strm.v.rdr_dev = info.id;
            }
            if (call_med->strm.v.cap_dev == PJMEDIA_VID_DEFAULT_CAPTURE_DEV) {
                pjmedia_vid_dev_info info;
                pjmedia_vid_dev_get_info(call_med->strm.v.cap_dev, &info);
                call_med->strm.v.cap_dev = info.id;
            }
        }
#endif
        
        set_media_tp_state(call_med, PJSUA_MED_TP_CREATING);
        
        if (pjsua_var.media_cfg.enable_ice) {
            status = create_ice_media_transport(tcfg, call_med, async);
            if (async && status == PJ_EPENDING) {
                /* We will resume call media initialization in the
                 * on_ice_complete() callback.
                 */
                call_med->med_create_cb = &call_media_init_cb;
                call_med->med_init_cb = cb;
                
                return PJ_EPENDING;
            }
        } else {
            status = create_udp_media_transport(tcfg, call_med);
        }
        
        if (status != PJ_SUCCESS) {
            PJ_PERROR(1,(THIS_FILE, status, "Error creating media transport"));
            return status;
        }
        
        /* Media transport creation completed immediately, so 
         * we don't need to call the callback.
         */
        call_med->med_init_cb = NULL;
        
    } else if (call_med->tp_st == PJSUA_MED_TP_DISABLED) {
        /* Media is being reenabled. */
        set_media_tp_state(call_med, PJSUA_MED_TP_INIT);
    }
    
    return call_media_init_cb(call_med, status, security_level,
                              sip_err_code);
}

pj_status_t pjsua_media_channel_deinit(pjsua_call_id call_id)
{
    PJ_LOG(3,(THIS_FILE, "pjsua_media_channel_deinit"));
    
    pjsua_call *call = &pjsua_var.calls[call_id];
    unsigned mi;

    PJSUA_LOCK();
    for (mi=0; mi<call->med_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media[mi];

        if (call_med->tp_st == PJSUA_MED_TP_CREATING) {
            /* We will do the deinitialization after media transport
             * creation is completed.
             */
            call->async_call.med_ch_deinit = PJ_TRUE;
            PJSUA_UNLOCK();
            return PJ_SUCCESS;
        }
        
        if (call_med->channel_id != -1) {
            VoEBase* base = VoEBase::GetInterface(voe);
            
            PJ_LOG(5,(THIS_FILE, "VoiceEngine::StopPlayout(%d)", call_med->channel_id));
            base->StopPlayout(call_med->channel_id);

            PJ_LOG(5,(THIS_FILE, "VoiceEngine::StopSend(%d)", call_med->channel_id));
            base->StopSend(call_med->channel_id);
            
            PJ_LOG(5,(THIS_FILE, "VoiceEngine::DeleteChannel(%d)", call_med->channel_id));
            base->DeleteChannel(call_med->channel_id);
            
            base->Release();

/*
            pjmedia_aud_dev_route route = PJMEDIA_AUD_DEV_ROUTE_DEFAULT;
            pjsua_snd_set_setting(PJMEDIA_AUD_DEV_CAP_OUTPUT_ROUTE, &route, PJ_TRUE); //Default device
            pjsua_conf_adjust_tx_level(0, 1);  //Unmute
*/
            
            call_med->channel_id = -1;
        }
    }
    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Call %d: deinitializing media..", call_id));
    pj_log_push_indent();

    for (mi=0; mi<call->med_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media[mi];

        if (call_med->type == PJMEDIA_TYPE_AUDIO && call_med->strm.a.stream)
            pjmedia_stream_send_rtcp_bye(call_med->strm.a.stream);
    }

    stop_media_session(call_id);

    for (mi=0; mi<call->med_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media[mi];

        if (call_med->tp_st > PJSUA_MED_TP_IDLE) {
	    pjmedia_transport_media_stop(call_med->tp);
	    set_media_tp_state(call_med, PJSUA_MED_TP_IDLE);
	}

	//if (call_med->tp_orig && call_med->tp &&
	//	call_med->tp != call_med->tp_orig)
	//{
	//    pjmedia_transport_close(call_med->tp);
	//    call_med->tp = call_med->tp_orig;
	//}
	if (call_med->tp) {
	    pjmedia_transport_close(call_med->tp);
	    call_med->tp = call_med->tp_orig = NULL;
	}
        call_med->tp_orig = NULL;
    }

    //check_snd_dev_idle();
    pj_log_pop_indent();

    return PJ_SUCCESS;
}


/*
 * DTMF callback from the stream.
 */
static void dtmf_callback(pjmedia_stream *strm, void *user_data,
			  int digit)
{
    PJ_UNUSED_ARG(strm);

    pj_log_push_indent();

    /* For discussions about call mutex protection related to this 
     * callback, please see ticket #460:
     *	http://trac.pjsip.org/repos/ticket/460#comment:4
     */
    if (pjsua_var.ua_cfg.cb.on_dtmf_digit) {
	pjsua_call_id call_id;

	call_id = (pjsua_call_id)(long)user_data;
	pjsua_var.ua_cfg.cb.on_dtmf_digit(call_id, digit);
    }

    pj_log_pop_indent();
}


static pj_status_t audio_channel_update(pjsua_call_media *call_med,
                                        pj_pool_t *tmp_pool,
				        const pjmedia_sdp_session *local_sdp,
				        const pjmedia_sdp_session *remote_sdp)
{
    PJ_LOG(3,(THIS_FILE, "audio_channel_update"));
    
    pjsua_call *call = call_med->call;
    pjmedia_stream_info the_si, *si = &the_si;
    pjmedia_port *media_port;
    unsigned strm_idx = call_med->idx;
    pj_status_t status;

    PJ_LOG(4,(THIS_FILE,"Audio channel update.."));
    pj_log_push_indent();
    
    status = pjmedia_stream_info_from_sdp(si, tmp_pool, pjsua_var.med_endpt,
                                          local_sdp, remote_sdp, strm_idx);
    if (status != PJ_SUCCESS)
	goto on_return;

    si->rtcp_sdes_bye_disabled = PJ_TRUE;

    /* Check if no media is active */
    if (si->dir == PJMEDIA_DIR_NONE) {
	/* Call media state */
	call_med->state = PJSUA_CALL_MEDIA_NONE;

	/* Call media direction */
	call_med->dir = PJMEDIA_DIR_NONE;

    } else {
//	pjmedia_transport_info tp_info;

//	/* Start/restart media transport */
//	status = pjmedia_transport_media_start(call_med->tp,
//					       tmp_pool, local_sdp,
//					       remote_sdp, strm_idx);
//	if (status != PJ_SUCCESS)
//	    goto on_return;
//
//	set_media_tp_state(call_med, PJSUA_MED_TP_RUNNING);
//
//	/* Get remote SRTP usage policy */
//	pjmedia_transport_info_init(&tp_info);
//	pjmedia_transport_get_info(call_med->tp, &tp_info);
//	if (tp_info.specific_info_cnt > 0) {
//	    unsigned i;
//	    for (i = 0; i < tp_info.specific_info_cnt; ++i) {
//		if (tp_info.spc_info[i].type == PJMEDIA_TRANSPORT_TYPE_SRTP) 
//		{
//		    pjmedia_srtp_info *srtp_info = 
//				(pjmedia_srtp_info*) tp_info.spc_info[i].buffer;
//
//		    call_med->rem_srtp_use = srtp_info->peer_use;
//		    break;
//		}
//	    }
//	}
//
//	/* Override ptime, if this option is specified. */
//	if (pjsua_var.media_cfg.ptime != 0) {
//	    si->param->setting.frm_per_pkt = (pj_uint8_t)
//		(pjsua_var.media_cfg.ptime / si->param->info.frm_ptime);
//	    if (si->param->setting.frm_per_pkt == 0)
//		si->param->setting.frm_per_pkt = 1;
//	}
//
//	/* Disable VAD, if this option is specified. */
//	if (pjsua_var.media_cfg.no_vad) {
//	    si->param->setting.vad = 0;
//	}
//
//
//	/* Optionally, application may modify other stream settings here
//	 * (such as jitter buffer parameters, codec ptime, etc.)
//	 */
//	si->jb_init = pjsua_var.media_cfg.jb_init;
//	si->jb_min_pre = pjsua_var.media_cfg.jb_min_pre;
//	si->jb_max_pre = pjsua_var.media_cfg.jb_max_pre;
//	si->jb_max = pjsua_var.media_cfg.jb_max;
//
//	/* Set SSRC */
//	si->ssrc = call_med->ssrc;
//
//	/* Set RTP timestamp & sequence, normally these value are intialized
//	 * automatically when stream session created, but for some cases (e.g:
//	 * call reinvite, call update) timestamp and sequence need to be kept
//	 * contigue.
//	 */
//	si->rtp_ts = call_med->rtp_tx_ts;
//	si->rtp_seq = call_med->rtp_tx_seq;
//	si->rtp_seq_ts_set = call_med->rtp_tx_seq_ts_set;
//
//#if defined(PJMEDIA_STREAM_ENABLE_KA) && PJMEDIA_STREAM_ENABLE_KA!=0
//	/* Enable/disable stream keep-alive and NAT hole punch. */
//	si->use_ka = pjsua_var.acc[call->acc_id].cfg.use_stream_ka;
//#endif
//
//	/* Create session based on session info. */
//	status = pjmedia_stream_create(pjsua_var.med_endpt, NULL, si,
//				       call_med->tp, NULL,
//				       &call_med->strm.a.stream);
//	if (status != PJ_SUCCESS) {
//	    goto on_return;
//	}
//
//	/* Start stream */
//	status = pjmedia_stream_start(call_med->strm.a.stream);
//	if (status != PJ_SUCCESS) {
//	    goto on_return;
//	}
//
//        if (call_med->prev_state == PJSUA_CALL_MEDIA_NONE)
//            pjmedia_stream_send_rtcp_sdes(call_med->strm.a.stream);
//
//	/* If DTMF callback is installed by application, install our
//	 * callback to the session.
//	 */
//	if (pjsua_var.ua_cfg.cb.on_dtmf_digit) {
//	    pjmedia_stream_set_dtmf_callback(call_med->strm.a.stream,
//					     &dtmf_callback,
//					     (void*)(long)(call->index));
//	}

	/* Get the port interface of the first stream in the session.
	 * We need the port interface to add to the conference bridge.
	 */
//	pjmedia_stream_get_port(call_med->strm.a.stream, &media_port);
//
//	/* Notify application about stream creation.
//	 * Note: application may modify media_port to point to different
//	 * media port
//	 */
//	if (pjsua_var.ua_cfg.cb.on_stream_created) {
//	    pjsua_var.ua_cfg.cb.on_stream_created(call->index,
//						  call_med->strm.a.stream,
//						  strm_idx, &media_port);
//	}
//
//	/*
//	 * Add the call to conference bridge.
//	 */
//	{
//	    char tmp[PJSIP_MAX_URL_SIZE];
//	    pj_str_t port_name;
//
//	    port_name.ptr = tmp;
//	    port_name.slen = pjsip_uri_print(PJSIP_URI_IN_REQ_URI,
//					     call->inv->dlg->remote.info->uri,
//					     tmp, sizeof(tmp));
//	    if (port_name.slen < 1) {
//		port_name = pj_str("call");
//	    }
//	    status = pjmedia_conf_add_port( pjsua_var.mconf, 
//					    call->inv->pool_prov,
//					    media_port, 
//					    &port_name,
//					    (unsigned*)
//					    &call_med->strm.a.conf_slot);
//	    if (status != PJ_SUCCESS) {
//		goto on_return;
//	    }
//	}
        
    int channel = call_med->channel_id;

    VoEBase* base = VoEBase::GetInterface(voe);
    VoECodec* codec = VoECodec::GetInterface(voe);
    VoERTP_RTCP* rtpRtcp = VoERTP_RTCP::GetInterface(voe);
    rtpRtcp->SetRTCPStatus(channel, true);
    rtpRtcp->Release();
    CodecInst codecInst;
    for (int i=0; i<codec->NumOfCodecs(); i++) {
        codec->GetCodec(i, codecInst);  //0-ISAC16 5-PCM
        if (strnicmp(codecInst.plname, si->fmt.encoding_name.ptr, si->fmt.encoding_name.slen) == 0) {
            codecInst.pltype = si->tx_pt;
            if (i == 0) { //ISAC WB
                codecInst.rate = -1; //Adaptive
            }
            PJ_LOG(5,(THIS_FILE, "VoiceEngine::SetSendCodec(%d, %s, %d)", channel, codecInst.plname, codecInst.pltype));
            codec->SetSendCodec(channel, codecInst);
            PJ_LOG(5,(THIS_FILE, "VoiceEngine::SetRecPayloadType(%d, %s, %d)", channel, codecInst.plname, codecInst.pltype));
            codec->SetRecPayloadType(channel, codecInst);
            break;
        }
    }
    codec->Release();

    /* Check that remote can receive DTMF events. */
    if (si->tx_event_pt >= 0) {
        VoEDtmf* dtmf = VoEDtmf::GetInterface(voe);
        PJ_LOG(5,(THIS_FILE, "VoiceEngine::SetSendTelephoneEventPayloadType(%d, %d)", channel, si->tx_event_pt));
        dtmf->SetSendTelephoneEventPayloadType(channel, si->tx_event_pt);
        dtmf->Release();
    }
    
    char remote_addr[PJ_INET6_ADDRSTRLEN];
    pj_inet_ntop2(si->rem_addr.addr.sa_family, pj_sockaddr_get_addr(&si->rem_addr), remote_addr, sizeof(remote_addr)); 
    pj_uint16_t remote_port = pj_sockaddr_get_port(&si->rem_addr);
    PJ_LOG(5,(THIS_FILE, "VoiceEngine::SetSendDestination(%d, %s:%d)", channel, remote_addr, remote_port));
    base->SetSendDestination(channel, remote_port, remote_addr);
    PJ_LOG(5,(THIS_FILE, "VoiceEngine::StartReceive(%d)", channel));
    base->StartReceive(channel);
    PJ_LOG(5,(THIS_FILE, "VoiceEngine::StartSend(%d)", channel));
    base->StartSend(channel);
    PJ_LOG(5,(THIS_FILE, "VoiceEngine::StartPlayout(%d)", channel));
    base->StartPlayout(channel);
        
    base->Release();

	/* Call media direction */
	call_med->dir = si->dir;

	/* Call media state */
	if (call->local_hold)
	    call_med->state = PJSUA_CALL_MEDIA_LOCAL_HOLD;
	else if (call_med->dir == PJMEDIA_DIR_DECODING)
	    call_med->state = PJSUA_CALL_MEDIA_REMOTE_HOLD;
	else
	    call_med->state = PJSUA_CALL_MEDIA_ACTIVE;
    }

    /* Print info. */
    {
	char info[80];
	int info_len = 0;
	int len;
	const char *dir;

	switch (si->dir) {
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
			       ", stream #%d: %.*s[%d] (%s)", strm_idx,
			       (int)si->fmt.encoding_name.slen,
			       si->fmt.encoding_name.ptr,
                   si->tx_pt,
			       dir);
	if (len > 0)
	    info_len += len;
	PJ_LOG(4,(THIS_FILE,"Audio updated%s", info));
    }

on_return:
    pj_log_pop_indent();
    return status;
}

pj_status_t pjsua_media_channel_update(pjsua_call_id call_id,
				       const pjmedia_sdp_session *local_sdp,
				       const pjmedia_sdp_session *remote_sdp)
{
    PJ_LOG(3,(THIS_FILE, "pjsua_media_channel_update"));
    
    pjsua_call *call = &pjsua_var.calls[call_id];
    pjsua_acc *acc = &pjsua_var.acc[call->acc_id];
    pj_pool_t *tmp_pool = call->inv->pool_prov;
    unsigned mi;
    pj_bool_t got_media = PJ_FALSE;
    pj_status_t status = PJ_SUCCESS;

    const pj_str_t STR_AUDIO = { "audio", 5 };
    const pj_str_t STR_VIDEO = { "video", 5 };
    pj_uint8_t maudidx[PJSUA_MAX_CALL_MEDIA];
    unsigned maudcnt = PJ_ARRAY_SIZE(maudidx);
    unsigned mtotaudcnt = PJ_ARRAY_SIZE(maudidx);
    pj_uint8_t mvididx[PJSUA_MAX_CALL_MEDIA];
    unsigned mvidcnt = PJ_ARRAY_SIZE(mvididx);
    unsigned mtotvidcnt = PJ_ARRAY_SIZE(mvididx);
    pj_bool_t need_renego_sdp = PJ_FALSE;

    if (pjsua_get_state() != PJSUA_STATE_RUNNING)
	return PJ_EBUSY;

    PJ_LOG(4,(THIS_FILE, "Call %d: updating media..", call_id));
    pj_log_push_indent();

    /* Destroy existing media session, if any. */
    stop_media_session(call->index);

    /* Call media count must be at least equal to SDP media. Note that
     * it may not be equal when remote removed any SDP media line.
     */
    pj_assert(call->med_cnt >= local_sdp->media_count);

    /* Reset audio_idx first */
    call->audio_idx = -1;

    /* Sort audio/video based on "quality" */
    sort_media(local_sdp, &STR_AUDIO, acc->cfg.use_srtp,
	       maudidx, &maudcnt, &mtotaudcnt);
#if PJMEDIA_HAS_VIDEO
    sort_media(local_sdp, &STR_VIDEO, acc->cfg.use_srtp,
	       mvididx, &mvidcnt, &mtotvidcnt);
#else
    PJ_UNUSED_ARG(STR_VIDEO);
    mvidcnt = mtotvidcnt = 0;
#endif

    /* Applying media count limitation. Note that in generating SDP answer,
     * no media count limitation applied, as we didn't know yet which media
     * would pass the SDP negotiation.
     */
    if (maudcnt > call->opt.aud_cnt || mvidcnt > call->opt.vid_cnt)
    {
	pjmedia_sdp_session *local_sdp2;

	maudcnt = PJ_MIN(maudcnt, call->opt.aud_cnt);
	mvidcnt = PJ_MIN(mvidcnt, call->opt.vid_cnt);
	local_sdp2 = pjmedia_sdp_session_clone(tmp_pool, local_sdp);

	for (mi=0; mi < local_sdp2->media_count; ++mi) {
	    pjmedia_sdp_media *m = local_sdp2->media[mi];

	    if (m->desc.port == 0 ||
		pj_memchr(maudidx, mi, maudcnt*sizeof(maudidx[0])) ||
		pj_memchr(mvididx, mi, mvidcnt*sizeof(mvididx[0])))
	    {
		continue;
	    }
	    
	    /* Deactivate this media */
	    pjmedia_sdp_media_deactivate(tmp_pool, m);
	}

	local_sdp = local_sdp2;
	need_renego_sdp = PJ_TRUE;
    }

    /* Process each media stream */
    for (mi=0; mi < call->med_cnt; ++mi) {
	pjsua_call_media *call_med = &call->media[mi];

	if (mi >= local_sdp->media_count ||
	    mi >= remote_sdp->media_count)
	{
	    /* This may happen when remote removed any SDP media lines in
	     * its re-offer.
	     */
	    continue;
#if 0
	    /* Something is wrong */
	    PJ_LOG(1,(THIS_FILE, "Error updating media for call %d: "
		      "invalid media index %d in SDP", call_id, mi));
	    status = PJMEDIA_SDP_EINSDP;
	    goto on_error;
#endif
	}

	switch (call_med->type) {
	case PJMEDIA_TYPE_AUDIO:
	    status = audio_channel_update(call_med, tmp_pool,
	                                  local_sdp, remote_sdp);
	    if (call->audio_idx==-1 && status==PJ_SUCCESS &&
		call_med->strm.a.stream)
	    {
		call->audio_idx = mi;
	    }
	    break;
#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0)
	case PJMEDIA_TYPE_VIDEO:
	    status = video_channel_update(call_med, tmp_pool,
	                                  local_sdp, remote_sdp);
	    break;
#endif
	default:
	    status = PJMEDIA_EINVALIMEDIATYPE;
	    break;
	}

	/* Close the transport of deactivated media, need this here as media
	 * can be deactivated by the SDP negotiation and the max media count
	 * (account) setting.
	 */
	if (local_sdp->media[mi]->desc.port==0 && call_med->tp) {
	    pjmedia_transport_close(call_med->tp);
	    call_med->tp = call_med->tp_orig = NULL;
	    set_media_tp_state(call_med, PJSUA_MED_TP_IDLE);
	}

	if (status != PJ_SUCCESS) {
	    PJ_PERROR(1,(THIS_FILE, status, "Error updating media call%02d:%d",
		         call_id, mi));
	} else {
	    got_media = PJ_TRUE;
	}
    }

    /* Perform SDP re-negotiation if needed. */
    if (got_media && need_renego_sdp) {
	pjmedia_sdp_neg *neg = call->inv->neg;

	/* This should only happen when we are the answerer. */
	PJ_ASSERT_RETURN(neg && !pjmedia_sdp_neg_was_answer_remote(neg),
			 PJMEDIA_SDPNEG_EINSTATE);
	
	status = pjmedia_sdp_neg_set_remote_offer(tmp_pool, neg, remote_sdp);
	if (status != PJ_SUCCESS)
	    goto on_error;

	status = pjmedia_sdp_neg_set_local_answer(tmp_pool, neg, local_sdp);
	if (status != PJ_SUCCESS)
	    goto on_error;

	status = pjmedia_sdp_neg_negotiate(tmp_pool, neg, 0);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }

    pj_log_pop_indent();
    return (got_media? PJ_SUCCESS : PJMEDIA_SDPNEG_ENOMEDIA);

on_error:
    pj_log_pop_indent();
    return status;
}

/*
 * Get maxinum number of conference ports.
 */
PJ_DEF(unsigned) pjsua_conf_get_max_ports(void)
{
    return pjsua_var.media_cfg.max_media_ports;
}


/*
 * Get current number of active ports in the bridge.
 */
PJ_DEF(unsigned) pjsua_conf_get_active_ports(void)
{
    unsigned ports[PJSUA_MAX_CONF_PORTS];
    unsigned count = PJ_ARRAY_SIZE(ports);
    pj_status_t status;

    status = pjmedia_conf_enum_ports(pjsua_var.mconf, ports, &count);
    if (status != PJ_SUCCESS)
	count = 0;

    return count;
}


/*
 * Enumerate all conference ports.
 */
PJ_DEF(pj_status_t) pjsua_enum_conf_ports(pjsua_conf_port_id id[],
					  unsigned *count)
{
    return pjmedia_conf_enum_ports(pjsua_var.mconf, (unsigned*)id, count);
}


/*
 * Get information about the specified conference port
 */
PJ_DEF(pj_status_t) pjsua_conf_get_port_info( pjsua_conf_port_id id,
					      pjsua_conf_port_info *info)
{
    pjmedia_conf_port_info cinfo;
    unsigned i;
    pj_status_t status;

    status = pjmedia_conf_get_port_info( pjsua_var.mconf, id, &cinfo);
    if (status != PJ_SUCCESS)
	return status;

    pj_bzero(info, sizeof(*info));
    info->slot_id = id;
    info->name = cinfo.name;
    info->clock_rate = cinfo.clock_rate;
    info->channel_count = cinfo.channel_count;
    info->samples_per_frame = cinfo.samples_per_frame;
    info->bits_per_sample = cinfo.bits_per_sample;

    /* Build array of listeners */
    info->listener_cnt = cinfo.listener_cnt;
    for (i=0; i<cinfo.listener_cnt; ++i) {
	info->listeners[i] = cinfo.listener_slots[i];
    }

    return PJ_SUCCESS;
}


/*
 * Add arbitrary media port to PJSUA's conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_conf_add_port( pj_pool_t *pool,
					 pjmedia_port *port,
					 pjsua_conf_port_id *p_id)
{
    pj_status_t status;

    status = pjmedia_conf_add_port(pjsua_var.mconf, pool,
				   port, NULL, (unsigned*)p_id);
    if (status != PJ_SUCCESS) {
	if (p_id)
	    *p_id = PJSUA_INVALID_ID;
    }

    return status;
}


/*
 * Remove arbitrary slot from the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_conf_remove_port(pjsua_conf_port_id id)
{
    pj_status_t status;

    status = pjmedia_conf_remove_port(pjsua_var.mconf, (unsigned)id);
    //check_snd_dev_idle();

    return status;
}


/*
 * Establish unidirectional media flow from souce to sink. 
 */
PJ_DEF(pj_status_t) pjsua_conf_connect( pjsua_conf_port_id source,
					pjsua_conf_port_id sink)
{
    PJ_LOG(3,(THIS_FILE, "pjsua_conf_connect"));
    
    pj_status_t status = PJ_SUCCESS;

    PJ_LOG(4,(THIS_FILE, "%s connect: %d --> %d",
	      (pjsua_var.is_mswitch ? "Switch" : "Conf"),
	      source, sink));
    pj_log_push_indent();

    /* If sound device idle timer is active, cancel it first. */
    PJSUA_LOCK();
    if (pjsua_var.snd_idle_timer.id) {
	pjsip_endpt_cancel_timer(pjsua_var.endpt, &pjsua_var.snd_idle_timer);
	pjsua_var.snd_idle_timer.id = PJ_FALSE;
    }
    PJSUA_UNLOCK();


    /* For audio switchboard (i.e. APS-Direct):
     * Check if sound device need to be reopened, i.e: its attributes
     * (format, clock rate, channel count) must match to peer's. 
     * Note that sound device can be reopened only if it doesn't have
     * any connection.
     */
    if (pjsua_var.is_mswitch) {
	pjmedia_conf_port_info port0_info;
	pjmedia_conf_port_info peer_info;
	unsigned peer_id;
	pj_bool_t need_reopen = PJ_FALSE;

	peer_id = (source!=0)? source : sink;
	status = pjmedia_conf_get_port_info(pjsua_var.mconf, peer_id, 
					    &peer_info);
	pj_assert(status == PJ_SUCCESS);

	status = pjmedia_conf_get_port_info(pjsua_var.mconf, 0, &port0_info);
	pj_assert(status == PJ_SUCCESS);

	/* Check if sound device is instantiated. */
	need_reopen = (pjsua_var.snd_port==NULL && pjsua_var.null_snd==NULL && 
		      !pjsua_var.no_snd);

	/* Check if sound device need to reopen because it needs to modify 
	 * settings to match its peer. Sound device must be idle in this case 
	 * though.
	 */
	if (!need_reopen && 
	    port0_info.listener_cnt==0 && port0_info.transmitter_cnt==0) 
	{
	    need_reopen = (peer_info.format.id != port0_info.format.id ||
			   peer_info.format.det.aud.avg_bps !=
				   port0_info.format.det.aud.avg_bps ||
			   peer_info.clock_rate != port0_info.clock_rate ||
			   peer_info.channel_count!=port0_info.channel_count);
	}

	if (need_reopen) {
	    if (pjsua_var.cap_dev != NULL_SND_DEV_ID) {
		pjmedia_snd_port_param param;

		/* Create parameter based on peer info */
		status = create_aud_param(&param.base, pjsua_var.cap_dev, 
					  pjsua_var.play_dev,
					  peer_info.clock_rate,
					  peer_info.channel_count,
					  peer_info.samples_per_frame,
					  peer_info.bits_per_sample);
		if (status != PJ_SUCCESS) {
		    pjsua_perror(THIS_FILE, "Error opening sound device",
				 status);
		    goto on_return;
		}

		/* And peer format */
		if (peer_info.format.id != PJMEDIA_FORMAT_PCM) {
		    param.base.flags |= PJMEDIA_AUD_DEV_CAP_EXT_FORMAT;
		    param.base.ext_fmt = peer_info.format;
		}

		param.options = 0;
		status = open_snd_dev(&param);
		if (status != PJ_SUCCESS) {
		    pjsua_perror(THIS_FILE, "Error opening sound device",
				 status);
		    goto on_return;
		}
	    } else {
		/* Null-audio */
		status = pjsua_set_snd_dev(pjsua_var.cap_dev,
					   pjsua_var.play_dev);
		if (status != PJ_SUCCESS) {
		    pjsua_perror(THIS_FILE, "Error opening sound device",
				 status);
		    goto on_return;
		}
	    }
	} else if (pjsua_var.no_snd) {
	    if (!pjsua_var.snd_is_on) {
		pjsua_var.snd_is_on = PJ_TRUE;
	    	/* Notify app */
	    	if (pjsua_var.ua_cfg.cb.on_snd_dev_operation) {
	    	    (*pjsua_var.ua_cfg.cb.on_snd_dev_operation)(1);
	    	}
	    }
	}

    } else {
	/* The bridge version */

	/* Create sound port if none is instantiated */
	if (pjsua_var.snd_port==NULL && pjsua_var.null_snd==NULL && 
	    !pjsua_var.no_snd) 
	{
	    pj_status_t status;

	    status = pjsua_set_snd_dev(pjsua_var.cap_dev, pjsua_var.play_dev);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "Error opening sound device", status);
		goto on_return;
	    }
	} else if (pjsua_var.no_snd && !pjsua_var.snd_is_on) {
	    pjsua_var.snd_is_on = PJ_TRUE;
	    /* Notify app */
	    if (pjsua_var.ua_cfg.cb.on_snd_dev_operation) {
		(*pjsua_var.ua_cfg.cb.on_snd_dev_operation)(1);
	    }
	}
    }

    status = pjmedia_conf_connect_port(pjsua_var.mconf, source, sink, 0);

on_return:
    pj_log_pop_indent();
    return status;
}


/*
 * Disconnect media flow from the source to destination port.
 */
PJ_DEF(pj_status_t) pjsua_conf_disconnect( pjsua_conf_port_id source,
					   pjsua_conf_port_id sink)
{
    pj_status_t status;

    PJ_LOG(4,(THIS_FILE, "%s disconnect: %d -x- %d",
	      (pjsua_var.is_mswitch ? "Switch" : "Conf"),
	      source, sink));
    pj_log_push_indent();

    status = pjmedia_conf_disconnect_port(pjsua_var.mconf, source, sink);
    //check_snd_dev_idle();

    pj_log_pop_indent();
    return status;
}


/*
 * Adjust the signal level to be transmitted from the bridge to the 
 * specified port by making it louder or quieter.
 */
PJ_DEF(pj_status_t) pjsua_conf_adjust_tx_level(pjsua_conf_port_id slot,
					       float level)
{
    pj_status_t status;

    VoEVolumeControl* volumeControl = VoEVolumeControl::GetInterface(voe);
        
    if (level == 0) {  //Mute
        volumeControl->SetInputMute(-1, true);   
    }
    else {  //Unmute
        volumeControl->SetInputMute(-1, false);
    }
    volumeControl->Release();

    return status;
}

/*
 * Adjust the signal level to be received from the specified port (to
 * the bridge) by making it louder or quieter.
 */
PJ_DEF(pj_status_t) pjsua_conf_adjust_rx_level(pjsua_conf_port_id slot,
					       float level)
{
    return pjmedia_conf_adjust_rx_level(pjsua_var.mconf, slot,
					(int)((level-1) * 128));
}


/*
 * Get last signal level transmitted to or received from the specified port.
 */
PJ_DEF(pj_status_t) pjsua_conf_get_signal_level(pjsua_conf_port_id slot,
						unsigned *tx_level,
						unsigned *rx_level)
{
    return pjmedia_conf_get_signal_level(pjsua_var.mconf, slot, 
					 tx_level, rx_level);
}

/*****************************************************************************
 * File player.
 */

static char* get_basename(const char *path, unsigned len)
{
    char *p = ((char*)path) + len;

    if (len==0)
	return p;

    for (--p; p!=path && *p!='/' && *p!='\\'; ) --p;

    return (p==path) ? p : p+1;
}


/*
 * Create a file player, and automatically connect this player to
 * the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_player_create( const pj_str_t *filename,
					 unsigned options,
					 pjsua_player_id *p_id)
{
    unsigned slot, file_id;
    char path[PJ_MAXPATH];
    pj_pool_t *pool = NULL;
    pjmedia_port *port;
    pj_status_t status = PJ_SUCCESS;

    if (pjsua_var.player_cnt >= PJ_ARRAY_SIZE(pjsua_var.player))
	return PJ_ETOOMANY;

    PJ_LOG(4,(THIS_FILE, "Creating file player: %.*s..",
	      (int)filename->slen, filename->ptr));
    pj_log_push_indent();

    PJSUA_LOCK();

    for (file_id=0; file_id<PJ_ARRAY_SIZE(pjsua_var.player); ++file_id) {
	if (pjsua_var.player[file_id].port == NULL)
	    break;
    }

    if (file_id == PJ_ARRAY_SIZE(pjsua_var.player)) {
	/* This is unexpected */
	pj_assert(0);
	status = PJ_EBUG;
	goto on_error;
    }

    pj_memcpy(path, filename->ptr, filename->slen);
    path[filename->slen] = '\0';

    pool = pjsua_pool_create(get_basename(path, filename->slen), 1000, 1000);
    if (!pool) {
	status = PJ_ENOMEM;
	goto on_error;
    }

    status = pjmedia_wav_player_port_create(
				    pool, path,
				    pjsua_var.mconf_cfg.samples_per_frame *
				    1000 / pjsua_var.media_cfg.channel_count /
				    pjsua_var.media_cfg.clock_rate, 
				    options, 0, &port);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to open file for playback", status);
	goto on_error;
    }

    status = pjmedia_conf_add_port(pjsua_var.mconf, pool, 
				   port, filename, &slot);
    if (status != PJ_SUCCESS) {
	pjmedia_port_destroy(port);
	pjsua_perror(THIS_FILE, "Unable to add file to conference bridge", 
		     status);
	goto on_error;
    }

    pjsua_var.player[file_id].type = 0;
    pjsua_var.player[file_id].pool = pool;
    pjsua_var.player[file_id].port = port;
    pjsua_var.player[file_id].slot = slot;

    if (p_id) *p_id = file_id;

    ++pjsua_var.player_cnt;

    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Player created, id=%d, slot=%d", file_id, slot));

    pj_log_pop_indent();
    return PJ_SUCCESS;

on_error:
    PJSUA_UNLOCK();
    if (pool) pj_pool_release(pool);
    pj_log_pop_indent();
    return status;
}


/*
 * Create a file playlist media port, and automatically add the port
 * to the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_playlist_create( const pj_str_t file_names[],
					   unsigned file_count,
					   const pj_str_t *label,
					   unsigned options,
					   pjsua_player_id *p_id)
{
    unsigned slot, file_id, ptime;
    pj_pool_t *pool = NULL;
    pjmedia_port *port;
    pj_status_t status = PJ_SUCCESS;

    if (pjsua_var.player_cnt >= PJ_ARRAY_SIZE(pjsua_var.player))
	return PJ_ETOOMANY;

    PJ_LOG(4,(THIS_FILE, "Creating playlist with %d file(s)..", file_count));
    pj_log_push_indent();

    PJSUA_LOCK();

    for (file_id=0; file_id<PJ_ARRAY_SIZE(pjsua_var.player); ++file_id) {
	if (pjsua_var.player[file_id].port == NULL)
	    break;
    }

    if (file_id == PJ_ARRAY_SIZE(pjsua_var.player)) {
	/* This is unexpected */
	pj_assert(0);
	status = PJ_EBUG;
	goto on_error;
    }


    ptime = pjsua_var.mconf_cfg.samples_per_frame * 1000 / 
	    pjsua_var.media_cfg.clock_rate;

    pool = pjsua_pool_create("playlist", 1000, 1000);
    if (!pool) {
	status = PJ_ENOMEM;
	goto on_error;
    }

    status = pjmedia_wav_playlist_create(pool, label, 
					 file_names, file_count,
					 ptime, options, 0, &port);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create playlist", status);
	goto on_error;
    }

    status = pjmedia_conf_add_port(pjsua_var.mconf, pool, 
				   port, &port->info.name, &slot);
    if (status != PJ_SUCCESS) {
	pjmedia_port_destroy(port);
	pjsua_perror(THIS_FILE, "Unable to add port", status);
	goto on_error;
    }

    pjsua_var.player[file_id].type = 1;
    pjsua_var.player[file_id].pool = pool;
    pjsua_var.player[file_id].port = port;
    pjsua_var.player[file_id].slot = slot;

    if (p_id) *p_id = file_id;

    ++pjsua_var.player_cnt;

    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Playlist created, id=%d, slot=%d", file_id, slot));

    pj_log_pop_indent();

    return PJ_SUCCESS;

on_error:
    PJSUA_UNLOCK();
    if (pool) pj_pool_release(pool);
    pj_log_pop_indent();

    return status;
}


/*
 * Get conference port ID associated with player.
 */
PJ_DEF(pjsua_conf_port_id) pjsua_player_get_conf_port(pjsua_player_id id)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);

    return pjsua_var.player[id].slot;
}

/*
 * Get the media port for the player.
 */
PJ_DEF(pj_status_t) pjsua_player_get_port( pjsua_player_id id,
					   pjmedia_port **p_port)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(p_port != NULL, PJ_EINVAL);
    
    *p_port = pjsua_var.player[id].port;

    return PJ_SUCCESS;
}

/*
 * Set playback position.
 */
PJ_DEF(pj_status_t) pjsua_player_set_pos( pjsua_player_id id,
					  pj_uint32_t samples)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].type == 0, PJ_EINVAL);

    return pjmedia_wav_player_port_set_pos(pjsua_var.player[id].port, samples);
}


/*
 * Close the file, remove the player from the bridge, and free
 * resources associated with the file player.
 */
PJ_DEF(pj_status_t) pjsua_player_destroy(pjsua_player_id id)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Destroying player %d..", id));
    pj_log_push_indent();

    PJSUA_LOCK();

    if (pjsua_var.player[id].port) {
	pjsua_conf_remove_port(pjsua_var.player[id].slot);
	pjmedia_port_destroy(pjsua_var.player[id].port);
	pjsua_var.player[id].port = NULL;
	pjsua_var.player[id].slot = 0xFFFF;
	pj_pool_release(pjsua_var.player[id].pool);
	pjsua_var.player[id].pool = NULL;
	pjsua_var.player_cnt--;
    }

    PJSUA_UNLOCK();
    pj_log_pop_indent();

    return PJ_SUCCESS;
}


/*****************************************************************************
 * File recorder.
 */

/*
 * Create a file recorder, and automatically connect this recorder to
 * the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_recorder_create( const pj_str_t *filename,
					   unsigned enc_type,
					   void *enc_param,
					   pj_ssize_t max_size,
					   unsigned options,
					   pjsua_recorder_id *p_id)
{
    enum Format
    {
	FMT_UNKNOWN,
	FMT_WAV,
	FMT_MP3,
    };
    unsigned slot, file_id;
    char path[PJ_MAXPATH];
    pj_str_t ext;
    int file_format;
    pj_pool_t *pool = NULL;
    pjmedia_port *port;
    pj_status_t status = PJ_SUCCESS;

    /* Filename must present */
    PJ_ASSERT_RETURN(filename != NULL, PJ_EINVAL);

    /* Don't support max_size at present */
    PJ_ASSERT_RETURN(max_size == 0 || max_size == -1, PJ_EINVAL);

    /* Don't support encoding type at present */
    PJ_ASSERT_RETURN(enc_type == 0, PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Creating recorder %.*s..",
	      (int)filename->slen, filename->ptr));
    pj_log_push_indent();

    if (pjsua_var.rec_cnt >= PJ_ARRAY_SIZE(pjsua_var.recorder)) {
	pj_log_pop_indent();
	return PJ_ETOOMANY;
    }

    /* Determine the file format */
    ext.ptr = filename->ptr + filename->slen - 4;
    ext.slen = 4;

    if (pj_stricmp2(&ext, ".wav") == 0)
	file_format = FMT_WAV;
    else if (pj_stricmp2(&ext, ".mp3") == 0)
	file_format = FMT_MP3;
    else {
	PJ_LOG(1,(THIS_FILE, "pjsua_recorder_create() error: unable to "
			     "determine file format for %.*s",
			     (int)filename->slen, filename->ptr));
	pj_log_pop_indent();
	return PJ_ENOTSUP;
    }

    PJSUA_LOCK();

    for (file_id=0; file_id<PJ_ARRAY_SIZE(pjsua_var.recorder); ++file_id) {
	if (pjsua_var.recorder[file_id].port == NULL)
	    break;
    }

    if (file_id == PJ_ARRAY_SIZE(pjsua_var.recorder)) {
	/* This is unexpected */
	pj_assert(0);
	status = PJ_EBUG;
	goto on_return;
    }

    pj_memcpy(path, filename->ptr, filename->slen);
    path[filename->slen] = '\0';

    pool = pjsua_pool_create(get_basename(path, filename->slen), 1000, 1000);
    if (!pool) {
	status = PJ_ENOMEM;
	goto on_return;
    }

    if (file_format == FMT_WAV) {
	status = pjmedia_wav_writer_port_create(pool, path, 
						pjsua_var.media_cfg.clock_rate, 
						pjsua_var.mconf_cfg.channel_count,
						pjsua_var.mconf_cfg.samples_per_frame,
						pjsua_var.mconf_cfg.bits_per_sample, 
						options, 0, &port);
    } else {
	PJ_UNUSED_ARG(enc_param);
	port = NULL;
	status = PJ_ENOTSUP;
    }

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to open file for recording", status);
	goto on_return;
    }

    status = pjmedia_conf_add_port(pjsua_var.mconf, pool, 
				   port, filename, &slot);
    if (status != PJ_SUCCESS) {
	pjmedia_port_destroy(port);
	goto on_return;
    }

    pjsua_var.recorder[file_id].port = port;
    pjsua_var.recorder[file_id].slot = slot;
    pjsua_var.recorder[file_id].pool = pool;

    if (p_id) *p_id = file_id;

    ++pjsua_var.rec_cnt;

    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Recorder created, id=%d, slot=%d", file_id, slot));

    pj_log_pop_indent();
    return PJ_SUCCESS;

on_return:
    PJSUA_UNLOCK();
    if (pool) pj_pool_release(pool);
    pj_log_pop_indent();
    return status;
}


/*
 * Get conference port associated with recorder.
 */
PJ_DEF(pjsua_conf_port_id) pjsua_recorder_get_conf_port(pjsua_recorder_id id)
{
    PJ_ASSERT_RETURN(id>=0 && id<(int)PJ_ARRAY_SIZE(pjsua_var.recorder), 
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.recorder[id].port != NULL, PJ_EINVAL);

    return pjsua_var.recorder[id].slot;
}

/*
 * Get the media port for the recorder.
 */
PJ_DEF(pj_status_t) pjsua_recorder_get_port( pjsua_recorder_id id,
					     pjmedia_port **p_port)
{
    PJ_ASSERT_RETURN(id>=0 && id<(int)PJ_ARRAY_SIZE(pjsua_var.recorder), 
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.recorder[id].port != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(p_port != NULL, PJ_EINVAL);

    *p_port = pjsua_var.recorder[id].port;
    return PJ_SUCCESS;
}

/*
 * Destroy recorder (this will complete recording).
 */
PJ_DEF(pj_status_t) pjsua_recorder_destroy(pjsua_recorder_id id)
{
    PJ_ASSERT_RETURN(id>=0 && id<(int)PJ_ARRAY_SIZE(pjsua_var.recorder), 
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.recorder[id].port != NULL, PJ_EINVAL);

    PJ_LOG(4,(THIS_FILE, "Destroying recorder %d..", id));
    pj_log_push_indent();

    PJSUA_LOCK();

    if (pjsua_var.recorder[id].port) {
	pjsua_conf_remove_port(pjsua_var.recorder[id].slot);
	pjmedia_port_destroy(pjsua_var.recorder[id].port);
	pjsua_var.recorder[id].port = NULL;
	pjsua_var.recorder[id].slot = 0xFFFF;
	pj_pool_release(pjsua_var.recorder[id].pool);
	pjsua_var.recorder[id].pool = NULL;
	pjsua_var.rec_cnt--;
    }

    PJSUA_UNLOCK();
    pj_log_pop_indent();

    return PJ_SUCCESS;
}


/*****************************************************************************
 * Sound devices.
 */

/*
 * Enum sound devices.
 */

PJ_DEF(pj_status_t) pjsua_enum_aud_devs( pjmedia_aud_dev_info info[],
					 unsigned *count)
{
    unsigned i, dev_count;

    dev_count = pjmedia_aud_dev_count();
    
    if (dev_count > *count) dev_count = *count;

    for (i=0; i<dev_count; ++i) {
	pj_status_t status;

	status = pjmedia_aud_dev_get_info(i, &info[i]);
	if (status != PJ_SUCCESS)
	    return status;
    }

    *count = dev_count;

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjsua_enum_snd_devs( pjmedia_snd_dev_info info[],
					 unsigned *count)
{
    unsigned i, dev_count;

    dev_count = pjmedia_aud_dev_count();
    
    if (dev_count > *count) dev_count = *count;
    pj_bzero(info, dev_count * sizeof(pjmedia_snd_dev_info));

    for (i=0; i<dev_count; ++i) {
	pjmedia_aud_dev_info ai;
	pj_status_t status;

	status = pjmedia_aud_dev_get_info(i, &ai);
	if (status != PJ_SUCCESS)
	    return status;

	strncpy(info[i].name, ai.name, sizeof(info[i].name));
	info[i].name[sizeof(info[i].name)-1] = '\0';
	info[i].input_count = ai.input_count;
	info[i].output_count = ai.output_count;
	info[i].default_samples_per_sec = ai.default_samples_per_sec;
    }

    *count = dev_count;

    return PJ_SUCCESS;
}

/* Create audio device parameter to open the device */
static pj_status_t create_aud_param(pjmedia_aud_param *param,
				    pjmedia_aud_dev_index capture_dev,
				    pjmedia_aud_dev_index playback_dev,
				    unsigned clock_rate,
				    unsigned channel_count,
				    unsigned samples_per_frame,
				    unsigned bits_per_sample)
{
    pj_status_t status;

    /* Normalize device ID with new convention about default device ID */
    if (playback_dev == PJMEDIA_AUD_DEFAULT_CAPTURE_DEV)
	playback_dev = PJMEDIA_AUD_DEFAULT_PLAYBACK_DEV;

    /* Create default parameters for the device */
    status = pjmedia_aud_dev_default_param(capture_dev, param);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error retrieving default audio "
				"device parameters", status);
	return status;
    }
    param->dir = PJMEDIA_DIR_CAPTURE_PLAYBACK;
    param->rec_id = capture_dev;
    param->play_id = playback_dev;
    param->clock_rate = clock_rate;
    param->channel_count = channel_count;
    param->samples_per_frame = samples_per_frame;
    param->bits_per_sample = bits_per_sample;

    /* Update the setting with user preference */
#define update_param(cap, field)    \
	if (pjsua_var.aud_param.flags & cap) { \
	    param->flags |= cap; \
	    param->field = pjsua_var.aud_param.field; \
	}
    update_param( PJMEDIA_AUD_DEV_CAP_INPUT_VOLUME_SETTING, input_vol);
    update_param( PJMEDIA_AUD_DEV_CAP_OUTPUT_VOLUME_SETTING, output_vol);
    update_param( PJMEDIA_AUD_DEV_CAP_INPUT_ROUTE, input_route);
    update_param( PJMEDIA_AUD_DEV_CAP_OUTPUT_ROUTE, output_route);
#undef update_param

    /* Latency settings */
    param->flags |= (PJMEDIA_AUD_DEV_CAP_INPUT_LATENCY | 
		     PJMEDIA_AUD_DEV_CAP_OUTPUT_LATENCY);
    param->input_latency_ms = pjsua_var.media_cfg.snd_rec_latency;
    param->output_latency_ms = pjsua_var.media_cfg.snd_play_latency;

    /* EC settings */
    if (pjsua_var.media_cfg.ec_tail_len) {
	param->flags |= (PJMEDIA_AUD_DEV_CAP_EC | PJMEDIA_AUD_DEV_CAP_EC_TAIL);
	param->ec_enabled = PJ_TRUE;
	param->ec_tail_ms = pjsua_var.media_cfg.ec_tail_len;
    } else {
	param->flags &= ~(PJMEDIA_AUD_DEV_CAP_EC|PJMEDIA_AUD_DEV_CAP_EC_TAIL);
    }

    return PJ_SUCCESS;
}

/* Internal: the first time the audio device is opened (during app
 *   startup), retrieve the audio settings such as volume level
 *   so that aud_get_settings() will work.
 */
static pj_status_t update_initial_aud_param()
{
    pjmedia_aud_stream *strm;
    pjmedia_aud_param param;
    pj_status_t status;

    PJ_ASSERT_RETURN(pjsua_var.snd_port != NULL, PJ_EBUG);

    strm = pjmedia_snd_port_get_snd_stream(pjsua_var.snd_port);

    status = pjmedia_aud_stream_get_param(strm, &param);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error audio stream "
				"device parameters", status);
	return status;
    }

#define update_saved_param(cap, field)  \
	if (param.flags & cap) { \
	    pjsua_var.aud_param.flags |= cap; \
	    pjsua_var.aud_param.field = param.field; \
	}

    update_saved_param(PJMEDIA_AUD_DEV_CAP_INPUT_VOLUME_SETTING, input_vol);
    update_saved_param(PJMEDIA_AUD_DEV_CAP_OUTPUT_VOLUME_SETTING, output_vol);
    update_saved_param(PJMEDIA_AUD_DEV_CAP_INPUT_ROUTE, input_route);
    update_saved_param(PJMEDIA_AUD_DEV_CAP_OUTPUT_ROUTE, output_route);
#undef update_saved_param

    return PJ_SUCCESS;
}

/* Get format name */
static const char *get_fmt_name(pj_uint32_t id)
{
    static char name[8];

    if (id == PJMEDIA_FORMAT_L16)
	return "PCM";
    pj_memcpy(name, &id, 4);
    name[4] = '\0';
    return name;
}

/* Open sound device with the setting. */
static pj_status_t open_snd_dev(pjmedia_snd_port_param *param)
{
    PJ_LOG(3,(THIS_FILE, "open_snd_dev"));
    
    return PJ_SUCCESS;
}


/* Close existing sound device */
static void close_snd_dev(void)
{
    PJ_LOG(3,(THIS_FILE, "close_snd_dev"));
}


/*
 * Select or change sound device. Application may call this function at
 * any time to replace current sound device.
 */
PJ_DEF(pj_status_t) pjsua_set_snd_dev( int capture_dev,
				       int playback_dev)
{
    PJ_LOG(3,(THIS_FILE, "pjsua_set_snd_dev"));
    
    return PJ_SUCCESS;
}


/*
 * Get currently active sound devices. If sound devices has not been created
 * (for example when pjsua_start() is not called), it is possible that
 * the function returns PJ_SUCCESS with -1 as device IDs.
 */
PJ_DEF(pj_status_t) pjsua_get_snd_dev(int *capture_dev,
				      int *playback_dev)
{
    if (capture_dev) {
	*capture_dev = pjsua_var.cap_dev;
    }
    if (playback_dev) {
	*playback_dev = pjsua_var.play_dev;
    }

    return PJ_SUCCESS;
}


/*
 * Use null sound device.
 */
PJ_DEF(pj_status_t) pjsua_set_null_snd_dev(void)
{
    pjmedia_port *conf_port;
    pj_status_t status;

    PJ_LOG(4,(THIS_FILE, "Setting null sound device.."));
    pj_log_push_indent();


    /* Close existing sound device */
    close_snd_dev();

    /* Notify app */
    if (pjsua_var.ua_cfg.cb.on_snd_dev_operation) {
	(*pjsua_var.ua_cfg.cb.on_snd_dev_operation)(1);
    }

    /* Create memory pool for sound device. */
    pjsua_var.snd_pool = pjsua_pool_create("pjsua_snd", 4000, 4000);
    PJ_ASSERT_RETURN(pjsua_var.snd_pool, PJ_ENOMEM);

    PJ_LOG(4,(THIS_FILE, "Opening null sound device.."));

    /* Get the port0 of the conference bridge. */
    conf_port = pjmedia_conf_get_master_port(pjsua_var.mconf);
    pj_assert(conf_port != NULL);

    /* Create master port, connecting port0 of the conference bridge to
     * a null port.
     */
    status = pjmedia_master_port_create(pjsua_var.snd_pool, pjsua_var.null_port,
					conf_port, 0, &pjsua_var.null_snd);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create null sound device",
		     status);
	pj_log_pop_indent();
	return status;
    }

    /* Start the master port */
    status = pjmedia_master_port_start(pjsua_var.null_snd);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    pjsua_var.cap_dev = NULL_SND_DEV_ID;
    pjsua_var.play_dev = NULL_SND_DEV_ID;

    pjsua_var.no_snd = PJ_FALSE;
    pjsua_var.snd_is_on = PJ_TRUE;

    pj_log_pop_indent();
    return PJ_SUCCESS;
}



/*
 * Use no device!
 */
PJ_DEF(pjmedia_port*) pjsua_set_no_snd_dev(void)
{
    /* Close existing sound device */
    close_snd_dev();

    pjsua_var.no_snd = PJ_TRUE;
    return pjmedia_conf_get_master_port(pjsua_var.mconf);
}


/*
 * Configure the AEC settings of the sound port.
 */
PJ_DEF(pj_status_t) pjsua_set_ec(unsigned tail_ms, unsigned options)
{
    pjsua_var.media_cfg.ec_tail_len = tail_ms;

    if (pjsua_var.snd_port)
	return pjmedia_snd_port_set_ec( pjsua_var.snd_port, pjsua_var.pool,
					tail_ms, options);
    
    return PJ_SUCCESS;
}


/*
 * Get current AEC tail length.
 */
PJ_DEF(pj_status_t) pjsua_get_ec_tail(unsigned *p_tail_ms)
{
    *p_tail_ms = pjsua_var.media_cfg.ec_tail_len;
    return PJ_SUCCESS;
}


/*
 * Check whether the sound device is currently active.
 */
PJ_DEF(pj_bool_t) pjsua_snd_is_active(void)
{
    return pjsua_var.snd_port != NULL;
}


/*
 * Configure sound device setting to the sound device being used. 
 */
PJ_DEF(pj_status_t) pjsua_snd_set_setting( pjmedia_aud_dev_cap cap,
					   const void *pval,
					   pj_bool_t keep)
{
    pj_status_t status = PJMEDIA_EAUD_INVCAP;

    if (cap == PJMEDIA_AUD_DEV_CAP_OUTPUT_ROUTE) {
        VoEHardware* hardware = VoEHardware::GetInterface(voe);

        pjmedia_aud_dev_route route = *(pjmedia_aud_dev_route*)pval;

        if (route == PJMEDIA_AUD_DEV_ROUTE_LOUDSPEAKER) {
            hardware->SetLoudspeakerStatus(true);
        }
        else {
            hardware->SetLoudspeakerStatus(false);
        }

        hardware->Release();
        status = PJ_SUCCESS;
    }

    return status;
}

/*
 * Retrieve a sound device setting.
 */
PJ_DEF(pj_status_t) pjsua_snd_get_setting( pjmedia_aud_dev_cap cap,
					   void *pval)
{
    pj_status_t status = PJMEDIA_EAUD_INVCAP;

    if (cap == PJMEDIA_AUD_DEV_CAP_OUTPUT_ROUTE) {
        VoEHardware* hardware = VoEHardware::GetInterface(voe);

        bool loudspeaker;
        hardware->GetLoudspeakerStatus(loudspeaker);

        pjmedia_aud_dev_route* pRoute = (pjmedia_aud_dev_route*)pval;
        *pRoute = loudspeaker ?
            PJMEDIA_AUD_DEV_ROUTE_LOUDSPEAKER :
            PJMEDIA_AUD_DEV_ROUTE_DEFAULT;
        
        hardware->Release();
        status = PJ_SUCCESS;
    }

    return status;
}


/*****************************************************************************
 * Codecs.
 */

/*
 * Enum all supported codecs in the system.
 */
PJ_DEF(pj_status_t) pjsua_enum_codecs( pjsua_codec_info id[],
				       unsigned *p_count )
{
    pjmedia_codec_mgr *codec_mgr;
    pjmedia_codec_info info[32];
    unsigned i, count, prio[32];
    pj_status_t status;

    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);
    count = PJ_ARRAY_SIZE(info);
    status = pjmedia_codec_mgr_enum_codecs( codec_mgr, &count, info, prio);
    if (status != PJ_SUCCESS) {
	*p_count = 0;
	return status;
    }

    if (count > *p_count) count = *p_count;

    for (i=0; i<count; ++i) {
	pj_bzero(&id[i], sizeof(pjsua_codec_info));

	pjmedia_codec_info_to_id(&info[i], id[i].buf_, sizeof(id[i].buf_));
	id[i].codec_id = pj_str(id[i].buf_);
	id[i].priority = (pj_uint8_t) prio[i];
    }

    *p_count = count;

    return PJ_SUCCESS;
}


/*
 * Change codec priority.
 */
PJ_DEF(pj_status_t) pjsua_codec_set_priority( const pj_str_t *codec_id,
					      pj_uint8_t priority )
{
    const pj_str_t all = { NULL, 0 };
    pjmedia_codec_mgr *codec_mgr;

    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);

    if (codec_id->slen==1 && *codec_id->ptr=='*')
	codec_id = &all;

    return pjmedia_codec_mgr_set_codec_priority(codec_mgr, codec_id, 
					        priority);
}


/*
 * Get codec parameters.
 */
PJ_DEF(pj_status_t) pjsua_codec_get_param( const pj_str_t *codec_id,
					   pjmedia_codec_param *param )
{
    const pj_str_t all = { NULL, 0 };
    const pjmedia_codec_info *info;
    pjmedia_codec_mgr *codec_mgr;
    unsigned count = 1;
    pj_status_t status;

    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);

    if (codec_id->slen==1 && *codec_id->ptr=='*')
	codec_id = &all;

    status = pjmedia_codec_mgr_find_codecs_by_id(codec_mgr, codec_id,
						 &count, &info, NULL);
    if (status != PJ_SUCCESS)
	return status;

    if (count != 1)
	return (count > 1? PJ_ETOOMANY : PJ_ENOTFOUND);

    status = pjmedia_codec_mgr_get_default_param( codec_mgr, info, param);
    return status;
}


/*
 * Set codec parameters.
 */
PJ_DEF(pj_status_t) pjsua_codec_set_param( const pj_str_t *codec_id,
					   const pjmedia_codec_param *param)
{
    const pjmedia_codec_info *info[2];
    pjmedia_codec_mgr *codec_mgr;
    unsigned count = 2;
    pj_status_t status;

    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);

    status = pjmedia_codec_mgr_find_codecs_by_id(codec_mgr, codec_id,
						 &count, info, NULL);
    if (status != PJ_SUCCESS)
	return status;

    /* Codec ID should be specific, except for G.722.1 */
    if (count > 1 && 
	pj_strnicmp2(codec_id, "G7221/16", 8) != 0 &&
	pj_strnicmp2(codec_id, "G7221/32", 8) != 0)
    {
	pj_assert(!"Codec ID is not specific");
	return PJ_ETOOMANY;
    }

    status = pjmedia_codec_mgr_set_default_param(codec_mgr, info[0], param);
    return status;
}


pj_status_t pjsua_media_apply_xml_control(pjsua_call_id call_id,
					  const pj_str_t *xml_st)
{
    return PJ_ENOTSUP;
}

/*
 * Send DTMF digits to remote using RFC 2833 payload formats.
 */
PJ_DEF(pj_status_t) pjsua_call_dial_dtmf( pjsua_call_id call_id, 
                      const pj_str_t *digits)
{
    pjsua_call *call;
    pjsip_dialog *dlg = NULL;
    pj_status_t status;
    int channel;
    VoEDtmf* dtmf;

    PJ_ASSERT_RETURN(call_id>=0 && call_id<(int)pjsua_var.ua_cfg.max_calls,
             PJ_EINVAL);
    
    PJ_LOG(4,(THIS_FILE, "Call %d dialing DTMF %.*s",
                 call_id, (int)digits->slen, digits->ptr));
    pj_log_push_indent();

    status = acquire_call("pjsua_call_dial_dtmf()", call_id, &call, &dlg);
    if (status != PJ_SUCCESS)
    goto on_return;

    if (!pjsua_call_has_media(call_id)) {
    PJ_LOG(3,(THIS_FILE, "Media is not established yet!"));
    status = PJ_EINVALIDOP;
    goto on_return;
    }

    channel = call->media[call->audio_idx].channel_id;
    dtmf = VoEDtmf::GetInterface(voe);

    for (int i=0; i<digits->slen; i++) {
        dtmf->SendTelephoneEvent(channel, (int)digits->ptr[i]);
    }

    dtmf->Release();

on_return:
    if (dlg) pjsip_dlg_dec_lock(dlg);
    pj_log_pop_indent();
    return status;
}
