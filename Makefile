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

MACSPOOF_LIB := libmacspoof.so

.PHONY: all clean install

all: $(MACSPOOF_LIB) $(SCRIPTS_OUT)

clean:
	$(RM) $(OBJECTS) $(MACSPOOF_LIB) $(SCRIPTS_OUT)

install: all
	$(INSTALL) -d $(LIBDIR) $(BINDIR) $(ETCDIR)
	$(INSTALL) $(SCRIPTS_OUT) $(BINDIR)
	$(INSTALL) $(MACSPOOF_LIB) $(LIBDIR)
	$(INSTALL) $(CONFIG) $(ETCDIR)

%: %.in
	$(SED) -e 's:@MACSPOOF_LIB@:$(LIBDIR)/$(MACSPOOF_LIB):g' $< > $@
	if ! $(CHMOD) +x $@; then rm -f $@; false; fi

$(MACSPOOF_LIB): $(OBJECTS)
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBRARIES)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

%.o: %.s
	$(CC) -c $(ASFLAGS) -o $@ $<
