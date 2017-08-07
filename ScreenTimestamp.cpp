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

#include <SkPaint.h>
#include <SkCanvas.h>
#include <SkBitmap.h>
#include <SkStream.h>
#include <SkImageDecoder.h>

#include <GLES/gl.h>
#include <GLES/glext.h>
#include <EGL/eglext.h>

#include "ScreenTimestamp.h"

#define FOR_ANDROID_KK

#define SC_FONT_SIZE 40
#define SC_H_MARGIN  30
#define SC_V_MARGIN  20
#define SC_SURFACE_W 300
#define SC_SURFACE_H (SC_V_MARGIN*2 + SC_FONT_SIZE)

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
ScreenTimestamp::ScreenTimestamp() : Thread(false) {
    mSession = new SurfaceComposerClient();

    mPaint = new SkPaint();
    mPaint->setTextSize(SkIntToScalar(SC_FONT_SIZE));
    mPaint->setColor(SK_ColorBLACK);
    mPaint->setAntiAlias(true);

    mWidth  = SC_SURFACE_W;
    mHeight = SC_SURFACE_H;

    mBitmap = new SkBitmap();
#ifdef FOR_ANDROID_KK
    mBitmap->setConfig(SkBitmap::kARGB_8888_Config, mWidth, mHeight);
    mBitmap->allocPixels();
#else
    SkImageInfo info = SkImageInfo::Make(mWidth, mHeight, kN32_SkColorType, kPremul_SkAlphaType);
    mBitmap->allocPixels(info);
#endif

    mCanvas = new SkCanvas(*mBitmap);
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

    return NO_ERROR;
}

static void drawText(const char *str, SkBitmap& bitmap, SkCanvas& canvas, SkPaint& paint) {
    SkString text(str);
    bitmap.lockPixels();

    // draw background
    canvas.drawColor(SK_ColorWHITE);

    // draw text
    canvas.drawText(text.c_str(), text.size(), SC_H_MARGIN, (SC_V_MARGIN+SC_FONT_SIZE), paint);

    bitmap.notifyPixelsChanged();
    bitmap.unlockPixels();
}

void ScreenTimestamp::draw() {
    // generate a texture
    const int w = mBitmap->width();
    const int h = mBitmap->height();
    GLint crop[4] = { 0, h, w, -h };

    GLuint textureHandle = 0;
    glGenTextures(1, &textureHandle);
    glBindTexture(GL_TEXTURE_2D, textureHandle);
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_CROP_RECT_OES, crop);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glEnable(GL_TEXTURE_2D);
    glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    do {
        unsigned int tsNow = (unsigned int)gettimestamp_ms();
        char strbuf[64] = {0};
        sprintf(strbuf, "%u.%u", tsNow/1000, (tsNow%1000)/100);
        drawText(strbuf, *mBitmap, *mCanvas, *mPaint);

        glDisable(GL_SCISSOR_TEST);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, textureHandle);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mBitmap->getPixels());
        glDrawTexiOES(0, 0, 0, w, h);

        EGLBoolean res = eglSwapBuffers(mDisplay, mSurface);

        unsigned int tsTaken = (unsigned int)gettimestamp_ms() - tsNow;
        if (tsTaken < SC_SLEEP_INTERVAL_MS) {
            usleep((SC_SLEEP_INTERVAL_MS - tsTaken)*1000);
        }
    } while (!exitPending());

    // delete the texture
    glDeleteTextures(1, &textureHandle);
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
