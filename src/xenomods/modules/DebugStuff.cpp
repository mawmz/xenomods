#include "DebugStuff.hpp"
#include "MenuHelper.hpp"
#include "ToolWindowLayout.hpp"

#include "xenomods/engine/apps/FrameworkLauncher.hpp"
#include "xenomods/engine/bdat/Bdat.hpp"
#include "xenomods/engine/fw/Framework.hpp"
#include "xenomods/engine/fw/Model.hpp"
#include "xenomods/engine/fw/Transform.hpp"
#include "xenomods/engine/fw/UpdateInfo.hpp"
#include "xenomods/engine/game/Debug.hpp"
#include "xenomods/engine/game/MenuGameData.hpp"
#include "xenomods/engine/game/Scripts.hpp"
#include "xenomods/engine/game/Utils.hpp"
#include "xenomods/engine/gf/Bgm.hpp"
#include "xenomods/engine/gf/Data.hpp"
#include "xenomods/engine/gf/Object.hpp"
#include "xenomods/engine/gf/Manager.hpp"
#include "xenomods/engine/gf/MenuObject.hpp"
#include "xenomods/engine/gf/Party.hpp"
#include "xenomods/engine/gf/PlayFactory.hpp"
#include "xenomods/engine/gf/PlayerController.hpp"
#include "xenomods/engine/gf/SaveGame.hpp"
#include "xenomods/engine/gf/Weather.hpp"
#include "xenomods/engine/gmk/Landmark.hpp"
#include "xenomods/engine/ml/Rand.hpp"
#include "xenomods/engine/ml/Scene.hpp"
#include "xenomods/engine/ml/SceneModel.hpp"
#include "xenomods/engine/ml/WinView.hpp"
#include "xenomods/engine/mm/MathTypes.hpp"
#include "xenomods/engine/mm/StdBase.hpp"
#include "xenomods/engine/mtl/Allocator.hpp"
#include "xenomods/engine/mtl/MemManager.hpp"
#include "xenomods/engine/mtl/MemoryInfo.hpp"
#include "xenomods/engine/tl/title.hpp"
#include "xenomods/stuff/utils/debug_util.hpp"
#include "xenomods/stuff/utils/util.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstring>

namespace {

	struct MMAssert : skylaunch::hook::Trampoline<MMAssert> {
		static void Hook(const char* expr, const char* file, unsigned line) {
			xenomods::g_Logger->LogFatal("Caught Assert!!! Expr \"{}\" ({} : {})", expr, file, line);
			Orig(expr, file, line);
		}
	};

#if XENOMODS_OLD_ENGINE
	// TitleMenu is embedded at TitleMain + 0x198. Its first field holds the
	// title-menu result consumed by waitTitleMenuDestroy() and
	// TitleStatePlayGame::enter(). Result 2 is the retail Continue selection.
	constexpr std::size_t TitleMenuOffset = 0x198;
	constexpr unsigned int TitleMenuResultContinue = 2;
	bool reloadPrimarySavePending = false;

	struct ObservedLocalGameFlag {
		unsigned int bitSize;
		int id;
		unsigned int value;
		bool occupied;
	};

	// This hook may be called frequently, so use a fixed open-addressed table instead
	// of allocating memory or linearly searching every observed flag.
	constexpr std::size_t MaxObservedLocalGameFlags = 1024;
	std::array<ObservedLocalGameFlag, MaxObservedLocalGameFlags> observedLocalGameFlags {};
	bool localGameFlagTraceOverflowLogged = false;

	constexpr int TutorialFlagIdBase = 54162;
	constexpr std::size_t TutorialIndexOffset = 0x38;
	// GmkPhantom is embedded at GmkTutorial + 0x60. Its byte at +0x80 is the
	// current collision state. The byte at tutorial + 0xE3 is only the
	// enter/leave notification latch consumed by updateInActiveList().
	constexpr std::size_t TutorialInsideTriggerOffset = 0xE0;
	constexpr std::size_t GmkShapeDataOffset = 0x10;
	constexpr std::size_t GmkShapeInfoOffset = 0x18;
	constexpr std::size_t GmkShapeTypeOffset = 0x0C;
	constexpr std::size_t GmkShapeSizeOffset = 0x20;
	constexpr int GmkShapeSphere = 2;
	constexpr int GmkShapeBox = 3;
	constexpr int GmkShapeCylinder = 4;
	constexpr std::size_t TutorialColiObjectOffset = 0xD8;
	constexpr std::size_t ColiObjectIdColiObjectOffset = 0x20;
	constexpr std::size_t IdColiObjectMatrixOffset = 0x70;
	constexpr std::size_t IdColiObjectHalfExtentsOffset = 0xB0;
	constexpr std::size_t IdColiObjectPrimitiveTypeOffset = 0xC0;
	constexpr int IdColiPrimitiveSphere = 0;
	constexpr int IdColiPrimitiveBox = 1;
	constexpr int IdColiPrimitiveCapsule = 2;
	constexpr float TutorialTriggerAlpha = 0.5f;
	constexpr float CutsceneTriggerAlpha = 0.5f;
	constexpr float LandmarkTriggerAlpha = 0.5f;
	constexpr float CollectionPointRangeAlpha = 0.5f;
	constexpr float CollectionPointFallbackRadius = 3.0f;
	constexpr float CollectionPointFallbackUpperHeight = 3.0f;
	constexpr float CollectionPointFallbackLowerHeight = 3.0f;
	constexpr float CollectionPointRenderDistance = 100.0f;
	constexpr unsigned int TutorialTriggerMapObjectBdatIndex = 0xAB;
	constexpr unsigned int TutorialTriggerMapObjectResourceId = 2;
	constexpr unsigned int TutorialSphereMapObjectResourceId = 16;
	constexpr const char* TutorialSphereModelName = "oj/oj490002";
	constexpr unsigned int TutorialCapsuleMapObjectResourceId = 43;
	constexpr const char* TutorialCapsuleModelName = "oj/oj101002";
	constexpr unsigned int CutsceneTriggerMapObjectResourceId = 108;
	constexpr const char* CutsceneTriggerModelName = "oj/oj700009";
	constexpr unsigned int CutsceneSphereMapObjectResourceId = 92;
	constexpr const char* CutsceneSphereModelName = "oj/oj740010";
	constexpr unsigned int CutsceneCapsuleMapObjectResourceId = 93;
	constexpr const char* CutsceneCapsuleModelName = "oj/oj740011";
	// RSC_MapObjList ID 4 has no BDAT references. Reserve it for landmarks.
	// The other unreferenced candidates remain available: 13, 16, 33, 43,
	// 92, 93, 94, 101, and 103.
	constexpr unsigned int LandmarkTriggerMapObjectResourceId = 4;
	constexpr const char* LandmarkTriggerModelName = "oj/oj200101";
	constexpr unsigned int LandmarkSphereMapObjectResourceId = 94;
	constexpr const char* LandmarkSphereModelName = "oj/oj740012";
	constexpr unsigned int LandmarkCapsuleMapObjectResourceId = 101;
	constexpr const char* LandmarkCapsuleModelName = "oj/oj700101";
	// RSC_MapObjList ID 103 is unreferenced by retail placement data. Reserve
	// its authored model path for the collection-point access cylinder.
	constexpr unsigned int CollectionPointMapObjectResourceId = 103;
	constexpr const char* CollectionPointModelName = "oj/oj741017";
	constexpr const char* GfInitParamGimmickVtableSymbol =
		"_ZTVN2gf18GfInitParamGimmickE";

	constexpr std::size_t MaxTutorialTriggers = 128;
	constexpr std::size_t MaxCutsceneTriggers = 128;
	constexpr std::size_t MaxLandmarkTriggers = 128;
	constexpr std::size_t MaxCollectionPoints = 256;
	constexpr std::size_t CollectionPositionPointerOffset = 0x10;
	constexpr std::size_t CollectionObjectHandleOffset = 0x48;
	constexpr std::size_t CollectionIdOffset = 0x38;
	constexpr std::size_t DropitemVelocityOffset = 0x20;
	constexpr std::size_t DropitemParamOffset = 0x40;
	constexpr std::size_t DropitemDistanceOffset = 0x4C;
	constexpr std::size_t DropitemParamDistanceOffset = 0x08;
	constexpr std::size_t DropitemParamTimeOffset = 0x10;
	constexpr std::size_t LandmarkIdOffset = 0x38;
	constexpr int LandmarkFlagIdBase = 51161;
	constexpr std::size_t GmkEventBdatInfoOffset = 0x30;
	constexpr std::size_t GmkEventIdOffset = 0x02;
	void* updatingTutorial = nullptr;
	unsigned char* tutorialTriggerMapObjectBdat = nullptr;
	unsigned char* validatedTriggerResourceBdat = nullptr;
	std::array<std::uint8_t, 256> triggerResourceValidation {};

	struct TriggerModelAsset {
		unsigned int resourceId;
		const char* modelName;
		glm::vec3 sourceExtent;
	};

	const TriggerModelAsset TutorialBoxAsset {
		TutorialTriggerMapObjectResourceId, "oj/oj900401", glm::vec3(1.92f)
	};
	const TriggerModelAsset TutorialSphereAsset {
		TutorialSphereMapObjectResourceId, TutorialSphereModelName, glm::vec3(1.92f)
	};
	const TriggerModelAsset TutorialCapsuleAsset {
		TutorialCapsuleMapObjectResourceId,
		TutorialCapsuleModelName,
		glm::vec3(1.92f, 3.84f, 1.92f)
	};
	const TriggerModelAsset CutsceneBoxAsset {
		CutsceneTriggerMapObjectResourceId, CutsceneTriggerModelName, glm::vec3(1.92f)
	};
	const TriggerModelAsset CutsceneSphereAsset {
		CutsceneSphereMapObjectResourceId, CutsceneSphereModelName, glm::vec3(1.92f)
	};
	const TriggerModelAsset CutsceneCapsuleAsset {
		CutsceneCapsuleMapObjectResourceId,
		CutsceneCapsuleModelName,
		glm::vec3(1.92f, 3.84f, 1.92f)
	};
	const TriggerModelAsset LandmarkBoxAsset {
		LandmarkTriggerMapObjectResourceId, LandmarkTriggerModelName, glm::vec3(1.92f)
	};
	const TriggerModelAsset LandmarkSphereAsset {
		LandmarkSphereMapObjectResourceId, LandmarkSphereModelName, glm::vec3(1.92f)
	};
	const TriggerModelAsset LandmarkCapsuleAsset {
		LandmarkCapsuleMapObjectResourceId,
		LandmarkCapsuleModelName,
		glm::vec3(1.92f, 3.84f, 1.92f)
	};
	const TriggerModelAsset CollectionPointCylinderAsset {
		CollectionPointMapObjectResourceId,
		CollectionPointModelName,
		glm::vec3(1.92f)
	};

	struct CollectionAccessParam {
		float forwardOffset;
		float radius;
		float upperHeight;
		float lowerHeight;
	};
	static_assert(sizeof(CollectionAccessParam) == 0x10);

	enum class TutorialTriggerRenderStage {
		Disabled,
		WaitingForTrigger,
		WaitingForFieldAssets,
		CreatingModel,
		LoadingModel,
		WaitingForObjectComponent,
		WaitingForSceneModel,
		InvalidModelBounds,
		Rendering
	};

	struct TutorialTriggerEntry {
		void* tutorial = nullptr;
		int flagId = -1;
		mm::Mat44 transform {};
		mm::Vec3 size {};
		int primitiveType = -1;
		bool hasShape = false;
		bool inside = false;
		bool repeatSuppressedUntilExit = false;
		gf::GF_OBJ_HANDLE* model = nullptr;
		bool modelBoundsReady = false;
		mm::Vec3 modelMin {};
		mm::Vec3 modelMax {};
		TutorialTriggerRenderStage renderStage =
			TutorialTriggerRenderStage::WaitingForFieldAssets;
	};

	struct CutsceneTriggerEntry {
		void* event = nullptr;
		int eventId = -1;
		mm::Mat44 transform {};
		mm::Vec3 size {};
		int primitiveType = -1;
		bool hasShape = false;
		bool inside = false;
		bool previousInside = false;
		bool modelNameChecked = false;
		bool modelNameValid = false;
		char actualModelName[32] {};
		gf::GF_OBJ_HANDLE* model = nullptr;
		bool modelBoundsReady = false;
		mm::Vec3 modelMin {};
		mm::Vec3 modelMax {};
		TutorialTriggerRenderStage renderStage =
			TutorialTriggerRenderStage::WaitingForFieldAssets;
	};

	struct LandmarkTriggerEntry {
		void* landmark = nullptr;
		int landmarkId = -1;
		int flagId = -1;
		mm::Mat44 transform {};
		mm::Vec3 size {};
		int primitiveType = -1;
		bool hasShape = false;
		bool inside = false;
		bool previousInside = false;
		bool modelNameChecked = false;
		bool modelNameValid = false;
		char actualModelName[32] {};
		gf::GF_OBJ_HANDLE* model = nullptr;
		bool modelBoundsReady = false;
		mm::Vec3 modelMin {};
		mm::Vec3 modelMax {};
		TutorialTriggerRenderStage renderStage =
			TutorialTriggerRenderStage::WaitingForFieldAssets;
	};

	struct CollectionPointEntry {
		void* collection = nullptr;
		int collectionId = -1;
		gf::GF_OBJ_HANDLE* target = nullptr;
		mm::Mat44 transform {};
		mm::Vec3 size {};
		bool hasShape = false;
		bool inside = false;
		gf::GF_OBJ_HANDLE* model = nullptr;
		bool modelBoundsReady = false;
		mm::Vec3 modelMin {};
		mm::Vec3 modelMax {};
		TutorialTriggerRenderStage renderStage =
			TutorialTriggerRenderStage::WaitingForFieldAssets;
	};

	struct TutorialTriggerMetrics {
		bool valid = false;
		int flagId = -1;
		glm::vec3 collisionCenter {};
		glm::vec3 matrixAxisLengths {};
		glm::vec3 halfExtents {};
		glm::vec3 currentRenderedSize {};
		glm::vec3 currentObjectPosition {};
		glm::vec3 currentObjectScale {};
		glm::vec3 sourceModelCenter {};
		glm::vec3 sourceModelExtent {};
		bool sourceBoundsReady = false;
		std::array<bool, 3> moverValid {};
		std::array<glm::vec3, 3> moverWorld {};
		std::array<glm::vec3, 3> moverLocal {};
		std::array<glm::vec3, 3> moverBoundaryRatio {};
		std::array<glm::vec3, 3> moverRotationLocal {};
		std::array<glm::vec3, 3> moverRotationBoundaryRatio {};
	};

	std::array<TutorialTriggerEntry, MaxTutorialTriggers> tutorialTriggers {};
	std::size_t tutorialTriggerCount = 0;
	void* tutorialControlTrigger = nullptr;
	bool tutorialControlLockObserved = false;
	int tutorialControlLockArmFrames = 0;
	bool tutorialTriggerRegistryOverflowLogged = false;
	int minimumTutorialFlagId = 0x7FFFFFFF;
	int maximumTutorialFlagId = -1;
	bool logTutorialTransformMetrics = true;
	TutorialTriggerMetrics latestTutorialTriggerMetrics {};
	std::array<CutsceneTriggerEntry, MaxCutsceneTriggers> cutsceneTriggers {};
	std::size_t cutsceneTriggerCount = 0;
	bool cutsceneTriggerRegistryOverflowLogged = false;
	unsigned char* validatedCutsceneMapObjectBdat = nullptr;
	bool cutsceneMapObjectValidationFailureLogged = false;
	bool logCutsceneTransformMetrics = true;
	std::array<LandmarkTriggerEntry, MaxLandmarkTriggers> landmarkTriggers {};
	std::size_t landmarkTriggerCount = 0;
	bool landmarkTriggerRegistryOverflowLogged = false;
	unsigned char* validatedLandmarkMapObjectBdat = nullptr;
	bool landmarkMapObjectValidationFailureLogged = false;
	bool logLandmarkTransformMetrics = false;
	std::array<CollectionPointEntry, MaxCollectionPoints> collectionPoints {};
	std::size_t collectionPointCount = 0;
	bool collectionPointRegistryOverflowLogged = false;
	CollectionAccessParam collectionAccessParam {};
	bool collectionAccessParamValid = false;
	float nearestCollectionPointDistance = -1.0f;
	std::size_t nearbyCollectionPointCount = 0;
	TutorialTriggerRenderStage nearestCollectionPointRenderStage =
		TutorialTriggerRenderStage::WaitingForTrigger;

	TutorialTriggerRenderStage tutorialTriggerRenderStage =
		TutorialTriggerRenderStage::Disabled;

