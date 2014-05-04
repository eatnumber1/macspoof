PREFIX := /usr/local
LIBDIR = $(PREFIX)/lib
BINDIR = $(PREFIX)/bin
ETCDIR = $(PREFIX)/etc

CC := clang
PKG_CONFIG := pkg-config
SED := sed
CHMOD := chmod
INSTALL := install

CFLAGS := -ggdb -fPIC -fvisibility=hidden
ASFLAGS :=
CPPFLAGS := -Wall -Werror -Wextra

CFLAGS += -DMACSPOOF_ETCDIR="\"$(ETCDIR)\""
CFLAGS += $(shell $(PKG_CONFIG) --cflags libconfig)
LIBRARIES := -ldl $(shell $(PKG_CONFIG) --libs libconfig)

CONFIG := macspoof.conf
SCRIPTS_IN := macspoof.in
C_SOURCES := macspoof.c
ASM_64_BIT := ioctl64.s
TEST_SOURCES := macspoof_test.c

SCRIPTS_OUT := $(SCRIPTS_IN:%.in=%)
ARCH_BITS := $(shell getconf LONG_BIT)
PLATFORM := $(shell uname -s)

ifneq "$(PLATFORM)" "Linux"
$(error Only Linux is supported)
endif

SOURCES := $(C_SOURCES)
ifeq "$(ARCH_BITS)" "64"
SOURCES := $(SOURCES) $(ASM_64_BIT)
else
$(error Architecture not supported)
endif

OBJECTS := $(SOURCES:.c=.o)
OBJECTS := $(OBJECTS:.s=.o)
TEST_OBJECTS := $(TEST_SOURCES:.c=.o)

MACSPOOF_LIB := libmacspoof.so
MACSPOOF_TEST := macspoof_test

.PHONY: all clean install test

all: $(MACSPOOF_LIB) $(SCRIPTS_OUT)

clean:
	$(RM) $(OBJECTS) $(MACSPOOF_LIB) $(SCRIPTS_OUT) $(MACSPOOF_TEST) $(TEST_OBJECTS)

install: all
	$(INSTALL) -d $(LIBDIR) $(BINDIR) $(ETCDIR)
	$(INSTALL) $(SCRIPTS_OUT) $(BINDIR)
	$(INSTALL) $(MACSPOOF_LIB) $(LIBDIR)
	$(INSTALL) $(CONFIG) $(ETCDIR)

test: $(MACSPOOF_LIB)
	$(MAKE) LIBRARIES="`$(PKG_CONFIG) --libs glib-2.0` -ldl" CFLAGS="`$(PKG_CONFIG) --cflags glib-2.0` -ggdb" $(MACSPOOF_TEST)
	./macspoof_test

$(MACSPOOF_TEST): $(TEST_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBRARIES)

%: %.in
	$(SED) -e 's:@MACSPOOF_LIB@:$(LIBDIR)/$(MACSPOOF_LIB):g' $< > $@
	if ! $(CHMOD) +x $@; then rm -f $@; false; fi

$(MACSPOOF_LIB): $(OBJECTS)
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBRARIES)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

%.o: %.s
	$(CC) -c $(ASFLAGS) -o $@ $<
