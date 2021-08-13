// IEffect.h
// Effect type that will be the basis for multipass rendering effects on RenderObjects (IMesh specifically)
// More specialized types will typically derive from EffectBase
/*
IEffect::PreSceneGraph() - operations that occur before scene graph updates

IEffect::Collect() - operations that occur during scene graph traversal

IEffect::PostSceneGraph() - operations that occur after scene graph updates

IEffect::PreDraw() - operations that occur before rendering

IEffect::Draw() - operations that occur during rendering

IEffect::PostDraw() - operations that occur after rendering
*/


#pragma once

#ifndef IEFFECT_H
#define IEFFECT_H

#if defined __linux__
#include <boost/smart_ptr/intrusive_ptr.hpp>
#else
#include <boost/intrusive_ptr.hpp>
#endif
#include <vector>

#include "CameraDrawInfo.h"
#include "RenderStateInfo.h"
#include "EffectData.h"
#include "RenderTargetInfo.h"

#include "../Visuals/RenderObject.h"
#include "../Visuals/IMaterial.h"

namespace Graphics
{
	class LightNode;
	class Node;

	typedef boost::intrusive_ptr<Node> SceneNodePtr;
}

namespace GamePrototype
{
	class EffectState;
	typedef std::shared_ptr<EffectState> EffectStatePtr;

	class IEffect : public GPCore::RefCount
	{
	public:
		enum Type
		{
			kBase,
			kDecorator,
			kMultiDecorator,
			kProxy
		};

		// NOTE: so far there is only one use case for this,
		// TextRenderer for OGL & VKN have an ITextRender base
		// interface that cannot be accessed through IEffect.
		template <typename T>
		T* GetInterface()
		{
			return dynamic_cast<T*>(this);
		}

		template <typename T>
		const T* GetInterface() const
		{
			return dynamic_cast<const T*>(this);
		}

		// EffectPtr is ptr to base type, so virtual dtor is needed on delete
		virtual ~IEffect() {}
		// initialize resources that this effect uses
		virtual bool Init() = 0;
		// query total number of drawing passes available in this effect
		virtual int NumPasses() const = 0;
		// set up render state, shaders, samplers, rendertargets & such for the draw calls to come in this pass
		virtual bool PrePass(int, const EffectStatePtr&) = 0;
		// collect the instances we'll eventually render
		virtual bool Collect(const Graphics::RenderObjectPtr&) = 0;
		virtual bool Collect(const std::vector<Graphics::RenderObjectPtr>&) = 0;
		// collect light nodes
		virtual bool Collect(const Graphics::SceneNodePtr&) = 0;
		// render based on the current PrePass settings
		virtual bool Draw(const Graphics::CameraDrawInfo&, const Graphics::RenderStateInfo&, const EffectStatePtr&) = 0;
		// any cleanup to perform based on the currently set pass?
		virtual bool PostPass(const EffectStatePtr& = EffectStatePtr()) = 0;
		// all rendering data is reset for new additions, if applicable
		virtual void ClearForNextFrame() = 0;
		// some form of ID for comparison of state (depending on type) to setup?
		virtual int GetID() const = 0;
		// free all resources, perform cleanup
		virtual void Free() = 0;
		// simple RTTI
		virtual Type GetType() const = 0;
		// NOTE: returns enum IEffectMgr::EffectType, not to be confused with Type above
		virtual int GetEffectType() const = 0;
		// effects are created from Materials, so the intent of this query
		// is to cut down on the number of effects created from material combinations
		virtual const Graphics::MaterialList& GetMaterials() const = 0;
		// return any render targets that I wish to represent
		virtual unsigned int GetNumRenderTargets() const = 0;
		// I'm relying on the fact that unsigned int and GLuint are interchangeable (32 bit unsigned) as of 4.2.
		// Intent is for client to request a specific index, function returns a handle representing the OpenGL
		// bound name (eventually DirectX texture id) to be used as a sampler within another effect
		virtual RenderTargetInfo GetRenderTarget(size_t) const = 0;

		// TODO: when it comes time to be able to modify shader variables (soon!)
		virtual void SetVariable(size_t shaderId, const char*, float val) = 0;
		// array of floats versions, hence the second size_t is the passed array size
		virtual void SetVariable(size_t shaderId, const char*, size_t, float*) = 0;

