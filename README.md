Macspoof
========

[![Build Status](https://travis-ci.org/eatnumber1/macspoof.svg?branch=master)](https://travis-ci.org/eatnumber1/macspoof)

## Introduction
Macspoof is a tool to override your mac address on a per-application basis.

Specifically, macspoof allows you to control what your system responds with when
applications ask your system for it's MAC address.

## Dependencies
[libconfig][libconfig]

## Installation
Just `sudo make install`. It will install to `/usr/local`.

## Use
Have a look at [macspoof.conf][macspoof-conf]. This file allows you to configure
how macspoof overrides MAC addresses. It allows you to override all or part of a
mac address on a per-application and per-interface basis. You declare which
applications will be overridden, and then when you run macspoof, you specify
which application is running with the `-a` flag. If you do not specify the `-a`
flag, the application named `default_application` will be used.

Each application's value must either be an array of integers or a list of
groups. If a list of groups is provided, the groups must contain a `mac`
element which is an array of integers. In either case, those integer values
must be in the range of -1 to 255 (hex or decimal is accepted). If a list of
groups is provided, you must additionally provide exactly one of `default` or
`interface`. `interface` specifies the exact interface to override. `default`
specifies that all not otherwise specified interfaces will be overridden.  The
values declared in the `mac` array will override the returned MAC address. The
array does not have to cover all six of the octets in a mac address. You can
also explicitly ignore an octet by using the value -1.

By default, the application will look for a config file in `~/.macspoofrc` and
`/etc/macspoof.conf`. You can override this behavior with the `-f` or the `-c`
options. `-f` specifies a config file to read from, `-c` allows you to specify
the config on the command line.

## Examples
These examples are using the provided [macspoof.conf][macspoof-conf] file.

If your machine's normal MAC addresses are:

```
$ ifconfig
eth0      Link encap:Ethernet  HWaddr f2:3c:91:96:d2:7f
    ...

eth0:0    Link encap:Ethernet  HWaddr f2:3c:91:96:d2:7f
    ...
```

Then using macspoof, you get:

```
$ macspoof ifconfig
eth0      Link encap:Ethernet  HWaddr de:ad:be:ef:d2:7f
    ...

eth0:0    Link encap:Ethernet  HWaddr de:ad:be:ef:d2:7f
    ...
```

```
$ macspoof -a ifconfig ifconfig
eth0      Link encap:Ethernet  HWaddr f2:c0:ff:96:ee:7f
    ...

eth0:0    Link encap:Ethernet  HWaddr f2:c0:ff:96:ee:7f
    ...
```

```
$ macspoof -f macspoof.conf -a ip ifconfig
eth0      Link encap:Ethernet  HWaddr c0:ff:ee:96:d2:7f
    ...

eth0:0    Link encap:Ethernet  HWaddr 00:01:42:96:d2:7f
    ...
```

```
$ macspoof -c 'default_application: [0xc0, 0xff, 0xee];' ifconfig
eth0      Link encap:Ethernet  HWaddr c0:ff:ee:96:d2:7f
    ...

eth0:0    Link encap:Ethernet  HWaddr c0:ff:ee:96:d2:7f
    ...
```

## Why is my platform or architecture not supported?
Because I didn't feel like it.

If you ask, I'll probably implement it. Seriously. Ask. I'm a pretty friendly
guy :). Specifically though, the BSD family including OS X uses a completely
different way of getting a MAC address, and I'd have to write all new ASM to
support 32-bit Linux.

# Testing
To run the tests, just run `make test`. Note that you must have glib installed.

# Advanced

## How does it work?
On Linux, the [system call][SIOCGIFHWADDR] to retrieve your MAC address is
`ioctl(SIOCGIFHWADDR)`. This tool overrides this call allowing you to change
what is returned.

### WTF, why is this partially written in ASM?
Ugh. I'll start with this.

    Getting involved with dynamic argument lists is a lot like getting involved with
    absinthe: darkly exotic and stimulating at first, but destructive and
    mind-rotting in the end.

Because [ioctl][ioctl] is a variadic function, you can't trivially wrap it.

 * There is no `vioctl`.
 * GCC has [builtins][GCC-call-construct] for constructing function calls, but
   `__builtin_apply` requires me to know the size of the arguments passed into
   the function. `ioctl` is just too broadly used to know that.

Instead, I borrowed a concept from what the [dynamic linker][dyld] does. Under
Macspoof, when you call `ioctl`, the ASM saves the entire state of the call,
then calls `ioctl_resolver` to figure out what the actual function to call is,
which it then returns. Back in the ASM, all of the context for the original call
to `ioctl` is restored and the function returned by `ioctl_resolver` is jumped
into. The end result is then that it seems as if you'd directly called the
function. All traces of the ASM or `ioctl_resolver` are gone.

Now all `ioctl_resolver` does is figure out whether this is a call to retrieve
the MAC address or not. If it's not, it returns a pointer to the real `ioctl`,
otherwise it returns a pointer to the `ioctl_get_hwaddr` function which performs
the overrides. By doing this we can know the exact number of arguments in
`ioctl_get_hwaddr`. [FML][FML]

[macspoof-conf]: https://github.com/eatnumber1/macspoof/blob/master/macspoof.conf
[libconfig]: http://www.hyperrealm.com/libconfig/
[FML]: http://www.fmylife.com/
[dyld]: http://en.wikipedia.org/wiki/Dynamic_linking
[GCC-call-construct]: http://gcc.gnu.org/onlinedocs/gcc/Constructing-Calls.html
[ioctl]: http://man7.org/linux/man-pages/man2/ioctl.2.html
[SIOCGIFHWADDR]: http://man7.org/linux/man-pages/man7/netdevice.7.html
