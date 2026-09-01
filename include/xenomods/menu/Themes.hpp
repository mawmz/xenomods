// Created by block on 8/28/23.

#pragma once

#include <imgui.h>

namespace xenomods {

	// Adapted from Doug Binks' SetupImGuiStyle:
	// https://gist.github.com/dougbinks/8089b4bbaccaaf6fa204236978d165a9
	inline void ImGuiStyleColorsDougBinks() {
		// Initialize color slots added after the original theme was published.
		ImGui::StyleColorsLight();

		ImGuiStyle& style = ImGui::GetStyle();
		// Keep text and controls crisp, but let the game remain visible through
		// the main panels like the translucent debug overlay reference.
		style.Alpha = 1.0f;
		style.FrameRounding = 3.0f;

		style.Colors[ImGuiCol_Text]                 = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled]         = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
		style.Colors[ImGuiCol_WindowBg]             = ImVec4(0.94f, 0.94f, 0.94f, 0.55f);
		style.Colors[ImGuiCol_ChildBg]              = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_PopupBg]              = ImVec4(1.00f, 1.00f, 1.00f, 0.88f);
		style.Colors[ImGuiCol_Border]               = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
		style.Colors[ImGuiCol_BorderShadow]         = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
		style.Colors[ImGuiCol_FrameBg]              = ImVec4(1.00f, 1.00f, 1.00f, 0.45f);
		style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
		style.Colors[ImGuiCol_FrameBgActive]        = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
		style.Colors[ImGuiCol_TitleBg]              = ImVec4(0.96f, 0.96f, 0.96f, 0.85f);
		style.Colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(1.00f, 1.00f, 1.00f, 0.51f);
		style.Colors[ImGuiCol_TitleBgActive]        = ImVec4(0.82f, 0.82f, 0.82f, 0.90f);
		style.Colors[ImGuiCol_MenuBarBg]            = ImVec4(0.86f, 0.86f, 0.86f, 0.90f);
		style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.98f, 0.98f, 0.98f, 0.53f);
		style.Colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.69f, 0.69f, 0.69f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.59f, 0.59f, 0.59f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);
		style.Colors[ImGuiCol_CheckMark]            = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_SliderGrab]           = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
		style.Colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_Button]               = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
		style.Colors[ImGuiCol_ButtonHovered]        = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_ButtonActive]         = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_Header]               = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
		style.Colors[ImGuiCol_HeaderHovered]        = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
		style.Colors[ImGuiCol_HeaderActive]         = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_Separator]            = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
		style.Colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
		style.Colors[ImGuiCol_SeparatorActive]      = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip]           = ImVec4(1.00f, 1.00f, 1.00f, 0.50f);
		style.Colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
		style.Colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
		style.Colors[ImGuiCol_PlotLines]            = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
		style.Colors[ImGuiCol_PlotLinesHovered]     = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram]        = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
		style.Colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);

		// The linked theme's dark mode inverts only low-saturation colors,
		// preserving the blue accent colors.
		for(int i = 0; i < ImGuiCol_COUNT; i++) {
			ImVec4& color = style.Colors[i];
			float hue;
			float saturation;
			float value;
			ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, hue, saturation, value);
			if(saturation < 0.1f)
				value = 1.0f - value;
			ImGui::ColorConvertHSVtoRGB(hue, saturation, value, color.x, color.y, color.z);
		}
	}

	// Fork of the Comfy style from ImThemes, supplied for XenoTAS Tools.
	inline void ImGuiStyleColorsComfy() {
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();

		style.Alpha = 1.0f;
		style.DisabledAlpha = 0.1f;
		style.WindowPadding = ImVec2(5.0f, 5.0f);
		style.WindowRounding = 10.0f;
		style.WindowBorderSize = 0.0f;
		style.WindowMinSize = ImVec2(30.0f, 30.0f);
		style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
		style.WindowMenuButtonPosition = ImGuiDir_Right;
		style.ChildRounding = 5.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupRounding = 10.0f;
		style.PopupBorderSize = 0.0f;
		style.FramePadding = ImVec2(3.0f, 2.5f);
		style.FrameRounding = 7.9f;
		style.FrameBorderSize = 0.0f;
		style.ItemSpacing = ImVec2(4.0f, 2.0f);
		style.ItemInnerSpacing = ImVec2(5.0f, 5.0f);
		style.CellPadding = ImVec2(4.0f, 2.0f);
		style.IndentSpacing = 5.0f;
		style.ColumnsMinSpacing = 5.0f;
		style.ScrollbarSize = 15.0f;
		style.ScrollbarRounding = 14.6f;
		style.GrabMinSize = 12.5f;
		style.GrabRounding = 7.1f;
		style.TabRounding = 4.7f;
		style.TabBorderSize = 0.0f;
		style.TabMinWidthForCloseButton = 0.0f;
		style.ColorButtonPosition = ImGuiDir_Right;
		style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
		style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

		auto* colors = style.Colors;
		colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		colors[ImGuiCol_TextDisabled] = ImVec4(1.0f, 1.0f, 1.0f, 0.360515f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.09803922f, 0.09803922f, 0.09803922f, 0.38197422f);
		colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.055794f, 0.05579344f, 0.05579344f, 0.6695279f);
		colors[ImGuiCol_Border] = ImVec4(0.42352942f, 0.38039216f, 0.57254905f, 0.5107296f);
		colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.15686275f, 0.15686275f, 0.15686275f, 0.33476394f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.38039216f, 0.42352942f, 0.57254905f, 0.54901963f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.61960787f, 0.5764706f, 0.76862746f, 0.54901963f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.09803922f, 0.09803922f, 0.09803922f, 0.81545067f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.09803922f, 0.09803922f, 0.09803922f, 1.0f);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.25882354f, 0.25882354f, 0.25882354f, 0.0f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.15686275f, 0.15686275f, 0.15686275f, 0.0f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.15686275f, 0.15686275f, 0.15686275f, 0.60085833f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.23529412f, 0.23529412f, 0.23529412f, 1.0f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.29411766f, 0.29411766f, 0.29411766f, 1.0f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.61960787f, 0.5764706f, 0.76862746f, 0.68240345f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.61960787f, 0.5764706f, 0.76862746f, 0.54901963f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.8156863f, 0.77254903f, 0.9647059f, 0.54901963f);
		colors[ImGuiCol_Button] = ImVec4(0.61960787f, 0.5764706f, 0.76862746f, 0.54901963f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.7372549f, 0.69411767f, 0.8862745f, 0.54901963f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.8156863f, 0.77254903f, 0.9647059f, 0.54901963f);
		colors[ImGuiCol_Header] = ImVec4(0.61960787f, 0.5764706f, 0.76862746f, 0.54901963f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.7372549f, 0.69411767f, 0.8862745f, 0.54901963f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.8156863f, 0.77254903f, 0.9647059f, 0.54901963f);
		colors[ImGuiCol_Separator] = ImVec4(0.61960787f, 0.5764706f, 0.76862746f, 0.54901963f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.7372549f, 0.69411767f, 0.8862745f, 0.54901963f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.8156863f, 0.77254903f, 0.9647059f, 0.54901963f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.61960787f, 0.5764706f, 0.76862746f, 0.54901963f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.7372549f, 0.69411767f, 0.8862745f, 0.54901963f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.8156863f, 0.77254903f, 0.9647059f, 0.54901963f);
		colors[ImGuiCol_Tab] = ImVec4(0.61960787f, 0.5764706f, 0.76862746f, 0.54901963f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.7372549f, 0.69411767f, 0.8862745f, 0.54901963f);
		colors[ImGuiCol_TabActive] = ImVec4(0.8156863f, 0.77254903f, 0.9647059f, 0.54901963f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.0f, 0.4509804f, 1.0f, 0.0f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.13333334f, 0.25882354f, 0.42352942f, 0.0f);
		colors[ImGuiCol_PlotLines] = ImVec4(0.29411766f, 0.29411766f, 0.29411766f, 1.0f);
		colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.7372549f, 0.69411767f, 0.8862745f, 0.54901963f);
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.61960787f, 0.5764706f, 0.76862746f, 0.54901963f);
		colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.7372549f, 0.69411767f, 0.8862745f, 0.54901963f);
		colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882353f, 0.1882353f, 0.2f, 1.0f);
		colors[ImGuiCol_TableBorderStrong] = ImVec4(0.42352942f, 0.38039216f, 0.57254905f, 0.54901963f);
		colors[ImGuiCol_TableBorderLight] = ImVec4(0.42352942f, 0.38039216f, 0.57254905f, 0.2918455f);
		colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03433478f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.7372549f, 0.69411767f, 0.8862745f, 0.54901963f);
		colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.9f);
		colors[ImGuiCol_NavHighlight] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.35f);
	}

	// "Titans"
	inline void ImGuiStyleColorsXB1() {
		ImGuiStyle& style = ImGui::GetStyle();

		style.Colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled]           = ImVec4(0.86f, 0.81f, 0.75f, 0.60f);
		style.Colors[ImGuiCol_WindowBg]               = ImVec4(0.24f, 0.22f, 0.17f, 0.90f);
		style.Colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
		style.Colors[ImGuiCol_Border]                 = ImVec4(0.57f, 0.51f, 0.45f, 0.50f);
		style.Colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_FrameBg]                = ImVec4(0.29f, 0.27f, 0.25f, 1.00f);
		style.Colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.56f, 0.53f, 0.46f, 0.40f);
		style.Colors[ImGuiCol_FrameBgActive]          = ImVec4(0.79f, 0.76f, 0.72f, 0.67f);
		style.Colors[ImGuiCol_TitleBg]                = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
		style.Colors[ImGuiCol_TitleBgActive]          = ImVec4(0.42f, 0.28f, 0.22f, 1.00f);
		style.Colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
		style.Colors[ImGuiCol_MenuBarBg]              = ImVec4(0.13f, 0.13f, 0.12f, 0.78f);
		style.Colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
		style.Colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
		style.Colors[ImGuiCol_CheckMark]              = ImVec4(0.90f, 0.94f, 0.30f, 0.78f);
		style.Colors[ImGuiCol_SliderGrab]             = ImVec4(0.60f, 0.57f, 0.52f, 1.00f);
		style.Colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.79f, 0.76f, 0.72f, 1.00f);
		style.Colors[ImGuiCol_Button]                 = ImVec4(0.32f, 0.30f, 0.28f, 1.00f);
		style.Colors[ImGuiCol_ButtonHovered]          = ImVec4(0.69f, 0.64f, 0.12f, 1.00f);
		style.Colors[ImGuiCol_ButtonActive]           = ImVec4(0.91f, 0.97f, 0.50f, 1.00f);
		style.Colors[ImGuiCol_Header]                 = ImVec4(0.80f, 0.24f, 0.24f, 0.31f);
		style.Colors[ImGuiCol_HeaderHovered]          = ImVec4(0.80f, 0.24f, 0.24f, 0.66f);
		style.Colors[ImGuiCol_HeaderActive]           = ImVec4(0.69f, 0.64f, 0.12f, 0.76f);
		style.Colors[ImGuiCol_Separator]              = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
		style.Colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
		style.Colors[ImGuiCol_SeparatorActive]        = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip]             = ImVec4(0.55f, 0.27f, 0.15f, 0.20f);
		style.Colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.55f, 0.27f, 0.15f, 0.67f);
		style.Colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.69f, 0.64f, 0.12f, 0.76f);
		style.Colors[ImGuiCol_Tab]                    = ImVec4(0.29f, 0.27f, 0.24f, 0.46f);
		style.Colors[ImGuiCol_TabHovered]             = ImVec4(0.75f, 0.87f, 0.31f, 0.80f);
		style.Colors[ImGuiCol_TabActive]              = ImVec4(0.55f, 0.27f, 0.15f, 1.00f);
		style.Colors[ImGuiCol_TabUnfocused]           = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
		style.Colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.13f, 0.26f, 0.42f, 1.00f);
		//style.Colors[ImGuiCol_DockingPreview]         = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
		//style.Colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		style.Colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
		style.Colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
		style.Colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
		style.Colors[ImGuiCol_TableBorderLight]       = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
		style.Colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
		style.Colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
		style.Colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
		style.Colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		style.Colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
		style.Colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
	}

	// "Alrest"
	inline void ImGuiStyleColorsXB2() {
		ImGuiStyle& style = ImGui::GetStyle();

		style.Colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
		style.Colors[ImGuiCol_WindowBg]               = ImVec4(0.11f, 0.15f, 0.28f, 0.88f);
		style.Colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
		style.Colors[ImGuiCol_Border]                 = ImVec4(0.84f, 0.91f, 1.00f, 0.56f);
		style.Colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_FrameBg]                = ImVec4(0.57f, 0.51f, 0.49f, 0.30f);
		style.Colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.84f, 0.89f, 1.00f, 0.27f);
		style.Colors[ImGuiCol_FrameBgActive]          = ImVec4(0.34f, 0.45f, 0.76f, 0.62f);
		style.Colors[ImGuiCol_TitleBg]                = ImVec4(0.63f, 0.69f, 0.83f, 1.00f);
		style.Colors[ImGuiCol_TitleBgActive]          = ImVec4(0.17f, 0.25f, 0.65f, 1.00f);
		style.Colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
		style.Colors[ImGuiCol_MenuBarBg]              = ImVec4(0.29f, 0.30f, 0.39f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.18f, 0.24f, 0.56f, 0.49f);
		style.Colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.19f, 0.42f, 0.57f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.45f, 0.73f, 0.91f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.36f, 0.62f, 0.78f, 1.00f);
		style.Colors[ImGuiCol_CheckMark]              = ImVec4(0.87f, 0.48f, 0.84f, 1.00f);
		style.Colors[ImGuiCol_SliderGrab]             = ImVec4(1.00f, 0.81f, 0.32f, 1.00f);
		style.Colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.99f, 0.90f, 0.67f, 1.00f);
		style.Colors[ImGuiCol_Button]                 = ImVec4(0.39f, 0.33f, 0.35f, 0.88f);
		style.Colors[ImGuiCol_ButtonHovered]          = ImVec4(0.87f, 0.48f, 0.84f, 1.00f);
		style.Colors[ImGuiCol_ButtonActive]           = ImVec4(0.81f, 0.41f, 0.78f, 1.00f);
		style.Colors[ImGuiCol_Header]                 = ImVec4(0.39f, 0.49f, 0.73f, 0.44f);
		style.Colors[ImGuiCol_HeaderHovered]          = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
		style.Colors[ImGuiCol_HeaderActive]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_Separator]              = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
		style.Colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
		style.Colors[ImGuiCol_SeparatorActive]        = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
		style.Colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
		style.Colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
		style.Colors[ImGuiCol_Tab]                    = ImVec4(0.19f, 0.22f, 0.50f, 0.83f);
		style.Colors[ImGuiCol_TabHovered]             = ImVec4(0.38f, 0.53f, 0.94f, 1.00f);
		style.Colors[ImGuiCol_TabActive]              = ImVec4(0.92f, 0.49f, 0.84f, 1.00f);
		style.Colors[ImGuiCol_TabUnfocused]           = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
		style.Colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.13f, 0.26f, 0.42f, 1.00f);
		//style.Colors[ImGuiCol_DockingPreview]         = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
		//style.Colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		style.Colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
		style.Colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.80f, 0.38f, 0.74f, 1.00f);
		style.Colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.18f, 0.64f, 0.76f, 1.00f);
		style.Colors[ImGuiCol_TableBorderLight]       = ImVec4(0.20f, 0.25f, 0.45f, 1.00f);
		style.Colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
		style.Colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
		style.Colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
		style.Colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		style.Colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
		style.Colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
	}

	// "Aionios"
	inline void ImGuiStyleColorsXB3() {
		ImGuiStyle& style = ImGui::GetStyle();

		style.Colors[ImGuiCol_Text]                   = ImVec4(1.00f, 0.98f, 0.91f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled]           = ImVec4(0.42f, 0.41f, 0.38f, 1.00f);
		style.Colors[ImGuiCol_WindowBg]               = ImVec4(0.06f, 0.06f, 0.25f, 0.87f);
		style.Colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
		style.Colors[ImGuiCol_Border]                 = ImVec4(1.00f, 0.72f, 0.60f, 0.71f);
		style.Colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_FrameBg]                = ImVec4(0.60f, 0.41f, 0.29f, 0.40f);
		style.Colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.15f, 0.63f, 0.59f, 0.40f);
		style.Colors[ImGuiCol_FrameBgActive]          = ImVec4(0.69f, 1.00f, 0.94f, 0.40f);
		style.Colors[ImGuiCol_TitleBg]                = ImVec4(0.02f, 0.01f, 0.13f, 1.00f);
		style.Colors[ImGuiCol_TitleBgActive]          = ImVec4(0.78f, 0.35f, 0.20f, 1.00f);
		style.Colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.07f, 0.51f);
		style.Colors[ImGuiCol_MenuBarBg]              = ImVec4(0.40f, 0.15f, 0.11f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.49f, 0.38f, 0.42f, 0.53f);
		style.Colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.58f, 0.45f, 0.41f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.92f, 0.71f, 0.54f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(1.00f, 0.77f, 0.57f, 1.00f);
		style.Colors[ImGuiCol_CheckMark]              = ImVec4(1.00f, 0.98f, 0.91f, 1.00f);
		style.Colors[ImGuiCol_SliderGrab]             = ImVec4(1.00f, 0.76f, 0.31f, 1.00f);
		style.Colors[ImGuiCol_SliderGrabActive]       = ImVec4(1.00f, 0.91f, 0.62f, 1.00f);
		style.Colors[ImGuiCol_Button]                 = ImVec4(0.63f, 0.27f, 0.09f, 0.82f);
		style.Colors[ImGuiCol_ButtonHovered]          = ImVec4(0.14f, 0.71f, 0.55f, 1.00f);
		style.Colors[ImGuiCol_ButtonActive]           = ImVec4(0.69f, 1.00f, 0.94f, 1.00f);
		style.Colors[ImGuiCol_Header]                 = ImVec4(0.02f, 0.00f, 0.18f, 0.76f);
		style.Colors[ImGuiCol_HeaderHovered]          = ImVec4(0.67f, 0.30f, 0.20f, 0.88f);
		style.Colors[ImGuiCol_HeaderActive]           = ImVec4(0.96f, 0.36f, 0.22f, 0.87f);
		style.Colors[ImGuiCol_Separator]              = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
		style.Colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.65f, 0.35f, 0.13f, 0.78f);
		style.Colors[ImGuiCol_SeparatorActive]        = ImVec4(1.00f, 0.75f, 0.35f, 0.84f);
		style.Colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.71f, 0.98f, 0.20f);
		style.Colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
		style.Colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
		style.Colors[ImGuiCol_Tab]                    = ImVec4(0.35f, 0.26f, 0.55f, 0.86f);
		style.Colors[ImGuiCol_TabHovered]             = ImVec4(0.55f, 0.31f, 0.57f, 0.91f);
		style.Colors[ImGuiCol_TabActive]              = ImVec4(0.72f, 0.22f, 0.11f, 1.00f);
		style.Colors[ImGuiCol_TabUnfocused]           = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
		style.Colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.13f, 0.26f, 0.42f, 1.00f);
		//style.Colors[ImGuiCol_DockingPreview]         = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
		//style.Colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		style.Colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
		style.Colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
		style.Colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
		style.Colors[ImGuiCol_TableBorderLight]       = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
		style.Colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
		style.Colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
		style.Colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
		style.Colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		style.Colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
		style.Colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
	}

}
