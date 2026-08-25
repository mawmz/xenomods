#include "Targeting.hpp"

#include "CameraTools.hpp"
#include "DebugStuff.hpp"
#include "MenuHelper.hpp"
#include "PlayerMovement.hpp"
#include "ToolWindowLayout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

#include <fmt/format.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <toml++/toml.hpp>

#include "xenomods/InputBuffer.hpp"
#include "xenomods/Logger.hpp"
#include "xenomods/NnFile.hpp"
#include "xenomods/State.hpp"
#include "xenomods/engine/gf/Manager.hpp"
#include "xenomods/stuff/utils/debug_util.hpp"

namespace xenomods {

	std::vector<Targeting::TargetData> Targeting::Targets {};
	bool Targeting::ShowWindow = false;
	bool Targeting::ShowAllTargets = false;
	bool Targeting::ShowTargetsOnMap = true;
	bool Targeting::RouteActive = false;
	bool Targeting::StartFromSelection = false;
	bool Targeting::WarpToStart = false;
	bool Targeting::UseArrivalRadius = false;
	float Targeting::ArrivalRadius = 0.35f;

	namespace {
		int activeTargetIndex = -1;
		bool waitingForPlayer = false;
		int startDelayFrames = 0;
		int activeDelayIndex = -1;
		int delayFramesRemaining = 0;
		int trackedTargetIndex = -1;
		glm::vec3 previousTargetDelta {};
		bool hasPreviousTargetDelta = false;
		bool targetingMenuTasPlayback = false;

		void ResetTargetApproach() {
			trackedTargetIndex = -1;
			previousTargetDelta = {};
			hasPreviousTargetDelta = false;
		}

		bool IsMenuTasStep(const Targeting::TargetData& target) {
			return target.type == Targeting::TargetType::MenuTas
				|| target.type == Targeting::TargetType::TravelTas;
		}

		bool StartMenuTasStep(const Targeting::TargetData& target) {
			return target.type == Targeting::TargetType::TravelTas
				? MenuHelper::StartTravelMenuPlayback(target.name)
				: MenuHelper::StartMainMenuPlayback(target.name);
		}

		bool ArmMenuTasStep(const Targeting::TargetData& target) {
			return target.type == Targeting::TargetType::TravelTas
				? MenuHelper::ArmTravelMenuPlayback(target.name)
				: MenuHelper::ArmMainMenuPlayback(target.name);
		}

		bool IsFinite(const glm::vec3& value) {
			return std::isfinite(value.x)
				&& std::isfinite(value.y)
				&& std::isfinite(value.z);
		}

		bool DrawPreciseCoordinate(const char* axis, float& value) {
			ImGui::TextUnformatted(axis);
			ImGui::SameLine(0.f, 3.f);
			ImGui::SetNextItemWidth(64.f);
			ImGui::PushID(axis);
			bool changed = ImGui::DragFloat(
				"##value",
				&value,
				0.001f,
				0.f,
				0.f,
				"%.3f"
			);
			if(ImGui::IsItemHovered()) {
				const float wheel = ImGui::GetIO().MouseWheel;
				if(wheel != 0.f) {
					value += wheel * 0.001f;
					changed = true;
				}
			}
			ImGui::PopID();
			return changed;
		}

		std::string TargetsPath() {
			return fmt::format(
				XENOMODS_CONFIG_PATH "/{}/targets.toml",
				XENOMODS_CODENAME_STR
			);
		}

		unsigned short CurrentMapId() {
			return detail::IsModuleRegistered(STRINGIFY(DebugStuff))
				? DebugStuff::GetMapId()
				: 0;
		}

		int FindFirstTargetForMap(unsigned short mapId) {
			for(int index = 0; index < static_cast<int>(Targeting::Targets.size()); index++) {
				if(Targeting::Targets[index].mapId == mapId)
					return index;
			}
			return -1;
		}

		int FindNextTargetForMap(int current, unsigned short mapId) {
			for(int index = current + 1; index < static_cast<int>(Targeting::Targets.size()); index++) {
				if(Targeting::Targets[index].mapId == mapId)
					return index;
			}
			return -1;
		}

		int RouteNumber(int targetIndex) {
			if(targetIndex < 0 || targetIndex >= static_cast<int>(Targeting::Targets.size()))
				return 0;
			const auto mapId = Targeting::Targets[targetIndex].mapId;
			int number = 0;
			for(int index = 0; index <= targetIndex; index++) {
				if(
					Targeting::Targets[index].mapId == mapId
					&& Targeting::Targets[index].type
						== Targeting::TargetType::Position
				)
					number++;
			}
			return number;
		}

