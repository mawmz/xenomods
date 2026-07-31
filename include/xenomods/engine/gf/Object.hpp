// Created by block on 3/19/23.

#pragma once

#include "xenomods/engine/fw/Transform.hpp"
#include "xenomods/engine/mm/MathTypes.hpp"

namespace fw {
	class ModelObject;
}

namespace gf {

	struct GF_OBJ_HANDLE {
	   public:
		unsigned long actual;

		// xenomods from here
		inline bool IsValid() {
			return reinterpret_cast<unsigned long>(this) != -1ul;
		}
		inline GF_OBJ_HANDLE* Ptr() {
			return reinterpret_cast<GF_OBJ_HANDLE*>(actual);
		}
	};

	struct GFOBJ_INFO {

	};

	struct GfComTransform {
	   public:
		mm::Vec3* getPosition() const;
		mm::Quat* getRotation() const;

		void setPosition(const mm::Vec3& pos);
		void setRotation(const mm::Quat& rot);
	};

	class GfComModel {
	   public:
		fw::ModelObject* getModelObject() const {
			const auto bytes = reinterpret_cast<const std::uint8_t*>(this);
			return *reinterpret_cast<fw::ModelObject* const*>(bytes + 0x18);
		}
	};

	// unknown name
	enum class ObjectType {
		Type1,
		Type2,
		Type3,
		Type4,
		Type5,
		Type6,
		Type7,
		Type8
	};

	// unknown name
	enum ObjectFlags : unsigned int {
		Destroy = 0,
		Clip = 2,
		GPUClip = 3,
		Rebuild = 6,
		CameraFadeDisableForEvent = 11,
		CameraFadeDisableForBattle = 12,
		CameraFadeDisable = 13,
		PartyObj = 18,
		Updated = 19,
		ToolMode = 24
	};

	enum class OBJDISP : unsigned int {
		Normal = 1,
		Event = 2,
		Field = 8
	};

	class GfObjAcc {
	   public:
		GfObjAcc(GF_OBJ_HANDLE* handle);

		ObjectType getType() const;
		bool getObjPosRot(mm::Vec3& pos, float& rot);
		float getAlphaCamera() const;
		const mm::Mat44* getWorldTransform() const;
		float getTargetSearchOffset() const;

		void setDisp(OBJDISP channel, bool displayed);
		bool isDisp(OBJDISP channel) const;
		void setAlphaNormal(float alpha);
		void setClip(bool enabled);
		void setGpuClip(bool enabled);
		void setModelScaleForMenu(const mm::Vec3& scale);

		bool isCollideModelClip() const;

	   private:
		// Ghidra 0x7100766318: GfObjAcc is a two-pointer stack accessor.
		GFOBJ_INFO* objInfo;
		void* object;
	};

	static_assert(sizeof(GfObjAcc) == 0x10, "[gf::GfObjAcc] size 0x10");

	class GfObjUtil {
	   public:
		static void* getObj(GF_OBJ_HANDLE* handle);
		static void* getProperty(GF_OBJ_HANDLE* handle);
		static char* getModelResourceName(GF_OBJ_HANDLE* handle);
		static GfComModel* getComModel(GF_OBJ_HANDLE* handle);
		static bool testFlag(GFOBJ_INFO* objInfo, unsigned int flag);
		static void destroy(GF_OBJ_HANDLE* handle);
		static void setWarpTransform(
			GF_OBJ_HANDLE* handle,
			const fw::Transform& transform,
			bool resetState
		);
	};

	// XC2 GfInitParamGimmick layout, recovered from
	// GfObjFactory::createGmkMapObj at main + 0x66C528.
	struct GfInitParamGimmick {
		void** vtable;
		std::uint32_t field08;
		std::uint32_t objectType;
		std::uint32_t resourceId;
		std::uint32_t field14;
		unsigned char** resourceBdat;
		std::int32_t field20;
		std::uint32_t field24;
		std::int32_t field28;
		std::uint32_t field2C;
		std::uint64_t followObject0;
		std::uint64_t followObject1;
		std::uint32_t field40;
		std::uint16_t field44;
		std::uint16_t field46;
		std::uint32_t field48;
		float field4C;
		float field50;
		std::uint8_t field54[0x2C];
		std::uint8_t field80;
		std::uint8_t field81;
		std::uint16_t field82;
		std::uint8_t field84;
		std::uint8_t field85[3];
		void* customAssetSetup;
		std::uint64_t field90;
		std::uint8_t field98;
		std::uint8_t field99[7];
	};

	static_assert(sizeof(GfInitParamGimmick) == 0xA0, "[gf::GfInitParamGimmick] size 0xA0");
	static_assert(offsetof(GfInitParamGimmick, resourceId) == 0x10);
	static_assert(offsetof(GfInitParamGimmick, resourceBdat) == 0x18);
	static_assert(offsetof(GfInitParamGimmick, customAssetSetup) == 0x88);

	struct GfInitParamDriver { // size at least 0x1b0 (
		INSERT_PADDING_BYTES(0x10);
		unsigned int driverIndex;
		INSERT_PADDING_BYTES(412);
	};

	class GfObjFactory {
	   public:
		static GF_OBJ_HANDLE* createMapObj(const GfInitParamGimmick& param);
		static GF_OBJ_HANDLE* createDriver(GfInitParamDriver& param);
	};

} // namespace gf
