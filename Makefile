CC := clang
CFLAGS := -ggdb -fPIC -fvisibility=hidden
ASFLAGS :=
CPPFLAGS := -Wall -Werror -Wextra
LIBRARIES := -ldl

C_SOURCES := macspoof.c

ASM_64_BIT := ioctl64.s

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

LIBRARY := libhwaddr.so

.PHONY: all clean

all: $(LIBRARY)

clean:
	$(RM) $(OBJECTS) $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBRARIES)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

%.o: %.s
	$(CC) -c $(ASFLAGS) -o $@ $<
