# Alarm management: `ietf-alarms` YANG module for sysrepo

![License](https://img.shields.io/github/license/CESNET/sysrepo-ietf-alarms)
[![Gerrit](https://img.shields.io/badge/patches-via%20Gerrit-blue)](https://gerrit.cesnet.cz/q/project:CzechLight/sysrepo-ietf-alarms)
[![Zuul CI](https://img.shields.io/badge/zuul-checked-blue)](https://zuul.gerrit.cesnet.cz/t/public/buildsets?project=CzechLight/sysrepo-ietf-alarms)

With this daemon, [sysrepo](https://www.sysrepo.org/) applications can implement the `ietf-alarms` YANG module from [RFC 8632](https://datatracker.ietf.org/doc/html/rfc8632).
As an app developer, you simply:

- create the required [alarm identities](https://datatracker.ietf.org/doc/html/rfc8632#section-3.2) based on `al:alarm-type`
- provide the [list of possible alarms](https://datatracker.ietf.org/doc/html/rfc8632#section-4.2)
- execute an [internal RPC](yang/sysrepo-ietf-alarms%402022-02-17.yang) each time an alarm event occurs

This daemon takes care of the rest:

- alarm [shelving](https://datatracker.ietf.org/doc/html/rfc8632#section-4.1.1)
- alarm [summaries](https://datatracker.ietf.org/doc/html/rfc8632#section-4.3) and statistics
- alarm [notifications](https://datatracker.ietf.org/doc/html/rfc8632#section-4.8)
- in future, alarm [history](https://datatracker.ietf.org/doc/html/rfc8632#section-3.5.1)

## Dependencies

- [libyang-cpp](https://github.com/CESNET/libyang-cpp) - C++ bindings for *libyang*
- [sysrepo-cpp](https://github.com/sysrepo/sysrepo-cpp) - C++ bindings for *sysrepo*
- C++20 compiler (e.g., GCC 10.x+, clang 10+)
- CMake 3.19+
- [Boost](https://www.boost.org/) 1.78+ (header-only is sufficient)
- [`pkg-config`](https://www.freedesktop.org/wiki/Software/pkg-config/)
- [`spdlog`](https://github.com/gabime/spdlog)
- [`fmt`](https://fmt.dev/) - C++ string formatting library
- [`date`](https://github.com/HowardHinnant/date) for time zones handling
- [`docopt`](https://github.com/docopt/docopt.cpp) for command line options
- [`doctest`](https://github.com/doctest/doctest) for unit testing
- [`trompeloeil`](https://github.com/rollbear/trompeloeil) for unit testing

## Contributing
The development is being done on Gerrit [here](https://gerrit.cesnet.cz/q/project:CzechLight/sysrepo-ietf-alarms).
Instructions on how to submit patches can be found [here](https://gerrit.cesnet.cz/Documentation/intro-gerrit-walkthrough-github.html).
GitHub Pull Requests are not used.
