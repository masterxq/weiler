// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

// Game data missing or not reachable: the player chooses a folder (folder dialog of the desktop portal, or an own
// touch folder browser if there is none), Weiler searches it with all sub folders, remembers the folder it found
// and starts. A remembered folder on a removable medium that is not inserted is reported with its path.

#include "App.h"
#include "Menus.h"
#include "MenuBackdrop.h"
#include "Music.h"
#include "Screen.h"
#include "ui/Ui.h"
#include <SDL3/SDL_dialog.h>
#include <boost/filesystem.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>

namespace bfs = boost::filesystem;

namespace weiler {

using namespace ui;

namespace {

/// Result of the folder dialog; shared with the dialog callback, which may run on another thread
struct DialogResult
{
    std::mutex mutex;
    bool done = false;
    bool failed = false;
    bfs::path folder; // empty: canceled
};

void SDLCALL onFolderChosen(void* userdata, const char* const* filelist, int)
{
    auto* holder = static_cast<std::shared_ptr<DialogResult>*>(userdata);
    {
        auto& r = **holder;
        std::lock_guard<std::mutex> lock(r.mutex);
        r.done = true;
        if(!filelist)
        {
            r.failed = true;
            std::cerr << "Folder dialog not available: " << SDL_GetError() << std::endl;
        } else if(filelist[0])
            r.folder = filelist[0];
    }
    delete holder;
}

std::string home()
{
    const char* h = std::getenv("HOME");
    return (h && *h) ? h : "/";
}

class DataSetupScreen : public Screen
{
public:
    explicit DataSetupScreen(App& app) : Screen(app), ui_(app.ui())
    {
        const auto& p = app.paths();
        mode_ = p.remembered == DataPaths::Remembered::Unreachable ? Mode::Unreachable : Mode::Missing;
        if(p.source == DataPaths::Source::Argument || p.source == DataPaths::Source::Environment)
            note_ = "Im angegebenen Ordner " + p.gameData.string() + " sind keine Spieldaten.";
        app.music().play(Music::Context::Menu);
    }
    ~DataSetupScreen() override
    {
        cancel_ = true;
        if(search_.valid())
            search_.wait();
        if(copy_.valid())
            copy_.wait();
    }
    const char* name() const override { return "datasetup"; }
    void onPointer(const PointerEvent& ev) override { ui_.pointer(ev); }
    void onWheel(float x, float y, float d) override { ui_.wheel(x, y, d); }

    void update(double dt) override
    {
        app_.menuBackdrop().update(dt);
        pollDialog();
        if(search_.valid() && search_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            const bfs::path found = search_.get();
            if(found.empty())
            {
                if(!cancel_)
                    setNote("In " + searchRoot_.string() + " und den Unterordnern sind keine Spieldaten der Gold "
                            "Edition.", true);
            } else
                useFound(found);
        }
        if(copy_.valid() && copy_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            const std::string error = copy_.get();
            if(error.empty())
                finish(DataPaths::userGameData());
            else
                setNote("Die Spieldaten ließen sich nicht übernehmen: " + error, true);
        }
    }

    void draw() override
    {
        app_.menuBackdrop().draw();
        if(mode_ == Mode::Browse)
            drawBrowser();
        else
            drawMain();
    }

    bool onBack() override
    {
        if(busy())
        {
            cancel_ = true;
            return true;
        }
        if(mode_ == Mode::Browse)
        {
            if(browseDir_.has_parent_path() && browseDir_ != browseDir_.root_path())
                openDir(browseDir_.parent_path());
            else
                mode_ = prevMode_;
            return true;
        }
        app_.quit();
        return true;
    }

    bool testCommand(const std::string& cmd, const std::vector<std::string>& args) override
    {
        // dialog <folder>|cancel|fail: result of the folder dialog (the test mode opens no real dialog)
        if(cmd == "dialog" && !args.empty() && dialog_)
        {
            std::lock_guard<std::mutex> lock(dialog_->mutex);
            dialog_->done = true;
            if(args[0] == "fail")
                dialog_->failed = true;
            else if(args[0] != "cancel")
                dialog_->folder = bfs::absolute(args[0]);
            return true;
        }
        // expect missing|unreachable|browse|busy|idle
        if(cmd == "expect" && !args.empty())
        {
            const std::string& s = args[0];
            return (s == "missing" && mode_ == Mode::Missing) || (s == "unreachable" && mode_ == Mode::Unreachable)
                   || (s == "browse" && mode_ == Mode::Browse) || (s == "busy" && busy())
                   || (s == "idle" && !busy());
        }
        return false;
    }

private:
    enum class Mode
    {
        Missing,
        Unreachable,
        Browse
    };

