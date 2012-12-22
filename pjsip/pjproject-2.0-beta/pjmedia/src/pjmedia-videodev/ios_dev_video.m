/* $Id: ios_dev.m 3902 2011-12-08 01:32:04Z ming $ */
/*
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
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
#include <pjmedia-videodev/videodev_imp.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/os.h>

#if PJMEDIA_VIDEO_DEV_HAS_IOS
#include "Availability.h"
#ifdef __IPHONE_4_0

#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreVideo/CoreVideo.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>

#include "math/esUtil.h"
#include "math/esShader.c"
#include "math/esShapes.c"
#include "math/esTransform.c"
#include "math/ImageUtils.m"

#define THIS_FILE		"ios_dev.c"
#define DEFAULT_CLOCK_RATE	90000
#define DEFAULT_WIDTH		352 //176 //352 //480
#define DEFAULT_HEIGHT		288 //144 //288 //360
#define DEFAULT_FPS		30

// Encode the captured frames without blocking the capture process. All frame decoding process are queued in a fix sized dispatching queue.
// THIS REQUIRES FURTHER TESTING, HAVING SUCH ASYNCHRONOUS CAPTURE INTRODUCES DELAY, and widht 5 frames it goes as high as 300ms extra
#define ASYNCH_CAPTURE_ENCODING_ENABLED 0

//Max frame decoding process enqueued at a time
#define ENCODING_DISPATCH_QUEUE_MAX_SIZE 1 //5 showld be suficient although in full load is 300ms delay at 15 fps, we need to really test the effect on perception as delay is critical for the user -> no impact on the decoder has been checked yet, as frames will be late, and information bursts will increase.

// Enable the fps counter label on screen
#define FPS_COUNTER_ENABLED 0


enum {
    UNIFORM_VIDEOFRAME,
	UNIFORM_INPUTCOLOR,
	UNIFORM_THRESHOLD,
    NUM_UNIFORMS
};

// Attribute index.
enum {
    ATTRIB_VERTEX,
    ATTRIB_TEXTUREPOSITON,
    NUM_ATTRIBUTES
};

static const GLfloat squareVertices[] = {
	-1.0f, -1.0f,
	1.0f, -1.0f,
	-1.0f,  1.0f,
	1.0f,  1.0f,
};

static const GLfloat textureVertices[] = {
	0.0f,  1.0f,
	0.0f,  0.0f,
	1.0f, 1.0f,
	1.0f, 0.0f,
};

static const GLfloat overlayTextureVertices[] = {
	0.0f,  0.0f,
	1.0f, 0.0f,
	0.0f,  1.0f,
	1.0f, 1.0f,
};

typedef struct ios_fmt_info
{
    pjmedia_format_id   pjmedia_format;
    UInt32		ios_format;
} ios_fmt_info;

static ios_fmt_info ios_fmts[] =
{
    {PJMEDIA_FORMAT_BGRA, kCVPixelFormatType_32BGRA} ,
};

/* qt device info */
struct ios_dev_info
{
    pjmedia_vid_dev_info	 info;
};

/* qt factory */
struct ios_factory
{
    pjmedia_vid_dev_factory	 base;
    pj_pool_t			*pool;
    pj_pool_factory		*pf;

    unsigned			 dev_count;
    struct ios_dev_info		*dev_info;
};

/*** VOutDelegate ***/

@interface VOutDelegate: NSObject 
<AVCaptureVideoDataOutputSampleBufferDelegate>
{
    @public
    struct ios_stream *stream;
    long last_time_spend;

    EAGLContext *context;
    EAGLSharegroup *sharegroup;

    /* The pixel dimensions of the backbuffer */
    GLint backingWidth, backingHeight;
    NSInteger texWidth, texHeight;

    CGImageRef	overlayCGImageRef;
    UIImage		*overlayImage;
    GLubyte *textureData;
	
    GLuint directDisplayProgram, thresholdProgram, positionProgram;
    GLuint videoFrameTexture, overlayFrameTexture;
	
    GLubyte *rawPositionPixels;
	
    GLfloat overlayVertices[8];
	
    /* OpenGL names for the renderbuffer and framebuffers used to render to this view */
    GLuint viewRenderbuffer, viewFramebuffer;

    GLuint videoTexture, overlayTexture;
    GLuint offscreenRenderbuffer, offscreenFramebuffer;
    ESMatrix projectionMatrix, modelviewMatrix, modelviewProjectionMatrix;

    GLint uniforms[NUM_UNIFORMS];

#if ASYNCH_CAPTURE_ENCODING_ENABLED
    dispatch_queue_t video_encode_queue;
    int current_encoding_threads_queued;
#endif

    NSTimer *timer_discard_frames;
    char target_buffer[405504]; //352 x 288 x 4 bytes (CIF * 4 bytes per pixel) //176 x 144 x 4 bytes (QCIF * 4 bytes per pixel)
}

- (void)disalowDiscardLateVideoFrames;
- (void)processCameraFrame:(CVImageBufferRef)cameraFrame rotationAngle:(GLint) angle adjustAspectRatio:(BOOL)adjust usePillarbox:(bool)usePillarbox;
- (BOOL)createFramebuffers;
- (void)destroyFramebuffer;

@end

/*** VPreviewViewController ***/

@interface VPreviewViewController: UIViewController
{
    UIView *previewView;
    AVCaptureVideoPreviewLayer *previewLayer;
    AVCaptureDeviceInput *devInput;
    AVCaptureSession *capSession;

    CGRect previewSize;
    bool previewViewIsHidden;

	@public
    struct ios_stream *stream;
}

@property (retain, nonatomic) UIView *previewView;
@property (retain, nonatomic) AVCaptureVideoPreviewLayer *previewLayer;
@property (retain, nonatomic) AVCaptureDeviceInput *devInput;
@property (retain, nonatomic) AVCaptureSession *capSession;

- (void)setOutgoingVideoStreamViewFrame: (NSNotification *)theNotification;
- (void)getOutgoingVideoStreamView: (NSNotification*)theNotification;
- (void)setOutgoingVideoStreamViewHidden: (NSNotification*)theNotification;
- (void)setOutgoingVideoStreamDevice: (NSNotification*)theNotification;
- (void)updateVideoPreview;
- (void)updateVideoPreviewHiddenStatus;
- (void)bringPreviewViewToFront: (NSNotification*)theNotification;

@end

@implementation VPreviewViewController

@synthesize previewView;
@synthesize previewLayer;
@synthesize devInput;
@synthesize capSession;

- (id)init
{
    self = [super init];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(setOutgoingVideoStreamViewFrame:) name:@"setOutgoingVideoStreamViewFrame" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(getOutgoingVideoStreamView:) name:@"getOutgoingVideoStreamView" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(setOutgoingVideoStreamViewHidden:) name:@"setOutgoingVideoStreamViewHidden" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(setOutgoingVideoStreamDevice:) name:@"setOutgoingVideoStreamDevice" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(bringPreviewViewToFront:) name:@"bringPreviewViewToFront" object:nil];

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(pausePreview:) name:@"pausePreview" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(resumePreview:) name:@"resumePreview" object:nil];

    previewViewIsHidden = NO; //Is visible by default
    previewView = nil;
    previewLayer = nil;
    previewSize = CGRectMake(214, 253, 96, 128); //default
    capSession = nil;
    devInput = nil;

    return self;
}