		void StartRoute(int selectedIndex = -1) {
			const auto mapId = CurrentMapId();
			if(Targeting::StartFromSelection) {
				if(
					selectedIndex < 0
					|| selectedIndex >= static_cast<int>(Targeting::Targets.size())
					|| Targeting::Targets[selectedIndex].mapId != mapId
				) {
					Targeting::RouteActive = false;
					activeTargetIndex = -1;
					InputBuffer::SetLeftStickOverride(false);
					g_Logger->ToastWarning(
						"targeting",
						"Select a target on the current map"
					);
					return;
				}
				activeTargetIndex = selectedIndex;
			} else {
				activeTargetIndex = FindFirstTargetForMap(mapId);
			}
			Targeting::RouteActive = activeTargetIndex >= 0;
			waitingForPlayer = false;
			startDelayFrames = 0;
			activeDelayIndex = -1;
			delayFramesRemaining = 0;
			ResetTargetApproach();
			InputBuffer::SetLeftStickOverride(false);
			if(!Targeting::RouteActive) {
				g_Logger->ToastWarning("targeting", "No targets exist for this map");
				return;
			}
			if(Targeting::WarpToStart) {
				int warpIndex = activeTargetIndex;
				while(
					warpIndex >= 0
					&& Targeting::Targets[warpIndex].type
						!= Targeting::TargetType::Position
				)
					warpIndex = FindNextTargetForMap(warpIndex, mapId);
				if(warpIndex >= 0) {
					PlayerMovement::SetPartyPosition(
						Targeting::Targets[warpIndex].position
					);
					startDelayFrames = 10;
				}
			}
		}

		int MoveTargetWithinMap(int index, int direction) {
			if(index < 0 || index >= static_cast<int>(Targeting::Targets.size()))
				return index;
			const auto mapId = Targeting::Targets[index].mapId;
			int other = index + direction;
			while(other >= 0 && other < static_cast<int>(Targeting::Targets.size())) {
				if(Targeting::Targets[other].mapId == mapId) {
					std::swap(Targeting::Targets[index], Targeting::Targets[other]);
					return other;
				}
				other += direction;
			}
			return index;
		}

		void WarpToTarget(int index) {
			if(index < 0 || index >= static_cast<int>(Targeting::Targets.size()))
				return;
			if(Targeting::Targets[index].type != Targeting::TargetType::Position)
				return;
			if(Targeting::Targets[index].mapId != CurrentMapId()) {
				g_Logger->ToastWarning(
					"targeting",
					"Selected target belongs to another map"
				);
				return;
			}
			Targeting::StopRoute();
			PlayerMovement::SetPartyPosition(Targeting::Targets[index].position);
		}

		bool ConsumeActiveDelaySteps(unsigned short mapId) {
			while(
				activeTargetIndex >= 0
				&& activeTargetIndex < static_cast<int>(Targeting::Targets.size())
				&& Targeting::Targets[activeTargetIndex].mapId == mapId
				&& Targeting::Targets[activeTargetIndex].type
					== Targeting::TargetType::Delay
			) {
				if(activeDelayIndex != activeTargetIndex) {
					activeDelayIndex = activeTargetIndex;
					delayFramesRemaining = std::max(
						0,
						Targeting::Targets[activeTargetIndex].delayFrames
					);
				}
				ResetTargetApproach();
				InputBuffer::SetLeftStickOverride(false);
				if(delayFramesRemaining > 0) {
					delayFramesRemaining--;
					return true;
				}

				activeTargetIndex = FindNextTargetForMap(activeTargetIndex, mapId);
				activeDelayIndex = -1;
				if(activeTargetIndex < 0) {
					Targeting::StopRoute();
					g_Logger->ToastInfo("targeting", "Target route complete");
					return true;
				}
			}
			return false;
		}

		bool ConsumeActiveSpecialSteps(unsigned short mapId) {
			while(
				activeTargetIndex >= 0
				&& activeTargetIndex < static_cast<int>(Targeting::Targets.size())
				&& Targeting::Targets[activeTargetIndex].mapId == mapId
				&& Targeting::Targets[activeTargetIndex].type
					!= Targeting::TargetType::Position
			) {
				if(Targeting::Targets[activeTargetIndex].type == Targeting::TargetType::Delay) {
					if(ConsumeActiveDelaySteps(mapId))
						return true;
					continue;
				}

				const auto& step = Targeting::Targets[activeTargetIndex];
				ResetTargetApproach();
				InputBuffer::SetLeftStickOverride(false);
				const bool initializesHelper =
					step.type == Targeting::TargetType::ShopTas
					|| (IsMenuTasStep(step) && step.intermediate);
				if(
					initializesHelper
					&& MenuHelper::IsPlaybackPendingOrActive()
				)
					return true;

				if(step.type == Targeting::TargetType::ShopTas) {
					MenuHelper::ArmShopPlayback(step.name);
				} else if(IsMenuTasStep(step) && !step.intermediate) {
					return false;
				} else if(IsMenuTasStep(step) && !ArmMenuTasStep(step)) {
					Targeting::StopRoute();
					g_Logger->ToastWarning(
						"targeting",
						"Could not initialize {}",
						step.type == Targeting::TargetType::TravelTas
							? "TravelTAS"
							: "MenuTAS"
					);
					return true;
				}

				activeTargetIndex = FindNextTargetForMap(activeTargetIndex, mapId);
				if(activeTargetIndex < 0) {
					Targeting::StopRoute();
					g_Logger->ToastInfo("targeting", "Target route complete");
					return true;
				}
			}
			return false;
		}

	} // namespace