    bool busy() const { return dialog_ || search_.valid() || copy_.valid(); }

    void setNote(std::string text, bool error)
    {
        note_ = std::move(text);
        noteError_ = error;
    }

    void chooseFolder()
    {
        setNote({}, false);
        dialog_ = std::make_shared<DialogResult>();
        if(app_.testMode())
            return; // the script delivers the result ("game dialog ...")
        const std::string start = home();
        SDL_ShowOpenFolderDialog(onFolderChosen, new std::shared_ptr<DialogResult>(dialog_), nullptr, start.c_str(),
                                 false);
    }

    void pollDialog()
    {
        if(!dialog_)
            return;
        bool failed;
        bfs::path folder;
        {
            std::lock_guard<std::mutex> lock(dialog_->mutex);
            if(!dialog_->done)
                return;
            failed = dialog_->failed;
            folder = dialog_->folder;
        }
        dialog_.reset();
        if(failed)
        {
            // No desktop portal (and no zenity): own folder browser
            prevMode_ = mode_ == Mode::Browse ? prevMode_ : mode_;
            mode_ = Mode::Browse;
            openDir(home());
        } else if(!folder.empty())
            startSearch(folder);
    }

    void startSearch(const bfs::path& root)
    {
        searchRoot_ = root;
        cancel_ = false;
        setNote({}, false);
        if(mode_ == Mode::Browse)
            mode_ = prevMode_;
        search_ = std::async(std::launch::async, [root, this] { return DataPaths::findGameData(root, &cancel_); });
    }

    void useFound(const bfs::path& found)
    {
        if(DataPaths::isGameData(found))
        {
            finish(found);
            return;
        }
        // File names in another case (e.g. copied by a tool that lowers them): Weiler needs the original upper case
        // names, so the needed files are copied once into the user folder
        const bfs::path dst = DataPaths::userGameData();
        const DataPaths paths = app_.paths();
        copy_ = std::async(std::launch::async, [paths, found, dst] {
            std::string error;
            paths.copyGameData(found, dst, error);
            return error;
        });
    }

    void finish(const bfs::path& dir)
    {
        std::cout << "Game data: " << dir.string() << std::endl;
        app_.setGameData(dir, true);
        app_.setScreen(createStartScreen(app_));
    }

    void retry()
    {
        const DataPaths& p = app_.paths();
        if(DataPaths::isGameData(p.rememberedData))
            finish(p.rememberedData);
        else
            setNote("Der Ordner ist immer noch nicht erreichbar.", true);
    }

    /// Content column and whether the word mark goes to the left (landscape)
    Rect2 column(float contentH, bool& side) const
    {
        const float W = static_cast<float>(app_.width()), H = static_cast<float>(app_.height());
        side = !app_.portrait();
        const float w = std::min(side ? W * 0.55f : W - app_.dp(32), app_.dp(480));
        const float x = side ? W * 0.6f - w / 2 + app_.dp(40) : (W - w) / 2;
        const float top = side ? std::max(app_.dp(16), (H - contentH) / 2) :
                                 std::max(app_.dp(230), H * 0.42f - contentH / 2);
        return {x, top, w, contentH};
    }

    /// Words wider than w (long paths) are broken after a '/' so that the text wraps
    std::string breakLong(const std::string& text, float size, float w)
    {
        std::string out, word;
        auto flush = [&] {
            size_t start = 0;
            while(start < word.size())
            {
                if(ui_.textWidth(word.substr(start), size) <= w)
                {
                    out += word.substr(start);
                    break;
                }
                // Longest piece ending with '/' that fits (at least one path part)
                size_t cut = std::string::npos;
                for(size_t p = word.find('/', start + 1); p != std::string::npos; p = word.find('/', p + 1))
                {
                    if(ui_.textWidth(word.substr(start, p + 1 - start), size) > w)
                    {
                        if(cut == std::string::npos)
                            cut = p;
                        break;
                    }
                    cut = p;
                }
                if(cut == std::string::npos)
                {
                    out += word.substr(start);
                    break;
                }
                out += word.substr(start, cut + 1 - start) + '\n';
                start = cut + 1;
            }
            word.clear();
        };
        for(char c : text)
        {
            if(c == ' ' || c == '\n')
            {
                flush();
                out += c;
            } else
                word += c;
        }
        flush();
        return out;
    }

