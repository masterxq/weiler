// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Triangle, edge and road geometry adapted from Return to the Roots (libs/s25main/TerrainRenderer.cpp),
// GPL-2.0-or-later.

#include "TerrainLayer.h"
#include "Perf.h"
#include "Loader.h"
#include "RttrForeachPt.h"
#include "helpers/EnumRange.h"
#include "ogl/glArchivItem_Bitmap.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldViewer.h"
#include "world/MapGeometry.h"
#include "gameTypes/MapTypes.h"
#include "gameData/MapConsts.h"
#include "gameData/EdgeDesc.h"
#include "gameData/LandscapeDesc.h"
#include "gameData/TerrainDesc.h"
#include "gameData/WorldDescription.h"
#include "libsiedler2/Archiv.h"
#include "libsiedler2/ArchivItem_PaletteAnimation.h"
#include "libsiedler2/PixelBufferBGRA.h"
#include "resources/ResourceId.h"
#include <boost/filesystem/path.hpp>
#include <cmath>
#include <stdexcept>

namespace weiler::gfx {

namespace {
const char* VS = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in float aB;
uniform vec2 uScreen;
uniform vec3 uCam;
out vec2 vUv;
out float vB;
void main()
{
    vec2 p = (aPos - uCam.xy) * uCam.z;
    vUv = aUv;
    vB = aB;
    gl_Position = vec4(p.x / uScreen.x * 2.0 - 1.0, 1.0 - p.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";
const char* FS = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
uniform float uOverdraw;
in vec2 vUv;
in float vB;
out vec4 fragColor;
void main()
{
    vec4 t = texture(uTex, vUv);
    // Unexplored land fades into a warm dark tone instead of hard black
    vec3 fog = vec3(0.075, 0.068, 0.06);
    fragColor = vec4(mix(fog, t.rgb * vB, smoothstep(0.0, 0.22, vB)), t.a);
    if(uOverdraw > 0.0)
        fragColor = vec4(uOverdraw, 0.0, 0.0, 0.0);
}
)";

Texture toTexture(const glArchivItem_Bitmap& bmp)
{
    const Extent size = bmp.getSize();
    libsiedler2::PixelBufferBGRA buf(size.x, size.y);
    bmp.renderSource(buf);
    std::vector<uint8_t> rgba(static_cast<size_t>(size.x) * size.y * 4);
    const uint8_t* in = buf.getPixelPtr();
    for(size_t i = 0; i < static_cast<size_t>(size.x) * size.y; ++i)
    {
        const unsigned a = in[i * 4 + 3];
        rgba[i * 4 + 0] = static_cast<uint8_t>(in[i * 4 + 2] * a / 255);
        rgba[i * 4 + 1] = static_cast<uint8_t>(in[i * 4 + 1] * a / 255);
        rgba[i * 4 + 2] = static_cast<uint8_t>(in[i * 4 + 0] * a / 255);
        rgba[i * 4 + 3] = static_cast<uint8_t>(a);
    }
    Texture t;
    t.create(static_cast<int>(size.x), static_cast<int>(size.y), false, true, rgba.data());
    return t;
}

DescIdx<EdgeDesc> edgeType(const TerrainDesc& t1, const TerrainDesc& t2)
{
    if(!t1.edgeType || t1.edgePriority <= t2.edgePriority)
        return {};
    return t1.edgeType;
}

constexpr unsigned NUM_ROAD_TYPES = helpers::NumEnumValues_v<LandRoadType>;
} // namespace

TerrainLayer::TerrainLayer() = default;

TerrainLayer::~TerrainLayer()
{
    if(program_)
    {
        glDeleteProgram(program_);
        glDeleteBuffers(1, &vbo_);
        glDeleteVertexArrays(1, &vao_);
    }
}

void TerrainLayer::init(const GameWorldViewer& gwv)
{
    gwv_ = &gwv;
    const GameWorldBase& world = gwv.getWorld();
    size_ = world.getSize();
    if(!program_)
    {
        program_ = createProgram(VS, FS);
        uScreen_ = glGetUniformLocation(program_, "uScreen");
        uCam_ = glGetUniformLocation(program_, "uCam");
        uOverdraw_ = glGetUniformLocation(program_, "uOverdraw");
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), reinterpret_cast<void*>(offsetof(Vtx, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), reinterpret_cast<void*>(offsetof(Vtx, u)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vtx), reinterpret_cast<void*>(offsetof(Vtx, b)));
        glBindVertexArray(0);
    }

