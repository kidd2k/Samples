// BatchDrawEffect.cpp
#ifndef __linux__
#include "stdafx.h"
#endif

#include "BatchDrawEffect.h"
#include "Renderer.h"
#include "EffectInitInfo.h"

namespace GamePrototype
{
	BatchDrawEffect::BatchDrawEffect(const EffectInitInfo& info)
	:
	m_renderer(info.m_renderer),
	m_materialList(info.m_materials.begin(), info.m_materials.end())
	{
	}

	const Graphics::MaterialList& BatchDrawEffect::GetMaterials() const
	{
		return m_materialList;
	}
}
