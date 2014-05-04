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
#include <string.h>

#include <libconfig.h>

#define DEFAULT_INTERFACE_NAME "default_interface"
#define DEFAULT_APPLICATION_NAME "default_application"

int (*real_ioctl)(int d, int request, ...);

__attribute__((noreturn)) static void die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "macspoof: ");
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

__attribute__((noreturn)) static void perror_die(const char *s) {
	char *msg = strerror(errno);
	die("%s: %s\n", s, msg);
}

__attribute__((constructor)) static void setup_real_ioctl() {
	dlerror();
	real_ioctl = dlsym(RTLD_NEXT, "ioctl");
	char *error = dlerror();
	if (error != NULL) die("%s\n", error);
}

static config_t config_real;
static config_t *config;
static config_setting_t *app_config;

static FILE *open_config_file(char **filename) {
	assert(filename != NULL);

	char *config = getenv("MACSPOOF_CONFIG");
	if (config != NULL) {
		*filename = "<none>";
		FILE *file = fmemopen(config, strlen(config), "r");
		if (file == NULL) perror_die("fmemopen");
		return file;
	}

	*filename = getenv("MACSPOOF_CONFIG_FILE");
	if (*filename != NULL) {
		FILE *file = fopen(*filename, "r");
		if (file == NULL) perror_die("fopen");
		return file;
	}

	*filename = "~/.macspoofrc";
	FILE *file = fopen(*filename, "r");
	if (file != NULL) return file;

	*filename = MACSPOOF_ETCDIR "/macspoof.conf";
	file = fopen(*filename, "r");
	if (file == NULL) {
		perror("fopen");
		die("macspoof: Cannot open any config file.\n");
	}
	return file;
}

static void read_config() {
	char *filename = NULL;
	FILE *file = open_config_file(&filename);

	if (config_read(config, file) != CONFIG_TRUE)
		die("%s:%d %s\n", filename, config_error_line(config), config_error_text(config));

	if (fclose(file) == EOF) perror_die("fclose");
}

static void typecheck_config_array(config_setting_t *array) {
	assert(config_setting_is_array(array));
	int length = config_setting_length(array);
	if (length > 6) die("Arrays must be fewer than seven elements.\n");
	for (int i = 0; i < length; i++) {
		config_setting_t *elem = config_setting_get_elem(array, i);
		int val = config_setting_get_int(elem);
		if (config_setting_type(elem) != CONFIG_TYPE_INT || val > 0xFF || val < -1) {
			die("Arrays must only contain numbers in the range -1 to 0xFF.\n");
		}
	}
}

static void typecheck_config_ifgroup(config_setting_t *ifgroup) {
	assert(config_setting_is_group(ifgroup));
	for (int i = 0; i < config_setting_length(ifgroup); i++) {
		config_setting_t *ary = config_setting_get_elem(ifgroup, i);
		if (!config_setting_is_array(ary)) {
			die("Interface groups must only contain arrays.\n");
		}
		typecheck_config_array(ary);
	}
}

static void read_app_config() {
	char *app_name = getenv("MACSPOOF_APPLICATION");
	if (app_name == NULL) app_name = DEFAULT_APPLICATION_NAME;

	config_setting_t *root = config_root_setting(config);
	assert(root != NULL);
	app_config = config_setting_get_member(root, app_name);
	if (app_config == NULL) die("Application \"%s\" not found.\n", app_name);
	switch(config_setting_type(app_config)) {
		case CONFIG_TYPE_ARRAY:
			typecheck_config_array(app_config);
			break;
		case CONFIG_TYPE_GROUP:
			typecheck_config_ifgroup(app_config);
			break;
		default:
			die("Config for application \"%s\" must be an array or group.\n", app_name);
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

// Unfortunately, config_setting_get_member returns eth0 when we asked for eth0:0.
// That's probably a bug.
static config_setting_t *config_setting_get_member_exact(const char *name) {
	config_setting_t *default_if_conf = NULL;
	for (int i = 0; i < config_setting_length(app_config); i++) {
		config_setting_t *cur = config_setting_get_elem(app_config, i);
		const char *if_conf_name = config_setting_name(cur);
		if (strcmp(if_conf_name, name) == 0) return cur;
		if (default_if_conf == NULL && strcmp(if_conf_name, "default_interface") == 0)
			default_if_conf = cur;
	}
	return default_if_conf;
}

static config_setting_t *mac_array_for_if(const char *ifname) {
	int app_config_type = config_setting_type(app_config);
	if (app_config_type == CONFIG_TYPE_ARRAY) {
		return app_config;
	}
	assert(app_config_type == CONFIG_TYPE_GROUP);
	return config_setting_get_member_exact(ifname);
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

	config_setting_t *mac_array = mac_array_for_if(ifreq->ifr_name);
	if (mac_array == NULL) return ret;
	for (int i = 0; i < config_setting_length(mac_array); i++) {
		int val = config_setting_get_int_elem(mac_array, i);
		if (val != -1) ifreq->ifr_hwaddr.sa_data[i] = val;
	}

	return ret;
}

void *ioctl_resolver(int d, int request) {
	(void) d;
	return request == SIOCGIFHWADDR ? ioctl_get_hwaddr : real_ioctl;
}