	const char* GetTutorialTriggerRenderStageName() {
		switch(tutorialTriggerRenderStage) {
			case TutorialTriggerRenderStage::Disabled:
				return "disabled";
			case TutorialTriggerRenderStage::WaitingForTrigger:
				return "waiting for selected tutorial trigger";
			case TutorialTriggerRenderStage::WaitingForFieldAssets:
				return "waiting for normal field asset system";
			case TutorialTriggerRenderStage::CreatingModel:
				return "creating map object RSC_MapObjList ID 2";
			case TutorialTriggerRenderStage::LoadingModel:
				return "loading world model";
			case TutorialTriggerRenderStage::WaitingForObjectComponent:
				return "waiting for game-object model component";
			case TutorialTriggerRenderStage::WaitingForSceneModel:
				return "waiting for scene-model interface";
			case TutorialTriggerRenderStage::InvalidModelBounds:
				return "box model returned invalid bounds";
			case TutorialTriggerRenderStage::Rendering:
				return "rendering world scene model";
		}
		return "unknown";
	}

	const char* GetTriggerRenderStageName(
		TutorialTriggerRenderStage stage
	) {
		switch(stage) {
			case TutorialTriggerRenderStage::Disabled:
				return "disabled";
			case TutorialTriggerRenderStage::WaitingForTrigger:
				return "waiting for nearby collection point";
			case TutorialTriggerRenderStage::WaitingForFieldAssets:
				return "waiting for field assets / RSC validation";
			case TutorialTriggerRenderStage::CreatingModel:
				return "creating map object";
			case TutorialTriggerRenderStage::LoadingModel:
				return "loading world model";
			case TutorialTriggerRenderStage::WaitingForObjectComponent:
				return "waiting for model component";
			case TutorialTriggerRenderStage::WaitingForSceneModel:
				return "waiting for scene model";
			case TutorialTriggerRenderStage::InvalidModelBounds:
				return "model returned invalid bounds";
			case TutorialTriggerRenderStage::Rendering:
				return "rendering";
		}
		return "unknown";
	}

	const char* GetTutorialPrimitiveTypeName(int primitiveType) {
		switch(primitiveType) {
			case IdColiPrimitiveSphere:
				return "sphere";
			case IdColiPrimitiveBox:
				return "box";
			case IdColiPrimitiveCapsule:
				return "capsule";
			default:
				return "unknown";
		}
	}

	const TriggerModelAsset& SelectTriggerAsset(
		int primitiveType,
		const TriggerModelAsset& box,
		const TriggerModelAsset& sphere,
		const TriggerModelAsset& capsule
	) {
		switch(primitiveType) {
			case IdColiPrimitiveSphere:
				return sphere;
			case IdColiPrimitiveCapsule:
				return capsule;
			default:
				return box;
		}
	}

	void LogTutorialTriggerMetrics(TutorialTriggerEntry& entry) {
		if(!entry.hasShape)
			return;

		TutorialTriggerMetrics metrics {};
		metrics.valid = true;
		metrics.flagId = entry.flagId;

		const glm::mat4 collisionTransform = entry.transform;
		const glm::mat3 collisionBasis = glm::mat3(collisionTransform);
		metrics.collisionCenter = glm::vec3(collisionTransform[3]);
		metrics.matrixAxisLengths = glm::vec3(
			glm::length(collisionBasis[0]),
			glm::length(collisionBasis[1]),
			glm::length(collisionBasis[2])
		);
		metrics.halfExtents = glm::vec3(entry.size) * 0.5f;
		// This is the world size produced by the current renderer. Comparing it
		// with 2 * halfExtents reveals whether matrix scale is being applied a
		// second time.
		metrics.currentRenderedSize =
			metrics.matrixAxisLengths * glm::vec3(entry.size);

		glm::mat3 collisionRotation = collisionBasis;
		if(
			metrics.matrixAxisLengths.x > 0.0001f
			&& metrics.matrixAxisLengths.y > 0.0001f
			&& metrics.matrixAxisLengths.z > 0.0001f
		) {
			collisionRotation[0] /= metrics.matrixAxisLengths.x;
			collisionRotation[1] /= metrics.matrixAxisLengths.y;
			collisionRotation[2] /= metrics.matrixAxisLengths.z;
		}

		if(entry.modelBoundsReady) {
			metrics.sourceBoundsReady = true;
			const glm::vec3 sourceMinimum = entry.modelMin;
			const glm::vec3 sourceMaximum = entry.modelMax;
			metrics.sourceModelCenter =
				(sourceMinimum + sourceMaximum) * 0.5f;
			// The decoded oj900401 vertex buffer spans exactly 1.92 units on
			// each axis. Its reported 2.12-unit AABB includes padding.
			metrics.sourceModelExtent = glm::vec3(1.92f);
			metrics.currentObjectScale =
				metrics.matrixAxisLengths
				* (glm::vec3(entry.size) / metrics.sourceModelExtent);
			metrics.currentObjectPosition =
				metrics.collisionCenter
				- collisionRotation
					* (
						metrics.currentObjectScale
						* metrics.sourceModelCenter
					);
		}

		const float determinant = glm::determinant(collisionTransform);
		if(std::isfinite(determinant) && std::abs(determinant) > 0.000001f) {
			const glm::mat4 inverseCollisionTransform =
				glm::inverse(collisionTransform);
			for(std::size_t index = 0; index < metrics.moverValid.size(); index++) {
				auto mover = gf::GfGameParty::getHandleMover(
					static_cast<unsigned int>(index)
				);
				if(
					mover == nullptr
					|| mover == reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1)
				)
					continue;

				mm::Vec3 moverPosition {};
				float moverRotation = 0.0f;
				gf::GfObjAcc moverAccessor(mover);
				if(!moverAccessor.getObjPosRot(moverPosition, moverRotation))
					continue;

				metrics.moverValid[index] = true;
				metrics.moverWorld[index] = moverPosition;
				metrics.moverLocal[index] = glm::vec3(
					inverseCollisionTransform
					* glm::vec4(metrics.moverWorld[index], 1.0f)
				);
				metrics.moverRotationLocal[index] =
					glm::transpose(collisionRotation)
					* (metrics.moverWorld[index] - metrics.collisionCenter);
				for(int axis = 0; axis < 3; axis++) {
					if(metrics.halfExtents[axis] > 0.0001f) {
						metrics.moverBoundaryRatio[index][axis] =
							std::abs(metrics.moverLocal[index][axis])
							/ metrics.halfExtents[axis];
						metrics.moverRotationBoundaryRatio[index][axis] =
							std::abs(metrics.moverRotationLocal[index][axis])
							/ metrics.halfExtents[axis];
					}
				}
			}
		}

		latestTutorialTriggerMetrics = metrics;

		xenomods::g_Logger->LogInfo(
			"[Tutorial metrics] Flag {} primitive {} inside {}; raw matrix columns follow",
			metrics.flagId,
			GetTutorialPrimitiveTypeName(entry.primitiveType),
			entry.inside
		);
		for(int column = 0; column < 4; column++) {
			xenomods::g_Logger->LogInfo(
				"[Tutorial metrics] M[{}] ({:.6f}, {:.6f}, {:.6f}, {:.6f})",
				column,
				collisionTransform[column].x,
				collisionTransform[column].y,
				collisionTransform[column].z,
				collisionTransform[column].w
			);
		}
		xenomods::g_Logger->LogInfo(
			"[Tutorial metrics] Flag {} collision center ({:.4f}, {:.4f}, {:.4f})",
			metrics.flagId,
			metrics.collisionCenter.x,
			metrics.collisionCenter.y,
			metrics.collisionCenter.z
		);
		xenomods::g_Logger->LogInfo(
			"[Tutorial metrics] Matrix axis lengths ({:.6f}, {:.6f}, {:.6f}); "
			"half-extents ({:.4f}, {:.4f}, {:.4f})",
			metrics.matrixAxisLengths.x,
			metrics.matrixAxisLengths.y,
			metrics.matrixAxisLengths.z,
			metrics.halfExtents.x,
			metrics.halfExtents.y,
			metrics.halfExtents.z
		);
		xenomods::g_Logger->LogInfo(
			"[Tutorial metrics] Current renderer world size ({:.4f}, {:.4f}, {:.4f}); "
			"unscaled phantom size ({:.4f}, {:.4f}, {:.4f})",
			metrics.currentRenderedSize.x,
			metrics.currentRenderedSize.y,
			metrics.currentRenderedSize.z,
			metrics.halfExtents.x * 2.0f,
			metrics.halfExtents.y * 2.0f,
			metrics.halfExtents.z * 2.0f
		);
		if(metrics.sourceBoundsReady) {
			xenomods::g_Logger->LogInfo(
				"[Tutorial metrics] Source center ({:.4f}, {:.4f}, {:.4f}), "
				"extent ({:.4f}, {:.4f}, {:.4f}); object pos ({:.4f}, {:.4f}, {:.4f}), "
				"scale ({:.6f}, {:.6f}, {:.6f})",
				metrics.sourceModelCenter.x,
				metrics.sourceModelCenter.y,
				metrics.sourceModelCenter.z,
				metrics.sourceModelExtent.x,
				metrics.sourceModelExtent.y,
				metrics.sourceModelExtent.z,
				metrics.currentObjectPosition.x,
				metrics.currentObjectPosition.y,
				metrics.currentObjectPosition.z,
				metrics.currentObjectScale.x,
				metrics.currentObjectScale.y,
				metrics.currentObjectScale.z
			);
		}
		for(std::size_t index = 0; index < metrics.moverValid.size(); index++) {
			if(!metrics.moverValid[index])
				continue;
			const auto world = metrics.moverWorld[index];
			const auto local = metrics.moverLocal[index];
			const auto ratio = metrics.moverBoundaryRatio[index];
			const auto rotationLocal = metrics.moverRotationLocal[index];
			const auto rotationRatio =
				metrics.moverRotationBoundaryRatio[index];
			xenomods::g_Logger->LogInfo(
				"[Tutorial metrics] Mover {} world ({:.4f}, {:.4f}, {:.4f}); "
				"matrix-local ({:.4f}, {:.4f}, {:.4f}); matrix boundary ratio ({:.4f}, {:.4f}, {:.4f}), max {:.4f}",
				index,
				world.x,
				world.y,
				world.z,
				local.x,
				local.y,
				local.z,
				ratio.x,
				ratio.y,
				ratio.z,
				std::max(ratio.x, std::max(ratio.y, ratio.z))
			);
			xenomods::g_Logger->LogInfo(
				"[Tutorial metrics] Mover {} rotation-local ({:.4f}, {:.4f}, {:.4f}); "
				"rotation-only boundary ratio ({:.4f}, {:.4f}, {:.4f}), max {:.4f}",
				index,
				rotationLocal.x,
				rotationLocal.y,
				rotationLocal.z,
				rotationRatio.x,
				rotationRatio.y,
				rotationRatio.z,
				std::max(
					rotationRatio.x,
					std::max(rotationRatio.y, rotationRatio.z)
				)
			);
		}
	}

	void SetTutorialTriggerRenderStage(TutorialTriggerRenderStage stage) {
		if(tutorialTriggerRenderStage == stage)
			return;

		tutorialTriggerRenderStage = stage;
		xenomods::g_Logger->LogInfo(
			"[Tutorial trigger] Renderer state: {}",
			GetTutorialTriggerRenderStageName()
		);
	}

	void DestroyTutorialTriggerModel(TutorialTriggerEntry& entry) {
		if(entry.model != nullptr && entry.model != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1))
			gf::GfObjUtil::destroy(entry.model);

		entry.model = nullptr;
		entry.modelBoundsReady = false;
		entry.modelMin = {};
		entry.modelMax = {};
		entry.renderStage = TutorialTriggerRenderStage::WaitingForFieldAssets;
	}

	void DestroyAllTutorialTriggerModels() {
		for(std::size_t i = 0; i < tutorialTriggerCount; i++)
			DestroyTutorialTriggerModel(tutorialTriggers[i]);
	}

	void ResetTutorialTriggerVisualization() {
		DestroyAllTutorialTriggerModels();
		SetTutorialTriggerRenderStage(
			xenomods::DebugStuff::renderTutorialTrigger
				? TutorialTriggerRenderStage::WaitingForFieldAssets
				: TutorialTriggerRenderStage::Disabled
		);
	}

	void ClearTutorialTriggerRegistry() {
		DestroyAllTutorialTriggerModels();
		tutorialTriggers = {};
		tutorialTriggerCount = 0;
		tutorialControlTrigger = nullptr;
		tutorialControlLockObserved = false;
		tutorialControlLockArmFrames = 0;
		tutorialTriggerRegistryOverflowLogged = false;
		minimumTutorialFlagId = 0x7FFFFFFF;
		maximumTutorialFlagId = -1;
		updatingTutorial = nullptr;
		tutorialTriggerMapObjectBdat = nullptr;
		latestTutorialTriggerMetrics = {};
		SetTutorialTriggerRenderStage(
			xenomods::DebugStuff::renderTutorialTrigger
				? TutorialTriggerRenderStage::WaitingForTrigger
				: TutorialTriggerRenderStage::Disabled
		);
	}

	void DestroyCutsceneTriggerModel(CutsceneTriggerEntry& entry) {
		if(entry.model != nullptr && entry.model != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1))
			gf::GfObjUtil::destroy(entry.model);

		entry.model = nullptr;
		entry.modelBoundsReady = false;
		entry.modelMin = {};
		entry.modelMax = {};
		entry.renderStage = TutorialTriggerRenderStage::WaitingForFieldAssets;
	}

	void DestroyAllCutsceneTriggerModels() {
		for(std::size_t i = 0; i < cutsceneTriggerCount; i++)
			DestroyCutsceneTriggerModel(cutsceneTriggers[i]);
	}

	void ResetCutsceneTriggerVisualization() {
		DestroyAllCutsceneTriggerModels();
		for(std::size_t i = 0; i < cutsceneTriggerCount; i++) {
			cutsceneTriggers[i].modelNameChecked = false;
			cutsceneTriggers[i].modelNameValid = false;
			cutsceneTriggers[i].actualModelName[0] = '\0';
		}
	}

	void ClearCutsceneTriggerRegistry() {
		DestroyAllCutsceneTriggerModels();
		cutsceneTriggers = {};
		cutsceneTriggerCount = 0;
		cutsceneTriggerRegistryOverflowLogged = false;
		validatedCutsceneMapObjectBdat = nullptr;
		cutsceneMapObjectValidationFailureLogged = false;
	}

	void DestroyLandmarkTriggerModel(LandmarkTriggerEntry& entry) {
		if(entry.model != nullptr && entry.model != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1))
			gf::GfObjUtil::destroy(entry.model);

		entry.model = nullptr;
		entry.modelBoundsReady = false;
		entry.modelMin = {};
		entry.modelMax = {};
		entry.renderStage = TutorialTriggerRenderStage::WaitingForFieldAssets;
	}

	void DestroyAllLandmarkTriggerModels() {
		for(std::size_t i = 0; i < landmarkTriggerCount; i++)
			DestroyLandmarkTriggerModel(landmarkTriggers[i]);
	}

	void ResetLandmarkTriggerVisualization() {
		DestroyAllLandmarkTriggerModels();
		for(std::size_t i = 0; i < landmarkTriggerCount; i++) {
			landmarkTriggers[i].modelNameChecked = false;
			landmarkTriggers[i].modelNameValid = false;
			landmarkTriggers[i].actualModelName[0] = '\0';
		}
	}

	void ClearLandmarkTriggerRegistry() {
		DestroyAllLandmarkTriggerModels();
		landmarkTriggers = {};
		landmarkTriggerCount = 0;
		landmarkTriggerRegistryOverflowLogged = false;
		validatedLandmarkMapObjectBdat = nullptr;
		landmarkMapObjectValidationFailureLogged = false;
	}

	void DestroyCollectionPointModel(CollectionPointEntry& entry) {
		if(
			entry.model != nullptr
			&& entry.model != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1)
		)
			gf::GfObjUtil::destroy(entry.model);

		entry.model = nullptr;
		entry.modelBoundsReady = false;
		entry.modelMin = {};
		entry.modelMax = {};
		entry.renderStage =
			TutorialTriggerRenderStage::WaitingForFieldAssets;
	}

	void DestroyAllCollectionPointModels() {
		for(std::size_t i = 0; i < collectionPointCount; i++)
			DestroyCollectionPointModel(collectionPoints[i]);
	}

	void ResetCollectionPointVisualization() {
		DestroyAllCollectionPointModels();
	}

	void ClearCollectionPointRegistry() {
		DestroyAllCollectionPointModels();
		collectionPoints = {};
		collectionPointCount = 0;
		collectionPointRegistryOverflowLogged = false;
		collectionAccessParam = {};
		collectionAccessParamValid = false;
	}