- (void)pausePreview: (NSNotification*)theNotification
{
    [capSession stopRunning];
}

- (void)resumePreview: (NSNotification*)theNotification
{
    [capSession startRunning];
}

- (void)getOutgoingVideoStreamView: (NSNotification*)theNotification
{
    // Assigns de previewView to the dictionary as a value to return
    [(NSMutableDictionary *)[theNotification userInfo] setObject:previewView forKey:@"view"];
}

- (void)updateVideoPreview
{
    [previewView setFrame:previewSize];
    [previewView.layer.sublayers makeObjectsPerformSelector:@selector(removeFromSuperlayer)];

    [previewLayer setVideoGravity:AVLayerVideoGravityResizeAspectFill];
    previewLayer.frame = previewView.bounds;
    [previewView.layer addSublayer:previewLayer];

    [[previewView superview] bringSubviewToFront:previewView];

    [previewView setHidden:previewViewIsHidden];
}

- (void)bringPreviewViewToFront: (NSNotification*)theNotification
{
     [[previewView superview] bringSubviewToFront:previewView];
}

- (void)setOutgoingVideoStreamViewFrame: (NSNotification*)theNotification
{
    int x = [[[theNotification userInfo] valueForKey:@"x"] intValue];
    int y = [[[theNotification userInfo] valueForKey:@"y"] intValue];
    int width = [[[theNotification userInfo] valueForKey:@"width"] intValue];
    int height = [[[theNotification userInfo] valueForKey:@"height"] intValue];

    previewSize = CGRectMake(x, y, width, height);

    [self performSelectorOnMainThread:@selector(updateVideoPreview) withObject:nil waitUntilDone:YES];
}

- (void)updateVideoPreviewHiddenStatus
{
    [[previewView superview] bringSubviewToFront:previewView];
    [previewView setHidden:previewViewIsHidden];
}

- (void)setOutgoingVideoStreamViewHidden: (NSNotification*)theNotification
{

    previewViewIsHidden = [[[theNotification userInfo] valueForKey:@"hidden"] intValue];
    [self performSelectorOnMainThread:@selector(updateVideoPreviewHiddenStatus) withObject:nil waitUntilDone:YES];
}

- (void)setOutgoingVideoStreamDevice: (NSNotification*)theNotification
{

    NSString *outgoingVideoStreamDeviceUniqueID = [[theNotification userInfo] valueForKey:@"deviceUniqueID"];
    AVCaptureDevice *outgoingVideoStreamDevice = [AVCaptureDevice deviceWithUniqueID:outgoingVideoStreamDeviceUniqueID];

    NSError *error;
    [capSession removeInput:devInput];
    devInput = [AVCaptureDeviceInput deviceInputWithDevice:outgoingVideoStreamDevice error: &error];
    [capSession addInput:devInput];
}

- (void)dealloc
{
    if (previewView)
    {
        [previewView release];
        previewView = NULL;
    }

    if (previewLayer)
    {
        [previewLayer release];
        previewLayer = NULL;
    }

    if (capSession)
    {
        [capSession release];
        capSession = NULL;
    }

    [[NSNotificationCenter defaultCenter] removeObserver:self];

    [super dealloc];
}

@end


/*** VInViewController ***/

@interface VInViewController: UIViewController
{
    UIView *inView;
    UIView *previewView;
    CGRect inSize;
    bool inViewIsHidden;
    UILabel *fpsCounterLabel;
    bool fpsCounterHidden;
    int framesCounter;
    NSTimer *fpsTimer;

    @public
    struct ios_stream *stream;
}

@property (retain, nonatomic) UIView *inView;
@property (retain, nonatomic) UIView *previewView;

- (void)updateShowFpsCounter;
- (void)updateHideFpsCounter;
#if FPS_COUNTER_ENABLED
- (void)frameReceivedFromIngoingStream: (NSNotification*)theNotification;
#endif
- (void)setIngoingVideoFpsHidden: (NSNotification*)theNotification;
- (void)getIngoingVideoStreamView: (NSNotification*)theNotification;
- (void)setPreviewViewViaNotification: (NSNotification*)theNotification;
- (void)addSubviewToVideoStreamView: (NSNotification*)theNotification;
- (void)updateAddSubviewToVideoStreamView: (UIView*)v;
- (void)bringPreviewViewToFront;
- (void)updateVideoIn;

@end

@implementation VInViewController

@synthesize inView, previewView;

- (id)init
{
    self = [super init];

#if FPS_COUNTER_ENABLED
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(frameReceivedFromIngoingStream:) name:@"frameReceivedFromIngoingStream" object:nil];
#endif
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(setIngoingVideoFpsHidden:) name:@"setIngoingVideoFpsHidden" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(setIngoingVideoStreamViewFrame:) name:@"setIngoingVideoStreamViewFrame" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(getIngoingVideoStreamView:) name:@"getIngoingVideoStreamView" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(setIngoingVideoStreamViewHidden:) name:@"setIngoingVideoStreamViewHidden" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(setPreviewViewViaNotification:) name:@"setPreviewView" object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(addSubviewToVideoStreamView:) name:@"addSubviewToVideoStreamView" object:nil];

    previewView = nil;
    inView = nil;
    inViewIsHidden = YES;			//Is visible by default
    fpsCounterHidden = YES;			//Is not visible by default
    inSize = CGRectMake(0, 0, 320, 480);	//Full size by default
    fpsCounterLabel = nil;
    framesCounter = 0;
    fpsTimer = nil;

    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];

    if (inView)
    {
        [inView release];
        inView = NULL;
    }

    if (previewView)
    {
        [previewView release];
        previewView = NULL;
    }

    if (fpsCounterLabel)
    {
        [fpsCounterLabel release];
        fpsCounterLabel = NULL;
    }

    if (fpsTimer)
    {
        [fpsTimer invalidate];
        [self cancelPreviousPerformRequestsWithTarget:self selector:@selector(refreshFpsCounterLabel) object:nil];
        [fpsTimer release];
        fpsTimer = NULL;
    }

    [super dealloc];
}

#if FPS_COUNTER_ENABLED
- (void)frameReceivedFromIngoingStream: (NSNotification*)theNotification
{
    framesCounter++;
}
#endif

- (void)updateAddSubviewToVideoStreamView: (UIView*)v
{
    dispatch_async(dispatch_get_main_queue(),
                   ^{ [inView addSubview:v]; });

    [inView addSubview:v];


    UIView *vista_test = [[UIView alloc] initWithFrame:CGRectMake(30, 80, 80, 40)];
    [vista_test setBackgroundColor:[UIColor greenColor]];

    dispatch_async(dispatch_get_main_queue(),
                   ^{ [inView addSubview:vista_test];
                      [inView addSubview:v]; });

    [inView addSubview:vista_test];
}

