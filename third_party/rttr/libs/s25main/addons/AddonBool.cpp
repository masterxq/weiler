// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "AddonBool.h"

AddonBool::AddonBool(const AddonId id, AddonGroup groups, const std::string& name, const std::string& description)
    : Addon(id, groups, name, description, 0)
{}

unsigned AddonBool::getNumOptions() const
{
    return 2;
}