	void Targeting::StopRoute() {
		RouteActive = false;
		activeTargetIndex = -1;
		waitingForPlayer = false;
		startDelayFrames = 0;
		activeDelayIndex = -1;
		delayFramesRemaining = 0;
		ResetTargetApproach();
		targetingMenuTasPlayback = false;
		InputBuffer::SetLeftStickOverride(false);
	}

	void Targeting::LoadTargetsFromFile() {
		toml::parse_result result = toml::parse_file(TargetsPath());
		if(!result) {
			g_Logger->LogDebug("Target file not loaded: {}", std::move(result).error().description());
			return;
		}

		auto table = std::move(result).table();
		auto array = table.get_as<toml::array>("targets");
		if(array == nullptr)
			return;

		std::vector<TargetData> loadedTargets;
		loadedTargets.reserve(array->size());
		for(auto& element : *array) {
			const auto entry = element.as_table();
			if(entry == nullptr)
				continue;

			const auto mapIdValue = (*entry)["mapId"].value_or<std::int64_t>(0);
			if(
				mapIdValue < 0
				|| mapIdValue > std::numeric_limits<unsigned short>::max()
			)
				continue;

			TargetData target;
			const auto type = (*entry)["type"].value_or<std::string>("position");
			target.type = type == "delay"
				? TargetType::Delay
				: type == "shop_tas"
					? TargetType::ShopTas
					: type == "menu_tas"
						? TargetType::MenuTas
						: type == "travel_tas"
							? TargetType::TravelTas
							: TargetType::Position;
			target.mapId = static_cast<unsigned short>(mapIdValue);
			target.mapName =
				(*entry)["mapNameReadOnly"].value_or<std::string>("Unknown");
			if(target.mapName.empty())
				target.mapName = "Unknown";

			if(target.type == TargetType::Delay) {
				const auto frames = (*entry)["frames"].value_or<std::int64_t>(60);
				target.delayFrames = static_cast<int>(std::clamp<std::int64_t>(
					frames,
					0,
					std::numeric_limits<int>::max()
				));
			} else if(target.type == TargetType::Position) {
				target.name = (*entry)["name"].value_or<std::string>("Target");
				const auto position = entry->get_as<toml::array>("position");
				if(position == nullptr || position->size() < 3)
					continue;
				target.position.x = (*position)[0].value_or(0.f);
				target.position.y = (*position)[1].value_or(0.f);
				target.position.z = (*position)[2].value_or(0.f);
				if(!IsFinite(target.position))
					continue;
			} else {
				target.name = (*entry)["recording"].value_or<std::string>("");
				if(
					target.type == TargetType::MenuTas
					|| target.type == TargetType::TravelTas
				) {
					target.intermediate =
						(*entry)["intermediate"].value_or(false);
					const auto position = entry->get_as<toml::array>("position");
					if(position == nullptr || position->size() < 3)
						continue;
					target.position.x = (*position)[0].value_or(0.f);
					target.position.y = (*position)[1].value_or(0.f);
					target.position.z = (*position)[2].value_or(0.f);
					if(!IsFinite(target.position))
						continue;
				}
			}
			loadedTargets.push_back(std::move(target));
		}

		StopRoute();
		Targets.swap(loadedTargets);
		g_Logger->ToastInfo("targeting", "Loaded {} target(s)", Targets.size());
	}

	void Targeting::SaveTargetsToFile() {
		toml::array allTargets;
		for(const auto& target : Targets) {
			toml::table entry;
			const char* type = "position";
			if(target.type == TargetType::Delay)
				type = "delay";
			else if(target.type == TargetType::ShopTas)
				type = "shop_tas";
			else if(target.type == TargetType::MenuTas)
				type = "menu_tas";
			else if(target.type == TargetType::TravelTas)
				type = "travel_tas";
			entry.emplace("type", type);
			entry.emplace("mapId", target.mapId);
			entry.emplace("mapNameReadOnly", target.mapName);
			if(target.type == TargetType::Delay) {
				entry.emplace("frames", std::max(0, target.delayFrames));
			} else if(target.type == TargetType::Position) {
				entry.emplace("name", target.name);
				entry.emplace(
					"position",
					toml::array {target.position.x, target.position.y, target.position.z}
				);
			} else {
				entry.emplace("recording", target.name);
				if(
					target.type == TargetType::MenuTas
					|| target.type == TargetType::TravelTas
				) {
					entry.emplace("intermediate", target.intermediate);
					entry.emplace(
						"position",
						toml::array {
							target.position.x,
							target.position.y,
							target.position.z
						}
					);
				}
			}
			allTargets.emplace_back(std::move(entry));
		}

		toml::table root;
		root.emplace("targets", std::move(allTargets));
		std::stringstream stream;
		stream << root;
		const std::string contents = stream.str();
		const auto path = TargetsPath();
		if(!NnFile::Preallocate(path, contents.size())) {
			g_Logger->LogError("Couldn't create targets file {}", path);
			return;
		}
		NnFile file(path, nn::fs::OpenMode_Write);
		file.Write(contents.c_str(), contents.size());
		file.Flush();
	}