- (void)addSubviewToVideoStreamView: (NSNotification*)theNotification
{
    [self performSelectorOnMainThread:@selector(updateAddSubviewToVideoStreamView:) withObject:[[theNotification userInfo] valueForKey:@"view"] waitUntilDone:YES];
}

- (void)updateHideFpsCounter
{
	if(fpsTimer != nil)
	{
		[fpsTimer invalidate];
		[fpsTimer release];
		fpsTimer = nil;
	}

    [fpsCounterLabel removeFromSuperview];
    [fpsCounterLabel release];
    fpsCounterLabel = nil;
}

- (void)refreshFpsCounterLabelOnMainThread
{
	[fpsCounterLabel setText:[NSString stringWithFormat:@"Drawing %d fps", framesCounter]];
	framesCounter = 0;
}

- (void)refreshFpsCounterLabel
{
	[self performSelectorOnMainThread:@selector(refreshFpsCounterLabelOnMainThread) withObject:nil waitUntilDone:YES];
}

- (void)updateShowFpsCounter
{
	framesCounter = 0;

        [inView addSubview:fpsCounterLabel];
        [fpsCounterLabel setText:[NSString stringWithFormat:@"Drawing %d fps", framesCounter]];

	if(fpsTimer == nil)
	{
		//Show the acumulated frames counted every second
		fpsTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0 target: self selector:@selector(refreshFpsCounterLabel) userInfo: nil repeats:YES];
	}
}

- (void)setIngoingVideoFpsHidden: (NSNotification*)theNotification
{
    fpsCounterHidden = [[[theNotification userInfo] valueForKey:@"hidden"] intValue];

    if((!fpsCounterHidden) && (fpsCounterLabel == nil))
    {
    	fpsCounterLabel = [[UILabel alloc] initWithFrame:CGRectMake(5,25,130,25)];
	fpsCounterLabel.textAlignment = UITextAlignmentCenter;
	[fpsCounterLabel setBackgroundColor:[UIColor whiteColor]];
    }

    if(fpsCounterHidden)
    {
	[self performSelectorOnMainThread:@selector(updateHideFpsCounter) withObject:nil waitUntilDone:YES];
    }
    else if(inView != nil)
    {	
	[self performSelectorOnMainThread:@selector(updateShowFpsCounter) withObject:nil waitUntilDone:YES];
    }
    else
    {
	[self performSelectorOnMainThread:@selector(updateHideFpsCounter) withObject:nil waitUntilDone:YES];
    }
}

- (void)bringPreviewViewToFront
{
    [[inView superview] bringSubviewToFront:inView];
    [[previewView superview] bringSubviewToFront:previewView];
}

- (void)updateVideoIn
{
    [inView setFrame:inSize];
    [self bringPreviewViewToFront];

    [inView setHidden:YES];
    [previewView setHidden:YES];
}

- (void)updateSetPreviewView
{
    [inView addSubview:previewView];
}

- (void)setPreviewViewViaNotification: (NSNotification*)theNotification
{
    [self setPreviewView:[[theNotification userInfo] valueForKey:@"previewView"]];
    [self performSelectorOnMainThread:@selector(updateSetPreviewView) withObject:nil waitUntilDone:YES];
}

- (void)getIngoingVideoStreamView: (NSNotification*)theNotification
{
    // Assigns de previewView to the dictionary as a value to return
    [(NSMutableDictionary*)[theNotification userInfo] setObject:inView forKey:@"view"];
}

- (void)setIngoingVideoStreamViewFrame: (NSNotification*)theNotification
{
    int x = [[[theNotification userInfo] valueForKey:@"x"] intValue];
    int y = [[[theNotification userInfo] valueForKey:@"y"] intValue];
    int width = [[[theNotification userInfo] valueForKey:@"width"] intValue];
    int height = [[[theNotification userInfo] valueForKey:@"height"] intValue];

    inSize = CGRectMake(x, y, width, height);

    [self performSelectorOnMainThread:@selector(updateVideoIn) withObject:nil waitUntilDone:YES];
}

- (void)updateInViewHiddenStatus
{
    [inView setHidden:inViewIsHidden];
}

- (void)setIngoingVideoStreamViewHidden: (NSNotification*)theNotification
{
    inViewIsHidden = [[[theNotification userInfo] valueForKey:@"hidden"] intValue];
    [self performSelectorOnMainThread:@selector(updateInViewHiddenStatus) withObject:nil waitUntilDone:YES];
}

@end

/* Video stream. */
struct ios_stream
{
    pjmedia_vid_dev_stream      base;		/**< Base stream       */
    pjmedia_vid_dev_param       param;		/**< Settings	       */
    pj_pool_t                   *pool;		/**< Memory pool       */

    pjmedia_vid_dev_cb          vid_cb;		/**< Stream callback   */
    void                        *user_data; /**< Application data  */

    pjmedia_rect_size           size;
    pj_uint8_t                  bpp;
    unsigned                    bytes_per_row;
    unsigned                    frame_size;

    AVCaptureSession            *cap_session;
    AVCaptureDeviceInput        *dev_input;
    AVCaptureVideoDataOutput	*video_output;
    AVCaptureVideoPreviewLayer  *preview_layer;
    AVCaptureConnection		*cap_connection;
    VOutDelegate                *vout_delegate;

    VPreviewViewController      *vpreview_view_controller;
    VInViewController           *vin_view_controller;

#if FPS_COUNTER_ENABLED
    long			last_in_fps_timestamp;
    int				last_in_fps;
#endif

    UIImageView                 *imgView;
    UIImageView                 *imgInView;

    void                        *buf;
    dispatch_queue_t            video_render_queue;

    UIView                      *previewView;

    pj_timestamp                frame_ts;
    unsigned                    ts_inc;
};


/* Prototypes */
static UIImage* scaleAndRotateImage(UIImage *image);
static pj_status_t ios_factory_init(pjmedia_vid_dev_factory *f);
static pj_status_t ios_factory_destroy(pjmedia_vid_dev_factory *f);
static pj_status_t ios_factory_refresh(pjmedia_vid_dev_factory *f);
static unsigned    ios_factory_get_dev_count(pjmedia_vid_dev_factory *f);
static pj_status_t ios_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					    unsigned index,
					    pjmedia_vid_dev_info *info);
static pj_status_t ios_factory_default_param(pj_pool_t *pool,
					     pjmedia_vid_dev_factory *f,
					     unsigned index,
					     pjmedia_vid_dev_param *param);
static pj_status_t ios_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm);

static pj_status_t ios_stream_get_param(pjmedia_vid_dev_stream *strm,
				        pjmedia_vid_dev_param *param);
static pj_status_t ios_stream_get_cap(pjmedia_vid_dev_stream *strm,
				      pjmedia_vid_dev_cap cap,
				      void *value);
