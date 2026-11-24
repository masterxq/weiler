// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Addon.h"


/**
 *  Addon baseclass for boolean addons
 */
class AddonBool : public Addon
{
public:
    AddonBool(AddonId id, AddonGroup groups, const std::string& name, const std::string& description);

    unsigned getNumOptions() const override;

};
