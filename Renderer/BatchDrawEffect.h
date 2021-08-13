// BatchDrawEffect.h
// Using the power of TextureArrays and large vertex arrays, this type is intended to allow
// rendering of many animated vertex objects as one draw call
#pragma once
#ifndef BATCH_DRAW_EFFECT_H
#define BATCH_DRAW_EFFECT_H

#include "IEffect.h"
#include "IMultiDraw.h"
#include "DrawPackageBuilder.h"
#include "TexturePack.h"
#include "IEffectImpl.h"

namespace GamePrototype
{
    class Renderer;
    struct EffectInitInfo;

    class BatchDrawEffect : public IEffect
    {
    public:
        explicit BatchDrawEffect(const EffectInitInfo&);

        enum TotalPasses
        {
            kFirstPass,
            kSecondPass,
            kMaxPasses
        };

        enum MyShaderPassIndex
        {
            kStaticShadowShaderIndex,
            kStaticShaderIndex,
            kDynamicShadowShaderIndex,
            kDynamicShaderIndex,
            kStaticAlphaBlendShaderIndex,
            kDynamicAlphaBlendShaderIndex,
            kMaxShaderIndex
        };

    protected:

        // IEffect
        virtual Type GetType() const override { return kBase; }
        virtual const Graphics::MaterialList& GetMaterials() const override;

        Renderer&								m_renderer;
        const Graphics::MaterialList			m_materialList;
    };
}

#endif //BATCH_DRAW_EFFECT_H