static pj_status_t ios_stream_set_cap(pjmedia_vid_dev_stream *strm,
				      pjmedia_vid_dev_cap cap,
				      const void *value);
static pj_status_t ios_stream_start(pjmedia_vid_dev_stream *strm);
static pj_status_t ios_stream_put_frame(pjmedia_vid_dev_stream *strm,
					const pjmedia_frame *frame);
static pj_status_t ios_stream_stop(pjmedia_vid_dev_stream *strm);
static pj_status_t ios_stream_destroy(pjmedia_vid_dev_stream *strm);

/* Operations */
static pjmedia_vid_dev_factory_op factory_op =
{
    &ios_factory_init,
    &ios_factory_destroy,
    &ios_factory_get_dev_count,
    &ios_factory_get_dev_info,
    &ios_factory_default_param,
    &ios_factory_create_stream,
    &ios_factory_refresh
};

static pjmedia_vid_dev_stream_op stream_op =
{
    &ios_stream_get_param,
    &ios_stream_get_cap,
    &ios_stream_set_cap,
    &ios_stream_start,
    NULL,
    &ios_stream_put_frame,
    &ios_stream_stop,
    &ios_stream_destroy
};


/****************************************************************************
 * Factory operations
 */
/*
 * Init ios_ video driver.
 */
pjmedia_vid_dev_factory* pjmedia_ios_factory(pj_pool_factory *pf)
{
    struct ios_factory *f;
    pj_pool_t *pool;

    pool = pj_pool_create(pf, "ios video", 512, 512, NULL);
    f = PJ_POOL_ZALLOC_T(pool, struct ios_factory);
    f->pf = pf;
    f->pool = pool;
    f->base.op = &factory_op;

    return &f->base;
}


/* API: init factory */
static pj_status_t ios_factory_init(pjmedia_vid_dev_factory *f)
{
    struct ios_factory *qf = (struct ios_factory*)f;
    struct ios_dev_info *qdi;
    unsigned i, l;
    
    /* Initialize input and output devices here */
    qf->dev_info = (struct ios_dev_info*)
		   pj_pool_calloc(qf->pool, 2,
				  sizeof(struct ios_dev_info));
    
    qf->dev_count = 0;
    qdi = &qf->dev_info[qf->dev_count++];
    pj_bzero(qdi, sizeof(*qdi));
    strcpy(qdi->info.name, "iOS UIView");
    strcpy(qdi->info.driver, "iOS");	    
    qdi->info.dir = PJMEDIA_DIR_RENDER;
    qdi->info.has_callback = PJ_FALSE;
    qdi->info.caps = PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW;
    
    if (NSClassFromString(@"AVCaptureSession")) {
	qdi = &qf->dev_info[qf->dev_count++];
	pj_bzero(qdi, sizeof(*qdi));
	strcpy(qdi->info.name, "iOS AVCapture");
	strcpy(qdi->info.driver, "iOS");	    
	qdi->info.dir = PJMEDIA_DIR_CAPTURE;
	qdi->info.has_callback = PJ_TRUE;
    }

    for (i = 0; i < qf->dev_count; i++)
    {
	qdi = &qf->dev_info[i];
	qdi->info.fmt_cnt = PJ_ARRAY_SIZE(ios_fmts);	    
	qdi->info.caps |= PJMEDIA_VID_DEV_CAP_FORMAT;
	
	for (l = 0; l < PJ_ARRAY_SIZE(ios_fmts); l++) {
	    pjmedia_format *fmt = &qdi->info.fmt[l];
	    pjmedia_format_init_video(fmt,
				      ios_fmts[l].pjmedia_format,
				      DEFAULT_WIDTH,
				      DEFAULT_HEIGHT,
				      DEFAULT_FPS, 1);	
	}
    }
    
    return PJ_SUCCESS;
}

/* API: destroy factory */
static pj_status_t ios_factory_destroy(pjmedia_vid_dev_factory *f)
{
    struct ios_factory *qf = (struct ios_factory*)f;
    pj_pool_t *pool = qf->pool;

    qf->pool = NULL;
    pj_pool_release(pool);

    return PJ_SUCCESS;
}

/* API: refresh the list of devices */
static pj_status_t ios_factory_refresh(pjmedia_vid_dev_factory *f)
{
    PJ_UNUSED_ARG(f);
    return PJ_SUCCESS;
}

/* API: get number of devices */
static unsigned ios_factory_get_dev_count(pjmedia_vid_dev_factory *f)
{
    struct ios_factory *qf = (struct ios_factory*)f;
    return qf->dev_count;
}

/* API: get device info */
static pj_status_t ios_factory_get_dev_info(pjmedia_vid_dev_factory *f,
					    unsigned index,
					    pjmedia_vid_dev_info *info)
{
    struct ios_factory *qf = (struct ios_factory*)f;

    PJ_ASSERT_RETURN(index < qf->dev_count, PJMEDIA_EVID_INVDEV);

    pj_memcpy(info, &qf->dev_info[index].info, sizeof(*info));

    return PJ_SUCCESS;
}

/* API: create default device parameter */
static pj_status_t ios_factory_default_param(pj_pool_t *pool,
					     pjmedia_vid_dev_factory *f,
					     unsigned index,
					     pjmedia_vid_dev_param *param)
{
    struct ios_factory *qf = (struct ios_factory*)f;
    struct ios_dev_info *di = &qf->dev_info[index];

    PJ_ASSERT_RETURN(index < qf->dev_count, PJMEDIA_EVID_INVDEV);

    PJ_UNUSED_ARG(pool);

    pj_bzero(param, sizeof(*param));
    if (di->info.dir & PJMEDIA_DIR_CAPTURE) {
	param->dir = PJMEDIA_DIR_CAPTURE;
	param->cap_id = index;
	param->rend_id = PJMEDIA_VID_INVALID_DEV;
    } else if (di->info.dir & PJMEDIA_DIR_RENDER) {
	param->dir = PJMEDIA_DIR_RENDER;
	param->rend_id = index;
	param->cap_id = PJMEDIA_VID_INVALID_DEV;
    } else {
	return PJMEDIA_EVID_INVDEV;
    }
    
    param->flags = PJMEDIA_VID_DEV_CAP_FORMAT;
    param->clock_rate = DEFAULT_CLOCK_RATE;
    pj_memcpy(&param->fmt, &di->info.fmt[0], sizeof(param->fmt));

    return PJ_SUCCESS;
}

@implementation VOutDelegate

- (id)init
{
    self = [super init];
#if ASYNCH_CAPTURE_ENCODING_ENABLED
    current_encoding_threads_queued = 0;
    video_encode_queue = dispatch_queue_create("com.yarnapp.video_encode_queue", NULL);
#endif

/*    backingWidth = 192;
    backingHeight = 176;		

    overlayImage = [[UIImage alloc] init];;
 
    EAGLContext* contextPush = [EAGLContext currentContext];

    sharegroup = [[EAGLSharegroup alloc] init];
    context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2 sharegroup:sharegroup];

    if (!context || ![EAGLContext setCurrentContext:context] || ![self createFramebuffers])
    {
	NSLog(@"Could not create GL context and frame buffers");
        [EAGLContext setCurrentContext:contextPush];
	[self dealloc];
	return nil;
    }
    [EAGLContext setCurrentContext:contextPush];
*/
    return self;
}

