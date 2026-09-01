#pragma once

#include "UpdatableModule.hpp"

namespace xenomods {

	class AutoTutorials : public UpdatableModule {
	  public:
		static bool Enabled;

		static void MenuSection();
		static void ClearRuntimeState();

		void Initialize() override;
		void OnSceneTransition() override;
	};

} // namespace xenomods
