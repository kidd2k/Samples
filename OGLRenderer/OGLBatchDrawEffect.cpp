// OGLBatchDrawEffect.cpp
#include "stdafx.h"
#include "OGLBatchDrawEffect.h"
#include "RenderUtilities.h"
#include "RenderObjectMeshBuilder.h"
#include "OGLShaders.h"
#include "OGLTexturePack.h"
#include "OGLRenderContext.h"

// these are objects that represent OpenGL ADZO techniques.
#include "MultiDrawArraysIndirectObject.h"
#include "MultiDrawArraysStaticIndirectObject.h"
#include "MultiDrawArrayObject.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/EffectInitInfo.h"
#include "../Renderer/IEffectMgr.h"

namespace GamePrototype
{
    OGLBatchDrawEffect::OGLBatchDrawEffect(const EffectInitInfo& info)
    :
    BatchDrawEffect(info),
    m_id(0),
    m_currentPass(-1),
    m_effectType(info.m_effectType),
    m_bIsFirstDynamic(true),
    m_bIsFirstStatic(true),
    m_bIsFirstAlphaDynamic(true),
    m_bIsFirstAlphaStatic(true),
    m_bIsInitialized(false),
    m_bSetStaticPackages(false),
    m_bSetDynamicPackages(false)
    {
    }

    bool OGLBatchDrawEffect::Init()
    {
        if (!m_bIsInitialized)
        {
            if (!m_materialList.empty())
            {
                m_cachedShaderPtrs.resize(m_materialList.size());
            }

            RenderContextPtr contextPtr = m_renderer.GetRenderContext();
            assert(contextPtr);
            if(contextPtr)
            {
                OGLRenderContext& oglContext = static_cast<OGLRenderContext&>(*contextPtr);

                int texWidth = TexturePack::s_kDefaultTextureWidth;
                int texHeight = TexturePack::s_kDefaultTextureHeight;
                int texMipMapLevels = TexturePack::s_kTextureMipMapLevels;
                auto& queryRenderPtr = oglContext.GetQueryRenderer();
                if(queryRenderPtr)
                {
                    QueryRenderer::TextureInfo forcedTextureInfo = queryRenderPtr->GetTextureInfo();
                    if(forcedTextureInfo.bForcedSize)
                    {
                        texWidth = forcedTextureInfo.maxWidthHeight;
                        texHeight = forcedTextureInfo.maxWidthHeight;
                        texMipMapLevels = forcedTextureInfo.numMipMaps;
                    }
                }

                // VKNBatchDrawEffect uses same texture format for BOTH
                // alphablended & non alpha'd TexPacks. Can we do the same
                // for OpenGL? (Probably not without data changes...)
                m_texPackPtr = std::make_shared<OGLTexturePack>(TexturePack::s_kDefaultTextureWidth,
                    TexturePack::s_kDefaultTextureHeight,
                    TexturePack::s_kTextureMipMapLevels,
                    TexturePack::s_kMaxTextures,
                    GL_RGB8);

                RenderCheckOK(m_texPackPtr != TexturePackPtr());
                RenderCheckOK(m_texPackPtr->Init());

                RenderCheckOK(!ErrorUtilities::IsOpenGLError());

                m_alphaTexPackPtr = std::make_shared<OGLTexturePack>(TexturePack::s_kDefaultTextureWidth,
                    TexturePack::s_kDefaultTextureHeight,
                    TexturePack::s_kTextureMipMapLevels,
                    TexturePack::s_kMaxTextures,
                    GL_RGBA8);

                RenderCheckOK(m_alphaTexPackPtr != TexturePackPtr());
                RenderCheckOK(m_alphaTexPackPtr->Init());

                RenderCheckOK(!ErrorUtilities::IsOpenGLError());

                m_dynamicMultiDrawObjectPtr = std::make_shared<MultiDrawArrayObject>(oglContext);
                RenderCheckOK(m_dynamicMultiDrawObjectPtr != MultiDrawPtr());
                m_dynamicMultiDrawObjectPtr->SetTexturePack(m_texPackPtr);
                RenderCheckOK(m_dynamicMultiDrawObjectPtr->Initialize());

                RenderCheckOK(!ErrorUtilities::IsOpenGLError());

                m_alphaDynamicMultiDrawObjectPtr = std::make_shared<MultiDrawArrayObject>(oglContext);
                RenderCheckOK(m_alphaDynamicMultiDrawObjectPtr != MultiDrawPtr());
                m_alphaDynamicMultiDrawObjectPtr->SetTexturePack(m_alphaTexPackPtr);
                RenderCheckOK(m_alphaDynamicMultiDrawObjectPtr->Initialize());
                m_alphaDynamicMultiDrawObjectPtr->SetAlphaBlending(true);

                RenderCheckOK(!ErrorUtilities::IsOpenGLError());

                m_staticMultiDrawObjectPtr = std::make_shared<MultiDrawArraysStaticIndirectObject>();
                RenderCheckOK(m_staticMultiDrawObjectPtr != MultiDrawPtr());
                m_staticMultiDrawObjectPtr->SetTexturePack(m_texPackPtr);
                RenderCheckOK(m_staticMultiDrawObjectPtr->Initialize());

                RenderCheckOK(!ErrorUtilities::IsOpenGLError());

                m_alphaStaticMultiDrawObjectPtr = std::make_shared<MultiDrawArraysStaticIndirectObject>();
                RenderCheckOK(m_alphaStaticMultiDrawObjectPtr != MultiDrawPtr());
                m_alphaStaticMultiDrawObjectPtr->SetTexturePack(m_alphaTexPackPtr);
                RenderCheckOK(m_alphaStaticMultiDrawObjectPtr->Initialize());
                m_alphaStaticMultiDrawObjectPtr->SetAlphaBlending(true);

                RenderCheckOK(!ErrorUtilities::IsOpenGLError());

                m_bIsInitialized = true;
            }
        }

        return m_bIsInitialized;
    }

