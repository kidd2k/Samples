// VKNBatchDrawEffect.h
// Vulkan implementation derived from BatchDrawEffect
// Using the power of TextureArrays and large vertex buffers, this type is intended to allow
// rendering of many animated vertex objects with fewer draw calls
#pragma once
#ifndef VKN_BATCH_DRAW_EFFECT_H
#define VKN_BATCH_DRAW_EFFECT_H

#include "VKNDescriptorSetBuilder.h"
#include "VKNPipelineBuilder.h"
#include "BufferMemoryHelper.h"
#include "VKNEffectState.h"

#include "../Renderer/BatchDrawEffect.h"

namespace GamePrototype
{
	class IVKNMultiDraw;
	typedef std::shared_ptr<IVKNMultiDraw> VKNMultiDrawPtr;

	class VKNBatchDrawEffect : public BatchDrawEffect
	{
	public:
		explicit VKNBatchDrawEffect(const EffectInitInfo&);

	protected:

		// IEffect
		virtual ~VKNBatchDrawEffect() override;
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
		virtual bool PostSceneGraph() override;

	private:

		bool DrawStaticPass(const Graphics::CameraDrawInfo&, const Graphics::RenderStateInfo&);
		bool DrawDynamicPass(const Graphics::CameraDrawInfo&, const Graphics::RenderStateInfo&);

		bool CheckBuffers();

		bool UpdateUniforms(const Graphics::CameraDrawInfo&, const Graphics::RenderStateInfo&);

		bool CreateMemBufferHelpers();

		bool SetupStaticShadowShader(const VKNShaderPtr&, const VKNEffectState&);
		bool SetupStaticShader(const VKNShaderPtr&, const VKNEffectState&);
		bool SetupDynamicShadowShader(const VKNShaderPtr&, const VKNEffectState&);
		bool SetupDynamicShader(const VKNShaderPtr&, const VKNEffectState&);
		bool SetupStaticAlphaBlendShader(const VKNShaderPtr&, const VKNEffectState&);
		bool SetupDynamicAlphaBlendShader(const VKNShaderPtr&, const VKNEffectState&);

		// sanity testing
		bool FakePrePass(int, const EffectStatePtr&);
		bool FakeDraw(const Graphics::CameraDrawInfo&, const Graphics::RenderStateInfo&);
		bool FakePostPass();
		bool FakeSetupShader(const VKNShaderPtr&, const VKNEffectState&);
		int FakeNumPasses() const { return 1; }

		VKNMultiDrawPtr							m_dynamicMultiDrawObjectPtr;
		VKNMultiDrawPtr							m_staticMultiDrawObjectPtr;
		VKNMultiDrawPtr							m_alphaDynamicMultiDrawObjectPtr;
		VKNMultiDrawPtr							m_alphaStaticMultiDrawObjectPtr;
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
		std::vector<DrawPackageDataPtr>			m_staticPackages;
		std::vector<DrawPackageDataPtr>			m_dynamicPackages;
		bool									m_bSetStaticPackages;
		bool									m_bSetDynamicPackages;

		// this type does need its own pipeline to bind to shaders used locally
		VKNPipelineBuilder						m_pipelineBuilder;

		// DescriptorSet (texturePack & uniform buffer for shader vars)
		// binding = 0
		struct UniformData
		{
			/* 
				TODO: whatever uniform data each shader needs can be added to this struct,
				and copied to mapped buffer memory and bound to the command buffer.
				Just make sure your alignments (OpenGL std 140?) are correct.
			*/
			float viewCam[Math::mat4::MAT4_SIZE];
			float projCam[Math::mat4::MAT4_SIZE];

			UniformData()
			:
			viewCam{},
			projCam{}
			{
				memcpy(viewCam, Math::mat4::Identity().Get(), Math::mat4::MAT4_SIZE*sizeof(float));
				memcpy(projCam, Math::mat4::Identity().Get(), Math::mat4::MAT4_SIZE*sizeof(float));
			}

			static bool Register(BufferMemoryHelper<UniformData>& bmh)
			{
				CheckOK(bmh.RegisterMember(IBufferMemoryHelper::kMemberType_Mat4, "viewCam", offsetof(UniformData, viewCam)));
				CheckOK(bmh.RegisterMember(IBufferMemoryHelper::kMemberType_Mat4, "projCam", offsetof(UniformData, projCam)));
				return true;
			}
		};

		BufferMemoryHelperPtr m_uniformMemHelperPtr;

		static const int VERTEX_BUFFER_BIND_ID = 0;
	};
}
#endif // VKN_BATCH_DRAW_EFFECT_H