#if 0
	// Retained temporarily as reverse-engineering notes only. oj900401's
	// phong45 runtime material does not expose a replaceable sampled slot in
	// XC2, so the renderer no longer compiles or calls this path.
	grlib::CGLibTextureBuffer* GetSolidColorTexture() {
		// The renderer keeps this pointer after setTextureRes(), so both the
		// texture object and its backing GPU allocation must live permanently.
		static grlib::CGLibTextureBuffer texture;
		static bool initialized = false;
		static float appliedColor[3] {-1.0f, -1.0f, -1.0f};

		if(!initialized) {
			grlib::CGLibTexture::createTextureBuff(
				texture,
				1,
				1,
				GrlSurfaceFormat::RGBA8Unorm
			);
			initialized = texture.isInitialized();
			if(!initialized) {
				tutorialTriggerColorTextureReady = false;
				return nullptr;
			}
		}

		if(
			appliedColor[0] != tutorialTriggerColor[0]
			|| appliedColor[1] != tutorialTriggerColor[1]
			|| appliedColor[2] != tutorialTriggerColor[2]
		) {
			grlib::CGLibAcc2DTexture accessor;
			if(!accessor.lock(texture)) {
				tutorialTriggerColorTextureReady = false;
				return nullptr;
			}

			accessor.setFloat(
				tutorialTriggerColor[0],
				tutorialTriggerColor[1],
				tutorialTriggerColor[2],
				1.0f,
				0,
				0
			);
			accessor.unlock();
			for(int channel = 0; channel < 3; channel++)
				appliedColor[channel] = tutorialTriggerColor[channel];
		}

		tutorialTriggerColorTextureReady = true;
		return &texture;
	}

	ml::DrMdlObj* GetDriverModel(ml::ScnObjModel* sceneModel) {
		// Recovered from ScnObjAccResMaterial(ScnObjModel*) in XC2 main.elf:
		//   sceneModel + 0x3D8 -> scene resource
		//   scene resource + 0x30 -> DrMdlObj
		constexpr std::size_t SceneResourceOffset = 0x3D8;
		constexpr std::size_t DriverModelOffset = 0x30;

		auto sceneBytes = reinterpret_cast<std::byte*>(sceneModel);
		auto sceneResource =
			*reinterpret_cast<std::byte**>(sceneBytes + SceneResourceOffset);
		if(sceneResource == nullptr)
			return nullptr;

		return *reinterpret_cast<ml::DrMdlObj**>(
			sceneResource + DriverModelOffset
		);
	}

	ml::DrResMdoTexList* GetDriverTextureList(ml::DrMdlObj* driverModel) {
		constexpr std::size_t DriverResourceOffset = 0x30;
		constexpr std::size_t TextureListOffset = 0xD0;

		auto driverBytes = reinterpret_cast<std::byte*>(driverModel);
		auto driverResource =
			*reinterpret_cast<std::byte**>(driverBytes + DriverResourceOffset);
		if(driverResource == nullptr)
			return nullptr;

		return *reinterpret_cast<ml::DrResMdoTexList**>(
			driverResource + TextureListOffset
		);
	}

	bool ReplaceMaterialModelTexture(
		ml::DrMdlObj* driverModel,
		int materialIndex,
		grlib::CGLibTextureBuffer* replacement
	) {
		// DrMdlObj::getModelTexture() resolves a material's sampled texture as:
		//   resource + 0x28 -> material records (stride 0x74)
		//   material + 0x20 -> offset into resource + 0x10 index data
		//   selected u16 -> resource + 0xE8 texture-pointer array
		// XC2's DrMdoSetup::setShader() passes that +0xE8 array directly to
		// DrShdMdoDecExtTbl::setTextureList(). The original entry is allowed to
		// be null: oj900401 reaches this point before its sampled texture is
		// present, and that null entry is precisely what we need to replace.
		// Do not gate this write on getModelTexture() returning a non-null value.
		//
		// setTextureRes() cannot be used here: it gates writes on the unrelated
		// "now texture" count at +0xF0, which is zero for this map object.
		constexpr std::size_t DriverResourceOffset = 0x30;
		constexpr std::size_t TextureIndexDataOffset = 0x10;
		constexpr std::size_t MaterialDataOffset = 0x28;
		constexpr std::size_t RuntimeTexturePointersOffset = 0xE8;
		constexpr std::size_t MaterialStride = 0x74;
		constexpr std::size_t MaterialTextureReferenceOffset = 0x20;

		auto driverBytes = reinterpret_cast<std::byte*>(driverModel);
		auto resource =
			*reinterpret_cast<std::byte**>(driverBytes + DriverResourceOffset);
		if(resource == nullptr)
			return false;

		auto textureIndexData =
			*reinterpret_cast<std::byte**>(resource + TextureIndexDataOffset);
		auto materialData =
			*reinterpret_cast<std::byte**>(resource + MaterialDataOffset);
		auto texturePointers =
			*reinterpret_cast<grlib::CGLibTextureBuffer***>(
				resource + RuntimeTexturePointersOffset
			);
		if(
			textureIndexData == nullptr
			|| materialData == nullptr
			|| texturePointers == nullptr
		)
			return false;

		auto material =
			materialData + static_cast<std::size_t>(materialIndex) * MaterialStride;
		const auto textureReferenceOffset =
			*reinterpret_cast<const std::uint32_t*>(
				material + MaterialTextureReferenceOffset
			);
		const auto textureIndex =
			*reinterpret_cast<const std::uint16_t*>(
				textureIndexData + textureReferenceOffset
			);
		// The live resource-list count is zero for this map object even while
		// getModelTexture() resolves a valid sampled pointer. The material's
		// own u16 reference is authoritative; retain a conservative sanity cap
		// before indexing the runtime pointer array.
		if(textureIndex >= 64)
			return false;

		texturePointers[textureIndex] = replacement;
		return true;
	}

	int ApplySolidColorTexture(ml::ScnObjModel* sceneModel) {
		auto driverModel = GetDriverModel(sceneModel);
		if(driverModel == nullptr) {
			tutorialTriggerDriverModelReady = false;
			tutorialTriggerTextureCount = -1;
			return 0;
		}
		tutorialTriggerDriverModelReady = true;

		const int resourceTextureCount = driverModel->getTextureResMax();
		if(!tutorialTriggerTextureMetadataLogged) {
			auto textureList = GetDriverTextureList(driverModel);
			xenomods::g_Logger->LogInfo(
				"[Tutorial trigger] Resource texture-list entries: {}",
				resourceTextureCount
			);
			for(
				int index = 0;
				textureList != nullptr && index < resourceTextureCount;
				index++
			) {
				const char* name = textureList->getTexName(
					static_cast<unsigned int>(index)
				);
				xenomods::g_Logger->LogInfo(
					"[Tutorial trigger]   texture[{}] = {}",
					index,
					name != nullptr ? name : "<unnamed>"
				);
			}
			tutorialTriggerTextureMetadataLogged = true;
		}

		auto colorTexture = GetSolidColorTexture();
		if(colorTexture == nullptr)
			return 0;

		int replacementCount = 0;
		const int materialCount = std::clamp(
			tutorialTriggerMaterialCount,
			1,
			64
		);
		for(int materialIndex = 0; materialIndex < materialCount; materialIndex++) {
			if(
				ReplaceMaterialModelTexture(
					driverModel,
					materialIndex,
					colorTexture
				)
			)
				replacementCount++;
		}
		tutorialTriggerTextureCount = replacementCount;
		return replacementCount;
	}

	void WriteMaterialRgb(void* material, const float color[3]) {
		if(material == nullptr)
			return;

		auto bytes = static_cast<std::byte*>(material);
		auto rgb = reinterpret_cast<float*>(bytes + 0x08);
		for(int channel = 0; channel < 3; channel++)
			rgb[channel] = color[channel];
	}

	int ApplyMaterialColor(
		ml::ScnObjModel* sceneModel,
		const float color[3]
	) {
		ml::ScnObjAccResMaterial materials(sceneModel);
		const int materialCount = materials.getMaterialCount();
		tutorialTriggerMaterialCount = materialCount;
		if(materialCount <= 0 || materialCount > 64)
			return 0;

		if(!tutorialTriggerMaterialMetadataLogged) {
			xenomods::g_Logger->LogInfo(
				"[Tutorial trigger] Live materials: {}",
				materialCount
			);
		}

		int appliedCount = 0;
		for(int index = 0; index < materialCount; index++) {
			void* materialResource = materials.getMatRes(index);
			if(materialResource == nullptr)
				continue;

			if(!tutorialTriggerMaterialMetadataLogged) {
				const char* name =
					*reinterpret_cast<const char**>(materialResource);
				xenomods::g_Logger->LogInfo(
					"[Tutorial trigger]   material[{}] = {}",
					index,
					name != nullptr ? name : "<unnamed>"
				);
			}

			// This is the per-instance runtime material record consumed by
			// matCall_BaseColor. Do not edit getMatData(): that points at the
			// authored resource and may be shared by other oj900401 instances.
			WriteMaterialRgb(materialResource, color);
			appliedCount++;
		}

		if(!tutorialTriggerMaterialMetadataLogged)
			tutorialTriggerMaterialMetadataLogged = true;
		return appliedCount;
	}

