// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ogl/SpriteRenderer.h"

namespace {
ISpriteRenderer* gRenderer = nullptr;
}

ISpriteSource::~ISpriteSource()
{
    if(rendererHandle && gRenderer)
        gRenderer->releaseSource(*this);
}

ISpriteRenderer* getSpriteRenderer()
{
    return gRenderer;
}

void setSpriteRenderer(ISpriteRenderer* renderer)
{
    gRenderer = renderer;
}
