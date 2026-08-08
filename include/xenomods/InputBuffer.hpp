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

	extern bool Enabled;
	extern bool BufferLeftStick;
	extern bool BufferRightStick;
	extern int StickBufferFrames;

	void Initialize();
	void DrawMenu();
	void Clear();
	std::uint32_t PendingButtons();
	void SetLeftStickOverride(bool active, float x = 0.f, float y = 0.f);
	void SetAcceptedActionCallback(AcceptedActionCallback callback);
	void SetAcceptedActionCapture(bool active);
	bool AcceptedActionCaptureWaitingForNeutral();
	void SetPlaybackOverride(
		bool active,
		PlaybackMode mode = PlaybackMode::StandardMenu
	);
	void SetPlaybackAction(AcceptedAction action);
	bool PlaybackActionPending();

} // namespace xenomods::InputBuffer