#endif

	int GetTutorialFlagId(const void* tutorial) {
		const auto bytes = static_cast<const std::uint8_t*>(tutorial);
		return *reinterpret_cast<const int*>(bytes + TutorialIndexOffset) + TutorialFlagIdBase;
	}

	bool IsInsideTutorialTrigger(const void* tutorial) {
		const auto bytes = static_cast<const std::uint8_t*>(tutorial);
		return bytes[TutorialInsideTriggerOffset] != 0;
	}

	bool GetTutorialTriggerBox(
		const void* tutorial,
		mm::Mat44& transform,
		mm::Vec3& size,
		int& primitiveType
	) {
		const auto bytes = static_cast<const std::uint8_t*>(tutorial);
		const auto collisionObject =
			*reinterpret_cast<const std::uint8_t* const*>(
				bytes + TutorialColiObjectOffset
			);
		if(collisionObject == nullptr)
			return false;

		// fw::ColiObject::setMatrix() composes the source gimmick transform
		// with its reference frame before handing it to IDColi. Read that
		// finalized IDColi record so the visualization uses the same transform
		// and primitive dimensions as the actual inside/outside test.
		const auto idColiObject =
			*reinterpret_cast<const std::uint8_t* const*>(
				collisionObject + ColiObjectIdColiObjectOffset
			);
		if(idColiObject == nullptr)
			return false;

		const int livePrimitiveType =
			*reinterpret_cast<const int*>(
				idColiObject + IdColiObjectPrimitiveTypeOffset
			);
		if(
			livePrimitiveType != IdColiPrimitiveSphere
			&& livePrimitiveType != IdColiPrimitiveBox
			&& livePrimitiveType != IdColiPrimitiveCapsule
		)
			return false;

		const auto halfExtents =
			reinterpret_cast<const float*>(
				idColiObject + IdColiObjectHalfExtentsOffset
			);
		glm::vec3 primitiveSize(
			std::abs(halfExtents[0]) * 2.0f,
			std::abs(halfExtents[1]) * 2.0f,
			std::abs(halfExtents[2]) * 2.0f
		);
		if(livePrimitiveType == IdColiPrimitiveSphere) {
			const float diameter = std::abs(halfExtents[0]) * 2.0f;
			primitiveSize = glm::vec3(diameter);
		} else if(livePrimitiveType == IdColiPrimitiveCapsule) {
			// createCapsule(length, radius) stores {radius, length, radius}.
			// IDColi tests a Y-axis line segment of `length` with radius around
			// both endpoints, so the complete visible envelope is length + 2r.
			const float radius = std::abs(halfExtents[0]);
			const float straightLength = std::abs(halfExtents[1]);
			primitiveSize = glm::vec3(
				radius * 2.0f,
				straightLength + radius * 2.0f,
				radius * 2.0f
			);
		}
		if(
			!std::isfinite(primitiveSize.x)
			|| !std::isfinite(primitiveSize.y)
			|| !std::isfinite(primitiveSize.z)
			|| primitiveSize.x <= 0.0f
			|| primitiveSize.y <= 0.0f
			|| primitiveSize.z <= 0.0f
		)
			return false;

		const auto matrix =
			reinterpret_cast<const float*>(
				idColiObject + IdColiObjectMatrixOffset
			);
		glm::mat4 worldMatrix(1.0f);
		std::memcpy(
			glm::value_ptr(worldMatrix),
			matrix,
			sizeof(worldMatrix)
		);
		for(std::size_t index = 0; index < 16; index++) {
			if(!std::isfinite(glm::value_ptr(worldMatrix)[index]))
				return false;
		}

		transform = mm::Mat44(worldMatrix);
		size = mm::Vec3(primitiveSize);
		primitiveType = livePrimitiveType;
		return true;
	}

	TutorialTriggerEntry* FindTutorialTrigger(const void* tutorial) {
		for(std::size_t i = 0; i < tutorialTriggerCount; i++) {
			if(tutorialTriggers[i].tutorial == tutorial)
				return &tutorialTriggers[i];
		}
		return nullptr;
	}

	TutorialTriggerEntry* FindTutorialTriggerByFlag(int flagId) {
		if(flagId < minimumTutorialFlagId || flagId > maximumTutorialFlagId)
			return nullptr;

		for(std::size_t i = 0; i < tutorialTriggerCount; i++) {
			if(tutorialTriggers[i].flagId == flagId)
				return &tutorialTriggers[i];
		}
		return nullptr;
	}

	TutorialTriggerEntry* CaptureTutorialTrigger(void* tutorial) {
		if(xenomods::IsSceneTransitionActive())
			return nullptr;

		auto entry = FindTutorialTrigger(tutorial);
		if(entry == nullptr) {
			if(tutorialTriggerCount >= tutorialTriggers.size()) {
				if(!tutorialTriggerRegistryOverflowLogged) {
					xenomods::g_Logger->LogWarning(
						"[Tutorial trigger] Registry is full; additional tutorial triggers will be ignored"
					);
					tutorialTriggerRegistryOverflowLogged = true;
				}
				return nullptr;
			}

			entry = &tutorialTriggers[tutorialTriggerCount++];
			entry->tutorial = tutorial;
			entry->flagId = GetTutorialFlagId(tutorial);
			minimumTutorialFlagId = std::min(minimumTutorialFlagId, entry->flagId);
			maximumTutorialFlagId = std::max(maximumTutorialFlagId, entry->flagId);
			xenomods::g_Logger->LogInfo(
				"[Tutorial trigger] Discovered flag ID {} ({} total on this map)",
				entry->flagId,
				tutorialTriggerCount
			);
		} else {
			const int currentFlagId = GetTutorialFlagId(tutorial);
			if(entry->flagId != currentFlagId) {
				entry->flagId = currentFlagId;
				entry->repeatSuppressedUntilExit = false;
				minimumTutorialFlagId = std::min(minimumTutorialFlagId, currentFlagId);
				maximumTutorialFlagId = std::max(maximumTutorialFlagId, currentFlagId);
			}
		}

		entry->hasShape = GetTutorialTriggerBox(
			tutorial,
			entry->transform,
			entry->size,
			entry->primitiveType
		);
		entry->inside = IsInsideTutorialTrigger(tutorial);
		if(entry->hasShape && entry->renderStage == TutorialTriggerRenderStage::WaitingForFieldAssets) {
			const auto bytes = static_cast<const std::uint8_t*>(tutorial);
			const auto shapeData =
				*reinterpret_cast<const std::uint8_t* const*>(bytes + GmkShapeDataOffset);
			const auto shapeInfo =
				*reinterpret_cast<const std::uint8_t* const*>(bytes + GmkShapeInfoOffset);
			if(shapeData != nullptr && shapeInfo != nullptr) {
				const auto position = reinterpret_cast<const float*>(shapeData);
				const auto rotation = reinterpret_cast<const float*>(shapeData + 0x10);
				const auto dimensions =
					reinterpret_cast<const float*>(shapeData + GmkShapeSizeOffset);
				const int shapeType =
					*reinterpret_cast<const int*>(shapeInfo + GmkShapeTypeOffset);
				const glm::vec3 renderedSize = entry->size;
				xenomods::g_Logger->LogInfo(
					"[Tutorial trigger] Flag {} shape {} raw ({:.3f}, {:.3f}, {:.3f}), "
					"render size ({:.3f}, {:.3f}, {:.3f}), pos ({:.3f}, {:.3f}, {:.3f}), "
					"rot ({:.3f}, {:.3f}, {:.3f})",
					entry->flagId,
					shapeType,
					dimensions[0],
					dimensions[1],
					dimensions[2],
					renderedSize.x,
					renderedSize.y,
					renderedSize.z,
					position[0],
					position[1],
					position[2],
					rotation[0],
					rotation[1],
					rotation[2]
				);
				// Mark the discovery report as complete while the model is
				// waiting to be created.
				entry->renderStage = TutorialTriggerRenderStage::WaitingForTrigger;
			}
		}
		return entry;
	}

	int GetCutsceneEventId(const void* event) {
		const auto bytes = static_cast<const std::uint8_t*>(event);
		const auto bdatInfo =
			*reinterpret_cast<const std::uint8_t* const*>(
				bytes + GmkEventBdatInfoOffset
			);
		if(bdatInfo == nullptr)
			return -1;
		return *reinterpret_cast<const std::uint16_t*>(
			bdatInfo + GmkEventIdOffset
		);
	}

	CutsceneTriggerEntry* FindCutsceneTrigger(const void* event) {
		for(std::size_t i = 0; i < cutsceneTriggerCount; i++) {
			if(cutsceneTriggers[i].event == event)
				return &cutsceneTriggers[i];
		}
		return nullptr;
	}

	CutsceneTriggerEntry* CaptureCutsceneTrigger(void* event) {
		if(xenomods::IsSceneTransitionActive())
			return nullptr;

		auto entry = FindCutsceneTrigger(event);
		if(entry == nullptr) {
			if(cutsceneTriggerCount >= cutsceneTriggers.size()) {
				if(!cutsceneTriggerRegistryOverflowLogged) {
					xenomods::g_Logger->LogWarning(
						"[Cutscene trigger] Registry is full; additional event triggers will be ignored"
					);
					cutsceneTriggerRegistryOverflowLogged = true;
				}
				return nullptr;
			}

			entry = &cutsceneTriggers[cutsceneTriggerCount++];
			entry->event = event;
			entry->eventId = GetCutsceneEventId(event);
			xenomods::g_Logger->LogInfo(
				"[Cutscene trigger] Discovered event ID {} ({} total on this map)",
				entry->eventId,
				cutsceneTriggerCount
			);
		} else {
			entry->eventId = GetCutsceneEventId(event);
		}

		entry->previousInside = entry->inside;
		entry->hasShape = GetTutorialTriggerBox(
			event,
			entry->transform,
			entry->size,
			entry->primitiveType
		);
		entry->inside = IsInsideTutorialTrigger(event);

		if(
			logCutsceneTransformMetrics
			&& entry->inside
			&& !entry->previousInside
		) {
			const glm::mat4 transform = entry->transform;
			const glm::vec3 center = glm::vec3(transform[3]);
			const glm::vec3 size = entry->size;
			xenomods::g_Logger->LogInfo(
				"[Cutscene trigger] Event {} entered; primitive {}, center ({:.3f}, {:.3f}, {:.3f}), size ({:.3f}, {:.3f}, {:.3f})",
				entry->eventId,
				GetTutorialPrimitiveTypeName(entry->primitiveType),
				center.x,
				center.y,
				center.z,
				size.x,
				size.y,
				size.z
			);
		}

		return entry;
	}

	int GetLandmarkId(const void* landmark) {
		const auto bytes = static_cast<const std::uint8_t*>(landmark);
		return *reinterpret_cast<const int*>(bytes + LandmarkIdOffset);
	}

	LandmarkTriggerEntry* FindLandmarkTrigger(const void* landmark) {
		for(std::size_t i = 0; i < landmarkTriggerCount; i++) {
			if(landmarkTriggers[i].landmark == landmark)
				return &landmarkTriggers[i];
		}
		return nullptr;
	}

	LandmarkTriggerEntry* CaptureLandmarkTrigger(void* landmark) {
		if(xenomods::IsSceneTransitionActive())
			return nullptr;

		auto entry = FindLandmarkTrigger(landmark);
		if(entry == nullptr) {
			if(landmarkTriggerCount >= landmarkTriggers.size()) {
				if(!landmarkTriggerRegistryOverflowLogged) {
					xenomods::g_Logger->LogWarning(
						"[Landmark trigger] Registry is full; additional landmark triggers will be ignored"
					);
					landmarkTriggerRegistryOverflowLogged = true;
				}
				return nullptr;
			}

			entry = &landmarkTriggers[landmarkTriggerCount++];
			entry->landmark = landmark;
			entry->landmarkId = GetLandmarkId(landmark);
			entry->flagId = entry->landmarkId + LandmarkFlagIdBase;
			xenomods::g_Logger->LogInfo(
				"[Landmark trigger] Discovered landmark ID {}, flag ID {} ({} total on this map)",
				entry->landmarkId,
				entry->flagId,
				landmarkTriggerCount
			);
		} else {
			entry->landmarkId = GetLandmarkId(landmark);
			entry->flagId = entry->landmarkId + LandmarkFlagIdBase;
		}

		entry->previousInside = entry->inside;
		entry->hasShape = GetTutorialTriggerBox(
			landmark,
			entry->transform,
			entry->size,
			entry->primitiveType
		);
		entry->inside = IsInsideTutorialTrigger(landmark);

		if(
			logLandmarkTransformMetrics
			&& entry->inside
			&& !entry->previousInside
		) {
			const glm::mat4 transform = entry->transform;
			const glm::vec3 center = glm::vec3(transform[3]);
			const glm::vec3 size = entry->size;
			xenomods::g_Logger->LogInfo(
				"[Landmark trigger] Landmark {} entered; primitive {}, center ({:.3f}, {:.3f}, {:.3f}), size ({:.3f}, {:.3f}, {:.3f})",
				entry->landmarkId,
				GetTutorialPrimitiveTypeName(entry->primitiveType),
				center.x,
				center.y,
				center.z,
				size.x,
				size.y,
				size.z
			);
		}

		return entry;
	}

	void CaptureCollectionAccessParam(const CollectionAccessParam* param) {
		if(xenomods::IsSceneTransitionActive() || param == nullptr)
			return;
		if(
			!std::isfinite(param->forwardOffset)
			|| !std::isfinite(param->radius)
			|| !std::isfinite(param->upperHeight)
			|| !std::isfinite(param->lowerHeight)
			|| param->radius <= 0.0f
			|| param->upperHeight < 0.0f
			|| param->lowerHeight < 0.0f
		)
			return;

		const bool firstCapture = !collectionAccessParamValid;
		collectionAccessParam = *param;
		collectionAccessParamValid = true;
		if(firstCapture) {
			xenomods::g_Logger->LogInfo(
				"[Collection range] Captured live AccessParam: forward {:.3f}, radius {:.3f}, upper {:.3f}, lower {:.3f}",
				param->forwardOffset,
				param->radius,
				param->upperHeight,
				param->lowerHeight
			);
		}
	}

	void CaptureCollectionAccessResource(const void* plugin) {
		if(xenomods::IsSceneTransitionActive() || plugin == nullptr)
			return;

		// AccessPlugin owns a pointer to the shared access-parameter resource
		// at +0x18. Entry zero is the normal field A-button interaction range.
		const auto pluginBytes =
			static_cast<const std::uint8_t*>(plugin);
		const auto resource =
			*reinterpret_cast<const std::uint8_t* const*>(
				pluginBytes + 0x18
			);
		if(resource == nullptr)
			return;

		const std::uint32_t count =
			*reinterpret_cast<const std::uint32_t*>(resource + 0x0C);
		const auto entries =
			*reinterpret_cast<const std::uint8_t* const*>(
				resource + 0x10
			);
		if(count == 0 || entries == nullptr)
			return;

		const auto param =
			*reinterpret_cast<const CollectionAccessParam* const*>(
				entries + 0x08
			);
		CaptureCollectionAccessParam(param);
	}

	CollectionPointEntry* FindCollectionPoint(const void* collection) {
		for(std::size_t i = 0; i < collectionPointCount; i++) {
			if(collectionPoints[i].collection == collection)
				return &collectionPoints[i];
		}
		return nullptr;
	}

	CollectionPointEntry* FindCollectionPoint(gf::GF_OBJ_HANDLE* target) {
		for(std::size_t i = 0; i < collectionPointCount; i++) {
			if(collectionPoints[i].target == target)
				return &collectionPoints[i];
		}
		return nullptr;
	}

	CollectionPointEntry* CaptureCollectionPoint(void* collection) {
		if(xenomods::IsSceneTransitionActive() || collection == nullptr)
			return nullptr;

		auto entry = FindCollectionPoint(collection);
		if(entry == nullptr) {
			if(collectionPointCount >= collectionPoints.size()) {
				if(!collectionPointRegistryOverflowLogged) {
					xenomods::g_Logger->LogWarning(
						"[Collection range] Registry is full; additional collection points will be ignored"
					);
					collectionPointRegistryOverflowLogged = true;
				}
				return nullptr;
			}

			entry = &collectionPoints[collectionPointCount++];
			entry->collection = collection;
			xenomods::g_Logger->LogInfo(
				"[Collection range] Discovered collection point ({} total on this map)",
				collectionPointCount
			);
		}

		const auto bytes = static_cast<const std::uint8_t*>(collection);
		entry->collectionId =
			*reinterpret_cast<const int*>(bytes + CollectionIdOffset);
		entry->target =
			*reinterpret_cast<gf::GF_OBJ_HANDLE* const*>(
				bytes + CollectionObjectHandleOffset
			);
		entry->hasShape = false;
		const auto position =
			*reinterpret_cast<const mm::Vec3* const*>(
				bytes + CollectionPositionPointerOffset
			);
		if(position == nullptr)
			return entry;

		const float upperHeight = collectionAccessParamValid
			? collectionAccessParam.upperHeight
			: CollectionPointFallbackUpperHeight;
		const float lowerHeight = collectionAccessParamValid
			? collectionAccessParam.lowerHeight
			: CollectionPointFallbackLowerHeight;
		const float baseRadius = collectionAccessParamValid
			? collectionAccessParam.radius
			: CollectionPointFallbackRadius;
		const float radius = baseRadius;
		const float height = upperHeight + lowerHeight;
		const glm::vec3 collectionPosition = *position;
		if(
			!std::isfinite(collectionPosition.x)
			|| !std::isfinite(collectionPosition.y)
			|| !std::isfinite(collectionPosition.z)
			|| !std::isfinite(radius)
			|| !std::isfinite(height)
			|| radius <= 0.0f
			|| height <= 0.0f
		)
			return entry;

		// isActiveRange() tests the target against a player-centered cylinder:
		//   -lower <= targetY - accessY <= upper.
		// Invert that relation to draw the equivalent access-origin region
		// around the target. The upper/lower heights therefore swap sides.
		glm::vec3 center = collectionPosition;
		center.y += (lowerHeight - upperHeight) * 0.5f;

		glm::mat4 cylinderTransform(1.0f);
		cylinderTransform[3] = glm::vec4(center, 1.0f);
		entry->transform = mm::Mat44(cylinderTransform);
		entry->size = mm::Vec3(
			glm::vec3(radius * 2.0f, height, radius * 2.0f)
		);
		entry->hasShape = true;
		return entry;
	}

	template<typename TriggerEntry>
	bool CreateTriggerModel(
		TriggerEntry& entry,
		const TriggerModelAsset& asset
	) {
		if(entry.model != nullptr || !entry.hasShape)
			return false;

			static const auto gimmickVtable =
				skylaunch::hook::detail::ResolveSymbol<void**>(
					GfInitParamGimmickVtableSymbol
				);
			if(
				gimmickVtable == reinterpret_cast<void**>(skylaunch::hook::INVALID_FUNCTION_PTR)
				|| gimmickVtable == nullptr
			) {
				entry.renderStage = TutorialTriggerRenderStage::WaitingForFieldAssets;
				return false;
			}

			tutorialTriggerMapObjectBdat =
				gf::GfDataBdat::getFP(TutorialTriggerMapObjectBdatIndex);
			if(tutorialTriggerMapObjectBdat == nullptr) {
				entry.renderStage = TutorialTriggerRenderStage::WaitingForFieldAssets;
				return false;
			}
			if(validatedTriggerResourceBdat != tutorialTriggerMapObjectBdat) {
				validatedTriggerResourceBdat = tutorialTriggerMapObjectBdat;
				triggerResourceValidation = {};
			}
			if(asset.resourceId >= triggerResourceValidation.size()) {
				entry.renderStage = TutorialTriggerRenderStage::WaitingForFieldAssets;
				return false;
			}
			auto& validation = triggerResourceValidation[asset.resourceId];
			if(validation == 0) {
				const char* modelName = reinterpret_cast<const char*>(
					Bdat::getVal(
						tutorialTriggerMapObjectBdat,
						"Model",
						asset.resourceId
					)
				);
				validation =
					modelName != nullptr
					&& std::strcmp(modelName, asset.modelName) == 0
						? 1
						: 2;
				if(validation == 1) {
					xenomods::g_Logger->LogInfo(
						"[Trigger renderer] Using unused RSC_MapObjList ID {} ({})",
						asset.resourceId,
						asset.modelName
					);
				} else {
					xenomods::g_Logger->LogWarning(
						"[Trigger renderer] RSC_MapObjList ID {} expected {} but found {}",
						asset.resourceId,
						asset.modelName,
						modelName != nullptr ? modelName : "<null>"
					);
				}
			}
			if(validation != 1) {
				entry.renderStage = TutorialTriggerRenderStage::WaitingForFieldAssets;
				return false;
			}

			gf::GfInitParamGimmick parameters {};
			// An Itanium ABI vtable symbol begins with offset-to-top and RTTI;
			// the object stores the address point at the first virtual function.
			parameters.vtable = gimmickVtable + 2;
			parameters.objectType = 8;
			parameters.resourceId = asset.resourceId;
			parameters.resourceBdat = &tutorialTriggerMapObjectBdat;
			parameters.field20 = -1;
			parameters.field28 = -1;
			parameters.followObject0 = UINT64_MAX;
			parameters.followObject1 = UINT64_MAX;
			parameters.field40 = 4;
			parameters.field4C = 1.0f;
			parameters.field50 = 1.0f;
			parameters.field81 = 1;

			entry.renderStage = TutorialTriggerRenderStage::CreatingModel;
			entry.model = gf::GfObjFactory::createMapObj(parameters);
			entry.modelBoundsReady = false;
			if(
				entry.model == nullptr
				|| entry.model == reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1)
			) {
				entry.model = nullptr;
				entry.renderStage = TutorialTriggerRenderStage::WaitingForFieldAssets;
				return false;
			}
			entry.renderStage = TutorialTriggerRenderStage::LoadingModel;
			return true;
	}

	template<typename TriggerEntry>
	void UpdateTriggerModel(
		TriggerEntry& entry,
		bool allowCreate,
		float triggerAlpha,
		const TriggerModelAsset& asset
	) {
		if(!entry.hasShape)
			return;

		if(entry.model == nullptr) {
			if(allowCreate)
				CreateTriggerModel(entry, asset);
			return;
		}

		gf::GfObjAcc objectAccessor(entry.model);
		objectAccessor.setDisp(gf::OBJDISP::Normal, true);
		objectAccessor.setDisp(gf::OBJDISP::Event, true);
		objectAccessor.setDisp(gf::OBJDISP::Field, true);
		objectAccessor.setAlphaNormal(triggerAlpha);
		// The object was created outside the map's placement list. Disable only
		// its frustum/GPU clipping; depth testing remains part of the normal
		// scene render and still occludes it behind map geometry.
		objectAccessor.setClip(false);
		objectAccessor.setGpuClip(false);

		auto component = gf::GfObjUtil::getComModel(entry.model);
		if(component == nullptr) {
			entry.renderStage = TutorialTriggerRenderStage::WaitingForObjectComponent;
			return;
		}

		auto modelObject = component->getModelObject();
		if(modelObject == nullptr) {
			entry.renderStage = TutorialTriggerRenderStage::WaitingForSceneModel;
			return;
		}

		auto sceneModel = modelObject->getInterface();
		if(sceneModel == nullptr) {
			entry.renderStage = TutorialTriggerRenderStage::WaitingForSceneModel;
			return;
		}

		ml::ScnObjAccResMdlInfo modelInfo(sceneModel);
		if(!entry.modelBoundsReady) {
			modelInfo.getInitialVertexAABBMin(entry.modelMin, false);
			modelInfo.getInitialVertexAABBMax(entry.modelMax, false);

			const glm::vec3 minimum = entry.modelMin;
			const glm::vec3 maximum = entry.modelMax;
			const glm::vec3 extent = maximum - minimum;
			entry.modelBoundsReady =
				std::isfinite(extent.x) && std::isfinite(extent.y) && std::isfinite(extent.z)
				&& extent.x > 0.0001f && extent.y > 0.0001f && extent.z > 0.0001f;

			if(!entry.modelBoundsReady) {
				entry.renderStage = TutorialTriggerRenderStage::InvalidModelBounds;
				return;
			}
		}

		const glm::vec3 minimum = entry.modelMin;
		const glm::vec3 maximum = entry.modelMax;
		// Preserve oj900401's authored center, including Y 0.96. Its reported
		// decoded visible vertices span exactly 1.92 units on every axis, so
		// use that measured extent for scale calibration.
		const glm::vec3 sourceExtent = asset.sourceExtent;
		const glm::vec3 sourceCenter = (minimum + maximum) * 0.5f;
		const glm::vec3 desiredSize = entry.size;
		const glm::vec3 scale = desiredSize / sourceExtent;
		const glm::mat4 triggerTransform = entry.transform;
		const glm::mat3 triggerBasis = glm::mat3(triggerTransform);
		const glm::vec3 triggerBasisScale(
			glm::length(triggerBasis[0]),
			glm::length(triggerBasis[1]),
			glm::length(triggerBasis[2])
		);
		if(
			triggerBasisScale.x <= 0.0001f
			|| triggerBasisScale.y <= 0.0001f
			|| triggerBasisScale.z <= 0.0001f
		) {
			entry.renderStage = TutorialTriggerRenderStage::InvalidModelBounds;
			return;
		}
		glm::mat3 triggerRotation = triggerBasis;
		triggerRotation[0] /= triggerBasisScale.x;
		triggerRotation[1] /= triggerBasisScale.y;
		triggerRotation[2] /= triggerBasisScale.z;
		const glm::vec3 objectScale = triggerBasisScale * scale;
		const glm::vec3 triggerPosition = glm::vec3(triggerTransform[3]);
		const glm::vec3 objectPosition =
			triggerPosition - triggerRotation * (objectScale * sourceCenter);
		const glm::quat objectRotation =
			glm::normalize(glm::quat_cast(triggerRotation));

		// Update the owning GfObj as well as its render interface. Field
		// culling and the model component both consume this transform.
		fw::Transform objectTransform {};
		objectTransform.position = mm::Vec3(objectPosition);
		objectTransform.rotation = mm::Quat(objectRotation);
		gf::GfObjUtil::setWarpTransform(
			entry.model,
			objectTransform,
			false
		);
		objectAccessor.setModelScaleForMenu(mm::Vec3(objectScale));

		// GfComModel derives its scene matrix from this owning object's
		// transform. Do not also call ScnObjAccResMdlInfo::setMatrix() here:
		// doing both applies the phantom transform twice and distorts its
		// position and proportions.
		// Keep this out of XC2's temporal/special-alpha path. That mode caused
		// the partially transparent box to break into flashing screen slices.
		modelInfo.setSpAlphaMode(false);
		modelInfo.setDblBuffAlpha(false);
		modelInfo.setAlpha(triggerAlpha);
		modelInfo.setSimpleShadow(false);
		modelInfo.setGpuClip(false);
		// The driver maintains its own camera and occlusion checks in addition
		// to the GfObj and ScnObj clip flags. Their authored bounds describe the
		// small stock cube and are invalid after scaling it to a large trigger.
		modelInfo.setCamCheckMode(false);
		modelInfo.setOcclusionQuery(false);
		sceneModel->setClip(false);

		entry.renderStage = TutorialTriggerRenderStage::Rendering;
	}

	void UpdateTutorialTriggerModel(
		TutorialTriggerEntry& entry,
		bool allowCreate
	) {
		const auto& asset = SelectTriggerAsset(
			entry.primitiveType,
			TutorialBoxAsset,
			TutorialSphereAsset,
			TutorialCapsuleAsset
		);
		UpdateTriggerModel(
			entry,
			allowCreate,
			TutorialTriggerAlpha,
			asset
		);
	}

	void UpdateTutorialTriggerModels() {
		if(!xenomods::DebugStuff::renderTutorialTrigger) {
			SetTutorialTriggerRenderStage(TutorialTriggerRenderStage::Disabled);
			return;
		}

		if(tutorialTriggerCount == 0) {
			SetTutorialTriggerRenderStage(TutorialTriggerRenderStage::WaitingForTrigger);
			return;
		}

		bool createdModel = false;
		bool anyRendering = false;
		TutorialTriggerRenderStage pendingStage =
			TutorialTriggerRenderStage::WaitingForFieldAssets;
		for(std::size_t i = 0; i < tutorialTriggerCount; i++) {
			auto& entry = tutorialTriggers[i];
			const bool canCreate = !createdModel && entry.model == nullptr;
			UpdateTutorialTriggerModel(entry, canCreate);
			if(canCreate && entry.model != nullptr)
				createdModel = true;
			if(entry.renderStage == TutorialTriggerRenderStage::Rendering)
				anyRendering = true;
			else
				pendingStage = entry.renderStage;
		}

		SetTutorialTriggerRenderStage(
			anyRendering ? TutorialTriggerRenderStage::Rendering : pendingStage
		);
	}

	void UpdateCutsceneTriggerModels() {
		if(!xenomods::DebugStuff::renderCutsceneTrigger)
			return;

		bool createdModel = false;
		for(std::size_t i = 0; i < cutsceneTriggerCount; i++) {
			auto& entry = cutsceneTriggers[i];
			const auto& asset = SelectTriggerAsset(
				entry.primitiveType,
				CutsceneBoxAsset,
				CutsceneSphereAsset,
				CutsceneCapsuleAsset
			);
			if(entry.modelNameChecked && !entry.modelNameValid)
				continue;
			const bool canCreate = !createdModel && entry.model == nullptr;
			UpdateTriggerModel(
				entry,
				canCreate,
				CutsceneTriggerAlpha,
				asset
			);
			if(canCreate && entry.model != nullptr)
				createdModel = true;

			if(entry.model != nullptr && !entry.modelNameChecked) {
				const char* actualModelName =
					gf::GfObjUtil::getModelResourceName(entry.model);
				if(actualModelName != nullptr && actualModelName[0] != '\0') {
					std::strncpy(
						entry.actualModelName,
						actualModelName,
						sizeof(entry.actualModelName) - 1
					);
					entry.actualModelName[
						sizeof(entry.actualModelName) - 1
					] = '\0';
					entry.modelNameChecked = true;
					entry.modelNameValid =
						std::strstr(
							entry.actualModelName,
							asset.modelName
						) != nullptr;
					xenomods::g_Logger->LogInfo(
						"[Cutscene trigger] Requested {}, created {} ({})",
						asset.modelName,
						entry.actualModelName,
						entry.modelNameValid ? "verified" : "rejected"
					);
					if(!entry.modelNameValid)
						DestroyCutsceneTriggerModel(entry);
				}
			}
		}
	}

	void UpdateLandmarkTriggerModels() {
		if(!xenomods::DebugStuff::renderLandmarkTrigger)
			return;

		bool createdModel = false;
		for(std::size_t i = 0; i < landmarkTriggerCount; i++) {
			auto& entry = landmarkTriggers[i];
			const auto& asset = SelectTriggerAsset(
				entry.primitiveType,
				LandmarkBoxAsset,
				LandmarkSphereAsset,
				LandmarkCapsuleAsset
			);
			if(entry.modelNameChecked && !entry.modelNameValid)
				continue;
			const bool canCreate = !createdModel && entry.model == nullptr;
			UpdateTriggerModel(
				entry,
				canCreate,
				LandmarkTriggerAlpha,
				asset
			);
			if(canCreate && entry.model != nullptr)
				createdModel = true;

			if(entry.model != nullptr && !entry.modelNameChecked) {
				const char* actualModelName =
					gf::GfObjUtil::getModelResourceName(entry.model);
				if(actualModelName != nullptr && actualModelName[0] != '\0') {
					std::strncpy(
						entry.actualModelName,
						actualModelName,
						sizeof(entry.actualModelName) - 1
					);
					entry.actualModelName[
						sizeof(entry.actualModelName) - 1
					] = '\0';
					entry.modelNameChecked = true;
					entry.modelNameValid =
						std::strstr(
							entry.actualModelName,
							asset.modelName
						) != nullptr;
					xenomods::g_Logger->LogInfo(
						"[Landmark trigger] Requested {}, created {} ({})",
						asset.modelName,
						entry.actualModelName,
						entry.modelNameValid ? "verified" : "rejected"
					);
					if(!entry.modelNameValid)
						DestroyLandmarkTriggerModel(entry);
				}
			}
		}
	}

	void UpdateCollectionPointModels() {
		if(!xenomods::DebugStuff::renderCollectionPointRange)
			return;

		mm::Vec3 playerPositionMm {};
		float playerRotation = 0.0f;
		bool playerPositionValid = false;
		auto leader = gf::GfGameParty::getLeader();
		if(
			leader != nullptr
			&& leader != reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1)
		) {
			gf::GfObjAcc playerAccessor(leader);
			playerPositionValid =
				playerAccessor.getObjPosRot(
					playerPositionMm,
					playerRotation
				);
		}
		const glm::vec3 playerPosition = playerPositionMm;

		std::size_t nearestMissing = collectionPointCount;
		float nearestMissingDistanceSquared =
			CollectionPointRenderDistance
			* CollectionPointRenderDistance;
		nearestCollectionPointDistance = -1.0f;
		nearbyCollectionPointCount = 0;

		for(std::size_t i = 0; i < collectionPointCount; i++) {
			auto& entry = collectionPoints[i];
			CaptureCollectionPoint(entry.collection);
			if(!entry.hasShape) {
				if(
					entry.model != nullptr
					&& entry.model
						!= reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1)
				) {
					gf::GfObjAcc accessor(entry.model);
					accessor.setDisp(gf::OBJDISP::Normal, false);
					accessor.setDisp(gf::OBJDISP::Event, false);
					accessor.setDisp(gf::OBJDISP::Field, false);
				}
				continue;
			}

			const glm::mat4 triggerTransform = entry.transform;
			const glm::vec3 pointPosition =
				glm::vec3(triggerTransform[3]);
			const float distanceSquared = playerPositionValid
				? glm::dot(
					pointPosition - playerPosition,
					pointPosition - playerPosition
				)
				: 0.0f;
			const bool nearby =
				!playerPositionValid
				|| distanceSquared
					<= CollectionPointRenderDistance
						* CollectionPointRenderDistance;
			if(!nearby) {
				if(
					entry.model != nullptr
					&& entry.model
						!= reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1)
				) {
					gf::GfObjAcc accessor(entry.model);
					accessor.setDisp(gf::OBJDISP::Normal, false);
					accessor.setDisp(gf::OBJDISP::Event, false);
					accessor.setDisp(gf::OBJDISP::Field, false);
				}
				continue;
			}

			nearbyCollectionPointCount++;
			if(
				entry.model == nullptr
				&& (
					nearestMissing == collectionPointCount
					|| distanceSquared < nearestMissingDistanceSquared
				)
			) {
				nearestMissing = i;
				nearestMissingDistanceSquared = distanceSquared;
			}
		}

		if(nearestMissing != collectionPointCount) {
			auto& entry = collectionPoints[nearestMissing];
			UpdateTriggerModel(
				entry,
				true,
				CollectionPointRangeAlpha,
				CollectionPointCylinderAsset
			);
			nearestCollectionPointRenderStage = entry.renderStage;
			nearestCollectionPointDistance =
				playerPositionValid
					? std::sqrt(nearestMissingDistanceSquared)
					: 0.0f;
		} else {
			nearestCollectionPointRenderStage =
				TutorialTriggerRenderStage::WaitingForTrigger;
		}

		for(std::size_t i = 0; i < collectionPointCount; i++) {
			auto& entry = collectionPoints[i];
			if(!entry.hasShape || entry.model == nullptr)
				continue;

			const glm::mat4 triggerTransform = entry.transform;
			const glm::vec3 pointPosition =
				glm::vec3(triggerTransform[3]);
			const float distanceSquared = playerPositionValid
				? glm::dot(
					pointPosition - playerPosition,
					pointPosition - playerPosition
				)
				: 0.0f;
			if(
				playerPositionValid
				&& distanceSquared
					> CollectionPointRenderDistance
						* CollectionPointRenderDistance
			)
				continue;

			UpdateTriggerModel(
				entry,
				false,
				CollectionPointRangeAlpha,
				CollectionPointCylinderAsset
			);
			if(
				nearestCollectionPointDistance < 0.0f
				|| distanceSquared
					< nearestCollectionPointDistance
						* nearestCollectionPointDistance
			) {
				nearestCollectionPointDistance =
					playerPositionValid
						? std::sqrt(distanceSquared)
						: 0.0f;
				nearestCollectionPointRenderStage =
					entry.renderStage;
			}
		}
	}

	constexpr std::size_t MaxTutorialCallSites = 32;
	std::array<std::uintptr_t, MaxTutorialCallSites> tutorialCallSites {};
	std::size_t tutorialCallSiteCount = 0;
	bool tutorialCallSiteOverflowLogged = false;

	std::uintptr_t GetGhidraAddress(std::uintptr_t runtimeAddress) {
		if(runtimeAddress >= skylaunch::utils::g_MainTextAddr && runtimeAddress < reinterpret_cast<std::uintptr_t>(&__module_start))
			return runtimeAddress - skylaunch::utils::g_MainTextAddr + dbgutil::TEXT_OFFSET;
		return 0;
	}

	void ResetTutorialCallSiteTrace() {
		tutorialCallSites = {};
		tutorialCallSiteCount = 0;
		tutorialCallSiteOverflowLogged = false;
	}

	void TraceTutorialCallSite(unsigned int bitSize, int id, unsigned int value, std::uintptr_t caller) {
		for(std::size_t i = 0; i < tutorialCallSiteCount; i++) {
			if(tutorialCallSites[i] == caller)
				return;
		}

		if(tutorialCallSiteCount >= tutorialCallSites.size()) {
			if(!tutorialCallSiteOverflowLogged) {
				xenomods::g_Logger->LogWarning("[Tutorial caller trace] Call-site table is full");
				tutorialCallSiteOverflowLogged = true;
			}
			return;
		}

		tutorialCallSites[tutorialCallSiteCount++] = caller;
		const auto ghidraAddress = GetGhidraAddress(caller);
		xenomods::g_Logger->LogInfo(
			"[Tutorial caller trace] Flag ID {} ({} bits, value {}) called from runtime {:#x}, Ghidra {:#x}: {}",
			id,
			bitSize,
			value,
			caller,
			ghidraAddress,
			dbgutil::getSymbol(caller)
		);

		const auto stack = dbgutil::getStackTrace();
		for(std::size_t i = 1; i < 8 && stack[i] != 0; i++) {
			xenomods::g_Logger->LogInfo(
				"[Tutorial caller trace]   stack[{}] runtime {:#x}, Ghidra {:#x}: {}",
				i,
				stack[i],
				GetGhidraAddress(stack[i]),
				dbgutil::getSymbol(stack[i])
			);
		}
	}

	void ResetTutorialRepeatCycle() {
		for(std::size_t i = 0; i < tutorialTriggerCount; i++)
			tutorialTriggers[i].repeatSuppressedUntilExit = false;
	}

	void ResetLocalGameFlagTrace() {
		observedLocalGameFlags = {};
		localGameFlagTraceOverflowLogged = false;
		xenomods::DebugStuff::lastChangedLocalFlagId = -1;
		xenomods::DebugStuff::lastChangedLocalFlagBitSize = 1;
	}

	void ObserveLocalGameFlag(unsigned int bitSize, int id, unsigned int value) {
		const unsigned int hash = (static_cast<unsigned int>(id) * 2654435761U) ^ (bitSize * 2246822519U);
		const std::size_t firstIndex = hash & (MaxObservedLocalGameFlags - 1);

		for(std::size_t probe = 0; probe < MaxObservedLocalGameFlags; probe++) {
			auto& observed = observedLocalGameFlags[(firstIndex + probe) & (MaxObservedLocalGameFlags - 1)];

			if(!observed.occupied) {
				observed = {bitSize, id, value, true};
				return;
			}

			if(observed.bitSize != bitSize || observed.id != id)
				continue;

			if(observed.value == 0 && value != 0) {
				xenomods::DebugStuff::lastChangedLocalFlagId = id;
				xenomods::DebugStuff::lastChangedLocalFlagBitSize = static_cast<int>(bitSize);
				xenomods::g_Logger->LogInfo(
					"[Tutorial flag trace] Local flag ID {} ({} bits) changed from 0 to {}",
					id,
					bitSize,
					value
				);
			}

			observed.value = value;
			return;
		}

		if(!localGameFlagTraceOverflowLogged) {
			xenomods::g_Logger->LogWarning(
				"[Tutorial flag trace] Observation table is full; disable and re-enable tracing to reset it"
			);
			localGameFlagTraceOverflowLogged = true;
		}
	}

	struct LocalGameFlagHook : skylaunch::hook::Trampoline<LocalGameFlagHook> {
		static unsigned int Hook(unsigned int bitSize, int id) {
			const unsigned int value = Orig(bitSize, id);

			if(xenomods::DebugStuff::traceLocalGameFlags)
				ObserveLocalGameFlag(bitSize, id, value);

			const bool isSelectedTutorialFlag =
				xenomods::DebugStuff::tutorialFlagId >= 0
					&& id == xenomods::DebugStuff::tutorialFlagId
					&& static_cast<int>(bitSize) == xenomods::DebugStuff::tutorialFlagBitSize;
			auto tutorialEntry =
				bitSize == 1 ? FindTutorialTriggerByFlag(id) : nullptr;

			if(xenomods::DebugStuff::traceTutorialCallSites && isSelectedTutorialFlag) {
				const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
				TraceTutorialCallSite(bitSize, id, value, caller);
			}

			if(
				xenomods::DebugStuff::repeatTutorialFlag
					&& tutorialEntry != nullptr
			) {
				if(!xenomods::DebugStuff::pauseTutorialRepeatUntilExit)
					return 0;

				// Every GmkTutorial update continues seeing 0 so its phantom
				// remains active. Other readers see the completed value after
				// beginTutorial() until this specific trigger reports an exit.
				if(
					(updatingTutorial != nullptr
						&& GetTutorialFlagId(updatingTutorial) == id)
					|| !tutorialEntry->repeatSuppressedUntilExit
				)
					return 0;

				return value;
			}

			return value;
		}
	};

	struct TutorialBeginHook : skylaunch::hook::Trampoline<TutorialBeginHook> {
		static void Hook(void* tutorial) {
			auto entry = CaptureTutorialTrigger(tutorial);
			Orig(tutorial);
			// Only a real beginTutorial() dispatch may authorize Targeting to run
			// through a tutorial-owned control lock. Merely standing inside the
			// trigger volume is insufficient: that state can outlive the tutorial
			// and overlap collection-point interactions.
			tutorialControlTrigger = tutorial;
			tutorialControlLockObserved = false;
			tutorialControlLockArmFrames = 30;
			if(entry != nullptr) {
				entry = CaptureTutorialTrigger(tutorial);
				xenomods::g_Logger->LogInfo(
					"[Tutorial trigger] Flag ID {} began (phantom inside: {})",
					entry->flagId,
					entry->inside
				);
				if(logTutorialTransformMetrics)
					LogTutorialTriggerMetrics(*entry);
			}

			if(
				xenomods::DebugStuff::repeatTutorialFlag
				&& xenomods::DebugStuff::pauseTutorialRepeatUntilExit
				&& entry != nullptr
			) {
				entry->repeatSuppressedUntilExit = true;
				xenomods::g_Logger->LogInfo(
					"[Tutorial repeat] Flag ID {} began; suppressing repeats until its phantom reports exit",
					entry->flagId
				);
			}
		}
	};

	struct TutorialUpdateHook : skylaunch::hook::Trampoline<TutorialUpdateHook> {
		static void Hook(void* tutorial, float deltaTime) {
			auto entry = CaptureTutorialTrigger(tutorial);
			void* previousUpdatingTutorial = updatingTutorial;
			updatingTutorial = tutorial;

			Orig(tutorial, deltaTime);

			updatingTutorial = previousUpdatingTutorial;
			entry = CaptureTutorialTrigger(tutorial);

			if(
				entry != nullptr
				&& entry->repeatSuppressedUntilExit
				&& !entry->inside
			) {
				entry->repeatSuppressedUntilExit = false;
				xenomods::g_Logger->LogInfo(
					"[Tutorial repeat] Flag ID {} reported trigger exit; repeat re-armed",
					entry->flagId
				);
			}
		}
	};

	struct CutsceneEventUpdateHook
		: skylaunch::hook::Trampoline<CutsceneEventUpdateHook> {
		static void Hook(void* event, float deltaTime) {
			CaptureCutsceneTrigger(event);
			Orig(event, deltaTime);
			CaptureCutsceneTrigger(event);
		}
	};

	struct LandmarkUpdateHook
		: skylaunch::hook::Trampoline<LandmarkUpdateHook> {
		static void Hook(void* landmark, float deltaTime) {
			CaptureLandmarkTrigger(landmark);
			Orig(landmark, deltaTime);
			CaptureLandmarkTrigger(landmark);
		}
	};

	struct CollectionUpdateHook
		: skylaunch::hook::Trampoline<CollectionUpdateHook> {
		static void Hook(void* collection, float deltaTime) {
			CaptureCollectionPoint(collection);
			Orig(collection, deltaTime);
			CaptureCollectionPoint(collection);
		}
	};

	struct CollectionAccessedHook
		: skylaunch::hook::Trampoline<CollectionAccessedHook> {
		static void Hook(
			void* collection,
			gf::GF_OBJ_HANDLE* player
		) {
			if(xenomods::DebugStuff::infiniteCollectionPoints)
				return;

			Orig(collection, player);
		}
	};

	struct DropitemInitializeHook
		: skylaunch::hook::Trampoline<DropitemInitializeHook> {
		static void Hook(void* dropitem) {
			Orig(dropitem);
			if(!xenomods::DebugStuff::minimumCollectionItemDistance)
				return;

			auto bytes = static_cast<std::uint8_t*>(dropitem);
			const auto param = *reinterpret_cast<const std::uint8_t* const*>(
				bytes + DropitemParamOffset
			);
			if(param == nullptr)
				return;

			const float minimumDistance = *reinterpret_cast<const float*>(
				param + DropitemParamDistanceOffset
			);
			const float travelTime = *reinterpret_cast<const float*>(
				param + DropitemParamTimeOffset
			);
			auto velocity = reinterpret_cast<glm::vec3*>(
				bytes + DropitemVelocityOffset
			);
			const float horizontalSpeed = std::sqrt(
				velocity->x * velocity->x + velocity->z * velocity->z
			);
			if(
				!std::isfinite(minimumDistance)
				|| !std::isfinite(travelTime)
				|| travelTime <= 0.0f
				|| horizontalSpeed <= 0.0001f
			)
				return;

			const float minimumHorizontalSpeed = minimumDistance / travelTime * 0.5f;
			const float scale = minimumHorizontalSpeed / horizontalSpeed;
			velocity->x *= scale;
			velocity->z *= scale;
			*reinterpret_cast<float*>(bytes + DropitemDistanceOffset) = minimumDistance;
		}
	};

	struct AccessPluginSetupHook
		: skylaunch::hook::Trampoline<AccessPluginSetupHook> {
		static std::uint64_t Hook(
			void* plugin,
			gf::GfComBehaviorPc* behavior
		) {
			const std::uint64_t result = Orig(plugin, behavior);
			CaptureCollectionAccessResource(plugin);
			return result;
		}
	};

	struct AccessPluginUpdateHook
		: skylaunch::hook::Trampoline<AccessPluginUpdateHook> {
		static void Hook(
			void* plugin,
			gf::GfComBehaviorPc* behavior,
			const fw::UpdateInfo* updateInfo
		) {
			// setup() may run before xenomods installs its hooks. update() is
			// continuous, so this reliably recovers the already-initialized
			// resource used by the visible A-button prompt.
			CaptureCollectionAccessResource(plugin);
			Orig(plugin, behavior, updateInfo);
			CaptureCollectionAccessResource(plugin);
		}
	};

	struct AccessRangeHook
		: skylaunch::hook::Trampoline<AccessRangeHook> {
		static bool Hook(
			gf::GfComPropertyPc* property,
			const CollectionAccessParam& param,
			gf::GF_OBJ_HANDLE* target
		) {
			CaptureCollectionAccessParam(&param);
			const bool inside = Orig(property, param, target);
			auto entry = FindCollectionPoint(target);
			if(entry != nullptr)
				entry->inside = inside;
			return inside;
		}
	};

	struct TitleSkipEventHook : skylaunch::hook::Trampoline<TitleSkipEventHook> {
		static void Hook(void* state, tl::TitleMain* titleMain) {
			if(!reloadPrimarySavePending) {
				Orig(state, titleMain);
				return;
			}

			// playTitleEvent() blocks while the title background event runs,
			// then performs these two renderer cleanup operations. Preserve
			// that cleanup without constructing or displaying the title scene.
			fw::RenderParam::resetScene();
			ml::ScnRenderDrSysParmAcc renderParams;
			renderParams.weatherFrmEnd();
			xenomods::g_Logger->LogInfo(
				"[Reload save] Skipped title event and reset its render state"
			);
		}
	};

	struct TitleSkipMenuHook : skylaunch::hook::Trampoline<TitleSkipMenuHook> {
		static bool Hook(void* state, tl::TitleMain* titleMain, bool unk) {
			if(reloadPrimarySavePending)
				return false;

			return Orig(state, titleMain, unk);
		}
	};

	struct TitleContinueHook : skylaunch::hook::Trampoline<TitleContinueHook> {
		static bool Hook(void* state, tl::TitleMain* titleMain) {
			if(reloadPrimarySavePending && titleMain != nullptr) {
				*reinterpret_cast<unsigned int*>(
					reinterpret_cast<std::uintptr_t>(titleMain)
					+ TitleMenuOffset
				) = TitleMenuResultContinue;
				reloadPrimarySavePending = false;
				xenomods::g_Logger->LogInfo(
					"[Reload save] Skipped title menu and submitted Continue"
				);

				// Returning false sends TitleStateMainScreen into its native
				// menu-destroy/Continue transition. The required title event
				// and render-scene synchronization have already completed.
				return false;
			}

			return Orig(state, titleMain);
		}
	};

	struct BGMDebugging : skylaunch::hook::Trampoline<BGMDebugging> {
		static void Hook(gf::BgmTrack* this_pointer, fw::UpdateInfo* updateInfo) {
			Orig(this_pointer, updateInfo);

			if(!xenomods::DebugStuff::enableDebugRendering)
				return;

			const int height = xenomods::debug::drawFontGetHeight();
			if(this_pointer->getTrackName() != nullptr) {
				std::string trackName = this_pointer->getTrackName();

				if(trackName == "EventBGM")
					return; // already shown by event::BgmManager

				if(this_pointer->isPlaying()) {
					mm::mtl::FixStr<64> bgmFileName {};

					if(!this_pointer->makePlayFileName(bgmFileName)) {
						// failed to make a filename, just fall back to playingBgmFileName
						bgmFileName.set(this_pointer->playingBgmFileName);
					}

					xenomods::debug::drawFontFmtShadow(0, 720 - (xenomods::DebugStuff::bgmTrackIndex++ * height) - height, mm::Col4::white,
													   "{}: {} {:.1f}/{:.1f}{}", trackName, bgmFileName.buffer, this_pointer->getPlayTime(), this_pointer->getTotalTime(), this_pointer->isLoop() ? " (∞)" : "");
				} else {
					// uncomment if you want every BgmTrack instance to show
					//xenomods::debug::drawFontFmtShadow(0, 720 - (xenomods::DebugStuff::bgmTrackIndex++ * height) - height, mm::Col4::white, "{}: not playing", trackName);
				}
			}
		}
	};

	struct JumpToClosedLandmarks_CanEnterMap : skylaunch::hook::Trampoline<JumpToClosedLandmarks_CanEnterMap> {
		static bool Hook(unsigned int mapjump) {
			return xenomods::DebugStuff::accessClosedLandmarks || Orig(mapjump);
		}
	};

	struct JumpToClosedLandmarks_CheckCondition : skylaunch::hook::Trampoline<JumpToClosedLandmarks_CheckCondition> {
		static bool Hook(unsigned int mapjump, mm::Pnt<short>* pos) {
			bool result = Orig(mapjump, pos);
			return xenomods::DebugStuff::accessClosedLandmarks || result;
		}
	};

	struct JumpToClosedLandmarks_WorldMap : skylaunch::hook::Trampoline<JumpToClosedLandmarks_WorldMap> {
		static bool Hook(const gf::MenuZoneMapInfo& info) {
			bool result = Orig(info);
			return xenomods::DebugStuff::accessClosedLandmarks || result;
		}
	};
	struct JumpToClosedLandmarks_ZoneMap : skylaunch::hook::Trampoline<JumpToClosedLandmarks_ZoneMap> {
		static bool Hook(const gf::MenuZoneMapInfo& info) {
			bool result = Orig(info);
			return xenomods::DebugStuff::accessClosedLandmarks || result;
		}
	};
	struct JumpToClosedLandmarks_IsFound : skylaunch::hook::Trampoline<JumpToClosedLandmarks_IsFound> {
		static bool Hook(gmk::GmkLandmark* this_pointer) {
			bool result = Orig(this_pointer);
			return xenomods::DebugStuff::accessClosedLandmarks || result;
		}
	};