    int OGLBatchDrawEffect::NumPasses() const
    {
        /*
        for each type of draw pass, whether it be shadows, opaque geometry,
        or translucent geometry, we have static and dynamic data.
        So let's go with 2 for now
        */

        return kMaxPasses; /* one static pass, one dynamic pass*/
    }

    bool OGLBatchDrawEffect::PrePass(int pass, const EffectStatePtr&)
    {
        m_currentPass = pass;
        if (!m_cachedShaderPtrs[0])
        {
            m_cachedShaderPtrs[kStaticShaderIndex] = m_renderer.GetShaderProgram(m_materialList[kStaticShaderIndex]);
            assert(m_cachedShaderPtrs[kStaticShaderIndex]);
            m_cachedShaderPtrs[kStaticShadowShaderIndex] = m_renderer.GetShaderProgram(m_materialList[kStaticShadowShaderIndex]);
            assert(m_cachedShaderPtrs[kStaticShadowShaderIndex]);
            m_cachedShaderPtrs[kDynamicShaderIndex] = m_renderer.GetShaderProgram(m_materialList[kDynamicShaderIndex]);
            assert(m_cachedShaderPtrs[kDynamicShaderIndex]);
            m_cachedShaderPtrs[kDynamicShadowShaderIndex] = m_renderer.GetShaderProgram(m_materialList[kDynamicShadowShaderIndex]);
            assert(m_cachedShaderPtrs[kDynamicShadowShaderIndex]);
            m_cachedShaderPtrs[kStaticAlphaBlendShaderIndex] = m_renderer.GetShaderProgram(m_materialList[kStaticAlphaBlendShaderIndex]);
            assert(m_cachedShaderPtrs[kStaticAlphaBlendShaderIndex]);
            m_cachedShaderPtrs[kDynamicAlphaBlendShaderIndex] = m_renderer.GetShaderProgram(m_materialList[kDynamicAlphaBlendShaderIndex]);
            assert(m_cachedShaderPtrs[kDynamicAlphaBlendShaderIndex]);
        }

        return true;
    }