    float wrappedHeight(const std::string& text, float size, float w)
    {
        return static_cast<float>(app_.font().wrap(text, size, w).size()) * size * 1.3f;
    }

    void drawMain()
    {
        const auto& th = ui_.theme;
        const float W = static_cast<float>(app_.width()), H = static_cast<float>(app_.height());
        const bool unreachable = mode_ == Mode::Unreachable;
        const std::string title = unreachable ? "Spieldaten nicht erreichbar" : "Spieldaten nicht gefunden";
        const std::string body =
          unreachable ? "Der gemerkte Ordner mit den Spieldaten ist gerade nicht erreichbar. Steckt die Speicherkarte "
                        "oder der USB-Stick? Nach dem Einlegen „Erneut versuchen“ tippen." :
                        "Weiler spielt mit den Daten der Gold Edition, die nicht dabei sind. Wähle den Ordner deiner "
                        "Installation oder CD, der DATA und GFX enthält, oder einen Ordner darüber: Weiler durchsucht "
                        "auch alle Unterordner und merkt sich den Fund.";
        const std::string hint = "Als Paket für alle Nutzer: im Terminal weiler-pack-data aufrufen.";
        const float pad = app_.dp(22);
        const float bh = app_.dp(52), gap = app_.dp(10);
        bool side;
        Rect2 col = column(0, side);
        const float tw = col.w - 2 * pad;
        const float fsTitle = app_.dp(th.type.title + 2), fsBody = app_.dp(th.type.body), fsSmall = app_.dp(th.type.small);
        const std::string path = breakLong(app_.paths().rememberedData.string(), fsBody, tw - app_.dp(36));
        const std::string note = breakLong(note_, fsSmall, tw);
        float h = pad + fsTitle * 1.4f + app_.dp(10) + wrappedHeight(body, fsBody, tw);
        if(unreachable)
            h += app_.dp(12) + wrappedHeight(path, fsBody, tw - app_.dp(36)) + app_.dp(4);
        else
            h += app_.dp(10) + wrappedHeight(hint, fsSmall, tw);
        if(!note.empty())
            h += app_.dp(12) + wrappedHeight(note, fsSmall, tw);
        // Landscape: the two main buttons side by side
        const bool row = side;
        const int buttons = unreachable ? (row ? 2 : 3) : (row ? 1 : 2);
        h += app_.dp(20) + static_cast<float>(buttons) * (bh + gap) + pad - gap;
        col = column(h, side);
        if(side)
            drawWordmark(app_, W * 0.2f, H / 2 - app_.dp(80), 0.85f, 1.f);
        else
            drawWordmark(app_, W / 2, std::max(app_.dp(30), col.y - app_.dp(200)), 0.95f, 1.f);

        // Too high for the screen (landscape, long path): the panel scrolls
        const float maxH = H - col.y - app_.dp(16);
        const bool scroll = h > maxH;
        if(scroll)
            col.h = maxH;
        ui_.panel(col, app_.dp(20));
        float x = col.x + pad, y = scroll ? ui_.beginScroll(id("data.scroll"), col) + pad : col.y + pad;
        const float y0 = y - pad;
        ui_.heading(x, y, title, std::min(fsTitle, fsTitle * tw / std::max(1.f, ui_.headingWidth(title, fsTitle))),
                    th.text);
        y += fsTitle * 1.4f + app_.dp(10);
        y += ui_.textWrapped(x, y, tw, body, fsBody, th.textDim);
        if(unreachable)
        {
            // The remembered folder, with a folder icon
            y += app_.dp(12);
            ui_.icon(Icon::Folder, x + app_.dp(12), y + fsBody * 0.65f, app_.dp(22), th.accent);
            y += ui_.textWrapped(x + app_.dp(36), y, tw - app_.dp(36), path, fsBody, th.text) + app_.dp(4);
        } else
        {
            y += app_.dp(10);
            y += ui_.textWrapped(x, y, tw, hint, fsSmall, th.textFaint);
        }
        if(!note.empty())
        {
            y += app_.dp(12);
            y += ui_.textWrapped(x, y, tw, note, fsSmall, noteError_ ? th.danger : th.ok);
        }
        y += app_.dp(20);
        if(busy())
            drawBusy(x, y, tw, bh);
        else
        {
            if(unreachable)
            {
                const float bw = row ? (tw - gap) / 2 : tw;
                if(ui_.button(id("data.retry"), {x, y, bw, bh}, "Erneut versuchen", Icon::Next, Style::Primary))
                    retry();
                const Rect2 other = row ? Rect2{x + bw + gap, y, bw, bh} : Rect2{x, y + bh + gap, tw, bh};
                if(ui_.button(id("data.other"), other, "Anderen Ordner wählen", Icon::Folder))
                    chooseFolder();
                y = other.y;
            } else
            {
                const float bw = row ? (tw - gap) / 2 : tw;
                if(ui_.button(id("data.choose"), {x, y, bw, bh}, "Ordner wählen", Icon::Folder, Style::Primary))
                    chooseFolder();
                if(row)
                {
                    if(ui_.button(id("data.quit"), {x + bw + gap, y, bw, bh}, "Beenden", Icon::Exit, Style::Flat))
                        app_.quit();
                    y -= bh + gap;
                }
            }
            y += bh + gap;
            if(!(row && !unreachable)
               && ui_.button(id("data.quit"), {x + tw / 4, y, tw / 2, bh}, "Beenden", Icon::Exit, Style::Flat))
                app_.quit();
            y += row && !unreachable ? 0 : bh;
        }
        if(scroll)
            ui_.endScroll(y + pad - y0);
    }