#endif

#if XENOMODS_CODENAME(bfsw)
	struct EnableDebugUnlockAll : skylaunch::hook::Trampoline<EnableDebugUnlockAll> {
		static bool Hook(fw::Document* doc) {
			// the original always returns 0
			return xenomods::DebugStuff::enableDebugUnlockAll;
		}
	};

	struct AlwaysAbleToOpenMenu : skylaunch::hook::Trampoline<AlwaysAbleToOpenMenu> {
		static bool Hook(fw::Document* doc) {
			return !xenomods::DebugStuff::enableDebugUnlockAll && Orig(doc);
		}
	};
#endif
} // namespace

namespace xenomods {

	bool DebugStuff::enableDebugRendering = false;
	bool DebugStuff::enableDebugUnlockAll = false;
	bool DebugStuff::accessClosedLandmarks = false;
	bool DebugStuff::pauseEnable = false;
	bool DebugStuff::enableMemoryDebug = false;
	bool DebugStuff::repeatTutorialFlag = false;
	bool DebugStuff::pauseTutorialRepeatUntilExit = true;
	bool DebugStuff::renderTutorialTrigger = false;
	bool DebugStuff::renderCutsceneTrigger = false;
	bool DebugStuff::renderLandmarkTrigger = false;
	bool DebugStuff::renderCollectionPointRange = false;
	bool DebugStuff::infiniteCollectionPoints = false;
	bool DebugStuff::minimumCollectionItemDistance = true;
	bool DebugStuff::traceLocalGameFlags = false;
	bool DebugStuff::traceTutorialCallSites = false;
	bool DebugStuff::showTriggerVisualizer = false;