		// client should copy the memory passed back to it. It is expected
		// to be reused once the call returns.
		virtual float* GetVariable(size_t shaderId, const char*) const = 0;

		// data from the GameThread (IEffectDataHelper subclasses) will be copied over thread boundaries to the RenderingThread
		// with the help of this function.
		// We can then use the data for whatever within this implementation.
		// the index param should match the material index of the Effect instance (at least be == to the number of materials on the effect)
		virtual void SetEffectData(size_t index, const EffectData&) = 0;

		// decides whether PrePass(), Draw(), and PostPass() calls will be allowed on this
		// instance.
		virtual void SetDrawActive(bool bVal) { m_bIsDrawActive = bVal; }
		virtual bool IsDrawActive() const { return m_bIsDrawActive; }

		// intent is to copy light data collected from an Effect to one of the children responsible for
		// iterating over the light shaders 
		virtual const std::vector<Graphics::SceneNodePtr> GetLights();

		// called after scene graph has finished updating
		virtual bool PostSceneGraph() { return true; }

	protected:

		IEffect::IEffect()
		:
		m_bIsDrawActive(true)
		{
		}

		bool m_bIsDrawActive;
	};

	typedef boost::intrusive_ptr<IEffect> EffectPtr;

	typedef std::vector<EffectPtr> EffectsList;

	class TexturePack;
	typedef std::shared_ptr<TexturePack> TexturePackPtr;

	/*
	the intent of this structure is to allow an effect to set itself up
	with some of the data from the previous effect used (e.g. RenderTargets).
	This may help with chaining effects together in a specific order
	*/
	class EffectState
	{
	public:

		enum Type
		{
			kBase,
			kDerived
		};

		EffectState() {}
		EffectState(const EffectPtr& parentPtr, const EffectPtr& previousPtr)
		:
		m_parentEffectPtr(parentPtr),
		m_previousEffectPtr(previousPtr)
		{
		}

		EffectState(const EffectState& rhs)
		:
		m_parentEffectPtr(rhs.m_parentEffectPtr),
		m_previousEffectPtr(rhs.m_previousEffectPtr),
		m_sharedTexPackPtr(rhs.m_sharedTexPackPtr),
		m_sharedAlphaTexPackPtr(rhs.m_sharedAlphaTexPackPtr)
		{

		}

		virtual Type GetType() const
		{
			return kBase;
		}

		virtual void Clear()
		{
			m_parentEffectPtr 	= nullptr;
			m_previousEffectPtr = nullptr;

			m_sharedTexPackPtr 		= nullptr;
			m_sharedAlphaTexPackPtr = nullptr;
		}

        // this is now in both VKN & OGL. Move to parent
        const EffectPtr& GetParentEffect() const
		{
			return m_parentEffectPtr;
		}

		void SetParentEffect(const EffectPtr& effectPtr)
		{
			m_parentEffectPtr = effectPtr;
		}

        const EffectPtr& GetPreviousEffect() const
		{
			return m_previousEffectPtr;
		}

		void SetPreviousEffect(const EffectPtr& effectPtr)
		{
			m_previousEffectPtr = effectPtr;
		}

		TexturePackPtr GetSharedTexturePack() const
		{
			return m_sharedTexPackPtr;
		}

		void SetSharedTexturePack(const TexturePackPtr& tpPtr)
		{
			m_sharedTexPackPtr = tpPtr;
		}

		TexturePackPtr GetSharedAlphaTexturePack() const
		{
			return m_sharedAlphaTexPackPtr;
		}

		void SetSharedAlphaTexturePack(const TexturePackPtr& tpPtr)
		{
			m_sharedAlphaTexPackPtr = tpPtr;
		}

	protected:

		/* should be const. objects that use this var should NOT be able to modify, just query */
		EffectPtr m_parentEffectPtr;
		EffectPtr m_previousEffectPtr;

		// textures shared between any desired IEffect instances
        TexturePackPtr      m_sharedTexPackPtr;
        TexturePackPtr      m_sharedAlphaTexPackPtr;
	};
}

#endif // IEFFECT_H