    bool OGLBatchDrawEffect::Collect(const Graphics::RenderObjectPtr& objPtr)
    {
        if (objPtr)
        {
            RenderCheckOK(m_dynamicMultiDrawObjectPtr != MultiDrawPtr());
            RenderCheckOK(m_staticMultiDrawObjectPtr != MultiDrawPtr());

            DrawPackagePtr dpPtr = objPtr->GetDrawPackage();
            if (!dpPtr)
            {
                // NOTE: place the alpha blending type earlier than 
                // default on list
                std::vector<MultiDrawPtr> dynamicDrawList
                {
                    m_alphaDynamicMultiDrawObjectPtr,
                    m_dynamicMultiDrawObjectPtr
                };

                DynamicDrawPackageBuilder dynamicBuilder(m_renderer.GetRenderContext(), dynamicDrawList, objPtr);
                dpPtr = dynamicBuilder.Create();

                // NOTE: place the alpha blending type earlier than 
                // default on list
                std::vector<MultiDrawPtr> staticDrawList
                {
                    m_alphaStaticMultiDrawObjectPtr,
                    m_staticMultiDrawObjectPtr
                };

                StaticDrawPackageBuilder staticBuilder(m_renderer.GetRenderContext(), staticDrawList, objPtr, dpPtr);
                dpPtr = staticBuilder.Create();

                objPtr->SetDrawPackage(dpPtr);
            }

            if (dpPtr)
            {
                for (size_t i = 0; i<dpPtr->GetNumDataEntries(); ++i)
                {
                    DrawPackageDataPtr dataPtr;
                    if (dpPtr->GetData(i, dataPtr))
                    {
                        if (dataPtr->IsDynamic())
                        {
                            m_dynamicPackages.push_back(dataPtr);
                        }
                        else
                        {
                            m_staticPackages.push_back(dataPtr);
                        }
                    }
                }

                m_renderer.AddEffect(this);
            }

            return true;
        }

        return false;
    }

    bool OGLBatchDrawEffect::Collect(const std::vector<Graphics::RenderObjectPtr>& list)
    {
        for (const auto& node : list)
        {
            if (!Collect(node))
            {
                return false;
            }
        }

        return true;
    }

    bool OGLBatchDrawEffect::Collect(const Graphics::SceneNodePtr&)
    {
        // NOTE: LightCollection now stores all lights. Since this is just a typical
        // geometry pass of a deferred render, lighting info is irrelevant right now.
        return false;
    }

    bool OGLBatchDrawEffect::Draw(const Graphics::CameraDrawInfo& cdi, const Graphics::RenderStateInfo& rsi, const EffectStatePtr&)
    {
        if (!m_bIsDrawActive)
        {
            return true;
        }

        // NOTE: this effect is not designed to handle postprocessing (yet).
        if(rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kPostProcess)
        {
            return true;
        }

        // NOTE: effect not quite ready for translucent passes yet either...
        // TODO: we need to setup a (shader?) pipeline that supports alpha
        // blending on the current rendering target? maybe not, and just
        // use the correct IMultiDraw alpha types?
        if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kTranslucent)
        {
            return true;
        }