	std::int8_t DebugStuff::pauseStepForward = 0;
	int DebugStuff::tempInt = 0;
	int DebugStuff::bgmTrackIndex = 0;
	int DebugStuff::tutorialFlagId = -1;
	int DebugStuff::tutorialFlagBitSize = 1;
	int DebugStuff::lastChangedLocalFlagId = -1;
	int DebugStuff::lastChangedLocalFlagBitSize = 1;

	unsigned short DebugStuff::GetMapId() {
#if XENOMODS_OLD_ENGINE
		return gf::GfMapManager::getMapID();
#elif XENOMODS_CODENAME(bfsw)
		if(DocumentPtr != nullptr)
			return game::MenuGameDataMap::getPlayerStayingMapId(*DocumentPtr);
#endif
		return 0;
	}

	std::string DebugStuff::GetMapName(int id) {
		std::string value = "No Map";

		if(id > 0) {
#if XENOMODS_OLD_ENGINE
			value = gf::GfDataMap::getName(id);
#elif XENOMODS_CODENAME(bfsw)
			game::MenuGameDataMap dataMap(*DocumentPtr);
			dataMap.create(id, game::MenuGameDataMap::MapType::OneFloor);

			game::MsText text = dataMap.getMapNameText();

			if(text.pBdat != nullptr)
				value = Bdat::getMSText(text.pBdat, text.index);
			else
				value = "";
#endif
		}

		// display "ID n" if the name is blank
		if(value == "")
			value = "ID " + std::to_string(id);

		return value;
	}

	void DebugStuff::DoMapJump(int mapjumpId) {
		if(mapjumpId == 0)
			return;

#if !XENOMODS_CODENAME(bf3)
		int end = 1;
		unsigned char* pBdat =
	#if XENOMODS_OLD_ENGINE
		Bdat::getFP("SYS_MapJumpList");
	#elif XENOMODS_CODENAME(bfsw)
		Bdat::getFP("landmarklist");
	#else
		nullptr;
	#endif
		if(pBdat != nullptr)
			end = Bdat::getIdEnd(pBdat);

		// can input negative numbers to wrap to the end
		if(mapjumpId < 0)
			mapjumpId = end - (std::abs(mapjumpId) - 1);

		mapjumpId = std::clamp<unsigned>(mapjumpId, 1, end);
#endif

#if XENOMODS_OLD_ENGINE
		gf::GfPlayFactory::createSkipTravel(mapjumpId);
		gf::GfMenuObjUtil::playSE(gf::GfMenuObjUtil::SEIndex::mapjump);
#elif XENOMODS_CODENAME(bfsw)
		game::MapJumpSetupInfo info;

		if(DocumentPtr == nullptr) {
			g_Logger->LogError("can't do a map jump cause no doc ptr!");
			return;
		}

		//g_Logger->LogInfo("going to make info");
		game::SeqUtil::makeMapJumpSetupInfoFromLandmark(info, *DocumentPtr, mapjumpId);
		//g_Logger->LogInfo("made info, going to request jump");
		game::SeqUtil::requestMapJump(*DocumentPtr, info);
		//g_Logger->LogInfo("jump requested");
#endif
	}

