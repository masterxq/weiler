// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Settings.h"
#include <boost/filesystem.hpp>
#include <cmath>
#include <fstream>
#include <sstream>

namespace weiler {

Settings::Settings(boost::filesystem::path file) : file_(std::move(file))
{
    std::ifstream in(file_.string());
    std::string line;
    while(std::getline(in, line))
    {
        const auto eq = line.find('=');
        if(eq == std::string::npos || line.empty() || line[0] == '#')
            continue;
        values_[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

void Settings::save() const
{
    // A read-only or full disk must not end the game: the settings are simply not kept
    boost::system::error_code ec;
    boost::filesystem::create_directories(file_.parent_path(), ec);
    const std::string tmp = file_.string() + ".tmp";
    {
        std::ofstream out(tmp);
        for(const auto& [k, v] : values_)
            out << k << '=' << v << '\n';
        if(!out.flush())
            return;
    }
    boost::filesystem::rename(tmp, file_, ec);
}

std::string Settings::get(const std::string& key, const std::string& def) const
{
    auto it = values_.find(key);
    return it == values_.end() ? def : it->second;
}

int Settings::getInt(const std::string& key, int def) const
{
    auto it = values_.find(key);
    if(it == values_.end())
        return def;
    try
    {
        return std::stoi(it->second);
    } catch(...)
    {
        return def;
    }
}

float Settings::getFloat(const std::string& key, float def) const
{
    auto it = values_.find(key);
    if(it == values_.end())
        return def;
    try
    {
        const float v = std::stof(it->second);
        return std::isfinite(v) ? v : def;
    } catch(...)
    {
        return def;
    }
}

void Settings::set(const std::string& key, const std::string& value)
{
    values_[key] = value;
}

std::vector<int> Settings::getList(const std::string& key) const
{
    std::vector<int> out;
    std::stringstream ss(get(key));
    std::string item;
    while(std::getline(ss, item, ','))
    {
        try
        {
            out.push_back(std::stoi(item));
        } catch(...)
        {}
    }
    return out;
}

void Settings::setList(const std::string& key, const std::vector<int>& values)
{
    std::string s;
    for(size_t i = 0; i < values.size(); ++i)
        s += (i ? "," : "") + std::to_string(values[i]);
    set(key, s);
}

} // namespace weiler
