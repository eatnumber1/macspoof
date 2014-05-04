#define _GNU_SOURCE
#include <stdlib.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

#include <libconfig.h>

int (*real_ioctl)(int d, int request, ...);

__attribute__((constructor)) static void setup_real_ioctl() {
	dlerror();
	real_ioctl = dlsym(RTLD_NEXT, "ioctl");
	char *error = dlerror();
	if (error != NULL) {
		fprintf(stderr, "%s\n", error);
		exit(EXIT_FAILURE);
	}
}

static config_t config_real;
static config_t *config;
static config_setting_t *mac_array;

static FILE *dfopen(const char *fn, const char *mode) {
	FILE *file = fopen(fn, mode);
	if (file == NULL) {
		perror("fopen");
		exit(EXIT_FAILURE);
	}
	return file;
}

static FILE *open_config_file(char **filename) {
	assert(filename != NULL);

	*filename = getenv("MACSPOOF_CONFIG");
	if (*filename != NULL) {
		return dfopen(*filename, "r");
	}

	*filename = "~/.macspoofrc";
	FILE *file = fopen(*filename, "r");
	if (file != NULL) return file;

	*filename = "/etc/macspoof.conf";
	file = fopen(*filename, "r");
	if (file == NULL) {
		perror("fopen");
		fprintf(stderr, "macspoof: Cannot open any config file.\n");
		exit(EXIT_FAILURE);
	}
	return file;
}

static void read_config() {
	char *filename = NULL;
	FILE *file = open_config_file(&filename);

	if (config_read(config, file) != CONFIG_TRUE) {
		fprintf(
			stderr,
			"%s:%d %s\n",
			filename,
			config_error_line(config),
			config_error_text(config)
		);
		exit(EXIT_FAILURE);
	}
}

static void read_app_config() {
	char *app_name = getenv("MACSPOOF_APPLICATION");
	if (app_name == NULL) app_name = "default";

	config_setting_t *root = config_root_setting(config);
	assert(root != NULL);
	mac_array = config_setting_get_member(root, app_name);
	if (mac_array == NULL) {
		fprintf(stderr, "macspoof: Application \"%s\" not found.\n", app_name);
		exit(EXIT_FAILURE);
	}
	if (config_setting_type(mac_array) != CONFIG_TYPE_ARRAY) {
		fprintf(stderr, "macspoof: Config for application \"%s\" is not an array.\n", app_name);
		exit(EXIT_FAILURE);
	}
	int length = config_setting_length(mac_array);
	if (length > 6) {
		fprintf(stderr, "macspoof: The array for application \"%s\" must be fewer than seven elements.\n", app_name);
		exit(EXIT_FAILURE);
	}
	for (int i = 0; i < length; i++) {
		config_setting_t *elem = config_setting_get_elem(mac_array, i);
		int val = config_setting_get_int(elem);
		if (config_setting_type(elem) != CONFIG_TYPE_INT || val > 0xFF || val < 0) {
			fprintf(stderr, "macspoof: Config for application \"%s\" must be an array of "
					"numbers in the range 0x00 - 0xFF.\n", app_name);
			exit(EXIT_FAILURE);
		}
	}
}

__attribute__((constructor)) static void setup_config() {
	config = &config_real;
	config_init(config);
	read_config();
	read_app_config();
}

__attribute__((destructor)) static void destroy_config() {
	if (config != NULL) config_destroy(config);
}

int ioctl_get_hwaddr(int d, int request, ...) {
	assert(request == SIOCGIFHWADDR);

	va_list ap;
	va_start(ap, request);
	struct ifreq *ifreq = va_arg(ap, struct ifreq *);
	va_end(ap);

	int ret = real_ioctl(d, request, ifreq);
	if (ret != 0) {
		return ret;
	}

	for (int i = 0; i < config_setting_length(mac_array); i++) {
		ifreq->ifr_hwaddr.sa_data[i] = config_setting_get_int_elem(mac_array, i);
	}

	return ret;
}

void *ioctl_resolver(int d, int request) {
	(void) d;
	return request == SIOCGIFHWADDR ? ioctl_get_hwaddr : real_ioctl;
}