	void DebugStuff::PlaySE(unsigned int soundEffect) {
#if XENOMODS_OLD_ENGINE
		gf::GfMenuObjUtil::playSE(soundEffect);
#endif
	}

	void DebugStuff::ReturnTitle(unsigned int slot) {
#if XENOMODS_OLD_ENGINE
		tl::TitleMain::returnTitle((gf::SAVESLOT)slot);
#elif XENOMODS_CODENAME(bfsw)
		if(DocumentPtr == nullptr) {
			g_Logger->LogError("can't return to title cause no doc ptr!");
			return;
		}

		game::SeqUtil::returnTitle(*DocumentPtr);
#endif
	}

	void DebugStuff::ReloadSave() {
#if XENOMODS_OLD_ENGINE
		reloadPrimarySavePending = true;
		BeginSceneTransition();
		// Start the normal field-to-title teardown. The required title event
		// provides the render-scene transition barrier; only the fullscreen
		// title menu itself is skipped before submitting Continue.
		tl::TitleMain::returnTitle(gf::CurrentSlot);
#endif
	}

	void DebugStuff::UpdateDebugRendering() {
#if XENOMODS_OLD_ENGINE
		fw::PadManager::enableDebugDraw(enableDebugRendering);
#endif
#if XENOMODS_CODENAME(bf3)
		unsigned int* s_flg = nullptr; //ml::_dsk::s_flg

		if(version::RuntimeVersion() == version::SemVer::v2_0_0)
			s_flg = reinterpret_cast<unsigned int*>(skylaunch::utils::AddrFromBase(0x7101c49c60));
		else if(version::RuntimeVersion() == version::SemVer::v2_1_0 || version::RuntimeVersion() == version::SemVer::v2_1_1 || version::RuntimeVersion() == version::SemVer::v2_2_0)
			s_flg = reinterpret_cast<unsigned int*>(skylaunch::utils::AddrFromBase(0x7101c4ac60));

		// sets the system info print to display
		if(s_flg != nullptr)
			*s_flg ^= (-enableDebugRendering ^ *s_flg) & (1 << 6);
#endif
	}

	void DebugStuff::MemoryDebugRendering() {
		if(!DebugStuff::enableMemoryDebug)
			return;

		// 511 handles are possible, but from what I can tell no game ever initially allocates more than 66
		// There is a "leak" in 2 with certain cutscenes, but that is negligible
		// Let's use 127 just in case
		static std::array<std::pair<int, mtl::MemoryInfo>, 127> memInfos {};
		static int lastActiveRegions;

		mtl::AllocHandle allocHandle { 0 };

		if(!ImGui::Begin("Memory", &DebugStuff::enableMemoryDebug)) {
			ImGui::End();
			return;
		}

		size_t activeRegions = 0;
		for(int i = 0; i < memInfos.max_size(); i++) {
			allocHandle.regionId = i + 1;
			if(!mtl::MemManager::GET_MEMORY_INFO(&allocHandle, &memInfos[i].second)) {
				memInfos[i].first = 0;
				activeRegions++;
				continue;
			}
			memInfos[i].first = i + 1;
			activeRegions++;
		}

		if(ImGui::BeginTable("memdbg", 5, ImGuiTableFlags_Sortable)) {
			// Headers
			ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 20.0, 1);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 0.0, 2);
			ImGui::TableSetupColumn("Used %", ImGuiTableColumnFlags_None, 0.0, 3);
			ImGui::TableSetupColumn("Allocated (MB)", ImGuiTableColumnFlags_None, 0.0, 4);
			ImGui::TableSetupColumn("Total (MB)", ImGuiTableColumnFlags_None, 0.0, 5);
			ImGui::TableHeadersRow();

			auto sortSpecs = ImGui::TableGetSortSpecs();
			// Only sort when a sort key is chosen, and only sort when necessary
			if(sortSpecs != nullptr && (lastActiveRegions != activeRegions || sortSpecs->SpecsDirty || sortSpecs->Specs[0].ColumnUserID >= 2)) {
				std::sort(memInfos.begin(), memInfos.begin() + activeRegions, [&sortSpecs](const auto& a, const auto& b) -> bool {
			        if (a.first == 0 || b.first == 0)
						return false; // value unimportant
					bool cmp;
					switch(sortSpecs->Specs[0].ColumnUserID) {
						case 1:
							cmp = a.first < b.first;
							break;
						case 2:
							cmp = std::strcmp(a.second.regionName, b.second.regionName) < 0;
							break;
						case 3:
							cmp = a.second.usedPercent < b.second.usedPercent;
							break;
						case 4:
							cmp = a.second.allocatedSize < b.second.allocatedSize;
							break;
						case 5:
							cmp = a.second.totalSize < b.second.totalSize;
							break;
						default:
							IM_ASSERT(0);
							break;
					}
					return sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Ascending ? cmp : !cmp;
				});
			}

