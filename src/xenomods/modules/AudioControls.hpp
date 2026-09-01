#pragma once

#include "UpdatableModule.hpp"

namespace xenomods {

	struct AudioControls : public UpdatableModule {
		static bool BackgroundMusic;
		static bool DisableErrorSound;

		static void MenuSection();
		static void PlaybackMenuSection();

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