	Targeting::TargetData* Targeting::NewTarget() {
		const auto mapId = CurrentMapId();
		const auto mapName = detail::IsModuleRegistered(STRINGIFY(DebugStuff))
			? DebugStuff::GetMapName(mapId)
			: "Unknown";
		int mapTargetCount = 0;
		for(const auto& target : Targets) {
			if(target.mapId == mapId)
				mapTargetCount++;
		}
		Targets.push_back({
			.name = fmt::format("{} Target {}", mapName, mapTargetCount + 1),
			.mapName = mapName,
			.mapId = mapId
		});
		auto target = &Targets.back();
		SetTarget(target);
		return target;
	}

	int Targeting::NewDelay(int insertAfter) {
		const auto mapId = CurrentMapId();
		const auto mapName = detail::IsModuleRegistered(STRINGIFY(DebugStuff))
			? DebugStuff::GetMapName(mapId)
			: "Unknown";
		TargetData delay {
			.type = TargetType::Delay,
			.mapName = mapName,
			.mapId = mapId,
			.delayFrames = 60
		};

		int insertionIndex = static_cast<int>(Targets.size());
		if(
			insertAfter >= 0
			&& insertAfter < static_cast<int>(Targets.size())
			&& Targets[insertAfter].mapId == mapId
		) {
			insertionIndex = insertAfter + 1;
		} else {
			for(int index = 0; index < static_cast<int>(Targets.size()); index++) {
				if(Targets[index].mapId == mapId)
					insertionIndex = index + 1;
			}
		}
		Targets.insert(Targets.begin() + insertionIndex, std::move(delay));
		StopRoute();
		return insertionIndex;
	}

	int Targeting::NewSpecialStep(
		TargetType type,
		int insertAfter,
		bool intermediate
	) {
		const auto mapId = CurrentMapId();
		const auto mapName = detail::IsModuleRegistered(STRINGIFY(DebugStuff))
			? DebugStuff::GetMapName(mapId)
			: "Unknown";
		TargetData step {
			.type = type,
			.mapName = mapName,
			.mapId = mapId,
			.intermediate =
				(type == TargetType::MenuTas || type == TargetType::TravelTas)
					&& intermediate
		};
		if(type == TargetType::ShopTas) {
			const auto recordings = MenuHelper::SavedShopRecordingNames();
			if(!recordings.empty())
				step.name = recordings.front();
		} else if(type == TargetType::MenuTas) {
			const auto recordings = MenuHelper::SavedMenuRecordingNames();
			if(!recordings.empty())
				step.name = recordings.front();
			if(const auto position = PlayerMovement::GetPartyPosition(); position != nullptr)
				step.position = *position;
		} else if(type == TargetType::TravelTas) {
			const auto recordings = MenuHelper::SavedTravelRecordingNames();
			if(!recordings.empty())
				step.name = recordings.front();
			if(const auto position = PlayerMovement::GetPartyPosition(); position != nullptr)
				step.position = *position;
		}

		int insertionIndex = static_cast<int>(Targets.size());
		if(
			insertAfter >= 0
			&& insertAfter < static_cast<int>(Targets.size())
			&& Targets[insertAfter].mapId == mapId
		) {
			insertionIndex = insertAfter + 1;
		} else {
			for(int index = 0; index < static_cast<int>(Targets.size()); index++) {
				if(Targets[index].mapId == mapId)
					insertionIndex = index + 1;
			}
		}
		Targets.insert(Targets.begin() + insertionIndex, std::move(step));
		StopRoute();
		return insertionIndex;
	}

	void Targeting::SetTarget(TargetData* target) {
		if(target == nullptr || target->type != TargetType::Position)
			return;
		target->mapId = CurrentMapId();
		target->mapName = detail::IsModuleRegistered(STRINGIFY(DebugStuff))
			? DebugStuff::GetMapName(target->mapId)
			: "Unknown";
		if(const auto position = PlayerMovement::GetPartyPosition(); position != nullptr)
			target->position = *position;
	}

