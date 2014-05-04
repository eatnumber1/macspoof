Macspoof
========

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
flag, the application named "default" will be used.

Each application's value must either be an array of integers or a group of
arrays of integers whose values range from -1 to 255 (hex or decimal is
accepted). The values declared in the array will override the returned MAC
address. The array does not have to cover all six of the octets in a mac
address. You can also explicitly ignore an octet by using the value -1. If you
use a group, the group's elements should be named by the interface names you
want to override. An interface name of `default_interface` will be used when an
interface name is not found in the group.

By default, the application will look for a config file in `~/.macspoofrc` and
`/etc/macspoof.conf`. You can override this behavior with the `-c` option.

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
$ macspoof -c macspoof.conf -a ip ifconfig eth0
eth0      Link encap:Ethernet  HWaddr c0:ff:ee:96:d2:7f
    ...

eth0:0    Link encap:Ethernet  HWaddr f2:3c:91:96:d2:7f
    ...
```

## Why is my platform or architecture not supported?
Because I didn't feel like it.

If you ask, I'll probably implement it. Seriously. Ask. I'm a pretty friendly
guy :). Specifically though, the BSD family including OS X uses a completely
different way of getting a MAC address, and I'd have to write all new ASM to
support 32-bit Linux.

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
