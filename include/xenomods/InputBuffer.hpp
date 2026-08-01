#pragma once

#include <cstdint>

namespace xenomods::InputBuffer {

	extern bool Enabled;
	extern bool BufferLeftStick;
	extern bool BufferRightStick;
	extern int StickBufferFrames;

	void Initialize();
	void DrawMenu();
	void Clear();
	std::uint32_t PendingButtons();
	void SetLeftStickOverride(bool active, float x = 0.f, float y = 0.f);

} // namespace xenomods::InputBuffer