			for(int i = 0; i < activeRegions; i++) {
				if (memInfos[i].first == 0)
					continue;

				auto memInfo = &memInfos[i].second;

				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Text("%d", memInfos[i].first);

				// some kind of memory corruption thing happening here? clamp this
				float usedPercent = memInfo->usedPercent;
				if (usedPercent > 100.0f)
					usedPercent = 100.0f;

				ImGui::TableNextColumn();
				ImGui::Text("%s", memInfo->regionName);

				// Used % progress bar
				ImU32 color;
				if(usedPercent >= 90)
					color = IM_COL32(161, 21, 13, 255);
				else if(usedPercent >= 50)
					color = IM_COL32(163, 108, 13, 255);
				else
					color = IM_COL32(28, 82, 52, 255);
				ImGui::TableNextColumn();
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
				ImGui::ProgressBar(usedPercent / 100, ImVec2(ImGui::GetFontSize() * 10, 0.0f), std::format("{:.1f}%", usedPercent).c_str());
				ImGui::PopStyleColor(1);

				ImGui::TableNextColumn();
				ImGui::Text("%.4f", memInfo->allocatedSize / 1e6);
				ImGui::TableNextColumn();
				ImGui::Text("%.4f", memInfo->totalSize / 1e6);
			}
			ImGui::EndTable();
		}
		lastActiveRegions = activeRegions;

		ImGui::End();
	}

	void DebugStuff::MenuSection() {
		if(ImGui::Checkbox("Enable debug rendering", &DebugStuff::enableDebugRendering))
			DebugStuff::UpdateDebugRendering();
		ImGui::Checkbox("Memory debug window", &DebugStuff::enableMemoryDebug);

		/*ImGui::Checkbox("Pause updates", &DebugStuff::pauseEnable);
		ImGui::SameLine();
		if (ImGui::Button("Step Frame"))
			pauseStepForward = 1;
		ImGui::SameLine();
		if (ImGui::Button("Step Sec"))
			pauseStepForward = 30;*/

#if XENOMODS_CODENAME(bfsw)
		ImGui::Checkbox("Debug unlock all", &DebugStuff::enableDebugUnlockAll);
#endif
#if !XENOMODS_CODENAME(bf3)
	#if XENOMODS_OLD_ENGINE
		ImGui::Checkbox("Access closed landmarks", &DebugStuff::accessClosedLandmarks);
	#endif
		ImGui::PushItemWidth(150.f);
		ImGui::InputInt("Temp Int", &DebugStuff::tempInt);
		ImGui::PopItemWidth();
		if(ImGui::Button("Jump to Landmark"))
			DebugStuff::DoMapJump(DebugStuff::tempInt);
	#if XENOMODS_OLD_ENGINE
		ImGui::SameLine();
		if(ImGui::Button("Play common sound effect"))
			DebugStuff::PlaySE(DebugStuff::tempInt);
	#endif
		if(ImGui::Button("Return to Title"))
			DebugStuff::ReturnTitle();
		ImGui::SameLine();
		if(ImGui::Button("Reload Save"))
			DebugStuff::ReloadSave();
#endif
	}

	void DebugStuff::TutorialMenuSection() {
#if XENOMODS_OLD_ENGINE
		ImGui::SeparatorText("Tutorial diagnostics");
		if(ImGui::Checkbox("Trace selected flag call sites", &DebugStuff::traceTutorialCallSites)) {
			if(DebugStuff::traceTutorialCallSites)
				ResetTutorialCallSiteTrace();
		}
		ImGui::TextWrapped(
			"Captures the game functions that read the selected tutorial flag. "
			"Use this while reproducing one tutorial activation."
		);
		ImGui::Text("Captured call sites: %zu", tutorialCallSiteCount);
		for(std::size_t i = 0; i < tutorialCallSiteCount; i++) {
			const auto ghidraAddress = GetGhidraAddress(tutorialCallSites[i]);
			if(ghidraAddress != 0)
				ImGui::Text("%zu: Ghidra 0x%llX", i + 1, static_cast<unsigned long long>(ghidraAddress));
			else
				ImGui::Text("%zu: runtime 0x%llX", i + 1, static_cast<unsigned long long>(tutorialCallSites[i]));
		}

		ImGui::PushItemWidth(150.f);
		if(ImGui::InputInt("Tutorial flag ID", &DebugStuff::tutorialFlagId)) {
			ResetTutorialRepeatCycle();
			ResetTutorialTriggerVisualization();
		}
		if(ImGui::InputInt("Tutorial flag bit size", &DebugStuff::tutorialFlagBitSize))
			ResetTutorialRepeatCycle();
		ImGui::PopItemWidth();
		DebugStuff::tutorialFlagBitSize = std::clamp(DebugStuff::tutorialFlagBitSize, 1, 32);

		if(ImGui::Checkbox("Trace local flags changing from 0", &DebugStuff::traceLocalGameFlags)) {
			if(DebugStuff::traceLocalGameFlags)
				ResetLocalGameFlagTrace();
		}
		ImGui::TextWrapped(
			"Enable tracing before entering the tutorial, finish it, then walk into "
			"the trigger again. The most recently observed 0-to-nonzero flag appears below."
		);

		if(DebugStuff::lastChangedLocalFlagId >= 0) {
			ImGui::Text(
				"Latest candidate: ID %d, %d bit(s)",
				DebugStuff::lastChangedLocalFlagId,
				DebugStuff::lastChangedLocalFlagBitSize
			);
			if(ImGui::Button("Use latest candidate")) {
				DebugStuff::tutorialFlagId = DebugStuff::lastChangedLocalFlagId;
				DebugStuff::tutorialFlagBitSize = DebugStuff::lastChangedLocalFlagBitSize;
				ResetTutorialRepeatCycle();
				ResetTutorialTriggerVisualization();
			}
		} else {
			ImGui::TextUnformatted("Latest candidate: none");
		}

		ImGui::SeparatorText("Cutscene trigger diagnostics");
		ImGui::TextUnformatted("Cutscene renderer build: loaded");
		ImGui::Checkbox(
			"Log cutscene transforms on activation",
			&logCutsceneTransformMetrics
		);
		std::size_t cutsceneRenderedCount = 0;
		std::size_t cutsceneActiveCount = 0;
		for(std::size_t i = 0; i < cutsceneTriggerCount; i++) {
			if(
				cutsceneTriggers[i].renderStage
					== TutorialTriggerRenderStage::Rendering
			)
				cutsceneRenderedCount++;
			if(cutsceneTriggers[i].inside)
				cutsceneActiveCount++;
		}
		ImGui::Text("Cutscene triggers found: %zu", cutsceneTriggerCount);
		ImGui::Text("Cutscene triggers rendered: %zu", cutsceneRenderedCount);
		ImGui::Text("Cutscene triggers active: %zu", cutsceneActiveCount);
		for(std::size_t i = 0; i < cutsceneTriggerCount; i++) {
			if(cutsceneTriggers[i].modelNameChecked) {
				ImGui::Text(
					"Cutscene model: %s (%s)",
					cutsceneTriggers[i].actualModelName,
					cutsceneTriggers[i].modelNameValid
						? "verified"
						: "rejected"
				);
				break;
			}
		}
		for(std::size_t i = 0; i < cutsceneTriggerCount; i++) {
			if(!cutsceneTriggers[i].inside)
				continue;
			const glm::mat4 transform = cutsceneTriggers[i].transform;
			const glm::vec3 center = glm::vec3(transform[3]);
			const glm::vec3 size = cutsceneTriggers[i].size;
			ImGui::Text(
				"Active event %d: %s",
				cutsceneTriggers[i].eventId,
				GetTutorialPrimitiveTypeName(cutsceneTriggers[i].primitiveType)
			);
			ImGui::Text(
				"Center %.2f, %.2f, %.2f; size %.2f, %.2f, %.2f",
				center.x,
				center.y,
				center.z,
				size.x,
				size.y,
				size.z
			);
		}

		ImGui::SeparatorText("Landmark trigger diagnostics");
		ImGui::Checkbox(
			"Log landmark transforms on activation",
			&logLandmarkTransformMetrics
		);
		std::size_t landmarkRenderedCount = 0;
		std::size_t landmarkActiveCount = 0;
		for(std::size_t i = 0; i < landmarkTriggerCount; i++) {
			if(
				landmarkTriggers[i].renderStage
					== TutorialTriggerRenderStage::Rendering
			)
				landmarkRenderedCount++;
			if(landmarkTriggers[i].inside)
				landmarkActiveCount++;
		}
		ImGui::Text("Landmark triggers found: %zu", landmarkTriggerCount);
		ImGui::Text("Landmark triggers rendered: %zu", landmarkRenderedCount);
		ImGui::Text("Landmark triggers active: %zu", landmarkActiveCount);
		for(std::size_t i = 0; i < landmarkTriggerCount; i++) {
			if(!landmarkTriggers[i].inside)
				continue;
			const glm::mat4 transform = landmarkTriggers[i].transform;
			const glm::vec3 center = glm::vec3(transform[3]);
			const glm::vec3 size = landmarkTriggers[i].size;
			ImGui::Text(
				"Active landmark %d, flag %d: %s",
				landmarkTriggers[i].landmarkId,
				landmarkTriggers[i].flagId,
				GetTutorialPrimitiveTypeName(landmarkTriggers[i].primitiveType)
			);
			ImGui::Text(
				"Center %.2f, %.2f, %.2f; size %.2f, %.2f, %.2f",
				center.x,
				center.y,
				center.z,
				size.x,
				size.y,
				size.z
			);
		}
#endif
	}

	void DebugStuff::TutorialToolsMenuSection() {
#if XENOMODS_OLD_ENGINE
		if(ImGui::Checkbox("Render tutorial triggers", &DebugStuff::renderTutorialTrigger))
			ResetTutorialTriggerVisualization();

		if(ImGui::Checkbox("Repeat Tutorials", &DebugStuff::repeatTutorialFlag))
			ResetTutorialRepeatCycle();

		if(
			ImGui::Checkbox(
				"Pause repeat until out of trigger",
				&DebugStuff::pauseTutorialRepeatUntilExit
			)
		)
			ResetTutorialRepeatCycle();

		ImGui::SeparatorText("Cutscene triggers");
		if(
			ImGui::Checkbox(
				"Render cutscene triggers",
				&DebugStuff::renderCutsceneTrigger
			)
		)
			ResetCutsceneTriggerVisualization();

		ImGui::Checkbox(
			"Log tutorial transform metrics on activation",
			&logTutorialTransformMetrics
		);
		if(ImGui::Button("Log metrics for active trigger")) {
			for(std::size_t i = 0; i < tutorialTriggerCount; i++) {
				if(tutorialTriggers[i].inside) {
					LogTutorialTriggerMetrics(tutorialTriggers[i]);
					break;
				}
			}
		}
		if(latestTutorialTriggerMetrics.valid) {
			const auto& metrics = latestTutorialTriggerMetrics;
			ImGui::Text("Latest metrics: flag ID %d", metrics.flagId);
			ImGui::Text(
				"Matrix axes: %.3f, %.3f, %.3f",
				metrics.matrixAxisLengths.x,
				metrics.matrixAxisLengths.y,
				metrics.matrixAxisLengths.z
			);
			ImGui::Text(
				"Shape half-extents: %.3f, %.3f, %.3f",
				metrics.halfExtents.x,
				metrics.halfExtents.y,
				metrics.halfExtents.z
			);
			ImGui::Text(
				"Current render size: %.3f, %.3f, %.3f",
				metrics.currentRenderedSize.x,
				metrics.currentRenderedSize.y,
				metrics.currentRenderedSize.z
			);
			for(std::size_t i = 0; i < metrics.moverValid.size(); i++) {
				if(!metrics.moverValid[i])
					continue;
				const auto matrixRatio = metrics.moverBoundaryRatio[i];
				const auto rotationRatio =
					metrics.moverRotationBoundaryRatio[i];
				ImGui::Text(
					"Mover %zu boundary max: matrix %.3f, rotation %.3f",
					i,
					std::max(
						matrixRatio.x,
						std::max(matrixRatio.y, matrixRatio.z)
					),
					std::max(
						rotationRatio.x,
						std::max(rotationRatio.y, rotationRatio.z)
					)
				);
			}
		}

		std::size_t renderedCount = 0;
		std::size_t pausedCount = 0;
		std::size_t insideCount = 0;
		for(std::size_t i = 0; i < tutorialTriggerCount; i++) {
			if(tutorialTriggers[i].renderStage == TutorialTriggerRenderStage::Rendering)
				renderedCount++;
			if(tutorialTriggers[i].repeatSuppressedUntilExit)
				pausedCount++;
			if(tutorialTriggers[i].inside)
				insideCount++;
		}

		ImGui::Separator();
		ImGui::Text("Tutorial triggers found: %zu", tutorialTriggerCount);
		ImGui::Text("Tutorial triggers currently active: %zu", insideCount);
		for(std::size_t i = 0; i < tutorialTriggerCount; i++) {
			if(tutorialTriggers[i].inside) {
				ImGui::Text("Inside flag ID: %d", tutorialTriggers[i].flagId);
				const glm::mat4 triggerTransform = tutorialTriggers[i].transform;
				const glm::vec3 center = glm::vec3(triggerTransform[3]);
				const glm::vec3 triggerSize = tutorialTriggers[i].size;
				ImGui::Text(
					"Active primitive: %s",
					GetTutorialPrimitiveTypeName(tutorialTriggers[i].primitiveType)
				);
				ImGui::Text(
					"Active center: %.2f, %.2f, %.2f",
					center.x,
					center.y,
					center.z
				);
				ImGui::Text(
					"Active size: %.2f, %.2f, %.2f",
					triggerSize.x,
					triggerSize.y,
					triggerSize.z
				);
			}
		}
		if(DebugStuff::renderTutorialTrigger)
			ImGui::Text("Tutorial triggers rendered: %zu", renderedCount);
		if(
			DebugStuff::repeatTutorialFlag
			&& DebugStuff::pauseTutorialRepeatUntilExit
		)
			ImGui::Text("Repeats paused inside triggers: %zu", pausedCount);
#endif
	}

	void DebugStuff::CutsceneTriggerToolsMenuSection() {
#if XENOMODS_OLD_ENGINE
		if(
			ImGui::Checkbox(
				"Render cutscene triggers",
				&DebugStuff::renderCutsceneTrigger
			)
		)
			ResetCutsceneTriggerVisualization();

		ImGui::TextWrapped(
			"Displays field cutscene/event trigger phantoms as light-blue "
			"depth-tested world boxes."
		);
#endif
	}

	void DebugStuff::LandmarkTriggerToolsMenuSection() {
#if XENOMODS_OLD_ENGINE
		if(
			ImGui::Checkbox(
				"Render landmark triggers",
				&DebugStuff::renderLandmarkTrigger
			)
		)
			ResetLandmarkTriggerVisualization();

		ImGui::TextWrapped(
			"Displays landmark discovery phantoms as yellow, depth-tested "
			"world boxes."
		);
		ImGui::Text("Landmark triggers found: %zu", landmarkTriggerCount);
#endif
	}

	void DebugStuff::CollectionPointToolsMenuSection() {
#if XENOMODS_OLD_ENGINE
		if(
			ImGui::Checkbox(
				"Render collection-point interaction ranges",
				&DebugStuff::renderCollectionPointRange
			)
		)
			ResetCollectionPointVisualization();

		ImGui::TextWrapped(
			"Displays the A-button access range for collection points as "
			"pink vertical cylinders. Radius and asymmetric vertical limits "
			"come from XC2's live AccessParam."
		);
		ImGui::Text(
			"Collection points found: %zu",
			collectionPointCount
		);
		if(collectionAccessParamValid) {
			ImGui::Text(
				"Access range: R %.2f, up %.2f, down %.2f",
				collectionAccessParam.radius,
				collectionAccessParam.upperHeight,
				collectionAccessParam.lowerHeight
			);
		} else {
			ImGui::Text(
				"Access range: fallback R %.2f, up %.2f, down %.2f",
				CollectionPointFallbackRadius,
				CollectionPointFallbackUpperHeight,
				CollectionPointFallbackLowerHeight
			);
		}

		std::size_t shapedCount = 0;
		std::size_t modelCount = 0;
		for(std::size_t i = 0; i < collectionPointCount; i++) {
			if(collectionPoints[i].hasShape)
				shapedCount++;
			if(
				collectionPoints[i].model != nullptr
				&& collectionPoints[i].model
					!= reinterpret_cast<gf::GF_OBJ_HANDLE*>(-1)
			)
				modelCount++;
		}
		ImGui::Text(
			"Ranges ready: %zu, models: %zu",
			shapedCount,
			modelCount
		);
		ImGui::Text(
			"Nearby (%.0fm): %zu",
			CollectionPointRenderDistance,
			nearbyCollectionPointCount
		);
		if(nearestCollectionPointDistance >= 0.0f) {
			ImGui::Text(
				"Nearest: %.2fm - %s",
				nearestCollectionPointDistance,
				GetTriggerRenderStageName(
					nearestCollectionPointRenderStage
				)
			);
		} else {
			ImGui::Text(
				"Nearest: none - %s",
				GetTriggerRenderStageName(
					nearestCollectionPointRenderStage
				)
			);
		}
#endif
	}

	void DebugStuff::TriggerTopBarButton() {
#if XENOMODS_OLD_ENGINE
		if(
			ImGui::MenuItem(
				"Triggers",
				nullptr,
				showTriggerVisualizer
			)
		)
		{
			showTriggerVisualizer = !showTriggerVisualizer;
			toolwindow::SetVisible(
				toolwindow::StackSlot::TriggerVisualizer,
				showTriggerVisualizer
			);
		}
#endif
	}

	void DebugStuff::TriggerVisualizerWindow() {
#if XENOMODS_OLD_ENGINE
		toolwindow::SetVisible(
			toolwindow::StackSlot::TriggerVisualizer,
			showTriggerVisualizer
		);
		if(!showTriggerVisualizer)
			return;

		toolwindow::SetStackedPosition(
			toolwindow::StackSlot::TriggerVisualizer
		);
		toolwindow::SetCompactWidth();
		if(
			!ImGui::Begin(
				"Trigger Visualizer",
				&showTriggerVisualizer,
				ImGuiWindowFlags_AlwaysAutoResize
			)
		) {
			toolwindow::RecordCurrentHeight(
				toolwindow::StackSlot::TriggerVisualizer
			);
			toolwindow::SetVisible(
				toolwindow::StackSlot::TriggerVisualizer,
				showTriggerVisualizer
			);
			ImGui::End();
			return;
		}

		if(ImGui::BeginTabBar("TriggerVisualizerTabs")) {
			if(ImGui::BeginTabItem("Render")) {
				if(
					ImGui::Checkbox(
						"Tutorial triggers - red",
						&renderTutorialTrigger
					)
				)
					ResetTutorialTriggerVisualization();
				if(
					ImGui::Checkbox(
						"Cutscene triggers - light blue",
						&renderCutsceneTrigger
					)
				)
					ResetCutsceneTriggerVisualization();
				if(
					ImGui::Checkbox(
						"Landmark triggers - yellow",
						&renderLandmarkTrigger
					)
				)
					ResetLandmarkTriggerVisualization();
				if(
					ImGui::Checkbox(
						"Collection-point ranges - pink",
						&renderCollectionPointRange
					)
				)
					ResetCollectionPointVisualization();
				ImGui::EndTabItem();
			}

			if(ImGui::BeginTabItem("Tutorials")) {
				if(
					ImGui::Checkbox(
						"Repeat tutorials",
						&repeatTutorialFlag
					)
				)
					ResetTutorialRepeatCycle();
				if(
					ImGui::Checkbox(
						"Pause repeat until outside trigger",
						&pauseTutorialRepeatUntilExit
					)
				)
					ResetTutorialRepeatCycle();
				ImGui::EndTabItem();
			}

			if(ImGui::BeginTabItem("Debug")) {
				const std::size_t totalTriggers =
					tutorialTriggerCount
					+ cutsceneTriggerCount
					+ landmarkTriggerCount
					+ collectionPointCount;
				std::size_t loadedTriggers = 0;
				std::size_t renderedModels = 0;
				std::size_t activeTriggers = 0;

				for(std::size_t i = 0; i < tutorialTriggerCount; i++) {
					if(tutorialTriggers[i].hasShape)
						loadedTriggers++;
					if(
						tutorialTriggers[i].renderStage
							== TutorialTriggerRenderStage::Rendering
					)
						renderedModels++;
					if(tutorialTriggers[i].inside)
						activeTriggers++;
				}
				for(std::size_t i = 0; i < cutsceneTriggerCount; i++) {
					if(cutsceneTriggers[i].hasShape)
						loadedTriggers++;
					if(
						cutsceneTriggers[i].renderStage
							== TutorialTriggerRenderStage::Rendering
					)
						renderedModels++;
					if(cutsceneTriggers[i].inside)
						activeTriggers++;
				}
				for(std::size_t i = 0; i < landmarkTriggerCount; i++) {
					if(landmarkTriggers[i].hasShape)
						loadedTriggers++;
					if(
						landmarkTriggers[i].renderStage
							== TutorialTriggerRenderStage::Rendering
					)
						renderedModels++;
					if(landmarkTriggers[i].inside)
						activeTriggers++;
				}
				for(std::size_t i = 0; i < collectionPointCount; i++) {
					if(collectionPoints[i].hasShape)
						loadedTriggers++;
					if(
						collectionPoints[i].renderStage
							== TutorialTriggerRenderStage::Rendering
					)
						renderedModels++;
					if(collectionPoints[i].inside)
						activeTriggers++;
				}

				ImGui::Text("Total triggers        %zu", totalTriggers);
				ImGui::Text("Loaded triggers       %zu", loadedTriggers);
				ImGui::Text("Active triggers       %zu", activeTriggers);
				ImGui::Text("Rendered models       %zu", renderedModels);
				ImGui::Separator();

				if(activeTriggers == 0) {
					ImGui::TextDisabled("Active trigger         none");
				} else {
					for(std::size_t i = 0; i < tutorialTriggerCount; i++) {
						if(tutorialTriggers[i].inside)
							ImGui::Text(
								"Active trigger         (Tutorial) %d",
								tutorialTriggers[i].flagId
							);
					}
					for(std::size_t i = 0; i < cutsceneTriggerCount; i++) {
						if(cutsceneTriggers[i].inside)
							ImGui::Text(
								"Active trigger         (Cutscene) %d",
								cutsceneTriggers[i].eventId
							);
					}
					for(std::size_t i = 0; i < landmarkTriggerCount; i++) {
						if(landmarkTriggers[i].inside)
							ImGui::Text(
								"Active trigger         (Landmark) %d",
								landmarkTriggers[i].landmarkId
							);
					}
					for(std::size_t i = 0; i < collectionPointCount; i++) {
						if(collectionPoints[i].inside)
							ImGui::Text(
								"Active trigger         (Collection) %d",
								collectionPoints[i].collectionId
							);
					}
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		toolwindow::RecordCurrentHeight(
			toolwindow::StackSlot::TriggerVisualizer
		);
		toolwindow::SetVisible(
			toolwindow::StackSlot::TriggerVisualizer,
			showTriggerVisualizer
		);
		ImGui::End();
#endif
	}

	void DebugStuff::Initialize() {
		UpdatableModule::Initialize();
		g_Logger->LogDebug("Setting up debug stuff...");

#if !XENOMODS_CODENAME(bf3)
		MMAssert::HookAt(&mm::MMStdBase::mmAssert);
#endif

#if XENOMODS_OLD_ENGINE
		BGMDebugging::HookAt("_ZN2gf8BgmTrack6updateERKN2fw10UpdateInfoE");
		LocalGameFlagHook::HookAt("_ZN2gf10GfGameFlag8getLocalEji");
		TutorialBeginHook::HookAt("_ZN3gmk11GmkTutorial13beginTutorialEv");
		TutorialUpdateHook::HookAt("_ZN3gmk11GmkTutorial6updateEf");
		CutsceneEventUpdateHook::HookAt("_ZN3gmk8GmkEvent6updateEf");
		LandmarkUpdateHook::HookAt("_ZN3gmk11GmkLandmark6updateEf");
		CollectionUpdateHook::HookAt("_ZN3gmk13GmkCollection6updateEf");
		CollectionAccessedHook::HookAt(
			"_ZN3gmk13GmkCollection16onPlayerAccessedEPN2gf13GF_OBJ_HANDLEE"
		);
		DropitemInitializeHook::HookAt(
			"_ZN2gf14GfFobjDropitem10initializeEv"
		);
		// Do not inspect AccessPlugin's partially initialized resource pointer.
		// AccessRangeHook receives the live AccessParam directly and safely.
		AccessRangeHook::HookAt(
			"_ZN2gf2pc13isActiveRangeERNS_15GfComPropertyPcERKNS_11AccessParamEPNS_13GF_OBJ_HANDLEE"
		);
		TitleSkipEventHook::HookAt(
			"_ZN2tl20TitleStateMainScreen14playTitleEventEPNS_9TitleMainE"
		);
		TitleSkipMenuHook::HookAt(
			"_ZN2tl20TitleStateMainScreen13dispTitleMenuEPNS_9TitleMainEb"
		);
		TitleContinueHook::HookAt(
			"_ZN2tl20TitleStateMainScreen15checkMenuResultEPNS_9TitleMainE"
		);

		JumpToClosedLandmarks_CanEnterMap::HookAt(&gf::GfMenuObjWorldMap::isEnterMap);
		JumpToClosedLandmarks_CheckCondition::HookAt(&gf::GfMenuObjWorldMap::chkMapCond);
		JumpToClosedLandmarks_WorldMap::HookAt(&gf::GfMenuObjWorldMap::isEnableJump);
		JumpToClosedLandmarks_ZoneMap::HookAt(&gf::GfMenuObjZoneMap::isEnableJump);
		JumpToClosedLandmarks_IsFound::HookAt("_ZNK3gmk11GmkLandmark7isFoundEv");
#elif XENOMODS_CODENAME(bfsw)
		EnableDebugUnlockAll::HookAt(&game::IsMenuDebugUnlockAll);
		AlwaysAbleToOpenMenu::HookAt(&game::DataUtil::isDisableMenu);
#endif

		UpdateDebugRendering();

		xenomods::g_Menu->RegisterRenderCallback(&DebugStuff::MemoryDebugRendering, false);
#if XENOMODS_OLD_ENGINE
		xenomods::g_Menu->RegisterRenderCallback(
			&DebugStuff::TriggerVisualizerWindow,
			true
		);
#endif
	}

	bool DebugStuff::ShouldBypassControlLockForTutorial(bool controlFree) {
		if(tutorialControlTrigger == nullptr)
			return false;

		const auto entry = FindTutorialTrigger(tutorialControlTrigger);
		if(entry == nullptr || !IsInsideTutorialTrigger(tutorialControlTrigger)) {
			tutorialControlTrigger = nullptr;
			tutorialControlLockObserved = false;
			tutorialControlLockArmFrames = 0;
			return false;
		}

		if(!controlFree) {
			tutorialControlLockObserved = true;
			return true;
		}

		// Once the tutorial-owned lock has released, revoke the exception even
		// if Rex remains physically inside the trigger. This is what prevents a
		// later collection animation in the same volume from inheriting it.
		if(tutorialControlLockObserved || --tutorialControlLockArmFrames <= 0) {
			tutorialControlTrigger = nullptr;
			tutorialControlLockObserved = false;
			tutorialControlLockArmFrames = 0;
		}
		return false;
	}

	void DebugStuff::Update(fw::UpdateInfo* updateInfo) {
		bgmTrackIndex = 0;

#if XENOMODS_OLD_ENGINE
		UpdateTutorialTriggerModels();
		UpdateCutsceneTriggerModels();
		UpdateLandmarkTriggerModels();
		UpdateCollectionPointModels();
#endif

		if(pauseEnable && pauseStepForward > 0) {
			pauseStepForward--;
		}
	}

	void DebugStuff::OnSceneTransition() {
#if XENOMODS_OLD_ENGINE
		ClearTutorialTriggerRegistry();
		ClearCutsceneTriggerRegistry();
		ClearLandmarkTriggerRegistry();
		ClearCollectionPointRegistry();
#endif
	}

	void DebugStuff::OnMapChange(unsigned short mapId) {
	}

	XENOMODS_REGISTER_MODULE(DebugStuff);

} // namespace xenomods