    /// Search or copy in progress (or the dialog open): moving bar and a cancel button
    void drawBusy(float x, float y, float w, float bh)
    {
        const auto& th = ui_.theme;
        const std::string what = copy_.valid() ? "Spieldaten werden übernommen …" :
                                 search_.valid() ? "Durchsuche " + searchRoot_.string() + " …" :
                                                   "Ordner wählen …";
        y += ui_.textWrapped(x, y, w, breakLong(what, app_.dp(th.type.small), w), app_.dp(th.type.small), th.textDim)
             + app_.dp(10);
        const Rect2 bar{x, y, w, app_.dp(6)};
        app_.batch().roundRect(bar, bar.h / 2, th.text.withAlpha(0.1f));
        const float t = static_cast<float>(std::fmod(ui_.time() * 0.8, 1.0));
        const float seg = w * 0.3f;
        const float sx = x - seg + (w + seg) * t;
        const float x0 = std::max(x, sx), x1 = std::min(x + w, sx + seg);
        if(x1 > x0)
            app_.batch().roundRectV({x0, bar.y, x1 - x0, bar.h}, bar.h / 2, th.accent, th.accentDeep);
        y += app_.dp(22);
        if(!copy_.valid() && ui_.button(id("data.cancel"), {x + w / 4, y, w / 2, bh}, "Abbrechen", Icon::Close,
                                        Style::Flat))
        {
            cancel_ = true;
            dialog_.reset();
        }
    }

    // ---- Own folder browser (when there is no folder dialog)
    void openDir(const bfs::path& dir)
    {
        browseDir_ = dir;
        subdirs_.clear();
        boost::system::error_code ec;
        for(bfs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        {
            const std::string name = it->path().filename().string();
            boost::system::error_code ec2;
            if(!name.empty() && name[0] != '.' && bfs::is_directory(it->status(ec2)))
                subdirs_.push_back(name);
        }
        std::sort(subdirs_.begin(), subdirs_.end(), [](const std::string& a, const std::string& b) {
            return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char x, unsigned char y) {
                return std::tolower(x) < std::tolower(y);
            });
        });
        scrollReset_++;
    }

