#define LOG_NDEBUG 0
#define LOG_TAG "TSRender"

#include <stdint.h>
#include <sys/types.h>
#include <math.h>
#include <fcntl.h>
#include <utils/misc.h>
#include <signal.h>

#include <cutils/properties.h>

#include <utils/Atomic.h>
#include <utils/Errors.h>
#include <utils/Log.h>
#include <utils/threads.h>

#include <GLES/egl.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <EGL/eglext.h>

#include <SkString.h>
#include <SkPaint.h>
#include <SkCanvas.h>
#include <SkBitmap.h>
#include <SkStream.h>

#include "TimestampRender.h"

#define DEFAULT_FONT_HEIGHT 60

namespace android {

// ---------------------------------------------------------------------------

TimestampRender::TimestampRender() :
mPaint(NULL), mBitmap(NULL), mCanvas(NULL), mTexture(0),
mFgColor(SK_ColorWHITE), mBgColor(SK_ColorGRAY) {
}

TimestampRender::~TimestampRender() {
    glDeleteTextures(1, &mTexture);
    mTexture = 0;
    if (mPaint)  { delete mPaint;  mPaint  = NULL; }
    if (mBitmap) { delete mBitmap; mBitmap = NULL; }
    if (mCanvas) { delete mCanvas; mCanvas = NULL; }
}

void TimestampRender::init(int w, int h) {
    if (!h) {
        h = DEFAULT_FONT_HEIGHT;
    }
    if (!mPaint) {
        mPaint = new SkPaint();
        mPaint->setTextSize(SkIntToScalar(h));
        mPaint->setColor(mFgColor);
        mPaint->setAntiAlias(true);
        mPaint->setTextAlign(SkPaint::kCenter_Align);
    }
    if (!w) {
        w = SkScalarRound(mPaint->measureText("888888.8", 8));
    }
    if (!mBitmap) {
        mBitmap = new SkBitmap();
#ifdef FOR_ANDROID_KK
        mBitmap->setConfig(SkBitmap::kARGB_8888_Config, w, h);
        mBitmap->allocPixels();
#else
        SkImageInfo info = SkImageInfo::Make(w, h, kN32_SkColorType, kPremul_SkAlphaType);
        mBitmap->allocPixels(info);
#endif
    }
    if (!mCanvas) {
        mCanvas = new SkCanvas(*mBitmap);
    }
    if (!mTexture) {
        glGenTextures(1, &mTexture);
        glBindTexture(GL_TEXTURE_2D, mTexture);
        GLint crop[4] = { 0, h, w, -h };
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_CROP_RECT_OES, crop);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
}

void TimestampRender::setColor(uint32_t fgColor,uint32_t bgColor) {
    mBgColor = bgColor;
    mFgColor = fgColor;
    mPaint->setColor(mFgColor);
}

void TimestampRender::drawText(const char * fmt, ...) {
    if (!mBitmap) {
        ALOGE("Error on TimestampRender::drawText()! Not initialized!!\n");
        return;
    }

    // build the text
    va_list ap;
    va_start(ap, fmt);
    SkString text;
    text.appendVAList(fmt, ap);
    va_end(ap);

    mBitmap->lockPixels();

    // draw background
    mCanvas->drawColor(mBgColor);

    // draw text, painter is using kCenter_Align
    ALOGD("%s %d: DrawText(%s)\n", __FUNCTION__, __LINE__, text.c_str());
    mCanvas->drawText(text.c_str(), text.size(), mBitmap->width()/2, mBitmap->height(), *mPaint);

    mBitmap->notifyPixelsChanged();
    mBitmap->unlockPixels();
}

void TimestampRender::renderToGL(int x, int y) {
    if (!mBitmap) {
        ALOGE("Error on TimestampRender::render()! Not initialized!!\n");
        return;
    }
    int w = mBitmap->width();
    int h = mBitmap->height();
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mBitmap->getPixels());
    glDrawTexiOES(x, y, 0, w, h);
}

// ---------------------------------------------------------------------------

}
; // namespace android
