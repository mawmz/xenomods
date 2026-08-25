#include <xenomods/UtilityMenu.hpp>

#include <algorithm>
#include <string>

#include <fmt/format.h>
#include <imgui.h>
#include <toml++/toml.hpp>

#include <xenomods/NnFile.hpp>
#include <xenomods/State.hpp>
#include <xenomods/menu/Menu.hpp>

#include "modules/BattleCheats.hpp"
#include "modules/AudioControls.hpp"
#include "modules/CameraTools.hpp"
#include "modules/CombatAiDebug.hpp"
#include "modules/DebugStuff.hpp"
#include "modules/PlayerMovement.hpp"
#include "modules/RenderingControls.hpp"
#include "modules/Targeting.hpp"
#include "modules/ToolWindowLayout.hpp"

namespace xenomods::UtilityMenu {

	bool ShowWindow = false;

	namespace {
		int selectedTab = 0;
		bool applySavedTab = true;

		struct SavedState {
			bool utility = false;
			bool warps = false;
			bool targeting = false;
			bool telemetry = false;
			bool triggers = false;
			bool frameCounter = false;
			bool backgroundMusic = true;
			int utilityTab = 0;

			bool operator==(const SavedState&) const = default;
		};

		SavedState lastSaved {};

		std::string SettingsPath() {
			return fmt::format(
				XENOMODS_CONFIG_PATH "/{}/toolWindows.toml",
				XENOMODS_CODENAME_STR
			);
		}

		SavedState CurrentState() {
			return {
				ShowWindow,
				PlayerMovement::ShowWarpsWindow,
				Targeting::ShowWindow,
				PlayerMovement::ShowPlayerTelemetry,
				DebugStuff::showTriggerVisualizer,
				CombatAiDebug::ShowFrameCounter,
				AudioControls::BackgroundMusic,
				selectedTab
			};
		}

		void SaveStateIfChanged() {
			const SavedState state = CurrentState();
			if(state == lastSaved)
				return;

			const auto contents = fmt::format(
				"utility = {}\nwarps = {}\ntargeting = {}\ntelemetry = {}\ntriggers = {}\n"
				"frame_counter = {}\nbackground_music = {}\nutility_tab = {}\n",
				state.utility,
				state.warps,
				state.targeting,
				state.telemetry,
				state.triggers,
				state.frameCounter,
				state.backgroundMusic,
				state.utilityTab
			);
			const auto path = SettingsPath();
			if(NnFile::Preallocate(path, contents.size())) {
				NnFile file(path, nn::fs::OpenMode_Write);
				file.Write(contents.c_str(), contents.size());
				file.Flush();
				lastSaved = state;
			}
		}

		bool Tab(const char* name, int index) {
			const ImGuiTabItemFlags flags =
				applySavedTab && selectedTab == index
					? ImGuiTabItemFlags_SetSelected
					: ImGuiTabItemFlags_None;
			if(!ImGui::BeginTabItem(name, nullptr, flags))
				return false;
			selectedTab = index;
			return true;
		}
	} // namespace

	void TopBar() {
#if XENOMODS_CODENAME(bf2)
		if(ImGui::BeginMenu("Saves")) {
			if(ImGui::MenuItem("Reload Save"))
				DebugStuff::ReloadSave();
			if(ImGui::MenuItem("Return to Title"))
				DebugStuff::ReturnTitle();
			ImGui::EndMenu();
		}

		if(ImGui::MenuItem("Warps", nullptr, PlayerMovement::ShowWarpsWindow))
			PlayerMovement::ShowWarpsWindow = !PlayerMovement::ShowWarpsWindow;
		if(ImGui::MenuItem("Targeting", nullptr, Targeting::ShowWindow))
			Targeting::ShowWindow = !Targeting::ShowWindow;
		if(ImGui::MenuItem("Utility", nullptr, ShowWindow)) {
			ShowWindow = !ShowWindow;
			if(ShowWindow)
				applySavedTab = true;
		}
		if(ImGui::MenuItem("Telemetry", nullptr, PlayerMovement::ShowPlayerTelemetry))
			PlayerMovement::ShowPlayerTelemetry = !PlayerMovement::ShowPlayerTelemetry;
		if(ImGui::MenuItem("Triggers", nullptr, DebugStuff::showTriggerVisualizer))
			DebugStuff::showTriggerVisualizer = !DebugStuff::showTriggerVisualizer;
		if(ImGui::MenuItem("Frame Counter", nullptr, CombatAiDebug::ShowFrameCounter))
			CombatAiDebug::ShowFrameCounter = !CombatAiDebug::ShowFrameCounter;

		SaveStateIfChanged();
#endif
	}

