#pragma once

#include "UpdatableModule.hpp"

namespace xenomods {

	struct ReactionControls : public UpdatableModule {
		static bool ShowWindow;

		static void TopBarButton();
		static void MenuWindow();

		void Initialize() override;
		bool NeedsUpdate() const override {
			return true;
		}
		void Update(fw::UpdateInfo* updateInfo) override;
		void OnSceneTransition() override;
	};

} // namespace xenomods
