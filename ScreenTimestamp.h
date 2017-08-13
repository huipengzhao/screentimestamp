#ifndef SCREENCLOCK_H
#define SCREENCLOCK_H

#include <stdint.h>
#include <sys/types.h>
#include <utils/Thread.h>
#include <EGL/egl.h>
#include <GLES/gl.h>

#include "TimestampRender.h"

namespace android {

// ---------------------------------------------------------------------------

class Surface;
class SurfaceControl;
class SurfaceComposerClient;

class ScreenTimestamp : public Thread, public IBinder::DeathRecipient
{
public:
    ScreenTimestamp(unsigned int msStoptime);
    virtual ~ScreenTimestamp();

private:
    virtual bool        threadLoop();
    virtual status_t    readyToRun();
    virtual void        onFirstRef();
    virtual void        binderDied(const wp<IBinder>& who);

    void draw();
    void checkExit();

private:
	unsigned int mStoptime; // in millisecond
    sp<SurfaceComposerClient> mSession;
    TimestampRender *mRender;
    int mWidth;  // surface width
    int mHeight; // surface height
    EGLDisplay mDisplay;
    EGLContext mContext;
    EGLSurface mSurface;
    sp<SurfaceControl> mFlingerSurfaceControl;
    sp<Surface> mFlingerSurface;
};

// ---------------------------------------------------------------------------
}; // namespace android

#endif //SCREENCLOCK_H
