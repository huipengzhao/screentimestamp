#define LOG_TAG "ScreenTimestamp"
#include <cutils/properties.h>

#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>

#include <utils/Log.h>
#include <utils/threads.h>

#if defined(HAVE_PTHREADS)
# include <pthread.h>
# include <sys/resource.h>
#endif

#include "ScreenTimestamp.h"

using namespace android;

// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
#if defined(HAVE_PTHREADS)
    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_DISPLAY);
#endif

    sp<ProcessState> proc(ProcessState::self());
    ProcessState::self()->startThreadPool();
    sp<ScreenTimestamp> clock = new ScreenTimestamp();
    IPCThreadState::self()->joinThreadPool();

    return 0;
}
