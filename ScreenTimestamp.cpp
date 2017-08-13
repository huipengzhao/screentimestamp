#define LOG_NDEBUG 0
#define LOG_TAG "ScreenTimestamp"

#include <stdint.h>
#include <sys/types.h>
#include <math.h>
#include <fcntl.h>
#include <utils/misc.h>
#include <signal.h>

#include <cutils/properties.h>

#include <androidfw/AssetManager.h>
#include <binder/IPCThreadState.h>
#include <utils/Atomic.h>
#include <utils/Errors.h>
#include <utils/Log.h>

#include <ui/PixelFormat.h>
#include <ui/Rect.h>
#include <ui/Region.h>
#include <ui/DisplayInfo.h>

#include <gui/ISurfaceComposer.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>

#include <GLES/gl.h>
#include <GLES/glext.h>
#include <EGL/eglext.h>

#include "ScreenTimestamp.h"

/*
    We want to show clock of the format XXX.Y seconds.
    So sample frequency should be 1/2 of 100 ms, that is 50 ms.
*/
#define SC_SLEEP_INTERVAL_MS (50)
#define SC_SLEEP_INTERVAL_US (SC_SLEEP_INTERVAL_MS*1000)

#include <time.h>
static inline long long gettimestamp_ns() {
    long long val_ns;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    val_ns = ts.tv_sec;
    val_ns*= 1000000000;
    val_ns+= ts.tv_nsec;
    return val_ns;
}

static inline long long gettimestamp_us() {
    return (gettimestamp_ns()+500)/1000;
}

static inline long long gettimestamp_ms() {
    return (gettimestamp_ns()+500000)/1000000;
}

namespace android {

struct Texture {
    GLint   w;
    GLint   h;
    GLuint  name;
};

// ---------------------------------------------------------------------------
ScreenTimestamp::ScreenTimestamp(unsigned int msStoptime) : Thread(false), mStoptime(msStoptime) {
    mSession = new SurfaceComposerClient();
    mRender = new TimestampRender();
    mWidth = 400;
    mHeight = 80;
}

ScreenTimestamp::~ScreenTimestamp() {
    //TODO: release the resource
}

void ScreenTimestamp::binderDied(const wp<IBinder>&) {
    // woah, surfaceflinger died!
    ALOGD("SurfaceFlinger died, exiting...");

    // calling requestExit() is not enough here because the Surface code
    // might be blocked on a condition variable that will never be updated.
    kill(getpid(), SIGKILL);
    requestExit();
}

void ScreenTimestamp::onFirstRef() {
    status_t err = mSession->linkToComposerDeath(this);
    ALOGE_IF(err, "linkToComposerDeath failed (%s) ", strerror(-err));
    if (err == NO_ERROR) {
        run("ScreenTimestamp", PRIORITY_DISPLAY);
    }
}

status_t ScreenTimestamp::readyToRun() {
    // create the native surface
    sp<SurfaceControl> control = mSession->createSurface(
            String8("ScreenTimestamp"), mWidth, mHeight, PIXEL_FORMAT_RGB_565);

    SurfaceComposerClient::openGlobalTransaction();
    control->setLayer(0x40000001); //higher than BootAnimation
    SurfaceComposerClient::closeGlobalTransaction();

    sp<Surface> s = control->getSurface();

    // initialize opengl and egl
    const EGLint attribs[] = {
            EGL_RED_SIZE,   8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE,  8,
            EGL_DEPTH_SIZE, 0,
            EGL_NONE
    };
    EGLint w, h, dummy;
    EGLint numConfigs;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    eglInitialize(display, 0, 0);
    eglChooseConfig(display, attribs, &config, 1, &numConfigs);
    surface = eglCreateWindowSurface(display, config, s.get(), NULL);
    context = eglCreateContext(display, config, NULL, NULL);
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE)
        return NO_INIT;

    mDisplay = display;
    mContext = context;
    mSurface = surface;
    mWidth = w;
    mHeight = h;
    mFlingerSurfaceControl = control;
    mFlingerSurface = s;

    ALOGD("%s %d: mWidth=%u, mHeight=%u\n", __FUNCTION__, __LINE__, mWidth, mHeight);
    return NO_ERROR;
}

void ScreenTimestamp::draw() {
    // clear screen
    glShadeModel(GL_FLAT);
    glDisable(GL_DITHER);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(mDisplay, mSurface);

    // Blend state
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    glEnable(GL_TEXTURE_2D);
    glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    mRender->init(mWidth, mHeight);

    do {
        nsecs_t now = systemTime();

        unsigned int ms = (unsigned int)ns2ms(now);
        mRender->drawText("%u.%u", ms/1000, (ms%1000)/100);
        glDisable(GL_SCISSOR_TEST); // let ts-render draw outside the scissor.
        mRender->renderToGL(0, 0); // coordinate to the surface.

        EGLBoolean res = eglSwapBuffers(mDisplay, mSurface);

        // 10fps: don't animate too fast to preserve CPU
        const nsecs_t sleepTime = 100000 - ns2us(systemTime() - now);
        if (sleepTime > 0)
            usleep(sleepTime);

        checkExit();

    } while (!exitPending());
}

void ScreenTimestamp::checkExit() {
    unsigned int msNow = (unsigned int)gettimestamp_ms();
    if (mStoptime && msNow >= mStoptime) {
        requestExit();
    }
}

bool ScreenTimestamp::threadLoop() {
    draw();

    eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(mDisplay, mContext);
    eglDestroySurface(mDisplay, mSurface);
    mFlingerSurface.clear();
    mFlingerSurfaceControl.clear();
    eglTerminate(mDisplay);
    IPCThreadState::self()->stopProcess();
    return false;
}

// ---------------------------------------------------------------------------
}; //namespace android