        // no functionality yet during skybox pass (update as changes happen)
        if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kSkybox)
        {
            return true;
        }

        if (m_currentPass == kFirstPass)
        {
            Graphics::RenderStateInfo mod_rsi(rsi);

            if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kShadows)
            {
                mod_rsi.SetShaderOverride(m_cachedShaderPtrs[MyShaderPassIndex::kStaticShadowShaderIndex]);
            }
            else if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kTranslucent)
            {
                mod_rsi.SetShaderOverride(m_cachedShaderPtrs[MyShaderPassIndex::kStaticAlphaBlendShaderIndex]);
            }
            else
            {
                mod_rsi.SetShaderOverride(m_cachedShaderPtrs[MyShaderPassIndex::kStaticShaderIndex]);
            }

            RenderCheckOK(DrawStaticPass(cdi, mod_rsi));
        }
        else if (m_currentPass == kSecondPass)
        {
            Graphics::RenderStateInfo mod_rsi(rsi);

            if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kShadows)
            {
                mod_rsi.SetShaderOverride(m_cachedShaderPtrs[MyShaderPassIndex::kDynamicShadowShaderIndex]);
            }
            else if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kTranslucent)
            {
                mod_rsi.SetShaderOverride(m_cachedShaderPtrs[MyShaderPassIndex::kDynamicAlphaBlendShaderIndex]);
            }
            else
            {
                mod_rsi.SetShaderOverride(m_cachedShaderPtrs[MyShaderPassIndex::kDynamicShaderIndex]);
            }

            RenderCheckOK(DrawDynamicPass(cdi, mod_rsi));
        }

        return true;
    }

    bool OGLBatchDrawEffect::PostPass(const EffectStatePtr&)
    {
        if (m_currentPass > -1)
        {
            return true;
        }

        return false;
    }

    void OGLBatchDrawEffect::ClearForNextFrame()
    {
        m_bIsFirstStatic = true;
        m_bIsFirstDynamic = true;
        m_bIsFirstAlphaDynamic = true;
        m_bIsFirstAlphaStatic = true;

        m_staticPackages.clear();
        m_dynamicPackages.clear();
        m_bSetStaticPackages = false;
        m_bSetDynamicPackages = false;
    }

    int OGLBatchDrawEffect::GetID() const
    {
        return m_id;
    }

    void OGLBatchDrawEffect::Free()
    {

    }

    int OGLBatchDrawEffect::GetEffectType() const
    {
        return m_effectType;
    }

    unsigned int OGLBatchDrawEffect::GetNumRenderTargets() const
    {
        return 0;
    }

    RenderTargetInfo OGLBatchDrawEffect::GetRenderTarget(size_t) const
    {
        return RenderTargetInfo();
    }

    void OGLBatchDrawEffect::SetVariable(size_t shaderId, const char* pName, float val)
    {
        assert(!"Not implemented yet!");
    }

    void OGLBatchDrawEffect::SetVariable(size_t shaderId, const char* pName, size_t count, float* pValues)
    {
        assert(!"Not implemented yet!");
    }

    float* OGLBatchDrawEffect::GetVariable(size_t shaderId, const char*) const
    {
        assert(!"Not implemented yet!");
        return 0;
    }

    void OGLBatchDrawEffect::SetEffectData(size_t index, const EffectData&)
    {
        assert(!"Not implemented yet!");
    }

    bool OGLBatchDrawEffect::PostSceneGraph()
    {
        return CheckBuffers();
    }

    bool OGLBatchDrawEffect::DrawStaticPass(const Graphics::CameraDrawInfo& cdi, const Graphics::RenderStateInfo& rsi)
    {
        int shaderIndex = MyShaderPassIndex::kStaticShaderIndex;
        MultiDrawPtr drawPtr;

        if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kTranslucent)
        {
            drawPtr = m_alphaStaticMultiDrawObjectPtr;
            shaderIndex = MyShaderPassIndex::kStaticAlphaBlendShaderIndex;
        }
        else
        {
            drawPtr = m_staticMultiDrawObjectPtr;
            shaderIndex = MyShaderPassIndex::kStaticShaderIndex;
        }

        if (drawPtr)
        {
            OGLShaderPtr currentShader = std::dynamic_pointer_cast<OGLShader, Shader>(rsi.GetShaderOverride());
            if (!currentShader)
            {
                currentShader = std::dynamic_pointer_cast<OGLShader, Shader>(m_cachedShaderPtrs[shaderIndex]);
            }

            if (currentShader)
            {
                m_renderer.SetActiveShaderProgram(currentShader);
                // we set the shader here as well because static multidraw type needs
                // to access uniform 'worldMat' to set it per object reference
                drawPtr->SetShader(currentShader);

                //currentShader->SetUniform("viewProjCamera", cdi.viewProjMat);
                currentShader->SetUniform("viewCam", cdi.viewMat);
                currentShader->SetUniform("projCam", cdi.projMat);

                //ErrorUtilities::CheckGLErrors();

                // the array buffer and texture array linking to shader state is done
                // within this call
                RenderCheckOK(drawPtr->Render());
            }
        }

        return true;
    }

    bool OGLBatchDrawEffect::DrawDynamicPass(const Graphics::CameraDrawInfo& cdi, const Graphics::RenderStateInfo& rsi)
    {
        int shaderIndex = MyShaderPassIndex::kDynamicShaderIndex;
        MultiDrawPtr drawPtr;

        if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kTranslucent)
        {
            drawPtr = m_alphaDynamicMultiDrawObjectPtr;
            shaderIndex = MyShaderPassIndex::kDynamicAlphaBlendShaderIndex;
        }
        else
        {
            drawPtr = m_dynamicMultiDrawObjectPtr;
            shaderIndex = MyShaderPassIndex::kDynamicShaderIndex;
        }

        if (drawPtr)
        {
            // we need to link the array buffer and texture array to shader state,
            // as well as cdi & rdi before rendering (we ignore rdi for testing 1/9/2015)
            OGLShaderPtr currentShader = std::dynamic_pointer_cast<OGLShader, Shader>(rsi.GetShaderOverride());
            if (!currentShader)
            {
                currentShader = std::dynamic_pointer_cast<OGLShader, Shader>(m_cachedShaderPtrs[shaderIndex]);
            }

            if (currentShader)
            {
                m_renderer.SetActiveShaderProgram(currentShader);
                currentShader->SetUniform("viewCam", cdi.viewMat);
                currentShader->SetUniform("projCam", cdi.projMat);

                //ErrorUtilities::CheckGLErrors();

                // we set the shader here as well because static multidraw type needs
                // to access uniform 'worldMat' to set it per object reference
                drawPtr->SetShader(currentShader);
                // the array buffer and texture array linking to shader state is done
                // within this call
                RenderCheckOK(drawPtr->Render());
            }
        }

        return true;
    }

    bool OGLBatchDrawEffect::CheckBuffers()
    {
        if (!m_bSetStaticPackages)
        {
            m_bSetStaticPackages = true;

            for (auto& dataPtr : m_staticPackages)
            {
                if (!dataPtr->HasAlpha())
                {
                    if (m_bIsFirstStatic)
                    {
                        m_staticMultiDrawObjectPtr->Add(dataPtr, IMultiDraw::FirstToken());
                        m_bIsFirstStatic = false;
                    }
                    else
                    {
                        m_staticMultiDrawObjectPtr->Add(dataPtr);
                    }
                }
            }

            m_staticMultiDrawObjectPtr->AddFinish();

            // alpha blended static meshes
            for (auto& dataPtr : m_staticPackages)
            {
                if (dataPtr->HasAlpha())
                {
                    if (m_bIsFirstAlphaStatic)
                    {
                        m_alphaStaticMultiDrawObjectPtr->Add(dataPtr, IMultiDraw::FirstToken());
                        m_bIsFirstAlphaStatic = false;
                    }
                    else
                    {
                        m_alphaStaticMultiDrawObjectPtr->Add(dataPtr);
                    }
                }
            }

            m_alphaStaticMultiDrawObjectPtr->AddFinish();
        }

        if (!m_bSetDynamicPackages)
        {
            m_bSetDynamicPackages = true;

            for (auto& dataPtr : m_dynamicPackages)
            {
                if (!dataPtr->HasAlpha())
                {
                    if (m_bIsFirstDynamic)
                    {
                        m_dynamicMultiDrawObjectPtr->Add(dataPtr, IMultiDraw::FirstToken());
                        m_bIsFirstDynamic = false;
                    }
                    else
                    {
                        m_dynamicMultiDrawObjectPtr->Add(dataPtr);
                    }
                }
            }

            m_dynamicMultiDrawObjectPtr->AddFinish();

            // alpha blended dynamic meshes
            for (auto& dataPtr : m_dynamicPackages)
            {
                if (dataPtr->HasAlpha())
                {
                    if (m_bIsFirstAlphaDynamic)
                    {
                        m_alphaDynamicMultiDrawObjectPtr->Add(dataPtr, IMultiDraw::FirstToken());
                        m_bIsFirstAlphaDynamic = false;
                    }
                    else
                    {
                        m_alphaDynamicMultiDrawObjectPtr->Add(dataPtr);
                    }
                }
            }

            m_alphaDynamicMultiDrawObjectPtr->AddFinish();
        }

        return true;
    }
}
