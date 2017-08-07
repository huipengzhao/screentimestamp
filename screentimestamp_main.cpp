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

#define OPT_LIST "d:h:"

struct opt_args {
	unsigned int duration; //second
	//todo: other args
};

void show_usage(void)
{
	printf("Usage: screentimestamp [options]\n");
	printf("        -h         show this help.\n");
	printf("        -d <duration>  programe run time.\n");

	printf("\n");
}

int parse_args(int argc, char *argv[], struct opt_args* args)
{
	int ch = 0;
	int err = 0;

	memset(args, 0, sizeof(*args));

	opterr=0;
	while ((ch=getopt(argc, argv, OPT_LIST)) != -1) {
		char * endch;
		switch (ch) {
		case 'h':
			show_usage();
			break;
		case 'd':
			args->duration = strtol(optarg, &endch, 0);
			err = (*endch != '\0' || args->duration <= 0);
			break;
		default:
			ch = optopt;
			err = 1;
			break;
		}
		if (err) {
			printf("Invalid option: -%c %s", ch, optarg);
			return -1;
		}
	}
	return 0;
}

int verify_args(struct opt_args * args)
{
	if (!args->duration) {
		args->duration = 30;
	}
	return 0;
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
#if defined(HAVE_PTHREADS)
    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_DISPLAY);
#endif
	struct opt_args args;
	if(parse_args(argc, argv, &args) < 0) {
		return -1;
	}
	verify_args(&args);
    sp<ProcessState> proc(ProcessState::self());
    ProcessState::self()->startThreadPool();
    sp<ScreenTimestamp> clock = new ScreenTimestamp(args.duration);
    IPCThreadState::self()->joinThreadPool();

    return 0;
}
