#pragma once

#include <cstdint>

#include "UpdatableModule.hpp"
#include "xenomods/InputBuffer.hpp"

namespace xenomods {

	struct SkipTravelProfiler : public UpdatableModule {
		static void TopBarButton();
		static void ProfilerWindow();

		static void OnOpenButtonPressed();
		static void OnOpenButtonFinished();
		static void OnOpenCommandStarted();
		static void OnOpenCommandResult(bool created);

		static void OnAuxShopRequest(
			std::uint16_t event,
			std::uint32_t shop,
			std::uint32_t object,
			const void* eventData
		);
		static void OnAuxRecipeEvent(
			bool after,
			std::uint16_t event,
			std::uint32_t action,
			std::uint32_t object,
			const void* listener,
			const void* eventData,
			bool dispatched = true
		);
		static void OnAuxEnableInput(bool after, std::uint32_t object);
		static void OnAuxInputLayerUpdate(
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
		static void OnTutorialBegin(
			std::uint32_t family,
			const void* menu
		);
		static void OnTutorialEnd(
			std::uint32_t family,
			const void* menu
		);
		static void OnTutorialListenerRegistered(
			std::uint32_t family,
			const void* menu,
			const void* listener,
			std::uint32_t object
		);
		static void OnTutorialListenerEvent(
			std::uint32_t family,
			const void* menu,
			const void* listener,
			std::uint16_t event,
			std::uint32_t action,
			std::uint32_t object,
			std::uint64_t sequenceCount,
			bool after
		);
		static void OnTutorialAdvanceRequested(
			std::uint32_t family,
			const void* menu,
			std::uint32_t object,
			std::uint64_t sequenceCount
		);
		static void OnTutorialAdvanceAccepted(
			std::uint32_t object,
			std::uint32_t layerHandle
		);
		static void OnTutorialInputLayerUpdate(
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
		static void OnAuxAcceptedAction(
			InputBuffer::AcceptedAction action,
			InputBuffer::ActionSource source
		);

		void Initialize() override;
		bool NeedsUpdate() const override {
			return true;
		}
		void Update(fw::UpdateInfo* updateInfo) override;
		void OnSceneTransition() override;
	};

} // namespace xenomods
