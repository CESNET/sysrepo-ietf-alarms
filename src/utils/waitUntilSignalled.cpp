/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
 */

#include <csignal>
#include <unistd.h>
#include "waitUntilSignalled.h"

namespace alarms::utils {
void waitUntilSignaled()
{
    signal(SIGTERM, [](int) {});
    signal(SIGINT, [](int) {});
    pause();
}

}
