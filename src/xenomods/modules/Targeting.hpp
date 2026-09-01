#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "UpdatableModule.hpp"
#include "xenomods/engine/mm/MathTypes.hpp"

namespace xenomods {

	struct Targeting : public UpdatableModule {
		enum class TargetType {
			Position,
			Delay,
			Action,
			Toggle,
			ShopTas,
			MenuTas,
			TravelTas
		};
		enum class ToggleSetting {
			AutoCutsceneSkips
		};
		enum class ActionInputType {
			Buttons,
			LeftStick,
			RightStick
		};

		struct TargetData {
			TargetType type = TargetType::Position;
			std::string name {};
			std::string mapName {};
			unsigned short mapId {};
			glm::vec3 position {};
			int delayFrames = 60;
			std::uint32_t buttonMask = 0;
			ActionInputType actionInputType = ActionInputType::Buttons;
			float stickX = 0.f;
			float stickY = 0.f;
			std::uint64_t lastFrames = 0;
			std::uint64_t bestFrames = 0;
			int holdFrames = 1;
			bool buffered = false;
			ToggleSetting toggleSetting = ToggleSetting::AutoCutsceneSkips;
			bool toggleEnabled = false;
			bool intermediate = false;
		};

		static std::vector<TargetData> Targets;
		static bool ShowWindow;
		static bool ShowAllTargets;
		static bool ShowTargetsOnMap;
		static bool RouteActive;
		static bool StartFromSelection;
		static bool WarpToStart;

		static void MenuWindow();
		static void LoadTargetsFromFile();
		static void SaveTargetsToFile();
		static int NewTarget(int insertAfter = -1);
		static int NewDelay(int insertAfter = -1);
		static int NewAction(int insertAfter = -1);
		static int NewToggle(int insertAfter = -1);
		static int NewSpecialStep(
			TargetType type,
			int insertAfter = -1,
			bool intermediate = false
		);
		static void SetTarget(TargetData* target);
		static void StopRoute();

		void Initialize() override;
		bool NeedsUpdate() const override {
			return true;
		}
		void Update(fw::UpdateInfo* updateInfo) override;
		void OnSceneTransition() override;
		void OnMapChange(unsigned short mapId) override;
	};

} // namespace xenomods
