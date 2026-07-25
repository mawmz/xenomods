#pragma once

#include "xenomods/engine/mm/MathTypes.hpp"

namespace ml {

	class ScnObjModel {
	   public:
		void setClip(bool enabled);
	};

	class ScnObjAccResMdlInfo {
	   public:
		explicit ScnObjAccResMdlInfo(ScnObjModel* model);

		void setMatrix(const mm::Mat44& matrix);
		void setAlpha(float alpha);
		void setSpAlphaMode(bool enabled);
		void setDblBuffAlpha(bool enabled);
		void setSimpleShadow(bool enabled);
		void setGpuClip(bool enabled);
		void setCamCheckMode(bool enabled);
		void setOcclusionQuery(bool enabled);
		void getInitialVertexAABBMax(mm::Vec3& result, bool includeSkinning) const;
		void getInitialVertexAABBMin(mm::Vec3& result, bool includeSkinning) const;

	   private:
		void* modelData;
	};

	class ScnObjAccResMaterial {
	   public:
		explicit ScnObjAccResMaterial(ScnObjModel* model);

		void* getMatRes(int index);
		void* getMatData(int index);
		int getMaterialCount() const {
			return materialCount;
		}

	   private:
		void* materialData;
		void* materialResources;
		int materialCount;
		int padding;
	};

	static_assert(sizeof(ScnObjAccResMaterial) == 0x18);

	class ScnObjAccResShaderParm {
	   public:
		explicit ScnObjAccResShaderParm(ScnObjModel* model);

		void setColor(int index, const mm::Col3& color);

	   private:
		void* shaderParameters;
	};

} // namespace ml
