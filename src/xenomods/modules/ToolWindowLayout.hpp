#pragma once

#include <array>
#include <cfloat>
#include <imgui.h>

namespace xenomods::toolwindow {

	// imgui-xeno renders this UI at a 2x framebuffer scale. These are logical
	// ImGui units. Measure the telemetry's widest fixed-format row so the
	// layout follows the active font and framebuffer scale without clipping.
	inline constexpr float Left = 2.f;
	inline constexpr float Top = 20.f;
	inline constexpr float Gap = 4.f;

	enum class StackSlot : std::size_t {
		Telemetry,
		TriggerVisualizer,
		FrameCounter,
		Warps,
		Targeting,
		Utility,
		Count
	};

	inline std::array<bool, static_cast<std::size_t>(StackSlot::Count)>
		StackVisibility {};
	inline std::array<float, static_cast<std::size_t>(StackSlot::Count)>
		StackHeights {116.f, 190.f, 92.f, 260.f, 300.f, 170.f};

	inline float CompactWidth() {
		return
			ImGui::CalcTextSize(
				"Pos    X -00000.00  Y -00000.00  Z -00000.00"
			).x
				+ ImGui::GetStyle().WindowPadding.x * 2.f
				+ 4.f;
	}

	inline void SetCompactWidth() {
		const float width = CompactWidth();
		ImGui::SetNextWindowSizeConstraints(
			ImVec2(width, 0.f),
			ImVec2(width, FLT_MAX)
		);
	}

	inline void SetVisible(StackSlot slot, bool visible) {
		StackVisibility[static_cast<std::size_t>(slot)] = visible;
	}

	inline void SetStackedPosition(StackSlot slot) {
		const auto slotIndex = static_cast<std::size_t>(slot);
		float y = Top;
		for(std::size_t index = 0; index < slotIndex; index++) {
			if(StackVisibility[index])
				y += StackHeights[index] + Gap;
		}
		ImGui::SetNextWindowPos(
			ImVec2(Left, y),
			ImGuiCond_Always
		);
	}

	inline void RecordCurrentHeight(StackSlot slot) {
		const float height = ImGui::GetWindowSize().y;
		if(height > 0.f)
			StackHeights[static_cast<std::size_t>(slot)] = height;
	}

} // namespace xenomods::toolwindow
