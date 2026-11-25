// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "notifications/Subscription.h"
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

struct FigureNote;

namespace weiler {

class Session;

/// Diagnosis of figures leaving for foreign land: every figure (no soldier) that steps into the territory of
/// another player is described in one line (game time, job, state, origin, goal, reason). Each figure is reported
/// once, at most maxLines lines per game (0: no limit).
class ForeignEntryLog
{
public:
    using Output = std::function<void(const std::string&)>;
    /// onlyPlayer: report only the figures of this player
    ForeignEntryLog(const Session& session, Output out, std::optional<unsigned> onlyPlayer = std::nullopt,
                    unsigned maxLines = 300);
    /// Number of reported entries
    unsigned count() const { return count_; }

    static std::string describe(const Session& session, const FigureNote& note);

private:
    Subscription sub_;
    unsigned count_ = 0;
    std::unordered_set<unsigned> reported_;
};

} // namespace weiler
