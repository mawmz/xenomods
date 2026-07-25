#pragma once

#include "xenomods/engine/mm/MathTypes.hpp"

namespace fw {

	// Position and rotation layout consumed by gf::GfObjUtil::setWarpTransform.
	struct alignas(0x10) Transform {
		mm::Vec3 position;
		float positionPadding;
		mm::Quat rotation;
	};

	static_assert(sizeof(Transform) == 0x20, "[fw::Transform] size 0x20");
	static_assert(offsetof(Transform, rotation) == 0x10);

} // namespace fw
