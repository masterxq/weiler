// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "CdImport.h"
#include <boost/filesystem.hpp>
#include <vorbis/vorbisenc.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace bfs = boost::filesystem;

namespace weiler {

namespace {
constexpr uint64_t SECTOR = 2352; // raw sector: 588 stereo frames of 16 bit audio

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Encode 16 bit stereo PCM (little endian) from a file range into an Ogg Vorbis file
bool encode(const bfs::path& src, uint64_t begin, uint64_t end, const bfs::path& dst, int number,
            const std::function<bool(float)>& onProgress)
{
    std::ifstream in(src.string(), std::ios::binary);
    if(!in)
        return false;
    in.seekg(static_cast<std::streamoff>(begin));
    FILE* out = std::fopen(dst.string().c_str(), "wb");
    if(!out)
        return false;
    vorbis_info vi;
    vorbis_info_init(&vi);
    if(vorbis_encode_init_vbr(&vi, 2, 44100, 0.4f) != 0)
    {
        std::fclose(out);
        return false;
    }
    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_comment_add_tag(&vc, "TITLE", ("CD " + std::to_string(number)).c_str());
    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);
    ogg_stream_state os;
    ogg_stream_init(&os, static_cast<int>(std::time(nullptr)) + number);
    ogg_packet hdr, hdrComm, hdrCode;
    vorbis_analysis_headerout(&vd, &vc, &hdr, &hdrComm, &hdrCode);
    ogg_stream_packetin(&os, &hdr);
    ogg_stream_packetin(&os, &hdrComm);
    ogg_stream_packetin(&os, &hdrCode);
    ogg_page og;
    while(ogg_stream_flush(&os, &og))
    {
        std::fwrite(og.header, 1, static_cast<size_t>(og.header_len), out);
        std::fwrite(og.body, 1, static_cast<size_t>(og.body_len), out);
    }
    std::vector<char> buf(SECTOR * 32);
    uint64_t pos = begin;
    bool eos = false;
    while(!eos)
    {
        const uint64_t want = std::min<uint64_t>(buf.size(), end - pos);
        long frames = 0;
        if(want > 0)
        {
            in.read(buf.data(), static_cast<std::streamsize>(want));
            frames = static_cast<long>(in.gcount() / 4);
            pos += static_cast<uint64_t>(in.gcount());
        }
        if(frames <= 0)
            vorbis_analysis_wrote(&vd, 0);
        else
        {
            float** pcm = vorbis_analysis_buffer(&vd, static_cast<int>(frames));
            const auto* s = reinterpret_cast<const unsigned char*>(buf.data());
            for(long i = 0; i < frames; ++i)
            {
                const auto l = static_cast<int16_t>(s[i * 4] | (s[i * 4 + 1] << 8));
                const auto r = static_cast<int16_t>(s[i * 4 + 2] | (s[i * 4 + 3] << 8));
                // About -4 dB: the CD is louder than the Weiler pieces (-16 vs. -20 LUFS)
                pcm[0][i] = l / 32768.f * 0.63f;
                pcm[1][i] = r / 32768.f * 0.63f;
            }
            vorbis_analysis_wrote(&vd, static_cast<int>(frames));
            if(!onProgress(static_cast<float>(pos - begin) / static_cast<float>(std::max<uint64_t>(1, end - begin))))
                pos = end; // cancelled: finish the stream, the caller discards the file
        }
        while(vorbis_analysis_blockout(&vd, &vb) == 1)
        {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            ogg_packet op;
            while(vorbis_bitrate_flushpacket(&vd, &op))
            {
                ogg_stream_packetin(&os, &op);
                while(!eos && ogg_stream_pageout(&os, &og))
                {
                    std::fwrite(og.header, 1, static_cast<size_t>(og.header_len), out);
                    std::fwrite(og.body, 1, static_cast<size_t>(og.body_len), out);
                    if(ogg_page_eos(&og))
                        eos = true;
                }
            }
        }
    }
    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    std::fclose(out);
    return true;
}
} // namespace

CdImport::~CdImport()
{
    // Quitting during an import: stop after the current block
    cancel_ = true;
    if(thread_.joinable())
        thread_.join();
}

bfs::path CdImport::findImage(const bfs::path& gameData)
{
    boost::system::error_code ec;
    std::vector<bfs::path> dirs = {gameData, gameData.parent_path(), gameData.parent_path().parent_path()};
    for(const bfs::path& base : std::vector<bfs::path>(dirs))
    {
        for(const auto& e : bfs::directory_iterator(base, ec))
        {
            if(bfs::is_directory(e.path(), ec))
                dirs.push_back(e.path());
        }
    }
    for(const bfs::path& d : dirs)
    {
        for(const auto& e : bfs::directory_iterator(d, ec))
        {
            const std::string ext = lower(e.path().extension().string());
            if((ext == ".ccd" || ext == ".cue") && !parse(e.path()).empty())
                return e.path();
        }
    }
    return {};
}