- (void)destroyFramebuffer;
{	
	if (viewFramebuffer) {
		glDeleteFramebuffers(1, &viewFramebuffer);
		viewFramebuffer = 0;
	}
	
	if (viewRenderbuffer) {
		glDeleteRenderbuffers(1, &viewRenderbuffer);
		viewRenderbuffer = 0;
	}
}

- (BOOL)createFramebuffers {	
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	
	
	// Offscreen position framebuffer object
	glGenFramebuffers(1, &offscreenFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, offscreenFramebuffer);
	
	glGenRenderbuffers(1, &offscreenRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, offscreenRenderbuffer);
	
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, backingWidth, backingHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, offscreenRenderbuffer);	
    
	
	// Offscreen position framebuffer texture target
	glGenTextures(1, &videoTexture);
    glBindTexture(GL_TEXTURE_2D, videoTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);	
    glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, backingWidth, backingHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, videoTexture, 0);

	overlayCGImageRef = overlayImage.CGImage;
	
	texWidth = CGImageGetWidth(overlayCGImageRef);
	texHeight = CGImageGetHeight(overlayCGImageRef);
	
//	static const GLfloat overlayVertices[] = {
//		0.2375f, 0.316f,
//		1.0f, 0.316f,
//		0.2375f,  1.0f,
//		1.0f,  1.0f,
//	};
	
	GLfloat widthRatio = (GLfloat) texWidth / (GLfloat) backingWidth;
	GLfloat heightRatio = (GLfloat) texHeight / (GLfloat) backingHeight;
	
	overlayVertices[0] = widthRatio;
	overlayVertices[1] = heightRatio;
	overlayVertices[2] = 1.0f;
	overlayVertices[3] = heightRatio;
	overlayVertices[4] = widthRatio;
	overlayVertices[5] = 1.0f;
	overlayVertices[6] = 1.0f;
	overlayVertices[7] = 1.0f;
	
	//NSLog(@"Texture Width %d, Texture Height %d", texWidth, texHeight);
	
	textureData = (GLubyte *)malloc(texWidth * texHeight * 4);
	
	CGContextRef textureContext = CGBitmapContextCreate(textureData, texWidth, texHeight, 8, texWidth * 4, CGImageGetColorSpace(overlayCGImageRef), kCGImageAlphaPremultipliedLast);
	
	//CGContextSetBlendMode(textureContext, kCGBlendModeCopy);
	CGContextDrawImage(textureContext, CGRectMake(0.0, 0.0, (float)texWidth, (float)texHeight), overlayCGImageRef);
	
	CGContextRelease(textureContext);

	
	// Create a new texture from the camera frame data, display that using the shaders
	glGenTextures(1, &overlayTexture);
	glBindTexture(GL_TEXTURE_2D, overlayTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// This is necessary for non-power-of-two textures
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	// Using BGRA extension to pull in video frame data directly
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bufferWidth, bufferHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, CVPixelBufferGetBaseAddress(cameraFrame));
	glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, texWidth, texHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, textureData);
	
	
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
		NSLog(@"Incomplete FBO: %d", status);
        //exit(1);
		return NO;
    }
	
    return YES;
}

- (void)processCameraFrame:(CVImageBufferRef)cameraFrame rotationAngle:(GLint) angle adjustAspectRatio:(BOOL) doAdjust usePillarbox:(bool)usePillarbox
{
	//We need to get this on the capture queue. This can create a dead lock on the main queue.
	EAGLContext* contextPush = [EAGLContext currentContext];
	if (contextPush != context) {
		[EAGLContext setCurrentContext:context];
    	}

	//dispatch_sync(dispatch_get_main_queue(), ^{
        CVPixelBufferLockBaseAddress(cameraFrame, 0);
		int bufferHeight = CVPixelBufferGetHeight(cameraFrame);
		int bufferWidth = CVPixelBufferGetWidth(cameraFrame);
		
		// Create a new texture from the camera frame data, display that using the shaders
		glGenTextures(1, &videoFrameTexture);
		glBindTexture(GL_TEXTURE_2D, videoFrameTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// This is necessary for non-power-of-two textures
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		
		// Using BGRA extension to pull in video frame data directly
		glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, bufferWidth, bufferHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, CVPixelBufferGetBaseAddress(cameraFrame));

        esMatrixLoadIdentity( &projectionMatrix );
        esOrtho(&projectionMatrix, -1.0, 1.0, -1.0, 1.0, -1.0, 1.0);            
        // generate a model view
		esMatrixLoadIdentity(&modelviewMatrix);
        esRotate(&modelviewMatrix, angle, 0, 0, -1);
        
        if (doAdjust) {
            if (usePillarbox) {
                //Pillar box.
                GLfloat sx = (double)bufferHeight / (double)bufferWidth;
                GLfloat sy = (double)bufferWidth / (double)bufferHeight;
                esScale(&modelviewMatrix, sx * sx, sx * sy, 1.0);
            } else {
                //Scale to fill.
                GLfloat dws = ( (GLfloat) bufferWidth - bufferHeight ) / (GLfloat) bufferWidth;;
                GLfloat aspectRatio = (GLfloat) bufferWidth / (GLfloat) bufferHeight;
                esScale(&modelviewMatrix, 1.0, aspectRatio + (aspectRatio - 1.0) + dws, 1.0);
            }
        }
        
		esMatrixMultiply(&modelviewProjectionMatrix, &modelviewMatrix, &projectionMatrix);
		
		glBindFramebuffer(GL_FRAMEBUFFER, offscreenFramebuffer);
		glViewport(0, 0, 176 /*backingWidth*/, 144/*backingHeight*/);
		
		//glClearColor(1.0, 0, 0, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );   
		
		glUseProgram(directDisplayProgram);
		
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, videoFrameTexture);
		
		GLint uMVPIndex  = glGetUniformLocation(directDisplayProgram, "uMvp");
		glUniformMatrix4fv( uMVPIndex, 1, GL_FALSE, (GLfloat*) &modelviewProjectionMatrix.m[0][0] );
		
		glUniform1i(uniforms[UNIFORM_VIDEOFRAME], 0);	
		
		// Update attribute values.
		glVertexAttribPointer(ATTRIB_VERTEX, 2, GL_FLOAT, 0, 0, squareVertices);
		glEnableVertexAttribArray(ATTRIB_VERTEX);
		glVertexAttribPointer(ATTRIB_TEXTUREPOSITON, 2, GL_FLOAT, 0, 0, textureVertices);
		glEnableVertexAttribArray(ATTRIB_TEXTUREPOSITON);
		
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		
		glReadPixels(0, 0, backingWidth, backingHeight, GL_BGRA, GL_UNSIGNED_BYTE, CVPixelBufferGetBaseAddress(cameraFrame));
		glDeleteTextures(1, &videoFrameTexture);

		CVPixelBufferUnlockBaseAddress(cameraFrame, 0);

    [EAGLContext setCurrentContext:contextPush];

}

