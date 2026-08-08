#pragma once

#include "UpdatableModule.hpp"

namespace xenomods {

	struct CombatAiDebug : public UpdatableModule {
		static bool ShowOverlay;
		static bool CaptureEnabled;
		static bool FreezeHistory;
		static bool LogEvents;
		static bool ShowFrameCounter;

		static void TopBarButton();
		static void Overlay();
		static void FrameCounterOverlay();
		static void MenuSection();

		void Initialize() override;
		bool NeedsUpdate() const override {
			return true;
		}
		bool UpdatesDuringSceneTransition() const override {
			return true;
		}
		void Update(fw::UpdateInfo* updateInfo) override;
		void OnMapChange(unsigned short mapId) override;
	};

} // namespace xenomods
