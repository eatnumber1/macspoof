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
#include <sys/types.h>
#include <sys/socket.h>

#include <libconfig.h>

#define DEFAULT_APPLICATION_NAME "default_application"

static int (*real_ioctl)(int d, int request, ...);

__attribute__((noreturn)) static void die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "macspoof: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(EXIT_FAILURE);
}

__attribute__((noreturn)) static void perror_die(const char *s) {
	char *msg = strerror(errno);
	die("%s: %s", s, msg);
}

__attribute__((constructor)) static void setup_real_ioctl(void) {
	dlerror();
	real_ioctl = dlsym(RTLD_NEXT, "ioctl");
	char *error = dlerror();
	if (error != NULL) die("%s", error);
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
		die("Cannot open any config file.");
	}
	return file;
}

static void read_config(void) {
	char *filename = NULL;
	FILE *file = open_config_file(&filename);

	if (config_read(config, file) != CONFIG_TRUE)
		die("%s:%d %s", filename, config_error_line(config), config_error_text(config));

	if (fclose(file) == EOF) perror_die("fclose");
}

static void typecheck_config_array(config_setting_t *array) {
	assert(config_setting_is_array(array));
	int length = config_setting_length(array);
	if (length > 6) die("Arrays must be fewer than seven elements.");
	for (int i = 0; i < length; i++) {
		config_setting_t *elem = config_setting_get_elem(array, i);
		int val = config_setting_get_int(elem);
		if (config_setting_type(elem) != CONFIG_TYPE_INT || val > 0xFF || val < -1) {
			die("Arrays must only contain numbers in the range -1 to 0xFF.");
		}
	}
}

static void typecheck_config_ifgroup(config_setting_t *ifgroup) {
	assert(config_setting_is_group(ifgroup));
	config_setting_t *dflt = config_setting_get_member(ifgroup, "default");
	config_setting_t *mac = config_setting_get_member(ifgroup, "mac");
	config_setting_t *interface = config_setting_get_member(ifgroup, "interface");

	if (dflt == NULL && interface == NULL)
		die("Interface group must contain either \"default\" or \"interface\" elements.");
	if (dflt != NULL && interface != NULL)
		die("Interface group must contain only one of \"default\" and \"interface\" elements.");
	if (mac == NULL)
		die("Interface group must contain a \"mac\" element.");
	if (dflt != NULL && config_setting_type(dflt) != CONFIG_TYPE_BOOL)
		die("Interface group element \"default\" must be a bool.");
	if (interface != NULL && config_setting_type(interface) != CONFIG_TYPE_STRING)
		die("Interface group element \"interface\" must be a string.");
	if (!config_setting_is_array(mac))
		die("Interface group element \"mac\" must be an array.");
	typecheck_config_array(mac);
}

static void typecheck_config_ifgrouplist(config_setting_t *list) {
	assert(config_setting_is_list(list));
	for (int i = 0; i < config_setting_length(list); i++) {
		config_setting_t *ifgroup = config_setting_get_elem(list, i);
		if (!config_setting_is_group(ifgroup))
			die("Interface group lists must only contain groups.");
		typecheck_config_ifgroup(ifgroup);
	}
}

static void read_app_config(void) {
	char *app_name = getenv("MACSPOOF_APPLICATION");
	if (app_name == NULL) app_name = DEFAULT_APPLICATION_NAME;

	config_setting_t *root = config_root_setting(config);
	assert(root != NULL);
	app_config = config_setting_get_member(root, app_name);
	if (app_config == NULL) die("Application \"%s\" not found.", app_name);
	switch(config_setting_type(app_config)) {
		case CONFIG_TYPE_ARRAY:
			typecheck_config_array(app_config);
			break;
		case CONFIG_TYPE_LIST:
			typecheck_config_ifgrouplist(app_config);
			break;
		default:
			die("Config for application \"%s\" must be an array or group.", app_name);
	}

}

__attribute__((constructor)) static void setup_config(void) {
	config = &config_real;
	config_init(config);
	read_config();
	read_app_config();
}

__attribute__((destructor)) static void destroy_config(void) {
	if (config != NULL) config_destroy(config);
}

static config_setting_t *interface_group_for(const char *ifname) {
	assert(config_setting_is_list(app_config));

	config_setting_t *default_if_conf = NULL;
	for (int i = 0; i < config_setting_length(app_config); i++) {
		config_setting_t *cur = config_setting_get_elem(app_config, i);

		config_setting_t *cur_ifname = config_setting_get_member(cur, "interface");
		if (cur_ifname != NULL) {
			const char *cur_ifname_str = config_setting_get_string(cur_ifname);
			assert(cur_ifname_str != NULL);
			if (strcmp(cur_ifname_str, ifname) == 0) return cur;
		}

		config_setting_t *cur_is_default = config_setting_get_member(cur, "default");
		if (
			default_if_conf == NULL &&
			cur_is_default != NULL &&
			config_setting_get_bool(cur_is_default)
		) {
			default_if_conf = cur;
		}
	}
	return default_if_conf;
}

static config_setting_t *mac_array_for_if(const char *ifname) {
	int app_config_type = config_setting_type(app_config);
	if (app_config_type == CONFIG_TYPE_ARRAY) {
		return app_config;
	}
	assert(app_config_type == CONFIG_TYPE_LIST);
	config_setting_t *ifgroup = interface_group_for(ifname);
	if (ifgroup == NULL) return NULL;
	config_setting_t *mac_array = config_setting_get_member(ifgroup, "mac");
	assert(mac_array != NULL);
	return mac_array;
}

static int ioctl_get_hwaddr(int d, int request, ...) {
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

static int ioctl_error(int d, int request, ...) {
	(void) d, (void) request;
	return -1;
}

void *ioctl_resolver(int d, int request) {
	if (request != SIOCGIFHWADDR) return real_ioctl;

	int type;
	socklen_t optlen = sizeof(int);
	if (getsockopt(d, SOL_SOCKET, SO_TYPE, &type, &optlen) == -1)
		return errno == ENOTSOCK ? real_ioctl : ioctl_error;

	return ioctl_get_hwaddr;
}