	void Window() {
#if XENOMODS_CODENAME(bf2)
		toolwindow::SetVisible(toolwindow::StackSlot::Utility, ShowWindow);
		if(!ShowWindow) {
			SaveStateIfChanged();
			return;
		}

		toolwindow::SetCompactWidth();
		toolwindow::SetStackedPosition(toolwindow::StackSlot::Utility);
		ImGui::SetNextWindowSize(ImVec2(300.f, 170.f), ImGuiCond_Appearing);
		if(!ImGui::Begin("Utility", &ShowWindow)) {
			toolwindow::RecordCurrentHeight(toolwindow::StackSlot::Utility);
			toolwindow::SetVisible(toolwindow::StackSlot::Utility, ShowWindow);
			ImGui::End();
			SaveStateIfChanged();
			return;
		}

		if(ImGui::BeginTabBar("UtilityTabs")) {
			if(Tab("Camera", 0)) {
				CameraTools::MenuSection();
				ImGui::EndTabItem();
			}
			if(Tab("Render", 1)) {
				RenderingControls::MenuSection();
				ImGui::SeparatorText("Toggles");
				if(ImGui::BeginChild("RenderingToggleList", ImVec2(0.f, 0.f), true))
					RenderingControls::MenuToggles();
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if(Tab("Battle", 2)) {
				BattleCheats::MenuSection();
				ImGui::EndTabItem();
			}
			if(Tab("Movement", 3)) {
				PlayerMovement::MenuSection();
				ImGui::SeparatorText("World interactions");
				ImGui::Checkbox(
					"Infinite collection points",
					&DebugStuff::infiniteCollectionPoints
				);
				ImGui::Checkbox(
					"Minimum collection-item distance",
					&DebugStuff::minimumCollectionItemDistance
				);
				if(ImGui::IsItemHovered()) {
					ImGui::SetTooltip(
						"Forces spawned field items to their drop parameter's "
						"minimum outward distance."
					);
				}
				ImGui::EndTabItem();
			}
			if(Tab("Audio", 4)) {
				AudioControls::MenuSection();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		applySavedTab = false;
		toolwindow::RecordCurrentHeight(toolwindow::StackSlot::Utility);
		toolwindow::SetVisible(toolwindow::StackSlot::Utility, ShowWindow);
		ImGui::End();
		SaveStateIfChanged();
#endif
	}

	void Initialize() {
#if XENOMODS_CODENAME(bf2)
		const toml::parse_result settings = toml::parse_file(SettingsPath());
		if(settings) {
			const auto& table = settings.table();
			ShowWindow = table["utility"].value_or(false);
			PlayerMovement::ShowWarpsWindow = table["warps"].value_or(false);
			Targeting::ShowWindow = table["targeting"].value_or(false);
			PlayerMovement::ShowPlayerTelemetry = table["telemetry"].value_or(false);
			DebugStuff::showTriggerVisualizer = table["triggers"].value_or(false);
			CombatAiDebug::ShowFrameCounter = table["frame_counter"].value_or(false);
			AudioControls::BackgroundMusic =
				table["background_music"].value_or(true);
			selectedTab = std::clamp(table["utility_tab"].value_or(0), 0, 4);
		}
		lastSaved = CurrentState();
		g_Menu->RegisterTopBarCallback(&TopBar);
		g_Menu->RegisterRenderCallback(&Window, true);
#endif
	}

} // namespace xenomods::UtilityMenu