- (void)captureOutput:(AVCaptureOutput *)captureOutput 
		      didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
		      fromConnection:(AVCaptureConnection *)connection
{
#if ASYNCH_CAPTURE_ENCODING_ENABLED
    if(current_encoding_threads_queued >= ENCODING_DISPATCH_QUEUE_MAX_SIZE)
        return;
#endif

    if (!sampleBuffer)
        return;

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer); 

    if(imageBuffer != nil) // If something did not work when frame downresampling then discard the frame
    {
        /* Get a CMSampleBuffer's Core Video image buffer for the media data */
        CVPixelBufferLockBaseAddress(imageBuffer, 0);
/*
        [self processCameraFrame:imageBuffer
                    rotationAngle:0
                adjustAspectRatio:YES
                     usePillarbox:YES];

*/
        const void *source_buffer = CVPixelBufferGetBaseAddress(imageBuffer);

/*        dispatch_queue_t concurrent_dispatcher_queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
        dispatch_group_t concurrent_group = dispatch_group_create();

        int y;

        for(y=0; y < 144; y++)
        {
            dispatch_group_async(concurrent_group, concurrent_dispatcher_queue, ^{
                pj_memcpy(&target_buffer[176*4*y], ((char*)source_buffer) + (192*4*y)+(8*4), 176*4);
            });
        }

        dispatch_group_wait(concurrent_group, DISPATCH_TIME_FOREVER);
        dispatch_release(concurrent_group);*/


CVPixelBufferLockBaseAddress(imageBuffer, 0);

        pjmedia_frame frame;
        frame.type = PJMEDIA_FRAME_TYPE_VIDEO;
        frame.buf = source_buffer; //target_buffer; //CVPixelBufferGetBaseAddress(imageBuffer);
        frame.size = 405504; //352 x 288 x 4 bytes (CIF * 4 bytes per pixel) //176 x 144 x 4 bytes (4 bytes per pixel)
        frame.bit_info = 0;
        frame.timestamp.u64 = stream->frame_ts.u64;
        
        //Encode the captured video frame
#if ASYNCH_CAPTURE_ENCODING_ENABLED
        current_encoding_threads_queued++;

        dispatch_async(/*dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0)*/video_encode_queue, ^{
            if (!pj_thread_is_registered())
            {
                pj_thread_desc  a_thread_desc;
                pj_thread_t     *a_thread;
                pj_thread_register("ipjsua", a_thread_desc, &a_thread);
            }
            (*stream->vid_cb.capture_cb)(&stream->base, stream->user_data, &frame);
            stream->frame_ts.u64 += stream->ts_inc;

            current_encoding_threads_queued--;
        });

#else
        (*stream->vid_cb.capture_cb)(&stream->base, stream->user_data, &frame);
        stream->frame_ts.u64 += stream->ts_inc;
#endif


    	/* Unlock the pixel buffer */
    	CVPixelBufferUnlockBaseAddress(imageBuffer ,0);

    }
    else
    {
        //Failed the frame capture
        stream->frame_ts.u64 += stream->ts_inc;
    }

}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];

#if ASYNCH_CAPTURE_ENCODING_ENABLED
    if (video_encode_queue) {
        dispatch_release(video_encode_queue);
        video_encode_queue = NULL;
    }
#endif

    [self destroyFramebuffer];
    glDeleteTextures(1, &overlayTexture);
    glDeleteProgram(directDisplayProgram);
    [context release];
    [sharegroup release];

    [super dealloc];
}

@end

static ios_fmt_info* get_ios_format_info(pjmedia_format_id id)
{
    unsigned i;

    for (i = 0; i < PJ_ARRAY_SIZE(ios_fmts); i++) {
        if (ios_fmts[i].pjmedia_format == id)
            return &ios_fmts[i];
    }
    
    return NULL;
}

