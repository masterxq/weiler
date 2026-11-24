// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <utility>

enum class SoundType
{
    Music,
    Effect
};

/// Reference counted handle to a sound loaded by the audio backend
class SoundHandle
{
public:
    SoundHandle() = default;
    SoundHandle(std::shared_ptr<void> data, SoundType type) : data_(std::move(data)), type_(type) {}
    explicit operator bool() const { return static_cast<bool>(data_); }
    const std::shared_ptr<void>& getData() const { return data_; }
    SoundType getType() const { return type_; }

private:
    std::shared_ptr<void> data_;
    SoundType type_ = SoundType::Effect;
};