    std::vector<std::pair<std::string, bfs::path>> places() const
    {
        std::vector<std::pair<std::string, bfs::path>> p;
        p.emplace_back("Eigener Ordner", home());
        boost::system::error_code ec;
        const char* user = std::getenv("USER");
        for(const bfs::path& media : {bfs::path("/run/media") / (user ? user : ""), bfs::path("/media") / (user ? user : ""),
                                      bfs::path("/media"), bfs::path("/mnt")})
        {
            if(bfs::is_directory(media, ec) && !bfs::is_empty(media, ec))
            {
                p.emplace_back("Wechselmedien", media);
                break;
            }
        }
        p.emplace_back("Gerät", "/");
        return p;
    }

    void drawBrowser()
    {
        const auto& th = ui_.theme;
        const float W = static_cast<float>(app_.width()), H = static_cast<float>(app_.height());
        const float s = app_.dp(48);
        if(ui_.iconButton(id("browse.back"), {app_.dp(14), app_.dp(14), s, s}, Icon::Back))
        {
            onBack();
            return;
        }
        const float fs = app_.dp(th.type.title + 2);
        ui_.heading(W / 2, app_.dp(14) + (s - fs * 1.3f) / 2, "Ordner wählen", fs, th.text, gfx::Align::Center);
        const float w = std::min(W - app_.dp(32), app_.dp(560));
        const float x = (W - w) / 2;
        float y = app_.dp(76);
        // Places
        const auto pl = places();
        const float gap = app_.dp(8);
        const float pw = (w - gap * static_cast<float>(pl.size() - 1)) / static_cast<float>(pl.size());
        for(size_t i = 0; i < pl.size(); ++i)
        {
            if(ui_.button(id("browse.place", i), {x + static_cast<float>(i) * (pw + gap), y, pw, app_.dp(44)},
                          pl[i].first, Icon::None, Style::Normal))
                openDir(pl[i].second);
        }
        y += app_.dp(56);
        // Current folder
        y += ui_.textWrapped(x + app_.dp(4), y, w - app_.dp(8),
                             breakLong(browseDir_.string(), app_.dp(th.type.small), w - app_.dp(8)),
                             app_.dp(th.type.small), th.textDim)
             + app_.dp(8);
        const float bh = app_.dp(52);
        const float bottom = H - app_.dp(16) - bh;
        const Rect2 panel{x, y, w, bottom - app_.dp(12) - y};
        ui_.panel(panel, app_.dp(20));
        const Rect2 view = panel.inset(app_.dp(8));
        float ly = ui_.beginScroll(id("browse.list", scrollReset_), view);
        const float ly0 = ly;
        const float rh = app_.dp(52);
        auto entry = [&](size_t i, const std::string& label, Icon ic) {
            const bool hit = ui_.button(id("browse.dir", i), {view.x, ly, view.w - app_.dp(6), rh}, label, ic, Style::Nav);
            ly += rh + app_.dp(4);
            return hit;
        };
        bfs::path next;
        if(browseDir_ != browseDir_.root_path() && entry(0, "Übergeordneter Ordner", Icon::Up))
            next = browseDir_.parent_path();
        for(size_t i = 0; i < subdirs_.size(); ++i)
        {
            if(entry(i + 1, subdirs_[i], Icon::Folder))
                next = browseDir_ / subdirs_[i];
        }
        if(subdirs_.empty())
            ui_.text(view.x + app_.dp(8), ly + app_.dp(8), "Keine Unterordner", app_.dp(th.type.body), th.textFaint);
        ui_.endScroll(ly - ly0 + app_.dp(40));
        if(!next.empty())
            openDir(next);
        if(ui_.button(id("browse.pick"), {x, bottom, w, bh}, "Diesen Ordner durchsuchen", Icon::Check, Style::Primary))
            startSearch(browseDir_);
    }

    Ui& ui_;
    Mode mode_ = Mode::Missing;
    Mode prevMode_ = Mode::Missing;
    std::string note_;
    bool noteError_ = true;
    std::shared_ptr<DialogResult> dialog_;
    std::atomic<bool> cancel_{false};
    bfs::path searchRoot_;
    std::future<bfs::path> search_;
    std::future<std::string> copy_;
    bfs::path browseDir_;
    std::vector<std::string> subdirs_;
    unsigned scrollReset_ = 0;
};

} // namespace

std::unique_ptr<Screen> createDataSetupScreen(App& app)
{
    return std::make_unique<DataSetupScreen>(app);
}

} // namespace weiler
