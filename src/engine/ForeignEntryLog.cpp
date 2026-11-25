// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ForeignEntryLog.h"
#include "Names.h"
#include "Session.h"
#include "buildings/noBaseBuilding.h"
#include "buildings/nobUsual.h"
#include "figures/noFigure.h"
#include "figures/nofBuildingWorker.h"
#include "figures/nofFlagWorker.h"
#include "nodeObjs/noFlag.h"
#include "notifications/FigureNote.h"
#include "world/GameWorld.h"
#include <limits>

namespace weiler {

namespace {
std::string point(MapPoint pt)
{
    return "(" + std::to_string(pt.x) + "," + std::to_string(pt.y) + ")";
}

std::string node(const noRoadNode& n)
{
    std::string name = "Flagge";
    if(const auto* bld = dynamic_cast<const noBaseBuilding*>(&n))
        name = buildingName(bld->getBuildingType());
    return name + " " + point(n.getPos()) + " von P" + std::to_string(n.getPlayer());
}

const char* stateName(FigureState fs)
{
    switch(fs)
    {
        case FigureState::GotToGoal: return "unterwegs zum Ziel";
        case FigureState::GoHome: return "auf dem Heimweg";
        case FigureState::Wander: return "irrt umher";
        case FigureState::Job: return "bei der Arbeit";
    }
    return "?";
}
} // namespace

std::string ForeignEntryLog::describe(const Session& session, const FigureNote& note)
{
    const noFigure& fig = note.figure;
    const auto* worker = dynamic_cast<const nofBuildingWorker*>(&fig);
    const auto* flagWorker = dynamic_cast<const nofFlagWorker*>(&fig);
    std::string origin = "-";
    if(worker && worker->getWorkplace())
        origin = "Arbeitsplatz " + node(*worker->getWorkplace());
    else if(flagWorker && flagWorker->getFlag())
        origin = "Flagge " + point(flagWorker->getFlag()->getPos());
    std::string reason;
    switch(fig.getState())
    {
        case FigureState::Job: reason = "Arbeit außerhalb des eigenen Gebiets"; break;
        case FigureState::GotToGoal: reason = "Weg zum Ziel"; break;
        case FigureState::GoHome: reason = "Weg zum Lager"; break;
        case FigureState::Wander:
            if(fig.isFleeingFromBurnedWarehouse())
                reason = "Lager abgebrannt";
            else if(!fig.getWanderStartPos().isValid())
                reason = "Ursache unbekannt (irrte schon vor dem Laden)";
            else
            {
                switch(fig.getStateBeforeWander())
                {
                    case FigureState::GotToGoal: reason = "Ziel oder Straße weggefallen"; break;
                    case FigureState::GoHome: reason = "kein Lager erreichbar"; break;
                    case FigureState::Job: reason = "Arbeit abgebrochen (Arbeitsplatz oder Flagge weg)"; break;
                    case FigureState::Wander: reason = "irrt umher"; break;
                }
            }
            if(fig.getWanderStartPos().isValid())
            {
                if(origin == "-")
                    origin.clear();
                else
                    origin += ", ";
                origin += "irrt seit " + session.formatGFTime(fig.getWanderStartGF()) + " ab "
                          + point(fig.getWanderStartPos()) + " (vorher "
                          + stateName(fig.getStateBeforeWander()) + ")";
            }
            break;
    }
    const std::string goal = fig.getGoal() ? node(*fig.getGoal()) : "-";
    return "[fremd] " + session.formatGFTime(session.getGF()) + " P" + std::to_string(note.player) + " "
           + jobName(fig.getJobType()) + " betritt Gebiet von P" + std::to_string(note.territoryOwner) + " bei "
           + point(note.pos) + ": " + stateName(fig.getState()) + (fig.getCurrentRoad() ? " auf Straße" : "")
           + ", Herkunft " + origin + ", Ziel " + goal + ", Grund: " + reason;
}

ForeignEntryLog::ForeignEntryLog(const Session& session, Output out, std::optional<unsigned> onlyPlayer,
                                 unsigned maxLines)
{
    if(!maxLines)
        maxLines = std::numeric_limits<unsigned>::max() - 1;
    sub_ = session.world().getNotifications().subscribe<FigureNote>(
      [this, &session, out = std::move(out), onlyPlayer, maxLines](const FigureNote& note) {
          if((onlyPlayer && note.player != *onlyPlayer) || count_ > maxLines
             || !reported_.insert(note.figure.getObjId()).second)
              return;
          if(++count_ > maxLines)
              out("[fremd] weitere Figuren in fremdem Gebiet werden nicht mehr gemeldet");
          else
              out(describe(session, note));
      });
}

} // namespace weiler
