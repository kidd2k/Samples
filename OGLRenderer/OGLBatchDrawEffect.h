// OGLBatchDrawEffect.h
// OpenGL implementation derived from BatchDrawEffect
// Using the power of TextureArrays and large vertex arrays, this type is intended to allow
// rendering of many animated vertex objects as one draw call
#pragma once
#ifndef OGL_BATCH_DRAW_EFFECT_H
#define OGL_BATCH_DRAW_EFFECT_H

#include "../Renderer/BatchDrawEffect.h"

namespace GamePrototype
{
	class OGLBatchDrawEffect : public BatchDrawEffect
	{
	public:
		explicit OGLBatchDrawEffect(const EffectInitInfo&);

	protected:

		// IEffect
		virtual bool Init() override;
		virtual int NumPasses() const override;
		virtual bool PrePass(int, const EffectStatePtr&) override;
		virtual bool Collect(const Graphics::RenderObjectPtr&) override;
		virtual bool Collect(const std::vector<Graphics::RenderObjectPtr>&) override;
		virtual bool Collect(const Graphics::SceneNodePtr&) override;
		virtual bool Draw(const Graphics::CameraDrawInfo&, const Graphics::RenderStateInfo&, const EffectStatePtr&) override;
		virtual bool PostPass(const EffectStatePtr&) override;
		virtual void ClearForNextFrame() override;
		virtual int GetID() const override;
		virtual void Free() override;
		virtual int GetEffectType() const override;
		virtual unsigned int GetNumRenderTargets() const override;
		virtual RenderTargetInfo GetRenderTarget(size_t) const override;
		virtual void SetVariable(size_t shaderId, const char*, float val) override;
		virtual void SetVariable(size_t shaderId, const char*, size_t, float*) override;
		virtual float* GetVariable(size_t shaderId, const char*) const override;
		virtual void SetEffectData(size_t index, const EffectData&) override;
		virtual bool PostSceneGraph() override;

	private:

		bool DrawStaticPass(const Graphics::CameraDrawInfo&, const Graphics::RenderStateInfo&);
		bool DrawDynamicPass(const Graphics::CameraDrawInfo&, const Graphics::RenderStateInfo&);

		bool CheckBuffers();

		MultiDrawPtr							m_dynamicMultiDrawObjectPtr;
		MultiDrawPtr							m_staticMultiDrawObjectPtr;
		MultiDrawPtr							m_alphaDynamicMultiDrawObjectPtr;
		MultiDrawPtr							m_alphaStaticMultiDrawObjectPtr;
		int										m_id;
		int										m_currentPass;
		const int								m_effectType;			// IEffectMgr::EffectType enum
		std::vector<ShaderPtr>					m_cachedShaderPtrs;
		bool									m_bIsFirstDynamic;
		bool									m_bIsFirstStatic;
		bool									m_bIsFirstAlphaDynamic;
		bool									m_bIsFirstAlphaStatic;
		bool									m_bIsInitialized;
		TexturePackPtr							m_texPackPtr;
		TexturePackPtr							m_alphaTexPackPtr;
		std::vector<DrawPackageDataPtr>			m_staticPackages;
		std::vector<DrawPackageDataPtr>			m_dynamicPackages;
		bool									m_bSetStaticPackages;
		bool									m_bSetDynamicPackages;
	};
}

#endif // OGL_BATCH_DRAW_EFFECT_H