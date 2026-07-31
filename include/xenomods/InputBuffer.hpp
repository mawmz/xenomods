#pragma once

#include <cstdint>

namespace xenomods::InputBuffer {

	extern bool Enabled;
	extern int ButtonBufferFrames;
	extern bool BufferLeftStick;
	extern bool BufferRightStick;
	extern int StickBufferFrames;

	void Initialize();
	void DrawMenu();
	void Clear();
	std::uint32_t PendingButtons();

} // namespace xenomods::InputBuffer