/* API: create stream */
static pj_status_t ios_factory_create_stream(
					pjmedia_vid_dev_factory *f,
					pjmedia_vid_dev_param *param,
					const pjmedia_vid_dev_cb *cb,
					void *user_data,
					pjmedia_vid_dev_stream **p_vid_strm)
{
    struct ios_factory *qf = (struct ios_factory*)f;
    pj_pool_t *pool;
    struct ios_stream *strm;
    const pjmedia_video_format_detail *vfd;
    const pjmedia_video_format_info *vfi;
    pj_status_t status = PJ_SUCCESS;
    ios_fmt_info *ifi = get_ios_format_info(param->fmt.id);
    NSError *error;

    PJ_ASSERT_RETURN(f && param && p_vid_strm, PJ_EINVAL);
    PJ_ASSERT_RETURN(param->fmt.type == PJMEDIA_TYPE_VIDEO &&
		     param->fmt.detail_type == PJMEDIA_FORMAT_DETAIL_VIDEO &&
                     (param->dir == PJMEDIA_DIR_CAPTURE ||
                     param->dir == PJMEDIA_DIR_RENDER),
		     PJ_EINVAL);

    if (!(ifi = get_ios_format_info(param->fmt.id)))
        return PJMEDIA_EVID_BADFORMAT;
    
    vfi = pjmedia_get_video_format_info(NULL, param->fmt.id);
    if (!vfi)
        return PJMEDIA_EVID_BADFORMAT;

    /* Create and Initialize stream descriptor */
    pool = pj_pool_create(qf->pf, "ios-dev", 4000, 4000, NULL);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    strm = PJ_POOL_ZALLOC_T(pool, struct ios_stream);
    pj_memcpy(&strm->param, param, sizeof(*param));
    strm->pool = pool;
    pj_memcpy(&strm->vid_cb, cb, sizeof(*cb));
    strm->user_data = user_data;

    vfd = pjmedia_format_get_video_format_detail(&strm->param.fmt, PJ_TRUE);

    pj_memcpy(&strm->size, &vfd->size, sizeof(vfd->size));
    strm->bpp = vfi->bpp;
    strm->bytes_per_row = strm->size.w * strm->bpp / 8;
    strm->frame_size = strm->bytes_per_row * strm->size.h;
    strm->ts_inc = PJMEDIA_SPF2(param->clock_rate, &vfd->fps, 1);

    if (param->dir & PJMEDIA_DIR_CAPTURE)
    {
        /* Create capture stream here */
        strm->cap_session = [[AVCaptureSession alloc] init];

        strm->previewView = [[UIImageView alloc] initWithFrame:CGRectMake(0, 0, 0, 0)]; //10, 342, 96, 128
        [strm->previewView setBackgroundColor:[UIColor blueColor]];

        strm->preview_layer = [[AVCaptureVideoPreviewLayer alloc] initWithSession:strm->cap_session];
        [strm->preview_layer setVideoGravity:AVLayerVideoGravityResizeAspectFill];
        strm->preview_layer.frame = strm->previewView.bounds;

	// The following code was the original decorator used to enable rounded corners in the preview view
	/*
        strm->preview_layer.shadowOffset = CGSizeMake(0, 3);
        strm->preview_layer.shadowRadius = 3.5;
        strm->preview_layer.cornerRadius = 6.0;
        strm->preview_layer.shadowColor = [UIColor blackColor].CGColor;
        strm->preview_layer.shadowOpacity = 0.75;;
	*/

        [strm->previewView.layer addSublayer:strm->preview_layer];
        [strm->previewView setHidden:NO];

        if (!strm->cap_session) {
            status = PJ_ENOMEM;
            goto on_error;
        }

        strm->cap_session.sessionPreset = AVCaptureSessionPreset352x288; //AVCaptureSessionPresetLow; //352x288; //AVCaptureSessionPresetLow; //AVCaptureSessionPreset352x288; //AVCaptureSessionPresetMedium;

        AVCaptureDevice *frontVideoCamera = nil;
        AVCaptureDevice *backVideoCamera = nil;
        AVCaptureDevice *selectedVideoCamera = nil;

        NSArray *devices = [AVCaptureDevice devices];

        for (AVCaptureDevice *device in devices)
        {
            if ([device hasMediaType:AVMediaTypeVideo])
            {
                if ([device hasMediaType:AVMediaTypeVideo])
                {
                    if([device position] == AVCaptureDevicePositionFront)
                    {
                        frontVideoCamera = device;
                    }
                    else if([device position] == AVCaptureDevicePositionBack)
                    {
                        backVideoCamera = device;
                    }
                }
            }
        }

	/* Open video device */

	if(frontVideoCamera != nil)	selectedVideoCamera = frontVideoCamera;
	else if(backVideoCamera != nil)	selectedVideoCamera = backVideoCamera;

        /* Add the video device to the session as a device input */	
        strm->dev_input = [AVCaptureDeviceInput deviceInputWithDevice:selectedVideoCamera error: &error];

        if (!strm->dev_input)
        {
            status = PJMEDIA_EVID_SYSERR;
            goto on_error;
        }

        [strm->cap_session addInput:strm->dev_input];

        // Controlador per controlar la vista del preview de la videoconferencia
        strm->vpreview_view_controller = [[VPreviewViewController alloc] init];
        strm->vpreview_view_controller->stream = strm;

        [strm->vpreview_view_controller setCapSession:strm->cap_session];
        [strm->vpreview_view_controller setDevInput:strm->dev_input];

        [strm->vpreview_view_controller setPreviewView:strm->previewView];
        [strm->vpreview_view_controller setPreviewLayer:strm->preview_layer];

        strm->video_output = [[AVCaptureVideoDataOutput alloc] init];
	strm->cap_connection = [strm->video_output connectionWithMediaType:AVMediaTypeVideo];

	if (strm->cap_connection.supportsVideoMinFrameDuration)
		strm->cap_connection.videoMinFrameDuration = CMTimeMake(vfd->fps.denum, vfd->fps.num);

	if (strm->cap_connection.supportsVideoMaxFrameDuration)
		strm->cap_connection.videoMaxFrameDuration = CMTimeMake(vfd->fps.denum, vfd->fps.num);

        strm->video_output.alwaysDiscardsLateVideoFrames = YES;

        if (!strm->video_output) {
            status = PJMEDIA_EVID_SYSERR;
            goto on_error;
        }

        [strm->cap_session addOutput:strm->video_output];
        
        /* Configure the video output */

        // CAPTURE
        strm->vout_delegate = [[VOutDelegate alloc] init];
#if FPS_COUNTER_ENABLED
        strm->last_in_fps_timestamp = 0;
        strm->last_in_fps = 0;
#endif
        strm->vout_delegate->stream = strm;

        dispatch_queue_t queue = dispatch_get_main_queue();// dispatch_queue_create("com.yarnapp.camera_frame_capture", NULL);
        [strm->video_output setSampleBufferDelegate:strm->vout_delegate queue:queue];
        dispatch_release(queue);	

        strm->video_output.videoSettings =
            [NSDictionary dictionaryWithObjectsAndKeys:
                [NSNumber numberWithUnsignedInt:/*kCVPixelFormatType_24BGR*/kCVPixelFormatType_32BGRA], /*[NSNumber numberWithInt:ifi->ios_format]*/ /* [NSNumber numberWithUnsignedInt:kCVPixelFormatType_32BGRA] */
                kCVPixelBufferPixelFormatTypeKey,
                [NSNumber numberWithInt: vfd->size.w],
                kCVPixelBufferWidthKey,
                [NSNumber numberWithInt: vfd->size.h],
                kCVPixelBufferHeightKey, nil];

        [strm->video_output setMinFrameDuration:CMTimeMake(vfd->fps.denum, vfd->fps.num)];

    } else if (param->dir & PJMEDIA_DIR_RENDER)
    {
        [strm->video_output setMinFrameDuration:CMTimeMake(vfd->fps.denum, vfd->fps.num)];

        /* Create renderer stream here */
        /* Get the main window */

        //UIWindow* window = (UIWindow*)[[UIApplication sharedApplication].windows objectAtIndex:0];

        UIWindow *window = [[UIApplication sharedApplication] keyWindow];
	
        if (param->flags & PJMEDIA_VID_DEV_CAP_OUTPUT_WINDOW && param->window.info.ios.window)
            window = (UIWindow *)param->window.info.ios.window;

        pj_assert(window);

        strm->imgView = [[UIImageView alloc] initWithFrame:CGRectMake(0, 0, 0, 0)];
        [window addSubview:strm->imgView];
        [strm->imgView setBackgroundColor:[UIColor clearColor]];

        strm->vin_view_controller = [[VInViewController alloc] init];
//      [strm->vin_view_controller setStream:strm];
        [strm->vin_view_controller setPreviewView:strm->previewView];

        [strm->vin_view_controller setInView:strm->imgView];
        [strm->vin_view_controller bringPreviewViewToFront];

        [strm->imgView setHidden:NO];


        if (!strm->vout_delegate) {
            strm->vout_delegate = [[VOutDelegate alloc] init];
            strm->vout_delegate->stream = strm;
        }

        strm->video_render_queue = dispatch_queue_create("com.yarnapp.video_render_queue", NULL);
        if (!strm->video_render_queue)
            goto on_error;

        strm->buf = pj_pool_alloc(pool, strm->frame_size);
    }
    
    /* Apply the remaining settings */
    /*    
     if (param->flags & PJMEDIA_VID_DEV_CAP_INPUT_SCALE) {
	ios_stream_set_cap(&strm->base,
			  PJMEDIA_VID_DEV_CAP_INPUT_SCALE,
			  &param->fmt);
     }
     */
    /* Done */
    strm->base.op = &stream_op;
  	  *p_vid_strm = &strm->base;
    
    return PJ_SUCCESS;
    
on_error:
    ios_stream_destroy((pjmedia_vid_dev_stream *)strm);
    
    return status;
}

