# Alarm management: `ietf-alarms` YANG module for sysrepo

![License](https://img.shields.io/github/license/CESNET/sysrepo-ietf-alarms)
[![Gerrit](https://img.shields.io/badge/patches-via%20Gerrit-blue)](https://gerrit.cesnet.cz/q/project:CzechLight/sysrepo-ietf-alarms)
[![Zuul CI](https://img.shields.io/badge/zuul-checked-blue)](https://zuul.gerrit.cesnet.cz/t/public/buildsets?project=CzechLight/sysrepo-ietf-alarms)

With this daemon, [sysrepo](https://www.sysrepo.org/) applications can implement the `ietf-alarms` YANG module from [RFC 8632](https://datatracker.ietf.org/doc/html/rfc8632).
As an app developer, you simply:

- create the required [alarm identities](https://datatracker.ietf.org/doc/html/rfc8632#section-3.2) based on `al:alarm-type`
- provide the [list of possible alarms](https://datatracker.ietf.org/doc/html/rfc8632#section-4.2)
- execute an internal RPC each time an alarm event occurs

This daemon takes care of the rest:

- alarm [shelving](https://datatracker.ietf.org/doc/html/rfc8632#section-4.1.1)
- in future, alarm [history](https://datatracker.ietf.org/doc/html/rfc8632#section-3.5.1)

## Dependencies

- [libyang-cpp](https://github.com/CESNET/libyang-cpp) - C++ bindings for *libyang*
- [sysrepo-cpp](https://github.com/sysrepo/sysrepo-cpp) - C++ bindings for *sysrepo*
- C++20 compiler (e.g., GCC 10.x+, clang 10+)
- CMake 3.19+
- [`pkg-config`](https://www.freedesktop.org/wiki/Software/pkg-config/)
- [`spdlog`](https://github.com/gabime/spdlog)
- [`date`](https://github.com/HowardHinnant/date) for time zones handling
- [`doctest`](https://github.com/doctest/doctest) for unit testing
- [`trompeloeil`](https://github.com/rollbear/trompeloeil) for unit testing

## Contributing
The development is being done on Gerrit [here](https://gerrit.cesnet.cz/q/project:CzechLight/sysrepo-ietf-alarms).
Instructions on how to submit patches can be found [here](https://gerrit.cesnet.cz/Documentation/intro-gerrit-walkthrough-github.html).
GitHub Pull Requests are not used.
