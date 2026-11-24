// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Addon.h"
#include <vector>


/**
 *  Addon baseclass for option-list addons
 */
class AddonList : public Addon
{
public:
    AddonList(AddonId id, AddonGroup groups, const std::string& name, const std::string& description,
              std::vector<std::string> options, unsigned defaultStatus = 0);

    unsigned getNumOptions() const override;
    const std::string& getOptionName(unsigned status) const;


private:
    std::vector<std::string> options;
};
