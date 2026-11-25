// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gameTypes/BuildingType.h"
#include "gameTypes/GoodTypes.h"
#include "gameTypes/JobTypes.h"
#include <string>

namespace weiler {

// German names shown in the user interface
const char* buildingName(BuildingType bt);
const char* wareName(GoodType gt);
const char* jobName(Job job);

} // namespace weiler