/* API: Get stream info. */
static pj_status_t ios_stream_get_param(pjmedia_vid_dev_stream *s,
				        pjmedia_vid_dev_param *pi)
{
    struct ios_stream *strm = (struct ios_stream*)s;

    PJ_ASSERT_RETURN(strm && pi, PJ_EINVAL);

    pj_memcpy(pi, &strm->param, sizeof(*pi));

/*    if (ios_stream_get_cap(s, PJMEDIA_VID_DEV_CAP_INPUT_SCALE,
                            &pi->fmt.info_size) == PJ_SUCCESS)
    {
        pi->flags |= PJMEDIA_VID_DEV_CAP_INPUT_SCALE;
    }
*/
    return PJ_SUCCESS;
}

/* API: get capability */
static pj_status_t ios_stream_get_cap(pjmedia_vid_dev_stream *s,
				      pjmedia_vid_dev_cap cap,
				      void *pval)
{
    struct ios_stream *strm = (struct ios_stream*)s;

    PJ_UNUSED_ARG(strm);

    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    if (cap==PJMEDIA_VID_DEV_CAP_INPUT_SCALE)
    {
        return PJMEDIA_EVID_INVCAP;
//	return PJ_SUCCESS;
    } else {
	return PJMEDIA_EVID_INVCAP;
    }
}

/* API: set capability */
static pj_status_t ios_stream_set_cap(pjmedia_vid_dev_stream *s,
				      pjmedia_vid_dev_cap cap,
				      const void *pval)
{
    struct ios_stream *strm = (struct ios_stream*)s;

    PJ_UNUSED_ARG(strm);

    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    if (cap==PJMEDIA_VID_DEV_CAP_INPUT_SCALE)
    {
	return PJ_SUCCESS;
    }

    return PJMEDIA_EVID_INVCAP;
}

/* API: Start stream. */
static pj_status_t ios_stream_start(pjmedia_vid_dev_stream *strm)
{
    struct ios_stream *stream = (struct ios_stream*)strm;

    PJ_UNUSED_ARG(stream);

	if (stream->cap_session) {
		[stream->cap_session startRunning];
    
		if (![stream->cap_session isRunning])
	    	return PJ_EUNKNOWN;
    }
    
    return PJ_SUCCESS;
}


/* API: Put frame from stream */
static pj_status_t ios_stream_put_frame(pjmedia_vid_dev_stream *strm,
					const pjmedia_frame *frame)
{
    if((!strm->sys.is_running) || (frame->is_empty))
        return PJ_SUCCESS;

#if FPS_COUNTER_ENABLED
    [[NSNotificationCenter defaultCenter] postNotificationName:@"frameReceivedFromIngoingStream" object:nil userInfo:nil];
#endif


    struct ios_stream *stream = (struct ios_stream*)strm;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    /* Create a device-dependent RGB color space */
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

    /* Create a bitmap graphics context with the sample buffer data */
    CGContextRef context =
        CGBitmapContextCreate(frame->buf, stream->size.w, stream->size.h, 8,
                              stream->bytes_per_row, colorSpace,
                              kCGBitmapByteOrder32Little |
                              kCGImageAlphaPremultipliedFirst);

    /**
     * Create a Quartz image from the pixel data in the bitmap graphics
     * context
     */
    CGImageRef quartzImage = CGBitmapContextCreateImage(context);


    /* Free up the context and color space */
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);

    UIImage *image = [UIImage imageWithCGImage:quartzImage scale:1 orientation:UIImageOrientationRight];


    /* Release the Quartz image */
    CGImageRelease(quartzImage);

    dispatch_async(dispatch_get_main_queue(), //stream->video_render_queue,
                   ^{
                       NSDictionary *notificationParameters = [NSDictionary dictionaryWithObjectsAndKeys:image, @"videoframe", nil];
                       [[NSNotificationCenter defaultCenter] postNotificationName:@"setIngoingVideoFrame" object:nil userInfo:notificationParameters];
                    });

#if FPS_COUNTER_ENABLED
    //Add un frame to the ingoing frame counter
    stream->last_in_fps++;
#endif

    [pool release];

    return PJ_SUCCESS;
}

/* API: Stop stream. */
static pj_status_t ios_stream_stop(pjmedia_vid_dev_stream *strm)
{
    // pjmedia calls this method when a 'a=sendonly' attribute is sent inside the SDP message from remote part
    struct ios_stream *stream = (struct ios_stream*)strm;

    PJ_UNUSED_ARG(stream);

    if (stream->cap_session && [stream->cap_session isRunning])
    {
        [stream->cap_session stopRunning];
    }

    return PJ_SUCCESS;
}


/* API: Destroy stream. */
static pj_status_t ios_stream_destroy(pjmedia_vid_dev_stream *strm)
{
    struct ios_stream *stream = (struct ios_stream*)strm;

    PJ_ASSERT_RETURN(stream != NULL, PJ_EINVAL);

    ios_stream_stop(strm);

    if (stream->imgView)
    {
        [stream->imgView release];
        stream->imgView = NULL;
    }

    if (stream->imgInView)
    {
        [stream->imgInView release];
        stream->imgInView = NULL;
    }

    if (stream->cap_session)
    {
        if(stream->video_output)
            [stream->cap_session removeOutput:stream->video_output];

        if(stream->dev_input)
            [stream->cap_session removeInput:stream->dev_input];

        [stream->cap_session release];
        stream->cap_session = NULL;
    }

    if (stream->cap_connection)
    {
	[stream->cap_connection release];
	stream->cap_connection = NULL;
    }

    if (stream->video_output)
    {
        [stream->video_output release];
        stream->video_output = NULL;
    }

    if (stream->dev_input)
    {
        [stream->dev_input release];
        stream->dev_input = NULL;
    }

    if (stream->preview_layer)
    {
        [stream->preview_layer release];
        stream->preview_layer = NULL;
    }

    if (stream->vout_delegate)
    {
        [stream->vout_delegate release];
        stream->vout_delegate = NULL;
    }

    if (stream->vpreview_view_controller)
    {
        [stream->vpreview_view_controller release];
        stream->vpreview_view_controller = NULL;
    }

    if (stream->vin_view_controller)
    {
        [stream->vin_view_controller release];
        stream->vin_view_controller = NULL;
    }

    if (stream->previewView)
    {
        [stream->previewView release];
        stream->previewView = NULL;
    }

    if (stream->video_render_queue) {
        dispatch_release(stream->video_render_queue);
        stream->video_render_queue = NULL;
    }

    pj_pool_release(stream->pool);

    return PJ_SUCCESS;
}

#endif
#endif	/* PJMEDIA_VIDEO_DEV_HAS_IOS */