    // Textures of all terrains, edges and roads of the world description
    const WorldDescription& desc = world.getDescription();
    terrainTex_.clear();
    edgeTex_.clear();
    roadTex_.clear();
    terrainTex_.resize(desc.terrain.size());
    for(unsigned ti = 0; ti < desc.terrain.size(); ++ti)
    {
        const DescIdx<TerrainDesc> idx(ti);
        const TerrainDesc& cur = desc.get(idx);
        if(cur.landscape != world.getLandscapeType())
            continue; // Only the landscape of this map
        const auto texName = ResourceId::make(boost::filesystem::path(cur.texturePath));
        glArchivItem_Bitmap* texBmp = LOADER.getImageN(texName, 0);
        if(!texBmp)
            throw std::runtime_error("Invalid texture '" + cur.texturePath + "' for terrain '" + cur.name + "'");
        Tex& t = terrainTex_[idx.value];
        libsiedler2::ArchivItem* animItem =
          cur.palAnimIdx >= 0 ? LOADER.getArchive(texName)[cur.palAnimIdx] : nullptr;
        if(animItem && texBmp->getPalette() && animItem->getBobType() == libsiedler2::BobType::PaletteAnim)
        {
            auto& anim = static_cast<libsiedler2::ArchivItem_PaletteAnimation&>(*animItem);
            auto frames = Loader::extractAnimatedTexture(*texBmp, cur.posInTexture, anim.firstClr,
                                                         anim.lastClr - anim.firstClr + 1);
            for(unsigned i = 0; i < frames->size(); ++i)
                t.frames.push_back(toTexture(dynamic_cast<const glArchivItem_Bitmap&>(*frames->get(i))));
        } else
            t.frames.push_back(toTexture(*Loader::extractTexture(*texBmp, cur.posInTexture)));
        const PointF texOrigin(cur.posInTexture.getOrigin());
        const PointF texSize(cur.posInTexture.getSize());
        TerrainDesc::Triangle tri = cur.getRSUTriangle();
        t.rsu = {(tri.tip - texOrigin) / texSize, (tri.left - texOrigin) / texSize, (tri.right - texOrigin) / texSize};
        tri = cur.getUSDTriangle();
        t.usd = {(tri.left - texOrigin) / texSize, (tri.tip - texOrigin) / texSize, (tri.right - texOrigin) / texSize};
    }
    edgeTex_.resize(desc.edges.size());
    for(unsigned ei = 0; ei < desc.edges.size(); ++ei)
    {
        const DescIdx<EdgeDesc> idx(ei);
        const EdgeDesc& cur = desc.get(idx);
        const auto texName = ResourceId::make(boost::filesystem::path(cur.texturePath));
        if(glArchivItem_Bitmap* texBmp = LOADER.getImageN(texName, 0))
            edgeTex_[idx.value] = toTexture(*Loader::extractTexture(*texBmp, cur.posInTexture));
    }
    roadTex_.resize(desc.landscapes.size() * NUM_ROAD_TYPES);
    for(unsigned li = 0; li < desc.landscapes.size(); ++li)
    {
        const DescIdx<LandscapeDesc> idx(li);
        const LandscapeDesc& cur = desc.get(idx);
        for(const auto rt : helpers::enumRange<LandRoadType>())
        {
            const auto texName = ResourceId::make(boost::filesystem::path(cur.roadTexDesc[rt].texturePath));
            if(glArchivItem_Bitmap* texBmp = LOADER.getImageN(texName, 0))
                roadTex_[idx.value * NUM_ROAD_TYPES + static_cast<unsigned>(rt)] =
                  toTexture(*Loader::extractTexture(*texBmp, cur.roadTexDesc[rt].posInTexture));
        }
    }

