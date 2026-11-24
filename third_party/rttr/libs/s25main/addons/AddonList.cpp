// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "AddonList.h"
#include "commonDefines.h"
#include <stdexcept>

AddonList::AddonList(const AddonId id, AddonGroup groups, const std::string& name, const std::string& description,
                     std::vector<std::string> options, unsigned defaultStatus /*=0*/)
    : Addon(id, groups, name, description, defaultStatus), options(std::move(options))
{
    if(defaultStatus >= this->options.size())
        throw std::logic_error("Invalid default option");
}


unsigned AddonList::getNumOptions() const
{
    return options.size();
}

const std::string& AddonList::getOptionName(unsigned status) const
{
    return options.at(status);
}

