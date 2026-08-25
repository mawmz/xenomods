#pragma once

#include "UpdatableModule.hpp"

namespace xenomods {

	struct AudioControls : public UpdatableModule {
		static bool BackgroundMusic;

		static void MenuSection();

		void Initialize() override;
		bool NeedsUpdate() const override {
			return true;
		}
		bool UpdatesDuringSceneTransition() const override {
			return true;
		}
		void Update(fw::UpdateInfo* updateInfo) override;
	};

} // namespace xenomods
