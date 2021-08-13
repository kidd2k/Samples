// VKNBatchDrawEffect.cpp
#include "stdafx.h"
#include "VKNBatchDrawEffect.h"
#include "VKNShader.h"
#include "VKNTexturePack.h"
#include "VKNMultiDrawObject.h"
#include "VKNMultiDrawInstancedObject.h"
#include "VulkanRenderContext.h"
#include "VulkanSwapChain.h"
#include "VKNVertexInfo.h"
#include "VulkanHelper.h"
#include "VKNCommonUniformBuffers.h"
#include "VKNCommandBuffer.h"
#include "IVKNMultiDraw.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/EffectInitInfo.h"
#include "../Renderer/IEffectMgr.h"

//#define KIRBY_SANITY

namespace
{
	std::vector<VkDynamicState> s_dynamicStateEnables =
	{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_DEPTH_BIAS
	};

	// statically #define'd in shader (DeferredRenderLightPass_NM_Effect_FS)
	static const int MAX_LIGHTS = 9;

	// struct ShadowMatBuf
	// converts a vector from the coordinate space of the viewer to the surface
	// to the coordinate space between the light and the surface so a comparison
	// can be made to determine if the surface is shadowed

	const size_t shadowMatSize = Math::mat4::MAT4_SIZE * MAX_LIGHTS;
	struct ShadowMatBuf // shadowmap specific uniform (StaticShadowMapShader_VS)
	{
		float shadowMatrix[shadowMatSize];

		ShadowMatBuf()
		:
		shadowMatrix{}
		{
		}
	};
}

namespace GamePrototype
{
	// we have now defined this in multiple places.
	// Perhaps it needs either one common shared location
	// or a rename?
	VkPipelineDynamicStateCreateInfo s1_dynamicState =
		VulkanHelper::InitPipelineDynamicStateCreateInfo(
		s_dynamicStateEnables.data(),
		static_cast<uint32_t>(s_dynamicStateEnables.size()),
		0);

	const VkVertexInputBindingDescription s_vertInputBindingDesc = VKNVertexInfo::GetVertexBinding();

	const VKNVertexInfo::VertexAttributes s_vertAttrs = VKNVertexInfo::GetVertexAttributes();

	const VkPipelineVertexInputStateCreateInfo s_vertexInputState =
	{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,	// VkStructureType
		0,	// pNext
		0,	// flags
		1,	// bindingDescriptionCount
		&s_vertInputBindingDesc,	// pVertexBindingDescriptions
		static_cast<uint32_t>(s_vertAttrs.size()),	// vertexAttributeDecriptionCount
		s_vertAttrs.data()	// pVertexAttributeDescriptions
	};

	VKNBatchDrawEffect::VKNBatchDrawEffect(const EffectInitInfo& info)
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
	m_bSetDynamicPackages(false),
	m_pipelineBuilder(static_cast<VulkanRenderContext&>(*info.m_renderer.GetRenderContext()))
	{
	}

	VKNBatchDrawEffect::~VKNBatchDrawEffect()
	{
	}

	bool VKNBatchDrawEffect::Init()
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
				VulkanRenderContext& vknContext = static_cast<VulkanRenderContext&>(*contextPtr);

				int texWidth = TexturePack::s_kDefaultTextureWidth;
				int texHeight = TexturePack::s_kDefaultTextureHeight;
				int texMipMapLevels = TexturePack::s_kTextureMipMapLevels;
				auto& queryRenderPtr = vknContext.GetQueryRenderer();
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

				// NOTE: unlike the current OpenGL implementation,
				// Vulkan IMultiDraw types share the same texture RGBA format 
				// regardless of whether mesh objects use the alpha channel
				m_texPackPtr = std::make_shared<VKNTexturePack>(vknContext,
					texWidth,
					texHeight,
					texMipMapLevels,
					TexturePack::s_kMaxTextures,
					VK_FORMAT_B8G8R8A8_UNORM);	// NOTE: this should NOT perform alpha blending (BGR8 format unsupported)

				RenderCheckOK(m_texPackPtr != TexturePackPtr());
				RenderCheckOK(m_texPackPtr->Init());

				m_dynamicMultiDrawObjectPtr = std::make_shared<VKNMultiDrawObject>(vknContext);
				RenderCheckOK(m_dynamicMultiDrawObjectPtr != MultiDrawPtr());
				m_dynamicMultiDrawObjectPtr->SetTexturePack(m_texPackPtr);
				RenderCheckOK(m_dynamicMultiDrawObjectPtr->Initialize());

				m_alphaDynamicMultiDrawObjectPtr = std::make_shared<VKNMultiDrawObject>(vknContext);
				RenderCheckOK(m_alphaDynamicMultiDrawObjectPtr != MultiDrawPtr());
				m_alphaDynamicMultiDrawObjectPtr->SetTexturePack(m_texPackPtr);
				RenderCheckOK(m_alphaDynamicMultiDrawObjectPtr->Initialize());
				m_alphaDynamicMultiDrawObjectPtr->SetAlphaBlending(true);

				//m_staticMultiDrawObjectPtr = std::make_shared<VKNMultiDrawObject>(vknContext);
				m_staticMultiDrawObjectPtr = std::make_shared<VKNMultiDrawInstancedObject>(vknContext);
				RenderCheckOK(m_staticMultiDrawObjectPtr != MultiDrawPtr());
				m_staticMultiDrawObjectPtr->SetTexturePack(m_texPackPtr);
				RenderCheckOK(m_staticMultiDrawObjectPtr->Initialize());

				m_alphaStaticMultiDrawObjectPtr = std::make_shared<VKNMultiDrawInstancedObject>(vknContext);
				RenderCheckOK(m_alphaStaticMultiDrawObjectPtr != MultiDrawPtr());
				m_alphaStaticMultiDrawObjectPtr->SetTexturePack(m_texPackPtr);
				RenderCheckOK(m_alphaStaticMultiDrawObjectPtr->Initialize());
				m_alphaStaticMultiDrawObjectPtr->SetAlphaBlending(true);

