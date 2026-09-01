#pragma once

#include <cstdint>

#include "UpdatableModule.hpp"

namespace xenomods {

	class AutoCutsceneSkips : public UpdatableModule {
	  public:
		static bool Enabled;

		static void SetEnabled(bool enabled);
		static void MenuSection();
		static void ProfilerSection();
		static void ClearRuntimeState(bool interrupted = true);
		static void OnInputLayerUpdate(
			std::uint32_t object,
			const void* inputLayer,
			std::uint32_t layerHandle,
			bool afterUpdate,
			bool accepted,
			std::uint16_t emittedEvent,
			std::uint16_t secondaryEvent,
			std::uint32_t secondaryAction,
			std::uint32_t heldButtons,
			std::uint32_t downButtons
		);

		void Initialize() override;
		void OnSceneTransition() override;
	};

} // namespace xenomods