    colors_.assign(static_cast<size_t>(size_.x) * size_.y, 0.f);
    allChanged();
}

void TerrainLayer::updateColor(MapPoint pt)
{
    const auto shadow = static_cast<float>(gwv_->getNode(pt).shadow);
    const float orig = -1.f / (256.f * 256.f) * shadow * shadow + 1.f / 90.f * shadow + 0.38f;
    // Weiler: gentler relief shading than the original (dark slopes were very heavy)
    const float clr = 1.f + (orig - 1.f) * 0.6f;
    float& c = colors_[static_cast<size_t>(pt.y) * size_.x + pt.x];
    switch(gwv_->getVisibility(pt))
    {
        case Visibility::Invisible: c = 0.f; break;
        case Visibility::FogOfWar: c = clr / 2.f; break;
        case Visibility::Visible: c = clr; break;
    }
}

void TerrainLayer::allChanged()
{
    if(!gwv_)
        return;
    maxAltitudePx_ = 0;
    RTTR_FOREACH_PT(MapPoint, size_)
    {
        updateColor(pt);
        maxAltitudePx_ = std::max(maxAltitudePx_, int(gwv_->getNode(pt).altitude) * HEIGHT_FACTOR);
    }
}

void TerrainLayer::altitudeChanged(MapPoint pt)
{
    updateColor(pt);
    for(const auto dir : helpers::EnumRange<Direction>{})
        updateColor(gwv_->getNeighbour(pt, dir));
    maxAltitudePx_ = std::max(maxAltitudePx_, int(gwv_->getNode(pt).altitude) * HEIGHT_FACTOR);
}

void TerrainLayer::visibilityChanged(MapPoint pt)
{
    updateColor(pt);
}

MapPoint TerrainLayer::toMapPoint(Position p) const
{
    const int w = size_.x, h = size_.y;
    return MapPoint(static_cast<MapCoord>(((p.x % w) + w) % w), static_cast<MapCoord>(((p.y % h) + h) % h));
}

PointF TerrainLayer::nodePos(Position p) const
{
    const MapPoint mp = toMapPoint(p);
    const Position offset = (p - Position(mp)) * Position(TR_W, TR_H);
    return PointF(gwv_->getWorld().getNodePos(mp) + offset);
}

float TerrainLayer::color(Position p) const
{
    const MapPoint mp = toMapPoint(p);
    return colors_[static_cast<size_t>(mp.y) * size_.x + mp.x];
}

NodeRange TerrainLayer::visibleRange(const Camera& cam) const
{
    NodeRange r;
    r.x0 = static_cast<int>(std::floor(cam.x / TR_W)) - 2;
    r.x1 = static_cast<int>(std::ceil((cam.x + cam.viewW()) / TR_W)) + 2;
    r.y0 = static_cast<int>(std::floor(cam.y / TR_H)) - 2;
    r.y1 = static_cast<int>(std::ceil((cam.y + cam.viewH() + maxAltitudePx_) / TR_H)) + 2;
    return r;
}

void TerrainLayer::addTri(std::vector<Vtx>& out, PointF p0, PointF p1, PointF p2, PointF t0, PointF t1, PointF t2,
                          float b0, float b1, float b2)
{
    out.push_back({p0.x, p0.y, t0.x, t0.y, b0});
    out.push_back({p1.x, p1.y, t1.x, t1.y, b1});
    out.push_back({p2.x, p2.y, t2.x, t2.y, b2});
}

void TerrainLayer::draw(const Camera& cam, unsigned timeMs)
{
    if(!gwv_)
        return;
    const GameWorldBase& world = gwv_->getWorld();
    const WorldDescription& desc = world.getDescription();
    const size_t numTerrain = terrainTex_.size();
    const size_t edgeBase = numTerrain;
    const size_t roadBase = edgeBase + edgeTex_.size();
    buckets_.resize(roadBase + roadTex_.size());
    for(auto& b : buckets_)
        b.clear();

    perf::Scope timeTerrain(perf::Phase::Terrain);
    const NodeRange r = visibleRange(cam);
    const PointF uv00(0, 0), uv10(1, 0), uvMid(0.5f, 1), uv01(0, 1), uv11(1, 1);
    for(int y = r.y0; y <= r.y1; ++y)
    {
        for(int x = r.x0; x <= r.x1; ++x)
        {
            const Position p(x, y);
            const MapPoint mp = toMapPoint(p);
            const Position pSW = ::getNeighbour(p, Direction::SouthWest);
            const Position pSE = ::getNeighbour(p, Direction::SouthEast);
            const Position pE = ::getNeighbour(p, Direction::East);
            const PointF v = nodePos(p), vSW = nodePos(pSW), vSE = nodePos(pSE), vE = nodePos(pE);
            const float c = color(p), cSW = color(pSW), cSE = color(pSE), cE = color(pE);
            if(c == 0 && cSW == 0 && cSE == 0 && cE == 0)
                continue;
            const MapNode& node = world.getNode(mp);
            const Tex& t1 = terrainTex_[node.t1.value];
            const Tex& t2 = terrainTex_[node.t2.value];
            if(!t1.frames.empty())
                addTri(buckets_[node.t1.value], v, vSW, vSE, t1.rsu[0], t1.rsu[1], t1.rsu[2], c, cSW, cSE);
            if(!t2.frames.empty())
                addTri(buckets_[node.t2.value], v, vSE, vE, t2.usd[0], t2.usd[1], t2.usd[2], c, cSE, cE);
        }
    }
    // Edges between terrains: drawn over the triangles
    for(int y = r.y0; y <= r.y1; ++y)
    {
        for(int x = r.x0; x <= r.x1; ++x)
        {
            const Position p(x, y);
            const MapPoint mp = toMapPoint(p);
            const Position pSW = ::getNeighbour(p, Direction::SouthWest);
            const Position pSE = ::getNeighbour(p, Direction::SouthEast);
            const Position pE = ::getNeighbour(p, Direction::East);
            const float c = color(p), cSW = color(pSW), cSE = color(pSE), cE = color(pE);
            if(c == 0 && cSW == 0 && cSE == 0 && cE == 0)
                continue;
            const MapNode& node = world.getNode(mp);
            const TerrainDesc& t1 = desc.get(node.t1);
            const TerrainDesc& t2 = desc.get(node.t2);
            const TerrainDesc& t3 = desc.get(world.getNode(toMapPoint(pE)).t1);
            const TerrainDesc& t4 = desc.get(world.getNode(toMapPoint(pSW)).t2);
            const PointF v = nodePos(p), vSW = nodePos(pSW), vSE = nodePos(pSE), vE = nodePos(pE);
            // Centers of the two triangles of this node and of the neighbours
            const PointF border0 = (vSW + v + vSE) / 3.f, border1 = (vE + v + vSE) / 3.f;
            const float bc0 = (cSW + c + cSE) / 3.f, bc1 = (cE + c + cSE) / 3.f;
            const Position pEsw = ::getNeighbour(pE, Direction::SouthWest),
                           pEse = ::getNeighbour(pE, Direction::SouthEast);
            const PointF borderE0 = (nodePos(pEsw) + vE + nodePos(pEse)) / 3.f;
            const float bcE0 = (color(pEsw) + cE + color(pEse)) / 3.f;
            const Position pSWe = ::getNeighbour(pSW, Direction::East),
                           pSWse = ::getNeighbour(pSW, Direction::SouthEast);
            const PointF borderSW1 = (nodePos(pSWe) + vSW + nodePos(pSWse)) / 3.f;
            const float bcSW1 = (color(pSWe) + cSW + color(pSWse)) / 3.f;

            auto addEdge = [&](DescIdx<EdgeDesc> e, PointF a, PointF b, PointF tip, float ca, float cb, float ct) {
                if(!e || edgeTex_[e.value].id() == 0)
                    return;
                addTri(buckets_[edgeBase + e.value], a, b, tip, uv00, uv10, uvMid, ca, cb, ct);
            };
            // left_right: edge between the two triangles of this node
            addEdge(edgeType(t2, t1), v, vSE, border0, c, cSE, bc0);
            addEdge(edgeType(t1, t2), v, vSE, border1, c, cSE, bc1);
            // right_left: edge between t2 and the RSU triangle of the east neighbour
            addEdge(edgeType(t3, t2), vSE, vE, border1, cSE, cE, bc1);
            addEdge(edgeType(t2, t3), vSE, vE, borderE0, cSE, cE, bcE0);
            // top_down: edge between t1 and the USD triangle of the south west neighbour
            addEdge(edgeType(t4, t1), vSW, vSE, border0, cSW, cSE, bc0);
            addEdge(edgeType(t1, t4), vSW, vSE, borderSW1, cSW, cSE, bcSW1);
        }
    }
    // Roads
    static constexpr helpers::EnumArray<std::array<Position, 4>, RoadDir> roadCoords = {{
      {{Position(0, -3), Position(0, 3), Position(3, 3), Position(3, -3)}},
      {{Position(2, -4), Position(-4, 2), Position(0, 6), Position(6, 0)}},
      {{Position(4, 2), Position(-2, -4), Position(-6, 0), Position(0, 6)}},
    }};
    for(int y = r.y0; y <= r.y1; ++y)
    {
        for(int x = r.x0; x <= r.x1; ++x)
        {
            const Position p(x, y);
            const MapPoint mp = toMapPoint(p);
            const Visibility vis = gwv_->getVisibility(mp);
            if(vis == Visibility::Invisible)
                continue;
            const Position start(Position::Truncate, nodePos(p));
            for(const RoadDir dir : helpers::EnumRange<RoadDir>{})
            {
                const PointRoad type = gwv_->getVisibleRoad(mp, dir, vis);
                if(type == PointRoad::None)
                    continue;
                const Direction targetDir = toDirection(dir);
                const Position pt2 = ::getNeighbour(p, targetDir);
                const Position end(Position::Truncate, nodePos(pt2));
                const auto terrain = world.getTerrain(mp, targetDir);
                const TerrainDesc& lT = desc.get(terrain.left);
                const TerrainDesc& rT = desc.get(terrain.right);
                unsigned idx;
                if(type == PointRoad::Boat)
                    idx = lT.landscape.value * NUM_ROAD_TYPES + static_cast<unsigned>(LandRoadType::Boat);
                else if(lT.kind == TerrainKind::Mountain)
                    idx = lT.landscape.value * NUM_ROAD_TYPES + static_cast<unsigned>(LandRoadType::Mountain);
                else if(rT.kind == TerrainKind::Mountain)
                    idx = rT.landscape.value * NUM_ROAD_TYPES + static_cast<unsigned>(LandRoadType::Mountain);
                else
                    idx = lT.landscape.value * NUM_ROAD_TYPES
                          + static_cast<unsigned>(type == PointRoad::Donkey ? LandRoadType::Upgraded :
                                                                              LandRoadType::Normal);
                if(idx >= roadTex_.size() || !roadTex_[idx].id())
                    continue;
                const float c1 = color(p), c2 = color(pt2);
                const auto& rc = roadCoords[dir];
                const PointF q0(start + rc[0]), q1(start + rc[1]), q2(end + rc[2]), q3(end + rc[3]);
                auto& bucket = buckets_[roadBase + idx];
                addTri(bucket, q0, q1, q2, uv00, uv01, uv11, c1, c1, c2);
                addTri(bucket, q0, q2, q3, uv00, uv11, uv10, c1, c2, c2);
            }
        }
    }

    glUseProgram(program_);
    glUniform2f(uScreen_, static_cast<float>(cam.screenW), static_cast<float>(cam.screenH));
    glUniform3f(uCam_, cam.x, cam.y, cam.zoom);
    glUniform1f(uOverdraw_, gOverdraw);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    for(size_t i = 0; i < buckets_.size(); ++i)
    {
        const auto& verts = buckets_[i];
        if(verts.empty())
            continue;
        GLuint tex;
        if(i < edgeBase)
        {
            const auto& frames = terrainTex_[i].frames;
            tex = frames[(timeMs / 120) % frames.size()].id();
        } else if(i < roadBase)
            tex = edgeTex_[i - edgeBase].id();
        else
            tex = roadTex_[i - roadBase].id();
        glBindTexture(GL_TEXTURE_2D, tex);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vtx)), verts.data(),
                     GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
        perf::drawCall();
    }
    glBindVertexArray(0);
}

} // namespace weiler::gfx
