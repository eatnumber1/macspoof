#include <sys/ioctl.h>
#include <net/if.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>

#include <glib.h>

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

static GSList *tempfiles;

static void cleanup_tempfile(void *namevoid) {
	char *name = namevoid;
	assert(name != NULL);
	if (unlink(name) == -1) {
		perror("unlink");
		_Exit(EXIT_FAILURE);
	}
	free(name);
}

static void cleanup_tempfiles() {
	g_slist_free_full(tempfiles, cleanup_tempfile);
}

static char *mkconfig(const char *fmt, ...) {
	char *name = strdup("macspoof_test.XXXXXXXX");
	if (name == NULL) perror_die("strdup");
	int fd = mkstemp(name);
	if (fd == -1) perror_die("mkstemp");
	tempfiles = g_slist_prepend(tempfiles, name);
	FILE *file = fdopen(fd, "r+");
	if (file == NULL) perror_die("fdopen");

	va_list ap;
	va_start(ap, fmt);
	if (vfprintf(file, fmt, ap) == -1) perror_die("vfprintf");
	va_end(ap);

	if (fclose(file) == EOF) perror_die("fclose");

	return name;
}

static int open_socket() {
	int sock;
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	return sock;
}

static void *load_macspoof() {
	void *handle = dlopen("./libmacspoof.so", RTLD_LAZY);
	if (handle == NULL) die("%s\n", dlerror());
	return handle;
}

static void unload_macspoof(void *handle) {
	if (dlclose(handle) != 0) die("%s\n", dlerror());
}

typedef int (ioctl_fn)(int, int, ...);
static ioctl_fn *get_ioctl(void *handle) {
	dlerror();
	ioctl_fn *ioctl = dlsym(handle, "ioctl");
	char *error = dlerror();
	if (error != NULL) die("%s\n", error);
	return ioctl;
}

static void get_mac(ioctl_fn *ioctl_f, char addr[static 6]) {
	int fd = open_socket();

	struct ifreq ifs[64];
	struct ifconf ifc;
	ifc.ifc_len = sizeof(ifs);
	ifc.ifc_req = ifs;
	// This will call the real ioctl since this behavior isn't part of the test.
	if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) perror_die("ioctl");

	for (struct ifreq *ifr = ifc.ifc_req; ifs + (ifc.ifc_len / sizeof(struct ifreq)); ifr++) {
		if (ifr->ifr_addr.sa_family == AF_INET) {
			struct ifreq ifreq;
			strncpy(ifreq.ifr_name, ifr->ifr_name, sizeof(ifreq.ifr_name));
			if (ioctl_f(fd, SIOCGIFHWADDR, &ifreq) < 0) perror_die("ioctl");
			memcpy(addr, ifreq.ifr_hwaddr.sa_data, 6);
			break;
		}
	}

	if (close(fd) == -1) perror_die("close");
}

static void setup_environment(const char *config, const char *application) {
	if (setenv("MACSPOOF_CONFIG_FILE", config, true) == -1) perror_die("setenv");
	if (application != NULL) {
		if (setenv("MACSPOOF_APPLICATION", application, true) == -1)
			perror_die("setenv");
	} else {
		if (unsetenv("MACSPOOF_APPLICATION") == -1) perror_die("unsetenv");
	}
}

static char *mkconfig_from_array(const char *application, int bytes[], size_t len) {
	if (application == NULL) application = "default_application";

	// The math here is
	// len * 4 for the four characters in 0xAA
	// (len - 1) * 2 for the ", " separators
	// 2 for the [ and ]
	// 1 for NULL
	size_t arylen = len * 4 + (len - 1) * 2 + 2 + 1;
	char ary[arylen];
	ary[0] = '\0';
	char *aryptr = strncat(ary, "[", 1) + 1;
	for (size_t i = 0; i < len; i++) {
		int nchrs;
		if (bytes[i] == -1) {
			nchrs = snprintf(aryptr, 3, "%d", bytes[i]);
			assert(nchrs == 2);
		} else {
			nchrs = snprintf(aryptr, 5, "0x%hhx", (char) bytes[i]);
			assert(nchrs == 4);
		}
		aryptr += nchrs;
		if (i != len - 1) aryptr = strncat(aryptr, ", ", 2) + 2;
	}
	aryptr = strncat(aryptr, "]", 1) + 1;
	assert(*aryptr == '\0');
	assert(strlen(ary) + 1 <= arylen);
	return mkconfig("%s: %s;", application, ary);
}

