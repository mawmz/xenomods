#pragma once

#include <cstdint>

namespace xenomods::InputBuffer {
	struct AcceptedAction {
		std::uint32_t input = 0;
		std::uint32_t layer = 0;
		std::uint32_t display = 0;

		bool operator==(const AcceptedAction&) const = default;
	};

	enum class ActionSource {
		Physical,
		Playback
	};

	enum class PlaybackMode {
		StandardMenu,
		// Ordinary shops must advance through the exact UIInputLayer captured by
		// the recording. Generic PadRelay resources can become visible while the
		// shop is still transitioning and must not complete these actions early.
		ShopMenuLayered,
		TravelUiBuffered
	};

	using AcceptedActionCallback = void (*)(
		AcceptedAction action,
		ActionSource source
	);
	using InputLayerUpdateCallback = void (*)(
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
	using AutoUiActionAcceptedCallback = void (*)(
		std::uint32_t object,
		std::uint32_t layerHandle
	);

	extern bool Enabled;
	extern bool BufferLeftStick;
	extern bool BufferRightStick;
	extern int StickBufferFrames;

	void Initialize();
	void DrawMenu();
	void Clear();
	std::uint32_t PendingButtons();
	void SetLeftStickOverride(bool active, float x = 0.f, float y = 0.f);
	// Scripted Targeting Actions take priority over the normal route-steering
	// override for their configured hold duration.
	void SetActionStickOverride(
		bool active,
		bool rightStick = false,
		float x = 0.f,
		float y = 0.f
	);
	// Field-route actions use a raw pad override rather than menu playback.
	// The mask is merged with P1's physical input; down is asserted for the
	// action's first frame only.
	void SetRawButtonOverride(
		bool active,
		std::uint32_t heldMask = 0,
		std::uint32_t downMask = 0
	);
	// Keeps a scripted field/UI action pending without suppressing movement.
	// Individual bits are consumed only by matching native handlers.
	void SetBufferedButtonAction(std::uint32_t inputMask);
	void CancelBufferedButtonAction();
	bool BufferedButtonActionPending();
	std::uint32_t BufferedButtonActionPendingMask();
	void SetAcceptedActionCallback(AcceptedActionCallback callback);
	void SetInputLayerUpdateCallback(InputLayerUpdateCallback callback);
	// Enables raw PadData snapshots for diagnostic input-layer callbacks. The
	// callback itself remains installed for lightweight layer readiness tracking.
	void SetInputLayerPadCapture(bool active);
	void SetAutoUiActionAcceptedCallback(AutoUiActionAcceptedCallback callback);
	// Queues one UI-pad action for one exact UI object. The low-bit mask is
	// offered only while that object's registered UIInputLayer is updating and
	// is completed only after the layer emits its native secondary action.
	void RequestAutoUiAction(std::uint32_t object, std::uint32_t inputMask);
	void CancelAutoUiAction(std::uint32_t object = 0);
	void SetAcceptedActionCapture(bool active);
	bool AcceptedActionCaptureWaitingForNeutral();
	void SetPlaybackOverride(
		bool active,
		PlaybackMode mode = PlaybackMode::StandardMenu
	);
	void SetPlaybackAction(AcceptedAction action);
	bool PlaybackActionPending();
	void SetFrameIndex(std::uint64_t frame);
	void SetAuxCoreAmountMenu(bool active, std::uint32_t object = 0);
	bool AcceptAuxCoreAmountPlayback();
	bool AcceptAuxCoreAmountInput();
	void ResetAuxCoreAmountInput();

} // namespace xenomods::InputBuffer