std::vector<CdImport::Track> CdImport::parse(const bfs::path& image)
{
    std::vector<Track> tracks;
    std::ifstream in(image.string());
    if(!in)
        return tracks;
    boost::system::error_code ec;
    const std::string ext = lower(image.extension().string());
    struct Entry
    {
        bfs::path file;
        uint64_t sector;
        bool audio;
    };
    std::vector<Entry> entries;
    std::string line;
    if(ext == ".ccd")
    {
        // CloneCD: [TRACK n] MODE=0 (audio) INDEX 1=<sector>; data in <name>.img
        bfs::path img = image;
        img.replace_extension(".img");
        bool inTrack = false, audio = false;
        while(std::getline(in, line))
        {
            if(!line.empty() && line.back() == '\r')
                line.pop_back();
            if(line.rfind("[TRACK", 0) == 0)
            {
                inTrack = true;
                audio = false;
            } else if(line.rfind("[", 0) == 0)
                inTrack = false;
            else if(inTrack && line.rfind("MODE=", 0) == 0)
                audio = line.substr(5) == "0";
            else if(inTrack && line.rfind("INDEX 1=", 0) == 0)
                entries.push_back({img, std::stoull(line.substr(8)), audio});
        }
    } else
    {
        // Cue sheet: FILE "x.bin" BINARY / TRACK nn AUDIO / INDEX 01 mm:ss:ff
        bfs::path file;
        bool audio = false;
        while(std::getline(in, line))
        {
            std::istringstream ls(line);
            std::string kw;
            ls >> kw;
            if(kw == "FILE")
            {
                const auto a = line.find('"'), b = line.rfind('"');
                if(a != std::string::npos && b > a)
                    file = image.parent_path() / line.substr(a + 1, b - a - 1);
            } else if(kw == "TRACK")
            {
                std::string n, type;
                ls >> n >> type;
                audio = type == "AUDIO";
            } else if(kw == "INDEX")
            {
                std::string n, t;
                ls >> n >> t;
                unsigned mm = 0, ss = 0, ff = 0;
                if(n == "01" && std::sscanf(t.c_str(), "%u:%u:%u", &mm, &ss, &ff) == 3)
                    entries.push_back({file, (mm * 60ull + ss) * 75ull + ff, audio});
            }
        }
    }
    for(size_t i = 0; i < entries.size(); ++i)
    {
        if(!entries[i].audio || !bfs::exists(entries[i].file, ec))
            continue;
        const uint64_t size = bfs::file_size(entries[i].file, ec);
        uint64_t end = size;
        if(i + 1 < entries.size() && entries[i + 1].file == entries[i].file)
            end = entries[i + 1].sector * SECTOR;
        const uint64_t begin = entries[i].sector * SECTOR;
        // At least ten seconds of audio
        if(end > begin && end <= size && end - begin > SECTOR * 75 * 10)
            tracks.push_back({entries[i].file, begin, end});
    }
    return tracks;
}

bool CdImport::start(const bfs::path& image, const bfs::path& outDir)
{
    if(running_)
        return false;
    if(thread_.joinable())
        thread_.join();
    auto tracks = parse(image);
    if(tracks.empty())
    {
        error_ = "Keine Audiospuren gefunden";
        return false;
    }
    error_.clear();
    running_ = true;
    progress_ = 0;
    written_ = 0;
    thread_ = std::thread([this, tracks = std::move(tracks), outDir] { run(tracks, outDir); });
    return true;
}

void CdImport::run(std::vector<Track> tracks, bfs::path outDir)
{
    boost::system::error_code ec;
    bfs::create_directories(outDir, ec);
    uint64_t total = 0, done = 0;
    for(const Track& t : tracks)
        total += t.end - t.begin;
    int n = 0;
    for(const Track& t : tracks)
    {
        ++n;
        char name[32];
        std::snprintf(name, sizeof(name), "track%02d.ogg", n);
        const bfs::path dst = outDir / name;
        const bfs::path tmp = outDir / (std::string(name) + ".part");
        const uint64_t len = t.end - t.begin;
        if(cancel_)
            break;
        const bool ok = encode(t.file, t.begin, t.end, tmp, n, [&](float f) {
            progress_ = static_cast<float>(done + static_cast<uint64_t>(f * static_cast<float>(len)))
                        / static_cast<float>(std::max<uint64_t>(1, total));
            return !cancel_;
        }) && !cancel_;
        if(ok)
        {
            bfs::rename(tmp, dst, ec);
            ++written_;
        } else
        {
            bfs::remove(tmp, ec);
            error_ = "Schreiben fehlgeschlagen";
        }
        done += len;
    }
    progress_ = 1;
    running_ = false;
}

} // namespace weiler
