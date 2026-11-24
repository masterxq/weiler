// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Names.h"
#include <array>

namespace weiler {

const char* buildingName(BuildingType bt)
{
    static constexpr std::array<const char*, 40> names = {
      "Hauptquartier", "Baracke",        "Wachstube",      "Abdeckerei",     "Wachturm",     "Weinberg",
      "Winzerei",      "Tempel",         "Gerberei",       "Festung",        "Granitmine",   "Kohlemine",
      "Eisenmine",     "Goldmine",       "Spähturm",       "Lederwerkstatt", "Katapult",     "Holzfäller",
      "Fischerei",     "Steinbruch",     "Förster",        "Schlachterei",   "Jäger",        "Brauerei",
      "Schmiede",      "Schlosserei",    "Eisenschmelze",  "Köhlerei",       "Schweinezucht", "Lagerhaus",
      "",              "Mühle",          "Bäckerei",       "Sägewerk",       "Münzprägerei", "Brunnen",
      "Werft",         "Bauernhof",      "Eselzucht",      "Hafengebäude"};
    const auto i = static_cast<size_t>(bt);
    return i < names.size() ? names[i] : "";
}

const char* wareName(GoodType gt)
{
    static constexpr std::array<const char*, 41> names = {
      "Bier",       "Zange",     "Hammer",   "Axt",      "Säge",     "Spitzhacke", "Schaufel", "Schmelztiegel",
      "Angel",      "Sense",     "Wasser",   "Wasser",   "Beil",     "Nudelholz",  "Bogen",    "Boot",
      "Schwert",    "Eisen",     "Mehl",     "Fisch",    "Brot",     "Schild",     "Holz",     "Bretter",
      "Steine",     "Schild",    "Schild",   "Getreide", "Münzen",   "Gold",       "Eisenerz", "Kohle",
      "Fleisch",    "Schinken",  "Schild",   "Trauben",  "Wein",     "Felle",      "Leder",    "Rüstung",
      ""};
    const auto i = static_cast<size_t>(gt);
    return i < names.size() ? names[i] : "";
}

const char* jobName(Job job)
{
    static constexpr std::array<const char*, 38> names = {
      "Träger",      "Holzfäller",  "Fischer",      "Förster",     "Schreiner",   "Steinmetz",   "Jäger",
      "Bauer",       "Müller",      "Bäcker",       "Metzger",     "Bergmann",    "Brauer",      "Schweinezüchter",
      "Eselzüchter", "Schmelzer",   "Münzpräger",   "Schlosser",   "Schmied",     "Bauarbeiter", "Planierer",
      "Gefreiter",   "Obergefreiter", "Feldwebel",  "Offizier",    "General",     "Geologe",     "Schiffbauer",
      "Späher",      "Packesel",    "Bootsträger",  "Köhler",      "Winzer",      "Weinbauer",   "Tempeldiener",
      "Abdecker",    "Gerber",      "Lederarbeiter"};
    const auto i = static_cast<size_t>(job);
    return i < names.size() ? names[i] : "";
}

} // namespace weiler
