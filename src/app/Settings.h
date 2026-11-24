// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <boost/filesystem/path.hpp>
#include <map>
#include <string>
#include <vector>

namespace weiler {

/// Simple persistent key/value settings (settings.ini in the user folder)
class Settings
{
public:
    explicit Settings(boost::filesystem::path file);
    void save() const;

    std::string get(const std::string& key, const std::string& def = {}) const;
    int getInt(const std::string& key, int def = 0) const;
    float getFloat(const std::string& key, float def = 0) const;
    bool getBool(const std::string& key, bool def = false) const { return getInt(key, def ? 1 : 0) != 0; }
    void set(const std::string& key, const std::string& value);
    void setInt(const std::string& key, int value) { set(key, std::to_string(value)); }
    void setFloat(const std::string& key, float value) { set(key, std::to_string(value)); }
    void setBool(const std::string& key, bool value) { setInt(key, value ? 1 : 0); }
    /// Integer list stored comma separated
    std::vector<int> getList(const std::string& key) const;
    void setList(const std::string& key, const std::vector<int>& values);

private:
    boost::filesystem::path file_;
    std::map<std::string, std::string> values_;
};

} // namespace weiler
