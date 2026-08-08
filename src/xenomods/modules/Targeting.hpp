#pragma once

#include <string>
#include <vector>

#include "UpdatableModule.hpp"
#include "xenomods/engine/mm/MathTypes.hpp"

namespace xenomods {

	struct Targeting : public UpdatableModule {
		enum class TargetType {
			Position,
			Delay,
			ShopTas,
			MenuTas,
			TravelTas
		};

		struct TargetData {
			TargetType type = TargetType::Position;
			std::string name {};
			std::string mapName {};
			unsigned short mapId {};
			glm::vec3 position {};
			int delayFrames = 60;
			bool intermediate = false;
		};

		static std::vector<TargetData> Targets;
		static bool ShowWindow;
		static bool ShowAllTargets;
		static bool ShowTargetsOnMap;
		static bool RouteActive;
		static bool StartFromSelection;
		static bool WarpToStart;
		static bool UseArrivalRadius;
		static float ArrivalRadius;

		static void MenuWindow();
		static void LoadTargetsFromFile();
		static void SaveTargetsToFile();
		static TargetData* NewTarget();
		static int NewDelay(int insertAfter = -1);
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
