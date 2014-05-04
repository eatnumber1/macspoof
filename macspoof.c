#define _GNU_SOURCE
#include <stdlib.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <assert.h>
#include <stdarg.h>

#include <stdio.h>

int (*real_ioctl)(int d, int request, ...);

__attribute((constructor)) static void setup_real_ioctl() {
	dlerror();
	real_ioctl = dlsym(RTLD_NEXT, "ioctl");
	char *error = dlerror();
	assert(error == NULL);
}

int ioctl_get_hwaddr(int d, int request, ...) {
	assert(request == SIOCGIFHWADDR);

	va_list ap;
	va_start(ap, request);
	struct ifreq *ifreq = va_arg(ap, struct ifreq *);
	va_end(ap);

	//(void) ifreq, (void) d;
	//int ret = 0;
	int ret = real_ioctl(d, request, ifreq);
	if (ret != 0) {
		return ret;
	}

	ifreq->ifr_hwaddr.sa_data[0] = 0x00;
	ifreq->ifr_hwaddr.sa_data[1] = 0x01;
	ifreq->ifr_hwaddr.sa_data[2] = 0x42;
	fprintf(stderr, "Hello world! SIOCGIFHWADDR=%jd\n", SIOCGIFHWADDR);

	return ret;
}

void *ioctl_resolver(int d, int request) {
	(void) d;
	return request == SIOCGIFHWADDR ? ioctl_get_hwaddr : real_ioctl;
}
