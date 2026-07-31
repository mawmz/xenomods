#pragma once

#include "UpdatableModule.hpp"
#include "xenomods/engine/gf/MenuObject.hpp"

namespace xenomods {

	struct DebugStuff : public xenomods::UpdatableModule {
		static bool enableDebugRendering;
		static bool enableDebugUnlockAll;
		static bool accessClosedLandmarks;
		static bool pauseEnable;
		static bool enableMemoryDebug;
		static bool repeatTutorialFlag;
		static bool pauseTutorialRepeatUntilExit;
		static bool renderTutorialTrigger;
		static bool renderCutsceneTrigger;
		static bool renderLandmarkTrigger;
		static bool renderCollectionPointRange;
		static bool traceLocalGameFlags;
		static bool traceTutorialCallSites;
		static bool showTriggerVisualizer;

		static std::int8_t pauseStepForward;
		static int tempInt;
		static int bgmTrackIndex;
		static int tutorialFlagId;
		static int tutorialFlagBitSize;
		static int lastChangedLocalFlagId;
		static int lastChangedLocalFlagBitSize;

		static unsigned short GetMapId();
		static std::string GetMapName(int id);
		static std::string GetMapName() {
			return GetMapName(GetMapId());
		}

		static void DoMapJump(int mapjumpId);
		static void PlaySE(unsigned int soundEffect);
		static void ReturnTitle(unsigned int slot = -1);
		static void ReloadSave();

		static void UpdateDebugRendering();
		static void MemoryDebugRendering();

		static void MenuSection();
		static void TutorialMenuSection();
		static void TutorialToolsMenuSection();
		static void CutsceneTriggerToolsMenuSection();
		static void LandmarkTriggerToolsMenuSection();
		static void CollectionPointToolsMenuSection();
		static void TriggerTopBarButton();
		static void TriggerVisualizerWindow();

		void Initialize() override;
		bool NeedsUpdate() const override {
			return true;
		}
		void Update(fw::UpdateInfo* updateInfo) override;
		void OnMapChange(unsigned short mapId) override;
	};

} // namespace xenomods