				RenderCheckOK(CreateMemBufferHelpers());

				m_bIsInitialized = true;
			}
		}

		return m_bIsInitialized;
	}

	int VKNBatchDrawEffect::NumPasses() const
	{
#if defined KIRBY_SANITY
		return FakeNumPasses();
#endif
		/*
		for each type of draw pass, whether it be shadows, opaque geometry,
		or translucent geometry, we have static and dynamic data.
		So let's go with 2 for now
		*/

		return kMaxPasses; /* one static pass, one dynamic pass*/
	}

	bool VKNBatchDrawEffect::PrePass(int pass, const EffectStatePtr& esPtr)
	{
		if (!m_bIsDrawActive)
		{
			return true;
		}

#if defined KIRBY_SANITY
		RenderCheckOK(FakePrePass(pass, esPtr));
		return true;
#endif

		// TODO: RenderStateInfo should be passed here too? PrePass->Draw->PostPass remember?

		m_currentPass = pass;
		//assert(m_currentPass < m_materialList.size());
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

			VulkanRenderContext& context = static_cast<VulkanRenderContext&>(*m_renderer.GetRenderContext());
			uint32_t swapChainCount = context.GetSwapChainImageCount();
			assert(swapChainCount > 0);

			if(m_uniformMemHelperPtr)
			{
				for(auto& cachedShaderPtr : m_cachedShaderPtrs)
				{
					if(cachedShaderPtr)
					{
						VKNShaderPtr shaderPtr = std::dynamic_pointer_cast<VKNShader, Shader>(cachedShaderPtr);
						if(shaderPtr)
						{
							RenderCheckOK(shaderPtr->Init(swapChainCount));
							shaderPtr->SetBufferMemoryHelper(m_uniformMemHelperPtr);
						}
					}
				}
			}

			const VKNEffectState& effectState = (esPtr->GetType() == EffectState::kDerived) ?
				static_cast<const VKNEffectState&>(*esPtr) :
				VKNEffectState(nullptr);

			VKNShaderPtr shaderPtr = std::dynamic_pointer_cast<VKNShader, Shader>(m_cachedShaderPtrs[kDynamicShaderIndex]);
			if(shaderPtr)
			{
				RenderCheckOK(SetupDynamicShader(shaderPtr, effectState));
			}

			shaderPtr = std::dynamic_pointer_cast<VKNShader, Shader>(m_cachedShaderPtrs[kStaticShaderIndex]);
			if (shaderPtr)
			{
				RenderCheckOK(SetupStaticShader(shaderPtr, effectState));
			}

			shaderPtr = std::dynamic_pointer_cast<VKNShader, Shader>(m_cachedShaderPtrs[kStaticShadowShaderIndex]);
			if (shaderPtr)
			{
				RenderCheckOK(SetupStaticShadowShader(shaderPtr, effectState));
			}

			shaderPtr = std::dynamic_pointer_cast<VKNShader, Shader>(m_cachedShaderPtrs[kDynamicShadowShaderIndex]);
			if(shaderPtr)
			{
				RenderCheckOK(SetupDynamicShadowShader(shaderPtr, effectState));
			}

			shaderPtr = std::dynamic_pointer_cast<VKNShader, Shader>(m_cachedShaderPtrs[kStaticAlphaBlendShaderIndex]);
			if (shaderPtr)
			{
				RenderCheckOK(SetupStaticAlphaBlendShader(shaderPtr, effectState));
			}

			shaderPtr = std::dynamic_pointer_cast<VKNShader, Shader>(m_cachedShaderPtrs[kDynamicAlphaBlendShaderIndex]);
			if (shaderPtr)
			{
				RenderCheckOK(SetupDynamicAlphaBlendShader(shaderPtr, effectState));
			}
		}

		return true;
	}

	bool VKNBatchDrawEffect::Collect(const Graphics::RenderObjectPtr& objPtr)
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

	bool VKNBatchDrawEffect::Collect(const std::vector<Graphics::RenderObjectPtr>& list)
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

	bool VKNBatchDrawEffect::Collect(const Graphics::SceneNodePtr&)
	{
		// NOTE: LightCollection now stores all lights. Since this is just a typical
		// geometry pass of a deferred render, lighting info is irrelevant right now.
		return false;
	}

	bool VKNBatchDrawEffect::Draw(const Graphics::CameraDrawInfo& cdi, const Graphics::RenderStateInfo& rsi, const EffectStatePtr&)
	{
		if (!m_bIsDrawActive)
		{
			return true;
		}

#if defined KIRBY_SANITY
		RenderCheckOK(FakeDraw(cdi, rsi));
		return true;
#endif

		Graphics::RenderStateInfo mod_rsi(rsi);

		// NOTE: this effect is not designed to handle postprocessing (yet).
		if(rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kPostProcess)
		{
			return true;
		}

		if (m_currentPass == kFirstPass)
		{
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

	bool VKNBatchDrawEffect::PostPass(const EffectStatePtr&)
	{
		if (!m_bIsDrawActive)
		{
			return true;
		}

#if defined KIRBY_SANITY
		RenderCheckOK(FakePostPass());
		return true;
#endif

		if (m_currentPass > -1)
		{
			return true;
		}

		return false;
	}

	void VKNBatchDrawEffect::ClearForNextFrame()
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

	int VKNBatchDrawEffect::GetID() const
	{
		return m_id;
	}

	void VKNBatchDrawEffect::Free()
	{
		VulkanRenderContext& ctx = static_cast<VulkanRenderContext&>(*m_renderer.GetRenderContext());

		// NOTE: we do this in multiple different places now. Perhaps it is time to merge into common?
		vkQueueWaitIdle(ctx.GetGraphicsQueue());
		vkDeviceWaitIdle(ctx.GetDevice());
		// recorded command buffers need to be released at this point so we can release Vulkan objects
		// previously recorded within it
		VKNCommandBufferPtrList* pCmdList = ctx.GetCommandBuffers();
		for (auto& cmdBufferPtr : *pCmdList)
		{
			cmdBufferPtr->ResetCommandBuffer();
		}

		if(m_dynamicMultiDrawObjectPtr)
		{
			m_dynamicMultiDrawObjectPtr->Shutdown();
			m_dynamicMultiDrawObjectPtr = nullptr;
		}

		if(m_staticMultiDrawObjectPtr)
		{
			m_staticMultiDrawObjectPtr->Shutdown();
			m_staticMultiDrawObjectPtr = nullptr;
		}

		if(m_alphaDynamicMultiDrawObjectPtr)
		{
			m_alphaDynamicMultiDrawObjectPtr->Shutdown();
			m_alphaDynamicMultiDrawObjectPtr = nullptr;
		}

		if(m_alphaStaticMultiDrawObjectPtr)
		{
			m_alphaStaticMultiDrawObjectPtr->Shutdown();
			m_alphaStaticMultiDrawObjectPtr = nullptr;
		}

		m_cachedShaderPtrs.clear();

		m_texPackPtr = nullptr;
		m_staticPackages.clear();
		m_dynamicPackages.clear();

		m_pipelineBuilder.Reset();

		m_uniformMemHelperPtr = nullptr;
	}

	int VKNBatchDrawEffect::GetEffectType() const
	{
		return m_effectType;
	}

	unsigned int VKNBatchDrawEffect::GetNumRenderTargets() const
	{
		return 0;
	}

	RenderTargetInfo VKNBatchDrawEffect::GetRenderTarget(size_t) const
	{
		return RenderTargetInfo();
	}

	void VKNBatchDrawEffect::SetVariable(size_t shaderId, const char* pName, float val)
	{
		assert(!"Not implemented yet!");
	}

	void VKNBatchDrawEffect::SetVariable(size_t shaderId, const char* pName, size_t count, float* pValues)
	{
		assert(!"Not implemented yet!");
	}

	float* VKNBatchDrawEffect::GetVariable(size_t shaderId, const char*) const
	{
		assert(!"Not implemented yet!");
		return 0;
	}

	void VKNBatchDrawEffect::SetEffectData(size_t index, const EffectData&)
	{
		assert(!"Not implemented yet!");
	}

	bool VKNBatchDrawEffect::PostSceneGraph()
	{
		return CheckBuffers();
	}

	bool VKNBatchDrawEffect::DrawStaticPass(const Graphics::CameraDrawInfo& cdi, const Graphics::RenderStateInfo& rsi)
	{
		int shaderIndex = MyShaderPassIndex::kStaticShaderIndex;
		MultiDrawPtr drawPtr;

		if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kTranslucent)
		{
			drawPtr = m_alphaStaticMultiDrawObjectPtr;
			shaderIndex = MyShaderPassIndex::kStaticAlphaBlendShaderIndex;
		}
		else if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kShadows)
		{
			drawPtr = m_staticMultiDrawObjectPtr;
			shaderIndex = MyShaderPassIndex::kStaticShadowShaderIndex;
		}
		else
		{
			drawPtr = m_staticMultiDrawObjectPtr;
			shaderIndex = MyShaderPassIndex::kStaticShaderIndex;
		}

		if (drawPtr)
		{
			VKNShaderPtr currentShader = std::dynamic_pointer_cast<VKNShader, Shader>(rsi.GetShaderOverride());
			if (!currentShader)
			{
				currentShader = std::dynamic_pointer_cast<VKNShader, Shader>(m_cachedShaderPtrs[shaderIndex]);
			}

			if (currentShader)
			{
				m_renderer.SetActiveShaderProgram(currentShader);
				// we set the shader here as well because static multidraw type needs
				// to access uniform 'worldMat' to set it per object reference
				drawPtr->SetShader(currentShader);

				UpdateUniforms(cdi, rsi);

				// the array buffer and texture array linking to shader state is done
				// within this call
				RenderCheckOK(drawPtr->Render());
			}
		}

		return true;
	}

	bool VKNBatchDrawEffect::DrawDynamicPass(const Graphics::CameraDrawInfo& cdi, const Graphics::RenderStateInfo& rsi)
	{
		int shaderIndex = MyShaderPassIndex::kDynamicShaderIndex;
		MultiDrawPtr drawPtr;

		if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kTranslucent)
		{
			drawPtr = m_alphaDynamicMultiDrawObjectPtr;
			shaderIndex = MyShaderPassIndex::kDynamicAlphaBlendShaderIndex;
		}
		else if (rsi.GetHint() == Graphics::RenderStateInfo::RenderPassHint::kShadows)
		{
			drawPtr = m_dynamicMultiDrawObjectPtr;
			shaderIndex = MyShaderPassIndex::kDynamicShadowShaderIndex;
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
			VKNShaderPtr currentShader = std::dynamic_pointer_cast<VKNShader, Shader>(rsi.GetShaderOverride());
			if (!currentShader)
			{
				currentShader = std::dynamic_pointer_cast<VKNShader, Shader>(m_cachedShaderPtrs[shaderIndex]);
			}

			if (currentShader)
			{
				// attempt to flip y vertices. Data as specified seems to be upside down on Vulkan,
				// probably due to the clip space origin being in the upper left corner as opposed
				// to the OpenGL lower left corner.
				Math::mat4 yScaleMat;
				yScaleMat[5] = -1.f;
				Graphics::CameraDrawInfo temp_cdi = cdi;
				temp_cdi.projMat = temp_cdi.projMat * yScaleMat;
				UpdateUniforms(/*cdi,*/temp_cdi, rsi);
				//UpdateUniforms(cdi, rsi);

				// start recording

				m_renderer.SetActiveShaderProgram(currentShader);

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

	bool VKNBatchDrawEffect::CheckBuffers()
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

			RenderCheckOK(m_staticMultiDrawObjectPtr->Update());

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

			RenderCheckOK(m_alphaStaticMultiDrawObjectPtr->Update());
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

			RenderCheckOK(m_dynamicMultiDrawObjectPtr->Update());

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

			RenderCheckOK(m_alphaDynamicMultiDrawObjectPtr->Update());
		}

		return true;
	}

	bool VKNBatchDrawEffect::CreateMemBufferHelpers()
	{
		VulkanRenderContext& context = static_cast<VulkanRenderContext&>(*m_renderer.GetRenderContext());

		if (!m_uniformMemHelperPtr)
		{
			m_uniformMemHelperPtr = std::make_shared<BufferMemoryHelper<UniformData>>(context);
			RenderCheckOK(m_uniformMemHelperPtr && m_uniformMemHelperPtr->Init(IBufferMemoryHelper::kBufferType_Uniform));

			// register our desired types
			RenderCheckOK(UniformData::Register(dynamic_cast<BufferMemoryHelper<UniformData>&>(*m_uniformMemHelperPtr)));
		}

		return true;
	}

	bool VKNBatchDrawEffect::UpdateUniforms(const Graphics::CameraDrawInfo& cdi, const Graphics::RenderStateInfo&)
	{
		if (m_uniformMemHelperPtr)
		{
			RenderCheckOK(m_uniformMemHelperPtr->SetValue("viewCam", cdi.viewMat.Get()));
			RenderCheckOK(m_uniformMemHelperPtr->SetValue("projCam", cdi.projMat.Get()));
			RenderCheckOK(m_uniformMemHelperPtr->CopyToDevice());
		}

		return true;
	}

	bool VKNBatchDrawEffect::SetupStaticShadowShader(const VKNShaderPtr& shaderPtr, const VKNEffectState& es)
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			VulkanHelper::InitPipelineInputAssemblyStateCreateInfo(
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			0,
			VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			VulkanHelper::InitPipelineRasterizationStateCreateInfo(
			VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_BACK_BIT,
			VK_FRONT_FACE_COUNTER_CLOCKWISE,
			0,
			VK_FALSE);

		// we are now using dynamic depth bias (vkCmdSetDepthBias) for shadows, this must be set as well
		rasterizationState.depthBiasEnable = VK_TRUE;

		// No color attachment in a depth buffer only pipeline
		VkPipelineColorBlendStateCreateInfo colorBlendState = 
			VulkanHelper::InitPipelineColorBlendStateCreateInfo(0, nullptr);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			VulkanHelper::InitPipelineDepthStencilStateCreateInfo(
			VK_TRUE,
			VK_TRUE,
			VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			VulkanHelper::InitPipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			VulkanHelper::InitPipelineMultisampleStateCreateInfo(
			VK_SAMPLE_COUNT_1_BIT,
			0);

		if (shaderPtr)
		{
			assert(m_uniformMemHelperPtr);
			shaderPtr->SetBufferMemoryHelper(m_uniformMemHelperPtr);

			VKNDescriptorSetBuilder& dsBuilder = shaderPtr->GetDescriptorSetBuilder();

			//layout(std140, binding = 0) uniform ViewProjLight
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0);

			//layout(std140, binding = 1) uniform Instances
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				1);

			dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				0,
				m_uniformMemHelperPtr->GetBufferObject());

			// remember that world matrix instances were collected as part of VKNMultiDrawInstancedObject.
			// You want those matrices for shadow rendering too
			assert(m_staticMultiDrawObjectPtr);
			if (m_staticMultiDrawObjectPtr)
			{
				VKNMultiDrawInstancedObject* pDrawInstancedObject = dynamic_cast<VKNMultiDrawInstancedObject*>(m_staticMultiDrawObjectPtr.get());
				if (pDrawInstancedObject)
				{
					auto& instBuffer = pDrawInstancedObject->GetInstanceBuffer();
					dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
						1,
						0,
						instBuffer);
				}
			}

			auto& depthOnlyFBOs = es.GetDepthOnlyFrameBufferObjects();
			assert(!depthOnlyFBOs.empty());
			if(depthOnlyFBOs.empty())
			{
				return false;
			}

			assert(depthOnlyFBOs[0]->renderPass != VK_NULL_HANDLE);

			// although when constructing VkCommandBuffers we use the actual VkRenderPass
			// from one of the light shadowmap framebuffers, we use this 'fake' FrameBufferObject
			// to set up the pipeline. The VkRenderPass should possess the same properties for all
			// shadowmaps.

			VKNPipelineBuilder& pipelineBuilder = shaderPtr->GetPipelineBuilder();
			pipelineBuilder.Add(inputAssemblyState).
				Add(s_vertexInputState).
				Add(rasterizationState).
				Add(colorBlendState).
				Add(depthStencilState).
				Add(viewportState).
				Add(multisampleState).
				Add(s1_dynamicState).
				Add(depthOnlyFBOs[0]->renderPass);

			return true;
		}

		return false;
	}

	bool VKNBatchDrawEffect::SetupStaticShader(const VKNShaderPtr& shaderPtr, const VKNEffectState& es)
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			VulkanHelper::InitPipelineInputAssemblyStateCreateInfo(
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			0,
			VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			VulkanHelper::InitPipelineRasterizationStateCreateInfo(
			VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_BACK_BIT, /*VK_CULL_MODE_NONE?*/
			VK_FRONT_FACE_COUNTER_CLOCKWISE,
			0,
			VK_FALSE);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			VulkanHelper::InitPipelineColorBlendAttachmentState(
			0xf,
			VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			VulkanHelper::InitPipelineColorBlendStateCreateInfo(0, nullptr);

		// tricky... BatchDrawEffect was designed for OpenGL to not have to worry about
		// which framebuffer output is rendered to because the state machine handled that.
		// In Vulkan, we don't have that luxury. So, we check to see if a GBuffer FBO is present.
		// If not, we fall back to the default FBO if present.
		const FrameBufferObject* pFBO = nullptr;

		auto& gBufferFBOs = es.GetGBufferFrameBufferObjects();
		if(!gBufferFBOs.empty())
		{
			pFBO = gBufferFBOs[0];
		}

		if(!pFBO)
		{
			auto& defaultFBOs = es.GetDefaultFrameBufferObjects();
			assert(!defaultFBOs.empty());
			if(!defaultFBOs.empty())
			{
				pFBO = defaultFBOs[0];
			}
		}

		assert(pFBO);
		if(!pFBO)
		{
			return false;
		}

		std::vector<VkPipelineColorBlendAttachmentState> colorBlendStates;

		if(pFBO)
		{
			for(auto& entry : pFBO->textureObjectInfos)
			{
				colorBlendStates.push_back(entry.colorBlendState);
			}

			colorBlendState =
				VulkanHelper::InitPipelineColorBlendStateCreateInfo(
				static_cast<uint32_t>(colorBlendStates.size()),
				colorBlendStates.data());
		}

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			VulkanHelper::InitPipelineDepthStencilStateCreateInfo(
			VK_TRUE,
			VK_TRUE,
			VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			VulkanHelper::InitPipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			VulkanHelper::InitPipelineMultisampleStateCreateInfo(
			VK_SAMPLE_COUNT_1_BIT,
			0);

		if (shaderPtr)
		{
			VKNDescriptorSetBuilder& dsBuilder = shaderPtr->GetDescriptorSetBuilder();

			//layout(std140, binding = 0) uniform ProjViewBuf
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0);

			//layout(std140, binding = 2) uniform Instances
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				2);

			//layout(binding = 1) uniform sampler2DArray textureMaps;
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1);

			if (m_uniformMemHelperPtr)
			{
				dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					0,
					0,
					m_uniformMemHelperPtr->GetBufferObject());
			}

			assert(m_staticMultiDrawObjectPtr);
			if (m_staticMultiDrawObjectPtr)
			{
				VKNMultiDrawInstancedObject* pDrawInstancedObject = dynamic_cast<VKNMultiDrawInstancedObject*>(m_staticMultiDrawObjectPtr.get());
				if(pDrawInstancedObject)
				{
					auto& instBuffer = pDrawInstancedObject->GetInstanceBuffer();
					dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
						2,
						0,
						instBuffer);
				}			
			}

			if (m_texPackPtr)
			{
				VKNTexturePackPtr vknTexPackPtr = std::dynamic_pointer_cast<VKNTexturePack, TexturePack>(m_texPackPtr);
				if (vknTexPackPtr)
				{
					TextureObject* pTexObj = vknTexPackPtr->GetTextureHandle();
					assert(pTexObj);
					if(pTexObj)
					{
						dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							1,
							0,
							*pTexObj);
					}
				}
			}

			assert(pFBO->renderPass);
			VKNPipelineBuilder& pipelineBuilder = shaderPtr->GetPipelineBuilder();
			pipelineBuilder.Add(inputAssemblyState).
				Add(s_vertexInputState).
				Add(rasterizationState).
				Add(colorBlendState).
				Add(depthStencilState).
				Add(viewportState).
				Add(multisampleState).
				Add(s1_dynamicState).
				Add(pFBO->renderPass);

			return true;
		}

		return false;
	}

	bool VKNBatchDrawEffect::SetupDynamicShadowShader(const VKNShaderPtr& shaderPtr, const VKNEffectState& es)
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			VulkanHelper::InitPipelineInputAssemblyStateCreateInfo(
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			0,
			VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			VulkanHelper::InitPipelineRasterizationStateCreateInfo(
			VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_BACK_BIT,
			VK_FRONT_FACE_COUNTER_CLOCKWISE,
			0,
			VK_FALSE);

		// no color state in a depth only pipeline
		VkPipelineColorBlendStateCreateInfo colorBlendState = 
			VulkanHelper::InitPipelineColorBlendStateCreateInfo(0, nullptr);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			VulkanHelper::InitPipelineDepthStencilStateCreateInfo(
			VK_TRUE,
			VK_TRUE,
			VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			VulkanHelper::InitPipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			VulkanHelper::InitPipelineMultisampleStateCreateInfo(
			VK_SAMPLE_COUNT_1_BIT,
			0);

		if (shaderPtr)
		{
			assert(m_uniformMemHelperPtr);
			shaderPtr->SetBufferMemoryHelper(m_uniformMemHelperPtr);

			VKNDescriptorSetBuilder& dsBuilder = shaderPtr->GetDescriptorSetBuilder();

			//layout(std140, binding = 0) uniform ViewProjCam
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0);

			dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				0,
				m_uniformMemHelperPtr->GetBufferObject());

			auto& depthFBOs = es.GetDepthOnlyFrameBufferObjects();
			assert(!depthFBOs.empty());
			if(depthFBOs.empty())
			{
				return false;
			}

			assert(depthFBOs[0]->renderPass);

			VKNPipelineBuilder& pipelineBuilder = shaderPtr->GetPipelineBuilder();
			pipelineBuilder.Add(inputAssemblyState).
				Add(s_vertexInputState).
				Add(rasterizationState).
				Add(colorBlendState).
				Add(depthStencilState).
				Add(viewportState).
				Add(multisampleState).
				Add(s1_dynamicState).
				Add(depthFBOs[0]->renderPass);

			return true;
		}

		return false;
	}

	bool VKNBatchDrawEffect::SetupDynamicShader(const VKNShaderPtr& shaderPtr, const VKNEffectState& es)
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			VulkanHelper::InitPipelineInputAssemblyStateCreateInfo(
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			0,
			VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			VulkanHelper::InitPipelineRasterizationStateCreateInfo(
			VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_BACK_BIT,
			VK_FRONT_FACE_COUNTER_CLOCKWISE,
			0,
			VK_FALSE);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			VulkanHelper::InitPipelineColorBlendAttachmentState(
			0xf,
			VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendState{};

		// tricky... BatchDrawEffect was designed for OpenGL to not have to worry about
		// which framebuffer output is rendered to because the state machine handled that.
		// In Vulkan, we don't have that luxury. So, we check to see if a GBuffer FBO is present.
		// If not, we fall back to the default FBO if present.
		const FrameBufferObject* pFBO = nullptr;

		auto& gBufferFBOs = es.GetGBufferFrameBufferObjects();
		if (!gBufferFBOs.empty())
		{
			pFBO = gBufferFBOs[0];
		}

		if (!pFBO)
		{
			auto& defaultFBOs = es.GetDefaultFrameBufferObjects();
			assert(!defaultFBOs.empty());
			if (!defaultFBOs.empty())
			{
				pFBO = defaultFBOs[0];
			}
		}

		assert(pFBO);
		if (!pFBO)
		{
			return false;
		}

		std::vector<VkPipelineColorBlendAttachmentState> colorBlendStates;

		if(pFBO)
		{
			for(auto& entry : pFBO->textureObjectInfos)
			{
				colorBlendStates.push_back(entry.colorBlendState);
			}

			colorBlendState =
				VulkanHelper::InitPipelineColorBlendStateCreateInfo(
				static_cast<uint32_t>(colorBlendStates.size()),
				colorBlendStates.data());
		}
		else
		{
			VkPipelineColorBlendStateCreateInfo colorBlendState =
				VulkanHelper::InitPipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);
		}

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			VulkanHelper::InitPipelineDepthStencilStateCreateInfo(
			VK_TRUE,
			VK_TRUE,
			VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			VulkanHelper::InitPipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			VulkanHelper::InitPipelineMultisampleStateCreateInfo(
			VK_SAMPLE_COUNT_1_BIT,
			0);

		if (shaderPtr)
		{
			VKNDescriptorSetBuilder& dsBuilder = shaderPtr->GetDescriptorSetBuilder();

			//layout(std140, binding = 0) uniform ProjViewBuf
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0);

			//layout(binding = 1) uniform sampler2DArray textureMaps;
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1);

			if(m_uniformMemHelperPtr)
			{
				dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					0,
					0,
					m_uniformMemHelperPtr->GetBufferObject());
			}

			if(m_texPackPtr)
			{
				VKNTexturePackPtr vknTexPackPtr = std::dynamic_pointer_cast<VKNTexturePack, TexturePack>(m_texPackPtr);
				if(vknTexPackPtr)
				{
					TextureObject* pTexObj = vknTexPackPtr->GetTextureHandle();
					assert(pTexObj);
					if(pTexObj)
					{
						dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							1,
							0,
							*pTexObj);
					}
				}
			}

			assert(pFBO->renderPass);
			VKNPipelineBuilder& pipelineBuilder = shaderPtr->GetPipelineBuilder();
			pipelineBuilder.Add(inputAssemblyState).
				Add(s_vertexInputState).
				Add(rasterizationState).
				Add(colorBlendState).
				Add(depthStencilState).
				Add(viewportState).
				Add(multisampleState).
				Add(s1_dynamicState).
				Add(pFBO->renderPass);

			return true;
		}

		return false;
	}

	bool VKNBatchDrawEffect::SetupStaticAlphaBlendShader(const VKNShaderPtr& shaderPtr, const VKNEffectState& es)
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			VulkanHelper::InitPipelineInputAssemblyStateCreateInfo(
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			0,
			VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			VulkanHelper::InitPipelineRasterizationStateCreateInfo(
			VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_BACK_BIT,
			VK_FRONT_FACE_COUNTER_CLOCKWISE,
			0);

		VkPipelineColorBlendAttachmentState alphaBlendAttachmentState
		{
			VK_TRUE,							// blendEnable
			VK_BLEND_FACTOR_SRC_COLOR,			// srcColorBlendFactor
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,// dstColorBlendFactor
			VK_BLEND_OP_ADD,					// colorBlendOp
			VK_BLEND_FACTOR_SRC_ALPHA,			// srcAlphaBlendFactor
			VK_BLEND_FACTOR_ONE,				// dstAlphaBlendFactor
			VK_BLEND_OP_ADD,					// alphaBlendOp
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT			// colorWriteMask
		};

		std::vector<VkPipelineColorBlendAttachmentState> alphaBlendColorBlendStates;
		alphaBlendColorBlendStates.push_back(alphaBlendAttachmentState);

		VkPipelineColorBlendStateCreateInfo alphaBlendColorBlendState
		{
			VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			0,									// pNext
			0,									// flags
			VK_FALSE,							// logicOpEnable
			VK_LOGIC_OP_CLEAR,					// logicOp
			static_cast<uint32_t>(alphaBlendColorBlendStates.size()),	// attachmentCount
			alphaBlendColorBlendStates.data(),	// pAttachments
			{ 1.f, 1.f, 1.f, 1.f }				// blend constants
		};

		FrameBufferObjectPtr fboAlphaBlendPtr = es.GetFinalFrameBufferObject();
		assert(fboAlphaBlendPtr);
		if (!fboAlphaBlendPtr)
		{
			return false;
		}

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			VulkanHelper::InitPipelineDepthStencilStateCreateInfo(
			VK_FALSE,
			VK_FALSE,
			VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			VulkanHelper::InitPipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			VulkanHelper::InitPipelineMultisampleStateCreateInfo(
			VK_SAMPLE_COUNT_1_BIT,
			0);

		if (shaderPtr)
		{
			VKNDescriptorSetBuilder& dsBuilder = shaderPtr->GetDescriptorSetBuilder();

			//layout(std140, binding = 0) uniform ProjViewBuf
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0);

			//layout(binding = 1) uniform sampler2DArray textureMaps;
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1);

			//layout(std140, binding = 2) uniform Instances
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				2);

			if (m_uniformMemHelperPtr)
			{
				dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					0,
					0,
					m_uniformMemHelperPtr->GetBufferObject());
			}

			if (m_texPackPtr)
			{
				VKNTexturePackPtr vknTexPackPtr = std::dynamic_pointer_cast<VKNTexturePack, TexturePack>(m_texPackPtr);
				if (vknTexPackPtr)
				{
					TextureObject* pTexObj = vknTexPackPtr->GetTextureHandle();
					assert(pTexObj);
					if (pTexObj)
					{
						dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							1,
							0,
							*pTexObj);
					}
				}
			}

			// remember that world matrix instances were collected as part of VKNMultiDrawInstancedObject.
			// You want those matrices for shadow rendering too
			assert(m_alphaStaticMultiDrawObjectPtr);
			if (m_alphaStaticMultiDrawObjectPtr)
			{
				VKNMultiDrawInstancedObject* pDrawInstancedObject = dynamic_cast<VKNMultiDrawInstancedObject*>(m_alphaStaticMultiDrawObjectPtr.get());
				if (pDrawInstancedObject)
				{
					auto& instBuffer = pDrawInstancedObject->GetInstanceBuffer();
					dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
						2,
						0,
						instBuffer);
				}
			}

			// NOTE: the renderpass you use here needs to preserve existing color attachments,
			// not clear or ignore them.

			assert(fboAlphaBlendPtr->renderPass);
			VKNPipelineBuilder& pipelineBuilder = shaderPtr->GetPipelineBuilder();
			pipelineBuilder.Add(inputAssemblyState).
				Add(s_vertexInputState).
				Add(rasterizationState).
				Add(alphaBlendColorBlendState).
				Add(depthStencilState).
				Add(viewportState).
				Add(multisampleState).
				Add(s1_dynamicState).
				Add(fboAlphaBlendPtr->renderPass);

			return true;
		}

		return false;
	}

	bool VKNBatchDrawEffect::SetupDynamicAlphaBlendShader(const VKNShaderPtr& shaderPtr, const VKNEffectState& es)
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			VulkanHelper::InitPipelineInputAssemblyStateCreateInfo(
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			0,
			VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			VulkanHelper::InitPipelineRasterizationStateCreateInfo(
			VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_BACK_BIT,
			VK_FRONT_FACE_COUNTER_CLOCKWISE,
			0);

		VkPipelineColorBlendAttachmentState alphaBlendAttachmentState
		{
			VK_TRUE,							// blendEnable
			VK_BLEND_FACTOR_ONE,				// srcColorBlendFactor
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA/*VK_BLEND_FACTOR_ZERO*/,// dstColorBlendFactor
			VK_BLEND_OP_ADD,					// colorBlendOp
			VK_BLEND_FACTOR_ONE,				// srcAlphaBlendFactor
			VK_BLEND_FACTOR_ZERO,				// dstAlphaBlendFactor
			VK_BLEND_OP_ADD,					// alphaBlendOp
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT			// colorWriteMask
		};

		std::vector<VkPipelineColorBlendAttachmentState> alphaBlendColorBlendStates;
		alphaBlendColorBlendStates.push_back(alphaBlendAttachmentState);

		VkPipelineColorBlendStateCreateInfo alphaBlendColorBlendState
		{
			VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			0,									// pNext
			0,									// flags
			VK_FALSE,							// logicOpEnable
			VK_LOGIC_OP_CLEAR,					// logicOp
			static_cast<uint32_t>(alphaBlendColorBlendStates.size()),	// attachmentCount
			alphaBlendColorBlendStates.data(),	// pAttachments
			{ 1.f, 1.f, 1.f, 1.f }				// blend constants
		};

		FrameBufferObjectPtr fboAlphaBlendPtr = es.GetFinalFrameBufferObject();		
		assert(fboAlphaBlendPtr);

		if (!fboAlphaBlendPtr)
		{
			return false;
		}

		std::vector<VkPipelineColorBlendAttachmentState> colorBlendStates;

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			VulkanHelper::InitPipelineDepthStencilStateCreateInfo(
			VK_TRUE,
			VK_FALSE,
			VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			VulkanHelper::InitPipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			VulkanHelper::InitPipelineMultisampleStateCreateInfo(
			VK_SAMPLE_COUNT_1_BIT,
			0);

		if (shaderPtr)
		{
			// NOTE: the layout and binding info is taken from the desired shaders and 
			// uniform blocks to determine how descriptor sets are formatted
			VKNDescriptorSetBuilder& dsBuilder = shaderPtr->GetDescriptorSetBuilder();

			//layout(std140, binding = 0) uniform ProjViewBuf
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0);

			//layout(binding = 1) uniform sampler2DArray textureMaps;
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1);

			if (m_uniformMemHelperPtr)
			{
				dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					0,
					0,
					m_uniformMemHelperPtr->GetBufferObject());
			}

			if (m_texPackPtr)
			{
				VKNTexturePackPtr vknTexPackPtr = std::dynamic_pointer_cast<VKNTexturePack, TexturePack>(m_texPackPtr);
				if (vknTexPackPtr)
				{
					TextureObject* pTexObj = vknTexPackPtr->GetTextureHandle();
					assert(pTexObj);
					if (pTexObj)
					{
						dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							1,
							0,
							*pTexObj);
					}
				}
			}

			// NOTE: the renderpass you use here needs to preserve existing color attachments,
			// not clear or ignore them.
			assert(fboAlphaBlendPtr->renderPass);
			VKNPipelineBuilder& pipelineBuilder = shaderPtr->GetPipelineBuilder();
			pipelineBuilder.Add(inputAssemblyState).
				Add(s_vertexInputState).
				Add(rasterizationState).
				Add(alphaBlendColorBlendState).
				Add(depthStencilState).
				Add(viewportState).
				Add(multisampleState).
				Add(s1_dynamicState).
				Add(fboAlphaBlendPtr->renderPass);

			return true;
		}

		return false;
	}

	bool VKNBatchDrawEffect::FakePrePass(int pass, const EffectStatePtr& esPtr)
	{
		m_currentPass = pass;
		if (!m_cachedShaderPtrs[0])
		{
			m_cachedShaderPtrs[0] = m_renderer.GetShaderProgram(m_materialList[0]);

			VulkanRenderContext& context = static_cast<VulkanRenderContext&>(*m_renderer.GetRenderContext());
			uint32_t swapChainCount = context.GetSwapChainImageCount();
			assert(swapChainCount > 0);

			assert(esPtr);
			if(!esPtr)
			{
				Log::PrintError("VKNBatchDrawEffect::FakePrePass() EffectStatePtr is NULL! Cannot continue!");
				return false;
			}

			const VKNEffectState& effectState = (esPtr->GetType() == EffectState::kDerived) ?
				static_cast<const VKNEffectState&>(*esPtr) :
				VKNEffectState(nullptr);

			VKNShaderPtr shaderPtr = std::dynamic_pointer_cast<VKNShader, Shader>(m_cachedShaderPtrs[0]);
			if (shaderPtr)
			{
				RenderCheckOK(FakeSetupShader(shaderPtr, effectState));
			}
		}

		// if we have open bindings, close them
		RenderCheckOK(CheckBuffers());

		return true;
	}

	bool VKNBatchDrawEffect::FakeDraw(const Graphics::CameraDrawInfo& cdi, const Graphics::RenderStateInfo& rsi)
	{
		int shaderIndex = 0;
		assert(m_dynamicMultiDrawObjectPtr);
		MultiDrawPtr drawPtr = m_dynamicMultiDrawObjectPtr;
		if (drawPtr)
		{
			VKNShaderPtr currentShader = std::dynamic_pointer_cast<VKNShader, Shader>(m_cachedShaderPtrs[shaderIndex]);
			assert(currentShader);
			if (currentShader)
			{
				// attempt to flip y vertices. Data as specified seems to be upside down on Vulkan.
				Math::mat4 yScaleMat;
				yScaleMat[5] = -1.f;
				Graphics::CameraDrawInfo temp_cdi = cdi;
				temp_cdi.projMat = temp_cdi.projMat * yScaleMat;
				UpdateUniforms(/*cdi,*/temp_cdi, rsi);

				// start recording

				m_renderer.SetActiveShaderProgram(currentShader);

				// we set the shader here as well because static multidraw type needs
				// to access uniform 'worldMat' to set it per object reference
				drawPtr->SetShader(currentShader);
				// the array buffer and texture array linking to shader state is done
				// within this call
				RenderCheckOK(drawPtr->Render());

				return true;
			}
		}

		return false;
	}

	bool VKNBatchDrawEffect::FakePostPass()
	{
		if (m_currentPass > -1)
		{
			return true;
		}

		return false;
	}

	bool VKNBatchDrawEffect::FakeSetupShader(const VKNShaderPtr& shaderPtr, const VKNEffectState& effectState)
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			VulkanHelper::InitPipelineInputAssemblyStateCreateInfo(
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			0,
			VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			VulkanHelper::InitPipelineRasterizationStateCreateInfo(
			VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_BACK_BIT,
			VK_FRONT_FACE_COUNTER_CLOCKWISE,
			0);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			VulkanHelper::InitPipelineColorBlendAttachmentState(
			0xf,
			VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendState{};

		auto& defaultFBOs = effectState.GetDefaultFrameBufferObjects();
		assert(!defaultFBOs.empty());

		if(defaultFBOs.empty())
		{
			return false;
		}

		std::vector<VkPipelineColorBlendAttachmentState> colorBlendStates;

		const FrameBufferObject* pFBO = defaultFBOs[0];
		if (pFBO)
		{
			for(auto& entry : pFBO->textureObjectInfos)
			{
				colorBlendStates.push_back(entry.colorBlendState);
			}

			colorBlendState =
				VulkanHelper::InitPipelineColorBlendStateCreateInfo(
				static_cast<uint32_t>(colorBlendStates.size()),
				colorBlendStates.data());
		}
		else
		{
			VkPipelineColorBlendStateCreateInfo colorBlendState =
				VulkanHelper::InitPipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);
		}

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			VulkanHelper::InitPipelineDepthStencilStateCreateInfo(
			VK_TRUE,
			VK_TRUE,
			VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			VulkanHelper::InitPipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			VulkanHelper::InitPipelineMultisampleStateCreateInfo(
			VK_SAMPLE_COUNT_1_BIT,
			0);

		if (shaderPtr)
		{
			// TODO: look at the desired shaders and uniform blocks to determine
			// how descriptor sets are formatted
			VKNDescriptorSetBuilder& dsBuilder = shaderPtr->GetDescriptorSetBuilder();

			//layout(std140, binding = 0) uniform ProjViewBuf
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0);

			//layout(binding = 1) uniform sampler2DArray textureMaps;
			dsBuilder.AddToLayout(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1);

			if (m_uniformMemHelperPtr)
			{
				dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					0,
					0,
					m_uniformMemHelperPtr->GetBufferObject());
			}

			if (m_texPackPtr)
			{
				VKNTexturePackPtr vknTexPackPtr = std::dynamic_pointer_cast<VKNTexturePack, TexturePack>(m_texPackPtr);
				if (vknTexPackPtr)
				{
					TextureObject* pTexObj = vknTexPackPtr->GetTextureHandle();
					assert(pTexObj);
					if (pTexObj)
					{
						dsBuilder.AddToWriteDescriptorSet(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							1,
							0,
							*pTexObj);
					}
				}
			}

			assert(defaultFBOs[0]->renderPass);
			VKNPipelineBuilder& pipelineBuilder = shaderPtr->GetPipelineBuilder();
			pipelineBuilder.Add(inputAssemblyState).
				Add(s_vertexInputState).
				Add(rasterizationState).
				Add(colorBlendState).
				Add(depthStencilState).
				Add(viewportState).
				Add(multisampleState).
				Add(s1_dynamicState).
				Add(defaultFBOs[0]->renderPass);

			return true;
		}

		return false;
	}
}