	void Targeting::MenuWindow() {
		// Targeting is docked on the right and no longer consumes a left-stack slot.
		toolwindow::SetVisible(toolwindow::StackSlot::Targeting, false);
		if(!ShowWindow)
			return;

		const auto& io = ImGui::GetIO();
		constexpr float edge = 2.f;
		const float top = toolwindow::RightDockTop;
		ImGui::SetNextWindowPos(
			ImVec2(io.DisplaySize.x - edge, top),
			ImGuiCond_Always,
			ImVec2(1.f, 0.f)
		);
		ImGui::SetNextWindowSize(
			ImVec2(
				toolwindow::CompactWidth(),
				std::max(220.f, io.DisplaySize.y - top - edge)
			),
			ImGuiCond_Always
		);
		const auto windowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
		if(!ImGui::Begin("Targeting", &ShowWindow, windowFlags)) {
			ImGui::End();
			return;
		}

		static int selectedIndex = -1;
		if(ImGui::Button("Load")) {
			LoadTargetsFromFile();
			selectedIndex = Targets.empty() ? -1 : 0;
		}
		ImGui::SameLine();
		if(ImGui::Button("Save"))
			SaveTargetsToFile();
		ImGui::SameLine();
		if(ImGui::Button("+ New")) {
			NewTarget();
			selectedIndex = static_cast<int>(Targets.size()) - 1;
		}
		ImGui::SameLine();
		if(RouteActive) {
			if(ImGui::Button("Stop"))
				StopRoute();
		} else if(ImGui::Button("Start")) {
			StartRoute(selectedIndex);
		}
		ImGui::Checkbox("Start from selection", &StartFromSelection);
		ImGui::SameLine();
		ImGui::Checkbox("Warp/start", &WarpToStart);

		ImGui::Checkbox("Show all", &ShowAllTargets);
		ImGui::SameLine();
		ImGui::Checkbox("Show on map", &ShowTargetsOnMap);
		ImGui::Checkbox("Use arrival radius", &UseArrivalRadius);
		if(UseArrivalRadius) {
			ImGui::PushItemWidth(90.f);
			ImGui::DragFloat(
				"Arrival radius",
				&ArrivalRadius,
				0.05f,
				0.05f,
				10.f,
				"%.2f"
			);
			ImGui::PopItemWidth();
		}

		static TargetType addType = TargetType::Delay;
		static bool addIntermediate = false;
		const bool canBeIntermediate =
			addType == TargetType::MenuTas || addType == TargetType::TravelTas;
		const char* addTypeName = addType == TargetType::Delay
			? "Delay"
			: addType == TargetType::ShopTas
				? "ShopTAS"
				: addType == TargetType::MenuTas ? "MenuTAS" : "TravelTAS";
		const float addStepHeight =
			ImGui::GetFrameHeight()
			+ ImGui::GetStyle().WindowPadding.y * 2.f
			+ (canBeIntermediate ? ImGui::GetFrameHeightWithSpacing() : 0.f);
		if(ImGui::BeginChild(
			"AddRouteStep",
			ImVec2(0.f, addStepHeight),
			true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
		)) {
			ImGui::SetNextItemWidth(120.f);
			if(ImGui::BeginCombo("##RouteStepType", addTypeName)) {
				for(const auto type : {
					TargetType::Delay,
					TargetType::ShopTas,
					TargetType::MenuTas,
					TargetType::TravelTas
				}) {
					const char* name = type == TargetType::Delay
						? "Delay"
						: type == TargetType::ShopTas
							? "ShopTAS"
							: type == TargetType::MenuTas ? "MenuTAS" : "TravelTAS";
					if(ImGui::Selectable(name, addType == type))
						addType = type;
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if(ImGui::Button("Add")) {
				selectedIndex = addType == TargetType::Delay
					? NewDelay(selectedIndex)
					: NewSpecialStep(
						addType,
						selectedIndex,
						canBeIntermediate && addIntermediate
					);
			}
			if(canBeIntermediate)
				ImGui::Checkbox("Intermediate", &addIntermediate);
		}
		ImGui::EndChild();
		if(RouteActive) {
			if(waitingForPlayer)
				ImGui::TextDisabled("Route paused: waiting for control");
			else if(startDelayFrames > 0)
				ImGui::Text("Starting in %d frame(s)", startDelayFrames);
			else if(activeDelayIndex == activeTargetIndex)
				ImGui::Text("Delay: %df", delayFramesRemaining);
			else if(
				activeTargetIndex >= 0
				&& activeTargetIndex < static_cast<int>(Targets.size())
				&& Targets[activeTargetIndex].type == TargetType::ShopTas
			)
				ImGui::Text(
					"ShopTAS: %s",
					Targets[activeTargetIndex].name.c_str()
				);
			else if(
				activeTargetIndex >= 0
				&& activeTargetIndex < static_cast<int>(Targets.size())
				&& Targets[activeTargetIndex].type == TargetType::TravelTas
			)
				ImGui::Text(
					"TravelTAS: %s",
					Targets[activeTargetIndex].name.c_str()
				);
			else if(
				activeTargetIndex >= 0
				&& activeTargetIndex < static_cast<int>(Targets.size())
				&& Targets[activeTargetIndex].type == TargetType::MenuTas
			)
				ImGui::Text(
					"MenuTAS: %s",
					Targets[activeTargetIndex].name.c_str()
				);
			else if(activeTargetIndex >= 0)
				ImGui::Text("Moving to target %d", RouteNumber(activeTargetIndex));
		}

		const auto mapId = CurrentMapId();
		std::vector<int> visibleIndices;
		for(int index = 0; index < static_cast<int>(Targets.size()); index++) {
			if(!ShowAllTargets && Targets[index].mapId != mapId)
				continue;
			visibleIndices.push_back(index);
		}
		if(
			std::find(visibleIndices.begin(), visibleIndices.end(), selectedIndex)
				== visibleIndices.end()
		)
			selectedIndex = visibleIndices.empty() ? -1 : visibleIndices.front();

		const bool hasSelection =
			selectedIndex >= 0 && selectedIndex < static_cast<int>(Targets.size());
		const bool selectedMenuTas = hasSelection
			&& IsMenuTasStep(Targets[selectedIndex]);
		const float editorReserve = hasSelection
			? ImGui::GetTextLineHeightWithSpacing()
				* (selectedMenuTas ? 5.5f : 4.5f)
				+ ImGui::GetFrameHeightWithSpacing()
			: 0.f;
		const float listHeight = std::max(
			ImGui::GetTextLineHeightWithSpacing() * 8.f,
			ImGui::GetContentRegionAvail().y - editorReserve
		);
		if(ImGui::BeginChild("TargetList", ImVec2(0.f, listHeight), true)) {
			for(const int index : visibleIndices) {
				ImGui::PushID(index);
				std::string label;
				if(Targets[index].type == TargetType::Delay) {
					label = fmt::format(
						"Delay: {}f{}",
						std::max(0, Targets[index].delayFrames),
						index == activeTargetIndex ? "  [ACTIVE]" : ""
					);
				} else if(Targets[index].type == TargetType::ShopTas) {
					label = fmt::format(
						"ShopTAS: {}{}",
						Targets[index].name.empty() ? "<select recording>" : Targets[index].name,
						index == activeTargetIndex ? "  [ACTIVE]" : ""
					);
				} else if(Targets[index].type == TargetType::MenuTas) {
					label = fmt::format(
						"MenuTAS: {}{}{}",
						Targets[index].name.empty() ? "<select recording>" : Targets[index].name,
						Targets[index].intermediate ? "  [INTERMEDIATE]" : "",
						index == activeTargetIndex ? "  [ACTIVE]" : ""
					);
				} else if(Targets[index].type == TargetType::TravelTas) {
					label = fmt::format(
						"TravelTAS: {}{}{}",
						Targets[index].name.empty() ? "<select recording>" : Targets[index].name,
						Targets[index].intermediate ? "  [INTERMEDIATE]" : "",
						index == activeTargetIndex ? "  [ACTIVE]" : ""
					);
				} else {
					label = fmt::format(
						"{}: {}{}",
						RouteNumber(index),
						Targets[index].name,
						index == activeTargetIndex ? "  [ACTIVE]" : ""
					);
				}
				if(ImGui::Selectable(label.c_str(), selectedIndex == index))
					selectedIndex = index;
				ImGui::PopID();
			}
		}
		ImGui::EndChild();

		if(selectedIndex >= 0 && selectedIndex < static_cast<int>(Targets.size())) {
			auto& target = Targets[selectedIndex];
			ImGui::Text("Map: %s", target.mapName.c_str());
			if(target.type == TargetType::Delay) {
				ImGui::SetNextItemWidth(100.f);
				if(ImGui::InputInt(
					"Frames",
					&target.delayFrames,
					1,
					60,
					ImGuiInputTextFlags_CharsDecimal
				))
					target.delayFrames = std::max(0, target.delayFrames);
				ImGui::TextDisabled("Delay: %df", std::max(0, target.delayFrames));
			} else if(target.type == TargetType::Position) {
				ImGui::InputText("Name", &target.name);
				ImGui::TextUnformatted("Position");
				ImGui::SameLine();
				DrawPreciseCoordinate("X", target.position.x);
				ImGui::SameLine();
				DrawPreciseCoordinate("Y", target.position.y);
				ImGui::SameLine();
				DrawPreciseCoordinate("Z", target.position.z);
				if(ImGui::Button("Update"))
					SetTarget(&target);
				ImGui::SameLine();
				if(ImGui::Button("Warp"))
					WarpToTarget(selectedIndex);
				ImGui::SameLine();
			} else if(target.type == TargetType::ShopTas) {
				const auto recordings = MenuHelper::SavedShopRecordingNames();
				const char* preview = target.name.empty()
					? "<select recording>"
					: target.name.c_str();
				ImGui::SetNextItemWidth(-1.f);
				if(ImGui::BeginCombo("Recording", preview)) {
					for(const auto& recording : recordings) {
						if(ImGui::Selectable(recording.c_str(), target.name == recording))
							target.name = recording;
					}
					ImGui::EndCombo();
				}
				if(recordings.empty())
					ImGui::TextDisabled("No saved ShopTAS recordings");
			} else {
				const auto recordings = target.type == TargetType::TravelTas
					? MenuHelper::SavedTravelRecordingNames()
					: MenuHelper::SavedMenuRecordingNames();
				const char* preview = target.name.empty()
					? "<select recording>"
					: target.name.c_str();
				ImGui::SetNextItemWidth(-1.f);
				if(ImGui::BeginCombo("Recording", preview)) {
					for(const auto& recording : recordings) {
						if(ImGui::Selectable(recording.c_str(), target.name == recording))
							target.name = recording;
					}
					ImGui::EndCombo();
				}
				if(recordings.empty())
					ImGui::TextDisabled(
						target.type == TargetType::TravelTas
							? "No saved TravelTAS recordings"
							: "No saved MenuTAS recordings"
					);
				if(ImGui::Checkbox("Intermediate", &target.intermediate))
					StopRoute();
				if(target.intermediate) {
					ImGui::TextDisabled(
						"Initializer: arms playback and continues the route"
					);
				} else {
					ImGui::TextUnformatted("Map marker");
					ImGui::SameLine();
					DrawPreciseCoordinate("X", target.position.x);
					ImGui::SameLine();
					DrawPreciseCoordinate("Y", target.position.y);
					ImGui::SameLine();
					DrawPreciseCoordinate("Z", target.position.z);
					if(ImGui::Button("Update marker")) {
						if(const auto position = PlayerMovement::GetPartyPosition(); position != nullptr)
							target.position = *position;
					}
				}
			}
			if(ImGui::Button("Up")) {
				selectedIndex = MoveTargetWithinMap(selectedIndex, -1);
				StopRoute();
			}
			ImGui::SameLine();
			if(ImGui::Button("Down")) {
				selectedIndex = MoveTargetWithinMap(selectedIndex, 1);
				StopRoute();
			}
			ImGui::SameLine();
			if(ImGui::Button("Delete")) {
				StopRoute();
				Targets.erase(Targets.begin() + selectedIndex);
				selectedIndex = Targets.empty()
					? -1
					: std::min(selectedIndex, static_cast<int>(Targets.size()) - 1);
			}
		}

		ImGui::End();
	}

	void Targeting::Initialize() {
		UpdatableModule::Initialize();
		LoadTargetsFromFile();
		g_Menu->RegisterRenderCallback(&MenuWindow, true);
	}

	void Targeting::Update(fw::UpdateInfo*) {
		if(
			ShowTargetsOnMap
			&& detail::IsModuleRegistered(STRINGIFY(DebugStuff))
			&& PlayerMovement::GetPartyPosition() != nullptr
			&& CameraTools::HasCameraState
		) {
			const auto mapId = CurrentMapId();
			for(int index = 0; index < static_cast<int>(Targets.size()); index++) {
				const auto& target = Targets[index];
				if(
					(target.type != TargetType::Position
						&& target.type != TargetType::MenuTas
						&& target.type != TargetType::TravelTas)
					|| (IsMenuTasStep(target) && target.intermediate)
					|| target.mapId != mapId
					|| !IsFinite(target.position)
				)
					continue;
				glm::mat4 matrix(1.f);
				matrix = glm::translate(matrix, target.position);
				fw::debug::drawCompareZ(false);
				fw::debug::drawAxis(matrix, 2.f);
				fw::debug::drawCompareZ(true);
				if(
					target.type == TargetType::MenuTas
					|| target.type == TargetType::TravelTas
				) {
					debug::drawFontFmtShadow3D(
						target.position,
						index == activeTargetIndex ? mm::Col4::yellow : mm::Col4::white,
						target.type == TargetType::TravelTas
							? "TravelTAS: {}"
							: "MenuTAS: {}",
						target.name
					);
				} else {
					debug::drawFontFmtShadow3D(
						target.position,
						index == activeTargetIndex ? mm::Col4::yellow : mm::Col4::white,
						"{}: {}",
						RouteNumber(index),
						target.name
					);
				}
			}
		}
		if(
			targetingMenuTasPlayback
			&& MenuHelper::IsMenuPlaybackPendingOrActive()
		) {
			InputBuffer::SetLeftStickOverride(false);
			ResetTargetApproach();
			return;
		}
		targetingMenuTasPlayback = false;

		if(!RouteActive) {
			InputBuffer::SetLeftStickOverride(false);
			return;
		}

		const auto mapId = CurrentMapId();
		if(
			activeTargetIndex < 0
			|| activeTargetIndex >= static_cast<int>(Targets.size())
			|| Targets[activeTargetIndex].mapId != mapId
		)
			activeTargetIndex = FindFirstTargetForMap(mapId);
		if(activeTargetIndex < 0) {
			InputBuffer::SetLeftStickOverride(false);
			return;
		}
		// ShopTAS is only an arming initializer and can be consumed before
		// field control returns. MenuTAS opens a real menu and is processed
		// below after the normal control-safety checks.
		if(Targets[activeTargetIndex].type == TargetType::ShopTas) {
			ConsumeActiveSpecialSteps(mapId);
			return;
		}

		const auto playerPosition = PlayerMovement::GetPartyPosition();
		if(playerPosition == nullptr || !CameraTools::HasCameraState) {
			waitingForPlayer = true;
			ResetTargetApproach();
			InputBuffer::SetLeftStickOverride(false);
			return;
		}
		if(!gf::GfGameManager::isControlFree()) {
			waitingForPlayer = true;
			ResetTargetApproach();
			InputBuffer::SetLeftStickOverride(false);
			return;
		}
		waitingForPlayer = false;
		if(
			!IsFinite(*playerPosition)
			|| !IsFinite(CameraTools::CamMeta.forward)
		) {
			InputBuffer::SetLeftStickOverride(false);
			return;
		}
		if(startDelayFrames > 0) {
			startDelayFrames--;
			ResetTargetApproach();
			InputBuffer::SetLeftStickOverride(false);
			return;
		}
		if(ConsumeActiveSpecialSteps(mapId)) {
			return;
		}

		int movementTargetIndex = activeTargetIndex;
		if(!IsFinite(Targets[movementTargetIndex].position)) {
			StopRoute();
			return;
		}

		glm::vec3 delta = Targets[movementTargetIndex].position - *playerPosition;
		delta.y = 0.f;
		float distance = glm::length(delta);
		const float completionDistance =
			UseArrivalRadius ? std::max(ArrivalRadius, 0.05f) : 0.005f;
		bool crossedExactTarget =
			!UseArrivalRadius
			&& hasPreviousTargetDelta
			&& trackedTargetIndex == movementTargetIndex
			&& glm::dot(previousTargetDelta, delta) <= 0.f;
		while(distance <= completionDistance || crossedExactTarget) {
			const auto& reachedTarget = Targets[movementTargetIndex];
			if(IsMenuTasStep(reachedTarget) && !reachedTarget.intermediate) {
				InputBuffer::SetLeftStickOverride(false);
				ResetTargetApproach();
				if(!StartMenuTasStep(reachedTarget)) {
					StopRoute();
					g_Logger->ToastWarning(
						"targeting",
						"Could not start {} at its marker",
						reachedTarget.type == TargetType::TravelTas
							? "TravelTAS"
							: "MenuTAS"
					);
					return;
				}

				activeTargetIndex = FindNextTargetForMap(
					movementTargetIndex,
					mapId
				);
				targetingMenuTasPlayback = true;
				if(activeTargetIndex < 0) {
					RouteActive = false;
					g_Logger->ToastInfo("targeting", "Target route complete");
				}
				return;
			}
			activeTargetIndex = FindNextTargetForMap(activeTargetIndex, mapId);
			ResetTargetApproach();
			if(activeTargetIndex < 0) {
				StopRoute();
				g_Logger->ToastInfo("targeting", "Target route complete");
				return;
			}
			if(ConsumeActiveSpecialSteps(mapId))
				return;
			movementTargetIndex = activeTargetIndex;
			delta = Targets[movementTargetIndex].position - *playerPosition;
			delta.y = 0.f;
			distance = glm::length(delta);
			crossedExactTarget = false;
			// Do not clear the existing override here. This update continues
			// below and atomically replaces it with the next target's direction,
			// leaving no neutral input frame between route points.
		}
		trackedTargetIndex = movementTargetIndex;
		previousTargetDelta = delta;
		hasPreviousTargetDelta = true;

		const glm::vec3 direction = delta / distance;
		glm::vec3 forward = CameraTools::CamMeta.forward;
		forward.y = 0.f;
		const float forwardLength = glm::length(forward);
		if(!std::isfinite(forwardLength) || forwardLength <= 0.0001f) {
			InputBuffer::SetLeftStickOverride(false);
			return;
		}
		forward /= forwardLength;
		const glm::vec3 right = glm::normalize(
			glm::cross(glm::vec3(0.f, 1.f, 0.f), forward)
		);
		InputBuffer::SetLeftStickOverride(
			true,
			glm::dot(direction, right),
			-glm::dot(direction, forward)
		);
	}

	void Targeting::OnSceneTransition() {
		targetingMenuTasPlayback = false;
		InputBuffer::SetLeftStickOverride(false);
		ResetTargetApproach();
		activeDelayIndex = -1;
		delayFramesRemaining = 0;
		waitingForPlayer = RouteActive;
	}

	void Targeting::OnMapChange(unsigned short mapId) {
		if(
			RouteActive
			&& (
				activeTargetIndex < 0
				|| activeTargetIndex >= static_cast<int>(Targets.size())
				|| Targets[activeTargetIndex].mapId != mapId
			)
		)
			activeTargetIndex = FindFirstTargetForMap(mapId);
	}

#if XENOMODS_CODENAME(bf2)
	XENOMODS_REGISTER_MODULE(Targeting);
#endif

} // namespace xenomods