static void assert_cmpary(int *expected, char *actual, size_t n) {
	for (size_t i = 0; i < n; i++) {
		if (expected[i] == -1) continue;
		g_assert_cmphex(actual[i], ==, (char) expected[i]);
	}
}

static void test_simple_array_config_default_application() {
	int test_mac[] = { 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad };
	size_t test_mac_len = sizeof(test_mac) / sizeof(int);
	char *config = mkconfig_from_array(NULL, test_mac, test_mac_len);

	setup_environment(config, NULL);
	void *handle = load_macspoof();

	char mac[6];
	get_mac(get_ioctl(handle), mac);

	unload_macspoof(handle);

	assert_cmpary(test_mac, mac, test_mac_len);
}

static void test_simple_array_config_specific_application() {
	int test_mac[] = { 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad };
	size_t test_mac_len = sizeof(test_mac) / sizeof(int);
	const char *application = "some_application";

	char *config = mkconfig_from_array(application, test_mac, test_mac_len);

	setup_environment(config, application);
	void *handle = load_macspoof();

	char mac[6];
	get_mac(get_ioctl(handle), mac);

	unload_macspoof(handle);

	assert_cmpary(test_mac, mac, test_mac_len);
}

static void test_simple_array_config_partial_mac_prefix() {
	int test_mac[] = { 0xde, 0xad, 0xbe, 0xef };
	size_t test_mac_len = sizeof(test_mac) / sizeof(int);

	char *config = mkconfig_from_array(NULL, test_mac, test_mac_len);

	setup_environment(config, NULL);
	void *handle = load_macspoof();

	char mac[6];
	get_mac(get_ioctl(handle), mac);

	unload_macspoof(handle);

	assert_cmpary(test_mac, mac, test_mac_len);
}

static void test_simple_array_config_partial_mac_suffix() {
	int test_mac[] = { -1, -1, 0xde, 0xad, 0xbe, 0xef };
	size_t test_mac_len = sizeof(test_mac) / sizeof(int);

	char *config = mkconfig_from_array(NULL, test_mac, test_mac_len);

	setup_environment(config, NULL);
	void *handle = load_macspoof();

	char mac[6];
	get_mac(get_ioctl(handle), mac);

	unload_macspoof(handle);

	assert_cmpary(test_mac, mac, test_mac_len);
}

static void test_group_config() {
	int test_mac[] = { 0xaa, 0xbb };
	size_t test_mac_len = sizeof(test_mac) / sizeof(int);
	char *config = mkconfig("default_application: { default_interface: [0xaa, 0xbb]; }");

	setup_environment(config, NULL);
	void *handle = load_macspoof();

	char mac[6];
	get_mac(get_ioctl(handle), mac);

	unload_macspoof(handle);

	assert_cmpary(test_mac, mac, test_mac_len);
}

int main(int argc, char **argv) {
	g_test_init(&argc, &argv, NULL);

	if (atexit(cleanup_tempfiles) != 0) {
		fprintf(stderr, "Unable to register atexit handler\n");
		exit(EXIT_FAILURE);
	}

	g_test_add_func("/macspoof/config/simple_array/default_app", test_simple_array_config_default_application);
	g_test_add_func("/macspoof/config/simple_array/specific_app", test_simple_array_config_specific_application);
	g_test_add_func("/macspoof/config/simple_array/partial_mac_prefix", test_simple_array_config_partial_mac_prefix);
	g_test_add_func("/macspoof/config/simple_array/partial_mac_suffix", test_simple_array_config_partial_mac_suffix);
	g_test_add_func("/macspoof/config/group", test_simple_array_config_partial_mac_suffix);

	return g_test_run();
}
