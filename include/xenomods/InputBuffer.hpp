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
		std::uint32_t heldButtons,
		std::uint32_t downButtons
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
	// Field-route actions use a raw pad override rather than menu playback.
	// The mask is merged with P1's physical input; down is asserted for the
	// action's first frame only.
	void SetRawButtonOverride(
		bool active,
		std::uint32_t heldMask = 0,
		bool down = false
	);
	void SetAcceptedActionCallback(AcceptedActionCallback callback);
	void SetInputLayerUpdateCallback(InputLayerUpdateCallback callback);
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
