#include "Targeting.hpp"

#include "CameraTools.hpp"
#include "AutoCutsceneSkips.hpp"
#include "DebugStuff.hpp"
#include "MenuHelper.hpp"
#include "PlayerMovement.hpp"
#include "ToolWindowLayout.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_map>

#include <fmt/format.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <nn/fs.h>
#include <nn/os.hpp>
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

	namespace {
		int activeTargetIndex = -1;
		int selectedTargetIndex = -1;
		bool waitingForPlayer = false;
		int startDelayFrames = 0;
		int activeDelayIndex = -1;
		int delayFramesRemaining = 0;
		struct ActiveAction {
			int index = -1;
			int delayFramesRemaining = 0;
			int holdFramesRemaining = 0;
			bool started = false;
			bool bufferedRequested = false;
			bool bufferedAccepted = false;
		};
		std::vector<ActiveAction> activeActions;
		int trackedTargetIndex = -1;
		glm::vec3 previousTargetDelta {};
		bool hasPreviousTargetDelta = false;
		bool targetingMenuTasPlayback = false;
		std::uint64_t scriptFrameCount = 0;
		std::uint64_t bestScriptFrames = 0;
		bool scriptTimerRunning = false;
		bool scriptMovementComplete = false;
		std::uint64_t targetToTargetFrameCount = 0;
		bool targetToTargetTimerRunning = false;
		int targetToTargetOriginIndex = -1;
		int mostRecentTargetToTargetOriginIndex = -1;
		bool inspectSelectedSplit = false;
		std::string lastSavedTargets;
		std::string currentRouteFile = "targets0.toml";
		bool currentRouteWasCreatedEmpty = false;
		std::vector<std::string> routeFiles;
		std::string routeFileError;
		std::string lastSavedRouteSetting;
		std::uint64_t lastBackupTick = 0;
		constexpr std::uint64_t SystemTicksPerSecond = 19200000;
		constexpr std::uint64_t BackupIntervalTicks =
			SystemTicksPerSecond * 60 * 5;

		struct ButtonChoice {
			const char* name;
			std::uint32_t mask;
		};

		// Targeting Actions are injected into XC2's fw::PadData/ml::DevPad path.
		// These are the game's physical pad masks, not nn::hid::NpadButton bit
		// positions. Keeping that distinction here is essential: for example,
		// Npad Plus (1 << 10) is XC2's Stick L bit, while physical Plus is 0x200.
		constexpr std::uint32_t PhysicalA = 0x00000004u;
		constexpr std::array<ButtonChoice, 16> ButtonChoices {{
			{"A", PhysicalA},
			{"B", 0x00000002u},
			{"X", 0x00000008u},
			{"Y", 0x00000001u},
			{"L", 0x00000010u},
			{"R", 0x00000040u},
			{"ZL", 0x00000080u},
			{"ZR", 0x00000800u},
			{"Plus", 0x00000200u},
			{"Minus", 0x00000100u},
			{"Stick L", 0x00000400u},
			{"Stick R", 0x00000020u},
			{"D-pad Up", 0x00002000u},
			{"D-pad Down", 0x00008000u},
			{"D-pad Left", 0x00001000u},
			{"D-pad Right", 0x00004000u}
		}};

		std::string ActionName(std::uint32_t mask) {
			std::string name;
			for(const auto& choice : ButtonChoices) {
				if((mask & choice.mask) == 0)
					continue;
				if(!name.empty())
					name += " + ";
				name += choice.name;
			}
			return name.empty() ? "<select input>" : name;
		}

		std::string ActionName(const Targeting::TargetData& action) {
			if(action.actionInputType == Targeting::ActionInputType::LeftStick)
				return fmt::format("L Stick X {:.2f} Y {:.2f}", action.stickX, action.stickY);
			if(action.actionInputType == Targeting::ActionInputType::RightStick)
				return fmt::format("R Stick X {:.2f} Y {:.2f}", action.stickX, action.stickY);
			return ActionName(action.buttonMask);
		}

		void ResetTargetApproach() {
			trackedTargetIndex = -1;
			previousTargetDelta = {};
			hasPreviousTargetDelta = false;
		}

		void ClearActiveActions() {
			activeActions.clear();
			InputBuffer::SetRawButtonOverride(false);
			InputBuffer::SetActionStickOverride(false);
			InputBuffer::CancelBufferedButtonAction();
		}

		void QueueAction(int index) {
			if(index < 0 || index >= static_cast<int>(Targeting::Targets.size()))
				return;
			const auto& action = Targeting::Targets[index];
			activeActions.push_back({
				.index = index,
				.delayFramesRemaining = std::max(0, action.delayFrames),
				.holdFramesRemaining = std::max(1, action.holdFrames)
			});
		}

		void UpdateActiveAction(bool controlFree) {
			std::uint32_t heldButtons = 0;
			std::uint32_t downButtons = 0;
			bool leftStickActive = false;
			bool rightStickActive = false;
			float leftX = 0.f;
			float leftY = 0.f;
			float rightX = 0.f;
			float rightY = 0.f;

			const std::uint32_t pendingBuffered =
				InputBuffer::BufferedButtonActionPendingMask();
			for(auto& runtime : activeActions) {
				if(
					runtime.index < 0
					|| runtime.index >= static_cast<int>(Targeting::Targets.size())
				)
					continue;
				const auto& action = Targeting::Targets[runtime.index];
				if(!runtime.started) {
					const bool bufferedButtonAction =
						action.actionInputType == Targeting::ActionInputType::Buttons
						&& action.buffered;
					if(!controlFree && !bufferedButtonAction)
						continue;
					if(runtime.delayFramesRemaining > 0) {
						runtime.delayFramesRemaining--;
						continue;
					}
					runtime.started = true;
				}

				if(action.actionInputType == Targeting::ActionInputType::Buttons) {
					if(action.buffered && !runtime.bufferedAccepted) {
						if(!runtime.bufferedRequested) {
							InputBuffer::SetBufferedButtonAction(action.buttonMask);
							runtime.bufferedRequested = true;
							continue;
						}
						if((pendingBuffered & action.buttonMask) != 0)
							continue;
						runtime.bufferedAccepted = true;
						runtime.holdFramesRemaining--;
						if(runtime.holdFramesRemaining <= 0)
							continue;
					}
					heldButtons |= action.buttonMask;
					if(runtime.holdFramesRemaining == std::max(1, action.holdFrames))
						downButtons |= action.buttonMask;
					runtime.holdFramesRemaining--;
				} else if(action.actionInputType == Targeting::ActionInputType::LeftStick) {
					leftStickActive = true;
					leftX = action.stickX;
					leftY = action.stickY;
					runtime.holdFramesRemaining--;
				} else {
					rightStickActive = true;
					rightX = action.stickX;
					rightY = action.stickY;
					runtime.holdFramesRemaining--;
				}
			}

			std::erase_if(activeActions, [](const ActiveAction& runtime) {
				return runtime.index < 0
					|| runtime.index >= static_cast<int>(Targeting::Targets.size())
					|| runtime.holdFramesRemaining <= 0;
			});
			InputBuffer::SetRawButtonOverride(
				heldButtons != 0,
				heldButtons,
				downButtons
			);
			InputBuffer::SetActionStickOverride(false);
			if(leftStickActive)
				InputBuffer::SetActionStickOverride(true, false, leftX, leftY);
			if(rightStickActive)
				InputBuffer::SetActionStickOverride(true, true, rightX, rightY);
			if(activeActions.empty())
				InputBuffer::CancelBufferedButtonAction();
		}

		bool IsMenuTasStep(const Targeting::TargetData& target) {
			return target.type == Targeting::TargetType::MenuTas
				|| target.type == Targeting::TargetType::TravelTas;
		}

		bool IsMovementTarget(const Targeting::TargetData& target) {
			return target.type == Targeting::TargetType::Position
				|| (IsMenuTasStep(target) && !target.intermediate);
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

		void SaveTargetsIfChanged();

		void MarkRouteMovementComplete() {
			Targeting::RouteActive = false;
			activeTargetIndex = -1;
			waitingForPlayer = false;
			startDelayFrames = 0;
			activeDelayIndex = -1;
			delayFramesRemaining = 0;
			ResetTargetApproach();
			InputBuffer::SetLeftStickOverride(false);
			scriptMovementComplete = true;
			targetToTargetTimerRunning = false;
		}

		void TryCompleteScriptTimer() {
			if(!scriptTimerRunning || !scriptMovementComplete)
				return;
			if(
				!activeActions.empty()
				|| targetingMenuTasPlayback
				|| MenuHelper::IsPlaybackPendingOrActive()
			)
				return;

			scriptTimerRunning = false;
			if(
				scriptFrameCount > 0
				&& (bestScriptFrames == 0 || scriptFrameCount < bestScriptFrames)
			) {
				bestScriptFrames = scriptFrameCount;
				SaveTargetsIfChanged();
				g_Logger->ToastInfo(
					"targeting",
					"Script complete: {}f (new best)",
					scriptFrameCount
				);
			} else {
				g_Logger->ToastInfo(
					"targeting",
					"Script complete: {}f",
					scriptFrameCount
				);
			}
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

		bool WriteTargetsFile(const std::string& path, const std::string& contents);
		bool NormalizeTargetNames(std::vector<Targeting::TargetData>& targets);

		std::string LegacyTargetsPath() {
			return fmt::format(
				XENOMODS_CONFIG_PATH "/{}/targets.toml",
				XENOMODS_CODENAME_STR
			);
		}

		std::string TargetsDirectory() {
			return fmt::format(
				XENOMODS_CONFIG_PATH "/{}/Targets",
				XENOMODS_CODENAME_STR
			);
		}

		std::string TargetsPath() {
			return fmt::format("{}/{}", TargetsDirectory(), currentRouteFile);
		}

		std::string TargetsBackupPath() {
			std::string stem = currentRouteFile;
			if(stem.size() >= 5 && stem.ends_with(".toml"))
				stem.resize(stem.size() - 5);
			return fmt::format("{}/{}.backup.toml", TargetsDirectory(), stem);
		}

		std::string TargetNamesMigrationBackupPath() {
			std::string stem = currentRouteFile;
			if(stem.size() >= 5 && stem.ends_with(".toml"))
				stem.resize(stem.size() - 5);
			return fmt::format(
				"{}/{}.pre-autoname.backup.toml",
				TargetsDirectory(),
				stem
			);
		}

		std::string TargetingSettingsPath() {
			return fmt::format(
				XENOMODS_CONFIG_PATH "/{}/targetingSettings.toml",
				XENOMODS_CODENAME_STR
			);
		}

		bool FileExists(const std::string& path) {
			nn::fs::DirectoryEntryType type {};
			return R_SUCCEEDED(nn::fs::GetEntryType(&type, path.c_str()))
				&& type == nn::fs::DirectoryEntryType_File;
		}

		bool IsRouteFile(const char* name) {
			if(name == nullptr)
				return false;
			const std::string value(name);
			return value.size() > 5
				&& value.ends_with(".toml")
				&& !value.ends_with(".backup.toml");
		}

		bool NaturalRouteFileLess(const std::string& left, const std::string& right) {
			std::size_t leftIndex = 0;
			std::size_t rightIndex = 0;
			while(leftIndex < left.size() && rightIndex < right.size()) {
				const bool leftDigit = std::isdigit(
					static_cast<unsigned char>(left[leftIndex])
				) != 0;
				const bool rightDigit = std::isdigit(
					static_cast<unsigned char>(right[rightIndex])
				) != 0;
				if(leftDigit && rightDigit) {
					const std::size_t leftRunStart = leftIndex;
					const std::size_t rightRunStart = rightIndex;
					while(leftIndex < left.size() && left[leftIndex] == '0')
						leftIndex++;
					while(rightIndex < right.size() && right[rightIndex] == '0')
						rightIndex++;
					const std::size_t leftValueStart = leftIndex;
					const std::size_t rightValueStart = rightIndex;
					while(
						leftIndex < left.size()
						&& std::isdigit(static_cast<unsigned char>(left[leftIndex]))
					)
						leftIndex++;
					while(
						rightIndex < right.size()
						&& std::isdigit(static_cast<unsigned char>(right[rightIndex]))
					)
						rightIndex++;
					const std::size_t leftDigits = leftIndex - leftValueStart;
					const std::size_t rightDigits = rightIndex - rightValueStart;
					if(leftDigits != rightDigits)
						return leftDigits < rightDigits;
					const int valueOrder = left.compare(
						leftValueStart,
						leftDigits,
						right,
						rightValueStart,
						rightDigits
					);
					if(valueOrder != 0)
						return valueOrder < 0;
					const std::size_t leftRunLength = leftIndex - leftRunStart;
					const std::size_t rightRunLength = rightIndex - rightRunStart;
					if(leftRunLength != rightRunLength)
						return leftRunLength < rightRunLength;
					continue;
				}

				const unsigned char leftCharacter = static_cast<unsigned char>(
					std::tolower(static_cast<unsigned char>(left[leftIndex]))
				);
				const unsigned char rightCharacter = static_cast<unsigned char>(
					std::tolower(static_cast<unsigned char>(right[rightIndex]))
				);
				if(leftCharacter != rightCharacter)
					return leftCharacter < rightCharacter;
				leftIndex++;
				rightIndex++;
			}
			if(leftIndex != left.size() || rightIndex != right.size())
				return leftIndex == left.size();
			return left < right;
		}

		void RescanRouteFiles() {
			routeFiles.clear();
			nn::fs::DirectoryHandle directory {};
			if(R_FAILED(nn::fs::OpenDirectory(
				&directory,
				TargetsDirectory().c_str(),
				nn::fs::OpenDirectoryMode_File
			)))
				return;
			s64 count = 0;
			if(R_FAILED(nn::fs::GetDirectoryEntryCount(&count, directory))) {
				nn::fs::CloseDirectory(directory);
				return;
			}
			std::vector<nn::fs::DirectoryEntry> entries(
				static_cast<std::size_t>(std::max<s64>(count, 0))
			);
			s64 read = 0;
			if(count > 0)
				nn::fs::ReadDirectory(&read, entries.data(), directory, count);
			nn::fs::CloseDirectory(directory);
			for(s64 index = 0; index < read; index++) {
				if(IsRouteFile(entries[index].name))
					routeFiles.emplace_back(entries[index].name);
			}
			std::sort(routeFiles.begin(), routeFiles.end(), NaturalRouteFileLess);
		}

		void SaveRouteSettingIfChanged() {
			if(currentRouteFile == lastSavedRouteSetting)
				return;
			toml::table root;
			root.emplace("active_route", currentRouteFile);
			std::stringstream stream;
			stream << root;
			if(WriteTargetsFile(TargetingSettingsPath(), stream.str()))
				lastSavedRouteSetting = currentRouteFile;
		}

		std::string SerializeTargets() {
			toml::array allTargets;
			for(const auto& target : Targeting::Targets) {
				toml::table entry;
				const char* type = "position";
				if(target.type == Targeting::TargetType::Delay)
					type = "delay";
				else if(target.type == Targeting::TargetType::Action)
					type = "action";
				else if(target.type == Targeting::TargetType::Toggle)
					type = "toggle";
				else if(target.type == Targeting::TargetType::ShopTas)
					type = "shop_tas";
				else if(target.type == Targeting::TargetType::MenuTas)
					type = "menu_tas";
				else if(target.type == Targeting::TargetType::TravelTas)
					type = "travel_tas";
				entry.emplace("type", type);
				entry.emplace("mapId", target.mapId);
				entry.emplace("mapNameReadOnly", target.mapName);
				if(target.type == Targeting::TargetType::Delay) {
					entry.emplace("frames", std::max(0, target.delayFrames));
				} else if(target.type == Targeting::TargetType::Action) {
					const char* inputType = "buttons";
					if(target.actionInputType == Targeting::ActionInputType::LeftStick)
						inputType = "left_stick";
					else if(target.actionInputType == Targeting::ActionInputType::RightStick)
						inputType = "right_stick";
					entry.emplace("input_type", inputType);
					toml::array buttons;
					for(const auto& choice : ButtonChoices) {
						if((target.buttonMask & choice.mask) != 0)
							buttons.emplace_back(choice.name);
					}
					entry.emplace("buttons", std::move(buttons));
					entry.emplace(
						"stick",
						toml::array {target.stickX, target.stickY}
					);
					entry.emplace("delay_frames", std::max(0, target.delayFrames));
					entry.emplace("hold_frames", std::max(1, target.holdFrames));
					entry.emplace("buffer", target.buffered);
				} else if(target.type == Targeting::TargetType::Toggle) {
					entry.emplace("setting", std::string("auto_cutscene_skips"));
					entry.emplace("enabled", target.toggleEnabled);
				} else if(target.type == Targeting::TargetType::Position) {
					entry.emplace("name", target.name);
					entry.emplace(
						"last_frames",
						static_cast<std::int64_t>(std::min<std::uint64_t>(
							target.lastFrames,
							static_cast<std::uint64_t>(
								std::numeric_limits<std::int64_t>::max()
							)
						))
					);
					entry.emplace(
						"best_frames",
						static_cast<std::int64_t>(std::min<std::uint64_t>(
							target.bestFrames,
							static_cast<std::uint64_t>(
								std::numeric_limits<std::int64_t>::max()
							)
						))
					);
					entry.emplace(
						"position",
						toml::array {
							target.position.x,
							target.position.y,
							target.position.z
						}
					);
				} else {
					entry.emplace("recording", target.name);
					if(
						target.type == Targeting::TargetType::MenuTas
						|| target.type == Targeting::TargetType::TravelTas
					) {
						entry.emplace(
							"last_frames",
							static_cast<std::int64_t>(std::min<std::uint64_t>(
								target.lastFrames,
								static_cast<std::uint64_t>(
									std::numeric_limits<std::int64_t>::max()
								)
							))
						);
						entry.emplace(
							"best_frames",
							static_cast<std::int64_t>(std::min<std::uint64_t>(
								target.bestFrames,
								static_cast<std::uint64_t>(
									std::numeric_limits<std::int64_t>::max()
								)
							))
						);
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
			root.emplace(
				"best_frames",
				static_cast<std::int64_t>(std::min<std::uint64_t>(
					bestScriptFrames,
					static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
				))
			);
			root.emplace("targets", std::move(allTargets));
			std::stringstream stream;
			stream << root;
			return stream.str();
		}

		bool WriteTargetsFile(const std::string& path, const std::string& contents) {
			if(!NnFile::Preallocate(path, contents.size())) {
				g_Logger->LogError("Couldn't create targets file {}", path);
				return false;
			}
			NnFile file(path, nn::fs::OpenMode_Write);
			if(!file.Ok()) {
				g_Logger->LogError("Couldn't open targets file {}", path);
				return false;
			}
			file.Write(contents.c_str(), contents.size());
			file.Flush();
			return true;
		}

		void SaveTargetsIfChanged() {
			NormalizeTargetNames(Targeting::Targets);
			const std::string contents = SerializeTargets();
			if(contents == lastSavedTargets)
				return;
			if(WriteTargetsFile(TargetsPath(), contents))
				lastSavedTargets = contents;
		}

		void UpdateTargetsBackup() {
			const std::uint64_t now = nn::os::GetSystemTick();
			if(lastBackupTick == 0) {
				lastBackupTick = now;
				return;
			}
			if(now - lastBackupTick < BackupIntervalTicks)
				return;
			lastBackupTick = now;
			WriteTargetsFile(TargetsBackupPath(), SerializeTargets());
		}

		bool CopyFileUnchanged(const std::string& source, const std::string& destination) {
			NnFile input(source, nn::fs::OpenMode_Read);
			if(!input.Ok() || input.Size() < 0)
				return false;
			std::string contents(static_cast<std::size_t>(input.Size()), '\0');
			if(!contents.empty() && !input.Read(contents.data(), input.Size()))
				return false;
			return WriteTargetsFile(destination, contents);
		}

		bool NormalizeTargetNames(std::vector<Targeting::TargetData>& targets) {
			std::unordered_map<unsigned short, int> mapOrdinals;
			bool changed = false;
			for(auto& target : targets) {
				if(target.type != Targeting::TargetType::Position)
					continue;
				const int ordinal = ++mapOrdinals[target.mapId];
				const std::string mapName = target.mapName.empty()
					? "Unknown"
					: target.mapName;
				const std::string canonicalName = fmt::format(
					"{} Target {}",
					mapName,
					ordinal
				);
				if(target.name != canonicalName) {
					target.name = canonicalName;
					changed = true;
				}
			}
			return changed;
		}

		std::vector<int> MapEntryIndices(unsigned short mapId) {
			std::vector<int> indices;
			for(int index = 0; index < static_cast<int>(Targeting::Targets.size()); index++) {
				if(Targeting::Targets[index].mapId == mapId)
					indices.push_back(index);
			}
			return indices;
		}

		int InsertionIndexAfterTargetBlock(unsigned short mapId, int selectedIndex) {
			const auto mapIndices = MapEntryIndices(mapId);
			if(mapIndices.empty())
				return static_cast<int>(Targeting::Targets.size());

			const auto selected = std::find(
				mapIndices.begin(),
				mapIndices.end(),
				selectedIndex
			);
			if(selected == mapIndices.end())
				return mapIndices.back() + 1;

			const int selectedLogical = static_cast<int>(selected - mapIndices.begin());
			int blockStart = selectedLogical;
			while(
				blockStart >= 0
				&& Targeting::Targets[mapIndices[blockStart]].type
					!= Targeting::TargetType::Position
			)
				blockStart--;

			if(blockStart < 0) {
				const int nextLogical = selectedLogical + 1;
				return nextLogical < static_cast<int>(mapIndices.size())
					? mapIndices[nextLogical]
					: mapIndices.back() + 1;
			}

			int nextBlock = blockStart + 1;
			while(
				nextBlock < static_cast<int>(mapIndices.size())
				&& Targeting::Targets[mapIndices[nextBlock]].type
					!= Targeting::TargetType::Position
			)
				nextBlock++;
			return nextBlock < static_cast<int>(mapIndices.size())
				? mapIndices[nextBlock]
				: mapIndices.back() + 1;
		}

		int RouteFileNumber(const std::string& name) {
			if(!name.starts_with("targets") || !name.ends_with(".toml"))
				return -1;
			const auto number = name.substr(7, name.size() - 12);
			if(number.empty())
				return -1;
			int result = 0;
			for(const char character : number) {
				if(!std::isdigit(static_cast<unsigned char>(character)))
					return -1;
				result = result * 10 + (character - '0');
			}
			return result;
		}

		void CreateNewRoute() {
			int maximum = -1;
			for(const auto& file : routeFiles)
				maximum = std::max(maximum, RouteFileNumber(file));
			currentRouteFile = fmt::format("targets{}.toml", maximum + 1);
			Targeting::StopRoute();
			Targeting::Targets.clear();
			bestScriptFrames = 0;
			scriptFrameCount = 0;
			targetToTargetFrameCount = 0;
			targetToTargetTimerRunning = false;
			mostRecentTargetToTargetOriginIndex = -1;
			lastSavedTargets.clear();
			Targeting::SaveTargetsToFile();
			currentRouteWasCreatedEmpty = true;
			RescanRouteFiles();
			SaveRouteSettingIfChanged();
			routeFileError.clear();
		}

		void InitializeRouteFiles() {
			const toml::parse_result settings = toml::parse_file(TargetingSettingsPath());
			if(settings)
				currentRouteFile = settings["active_route"].value_or<std::string>(
					"targets0.toml"
				);

			RescanRouteFiles();
			if(routeFiles.empty()) {
				currentRouteFile = "targets0.toml";
				if(FileExists(LegacyTargetsPath())) {
					if(!CopyFileUnchanged(LegacyTargetsPath(), TargetsPath()))
						g_Logger->LogError("Couldn't migrate legacy targets.toml");
				} else {
					Targeting::Targets.clear();
					lastSavedTargets.clear();
					Targeting::SaveTargetsToFile();
				}
				RescanRouteFiles();
			}
			if(
				std::find(routeFiles.begin(), routeFiles.end(), currentRouteFile)
					== routeFiles.end()
			)
				currentRouteFile = routeFiles.empty() ? "targets0.toml" : routeFiles.front();
			lastSavedRouteSetting.clear();
			SaveRouteSettingIfChanged();
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

		int FindNextMovementTargetForMap(int current, unsigned short mapId) {
			for(
				int index = current + 1;
				index < static_cast<int>(Targeting::Targets.size());
				index++
			) {
				if(
					Targeting::Targets[index].mapId == mapId
					&& IsMovementTarget(Targeting::Targets[index])
				)
					return index;
			}
			return -1;
		}

		void CompleteTargetSplit(int targetIndex, unsigned short mapId) {
			if(
				targetIndex < 0
				|| targetIndex >= static_cast<int>(Targeting::Targets.size())
			)
				return;
			const bool hasNextTarget =
				FindNextMovementTargetForMap(targetIndex, mapId) >= 0;
			if(targetToTargetOriginIndex < 0) {
				targetToTargetOriginIndex = targetIndex;
				targetToTargetFrameCount = 0;
				targetToTargetTimerRunning = hasNextTarget;
				return;
			}
			if(!targetToTargetTimerRunning)
				return;

			if(
				targetToTargetOriginIndex
					>= static_cast<int>(Targeting::Targets.size())
			)
				return;
			auto& section = Targeting::Targets[targetToTargetOriginIndex];
			mostRecentTargetToTargetOriginIndex = targetToTargetOriginIndex;
			inspectSelectedSplit = false;
			section.lastFrames = targetToTargetFrameCount;
			if(
				targetToTargetFrameCount > 0
				&& (section.bestFrames == 0
					|| targetToTargetFrameCount < section.bestFrames)
			) {
				section.bestFrames = targetToTargetFrameCount;
			}
			SaveTargetsIfChanged();
			targetToTargetOriginIndex = targetIndex;
			targetToTargetFrameCount = 0;
			targetToTargetTimerRunning = hasNextTarget;
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
			ClearActiveActions();
			ResetTargetApproach();
			InputBuffer::SetLeftStickOverride(false);
			if(!Targeting::RouteActive) {
				g_Logger->ToastWarning("targeting", "No targets exist for this map");
				return;
			}
			scriptFrameCount = 0;
			scriptTimerRunning = true;
			scriptMovementComplete = false;
			targetToTargetFrameCount = 0;
			targetToTargetTimerRunning = false;
			targetToTargetOriginIndex = -1;
			inspectSelectedSplit = false;
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
			if(direction != -1 && direction != 1)
				return index;
			const auto mapId = Targeting::Targets[index].mapId;
			const auto mapIndices = MapEntryIndices(mapId);
			const auto selected = std::find(mapIndices.begin(), mapIndices.end(), index);
			if(selected == mapIndices.end())
				return index;
			const int logicalIndex = static_cast<int>(selected - mapIndices.begin());

			if(Targeting::Targets[index].type != Targeting::TargetType::Position) {
				const int otherLogical = logicalIndex + direction;
				if(
					otherLogical < 0
					|| otherLogical >= static_cast<int>(mapIndices.size())
					|| Targeting::Targets[mapIndices[otherLogical]].type
						== Targeting::TargetType::Position
				)
					return index;
				std::swap(
					Targeting::Targets[index],
					Targeting::Targets[mapIndices[otherLogical]]
				);
				NormalizeTargetNames(Targeting::Targets);
				return mapIndices[otherLogical];
			}

			int currentEnd = logicalIndex + 1;
			while(
				currentEnd < static_cast<int>(mapIndices.size())
				&& Targeting::Targets[mapIndices[currentEnd]].type
					!= Targeting::TargetType::Position
			)
				currentEnd++;

			int rotateFirst = logicalIndex;
			int rotateMiddle = currentEnd;
			int rotateLast = currentEnd;
			int newLogicalIndex = logicalIndex;
			if(direction < 0) {
				int previousStart = logicalIndex - 1;
				while(
					previousStart >= 0
					&& Targeting::Targets[mapIndices[previousStart]].type
						!= Targeting::TargetType::Position
				)
					previousStart--;
				if(previousStart < 0)
					return index;
				rotateFirst = previousStart;
				rotateMiddle = logicalIndex;
				rotateLast = currentEnd;
				newLogicalIndex = previousStart;
			} else {
				if(currentEnd >= static_cast<int>(mapIndices.size()))
					return index;
				int nextEnd = currentEnd + 1;
				while(
					nextEnd < static_cast<int>(mapIndices.size())
					&& Targeting::Targets[mapIndices[nextEnd]].type
						!= Targeting::TargetType::Position
				)
					nextEnd++;
				rotateFirst = logicalIndex;
				rotateMiddle = currentEnd;
				rotateLast = nextEnd;
				newLogicalIndex = logicalIndex + (nextEnd - currentEnd);
			}

			std::vector<Targeting::TargetData> mapEntries;
			mapEntries.reserve(mapIndices.size());
			for(const int mapIndex : mapIndices)
				mapEntries.push_back(std::move(Targeting::Targets[mapIndex]));
			std::rotate(
				mapEntries.begin() + rotateFirst,
				mapEntries.begin() + rotateMiddle,
				mapEntries.begin() + rotateLast
			);
			for(int logical = 0; logical < static_cast<int>(mapIndices.size()); logical++)
				Targeting::Targets[mapIndices[logical]] = std::move(mapEntries[logical]);
			NormalizeTargetNames(Targeting::Targets);
			return mapIndices[newLogicalIndex];
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
					MarkRouteMovementComplete();
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
				if(Targeting::Targets[activeTargetIndex].type == Targeting::TargetType::Action) {
					QueueAction(activeTargetIndex);
					activeTargetIndex = FindNextTargetForMap(activeTargetIndex, mapId);
					if(activeTargetIndex < 0) {
						MarkRouteMovementComplete();
						return true;
					}
					continue;
				}
				if(Targeting::Targets[activeTargetIndex].type == Targeting::TargetType::Toggle) {
					const auto& toggle = Targeting::Targets[activeTargetIndex];
					if(toggle.toggleSetting == Targeting::ToggleSetting::AutoCutsceneSkips)
						AutoCutsceneSkips::SetEnabled(toggle.toggleEnabled);
					activeTargetIndex = FindNextTargetForMap(activeTargetIndex, mapId);
					if(activeTargetIndex < 0) {
						MarkRouteMovementComplete();
						return true;
					}
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
					MarkRouteMovementComplete();
					return true;
				}
			}
			return false;
		}

	} // namespace

	void Targeting::StopRoute() {
		MenuHelper::CancelActivePlayback();
		RouteActive = false;
		activeTargetIndex = -1;
		waitingForPlayer = false;
		startDelayFrames = 0;
		activeDelayIndex = -1;
		delayFramesRemaining = 0;
		ClearActiveActions();
		ResetTargetApproach();
		targetingMenuTasPlayback = false;
		scriptTimerRunning = false;
		scriptMovementComplete = false;
		targetToTargetTimerRunning = false;
		targetToTargetOriginIndex = -1;
		InputBuffer::SetLeftStickOverride(false);
	}

	void Targeting::LoadTargetsFromFile() {
		toml::parse_result result = toml::parse_file(TargetsPath());
		if(!result) {
			routeFileError = fmt::format(
				"Reload failed: {}",
				std::move(result).error().description()
			);
			g_Logger->ToastWarning("targeting", "{}", routeFileError);
			return;
		}

		auto table = std::move(result).table();
		const auto loadedBestFrames = std::max<std::int64_t>(
			0,
			table["best_frames"].value_or<std::int64_t>(0)
		);
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
				: type == "action"
					? TargetType::Action
				: type == "toggle"
					? TargetType::Toggle
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
			} else if(target.type == TargetType::Action) {
				const auto inputType =
					(*entry)["input_type"].value_or<std::string>("buttons");
				target.actionInputType = inputType == "left_stick"
					? ActionInputType::LeftStick
					: inputType == "right_stick"
						? ActionInputType::RightStick
						: ActionInputType::Buttons;
				target.buttonMask = 0;
				if(const auto buttons = entry->get_as<toml::array>("buttons")) {
					for(const auto& button : *buttons) {
						const auto name = button.value<std::string>();
						if(!name)
							continue;
						for(const auto& choice : ButtonChoices) {
							if(*name == choice.name)
								target.buttonMask |= choice.mask;
						}
					}
				}
				if(const auto stick = entry->get_as<toml::array>("stick")) {
					if(stick->size() >= 2) {
						target.stickX = std::clamp((*stick)[0].value_or(0.f), -1.f, 1.f);
						target.stickY = std::clamp((*stick)[1].value_or(0.f), -1.f, 1.f);
					}
				}
				target.delayFrames = static_cast<int>(std::clamp<std::int64_t>(
					(*entry)["delay_frames"].value_or<std::int64_t>(0),
					0,
					std::numeric_limits<int>::max()
				));
				target.holdFrames = static_cast<int>(std::clamp<std::int64_t>(
					(*entry)["hold_frames"].value_or<std::int64_t>(1),
					1,
					std::numeric_limits<int>::max()
				));
				target.buffered = (*entry)["buffer"].value_or(false);
				if(target.actionInputType != ActionInputType::Buttons)
					target.buffered = false;
			} else if(target.type == TargetType::Toggle) {
				// Unknown future toggle names safely fall back to the only currently
				// supported setting without changing the file's enabled state.
				target.toggleSetting = ToggleSetting::AutoCutsceneSkips;
				target.toggleEnabled = (*entry)["enabled"].value_or(false);
			} else if(target.type == TargetType::Position) {
				target.name = (*entry)["name"].value_or<std::string>("Target");
				target.lastFrames = static_cast<std::uint64_t>(
					std::max<std::int64_t>(
						0,
						(*entry)["last_frames"].value_or<std::int64_t>(0)
					)
				);
				target.bestFrames = static_cast<std::uint64_t>(
					std::max<std::int64_t>(
						0,
						(*entry)["best_frames"].value_or<std::int64_t>(0)
					)
				);
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
					target.lastFrames = static_cast<std::uint64_t>(
						std::max<std::int64_t>(
							0,
							(*entry)["last_frames"].value_or<std::int64_t>(0)
						)
					);
					target.bestFrames = static_cast<std::uint64_t>(
						std::max<std::int64_t>(
							0,
							(*entry)["best_frames"].value_or<std::int64_t>(0)
						)
					);
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

		const bool namesMigrated = NormalizeTargetNames(loadedTargets);
		if(namesMigrated) {
			const std::string migrationBackup = TargetNamesMigrationBackupPath();
			if(
				!FileExists(migrationBackup)
				&& !CopyFileUnchanged(TargetsPath(), migrationBackup)
			) {
				g_Logger->LogError(
					"Couldn't create target-name migration backup {}",
					migrationBackup
				);
			}
		}

		StopRoute();
		Targets.swap(loadedTargets);
		bestScriptFrames = static_cast<std::uint64_t>(loadedBestFrames);
		scriptFrameCount = 0;
		targetToTargetFrameCount = 0;
		targetToTargetOriginIndex = -1;
		mostRecentTargetToTargetOriginIndex = -1;
		lastSavedTargets = namesMigrated ? std::string {} : SerializeTargets();
		routeFileError.clear();
		SaveRouteSettingIfChanged();
		if(namesMigrated)
			SaveTargetsIfChanged();
		g_Logger->ToastInfo("targeting", "Loaded {} target(s)", Targets.size());
	}

	void Targeting::SaveTargetsToFile() {
		NormalizeTargetNames(Targets);
		const std::string contents = SerializeTargets();
		if(WriteTargetsFile(TargetsPath(), contents))
			lastSavedTargets = contents;
	}

	int Targeting::NewTarget(int insertAfter) {
		const auto mapId = CurrentMapId();
		const auto mapName = detail::IsModuleRegistered(STRINGIFY(DebugStuff))
			? DebugStuff::GetMapName(mapId)
			: "Unknown";
		TargetData target {
			.name = "",
			.mapName = mapName,
			.mapId = mapId
		};
		const int insertionIndex = InsertionIndexAfterTargetBlock(mapId, insertAfter);
		Targets.insert(Targets.begin() + insertionIndex, std::move(target));
		SetTarget(&Targets[insertionIndex]);
		NormalizeTargetNames(Targets);
		StopRoute();
		return insertionIndex;
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

	int Targeting::NewAction(int insertAfter) {
		const auto mapId = CurrentMapId();
		const auto mapName = detail::IsModuleRegistered(STRINGIFY(DebugStuff))
			? DebugStuff::GetMapName(mapId)
			: "Unknown";
		TargetData action {
			.type = TargetType::Action,
			.mapName = mapName,
			.mapId = mapId,
			.delayFrames = 0,
			.buttonMask = PhysicalA,
			.holdFrames = 1
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
		Targets.insert(Targets.begin() + insertionIndex, std::move(action));
		StopRoute();
		return insertionIndex;
	}

	int Targeting::NewToggle(int insertAfter) {
		const auto mapId = CurrentMapId();
		const auto mapName = detail::IsModuleRegistered(STRINGIFY(DebugStuff))
			? DebugStuff::GetMapName(mapId)
			: "Unknown";
		TargetData toggle {
			.type = TargetType::Toggle,
			.mapName = mapName,
			.mapId = mapId,
			.toggleSetting = ToggleSetting::AutoCutsceneSkips,
			.toggleEnabled = false
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
		Targets.insert(Targets.begin() + insertionIndex, std::move(toggle));
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
		NormalizeTargetNames(Targets);
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

		int& selectedIndex = selectedTargetIndex;
		const float reloadWidth = ImGui::CalcTextSize("Reload").x
			+ ImGui::GetStyle().FramePadding.x * 2.f;
		const float newWidth = ImGui::CalcTextSize("+ New").x
			+ ImGui::GetStyle().FramePadding.x * 2.f;
		ImGui::SetNextItemWidth(std::max(
			100.f,
			ImGui::GetContentRegionAvail().x - reloadWidth - newWidth
				- ImGui::GetStyle().ItemSpacing.x * 2.f
		));
		std::string requestedRouteFile;
		if(ImGui::BeginCombo("##RouteFile", currentRouteFile.c_str())) {
			for(const auto& routeFile : routeFiles) {
				if(ImGui::Selectable(routeFile.c_str(), routeFile == currentRouteFile)) {
					if(routeFile != currentRouteFile)
						requestedRouteFile = routeFile;
				}
			}
			ImGui::EndCombo();
		}
		if(!requestedRouteFile.empty()) {
			if(currentRouteWasCreatedEmpty && Targets.empty()) {
				const std::string emptyRoutePath = TargetsPath();
				if(R_FAILED(nn::fs::DeleteFile(emptyRoutePath.c_str())))
					g_Logger->LogError("Couldn't delete empty target route {}", emptyRoutePath);
			} else {
				SaveTargetsIfChanged();
			}
			currentRouteWasCreatedEmpty = false;
			currentRouteFile = requestedRouteFile;
			LoadTargetsFromFile();
			RescanRouteFiles();
			selectedIndex = Targets.empty() ? -1 : 0;
		}
		ImGui::SameLine();
		if(ImGui::Button("Reload")) {
			RescanRouteFiles();
			LoadTargetsFromFile();
			selectedIndex = Targets.empty() ? -1 : 0;
		}
		ImGui::SameLine();
		if(ImGui::Button("+ New")) {
			SaveTargetsIfChanged();
			CreateNewRoute();
			selectedIndex = -1;
		}

		if(RouteActive || scriptTimerRunning) {
			if(ImGui::Button("Stop"))
				StopRoute();
		} else if(ImGui::Button("Start")) {
			StartRoute(selectedIndex);
		}
		const std::string bestFrameText = bestScriptFrames == 0
			? "--"
			: fmt::format("{}f", bestScriptFrames);
		ImGui::SameLine();
		ImGui::Text(
			"Script: %lluf  Best: %s",
			static_cast<unsigned long long>(scriptFrameCount),
			bestFrameText.c_str()
		);
		if(ImGui::Button("Reset timer"))
			scriptFrameCount = 0;
		ImGui::SameLine();
		if(ImGui::Button("Reset best")) {
			bestScriptFrames = 0;
			SaveTargetsIfChanged();
		}
		ImGui::SameLine();
		if(ImGui::Button("Reset T2T")) {
			for(auto& target : Targets)
				target.bestFrames = 0;
			SaveTargetsIfChanged();
		}
		ImGui::Checkbox("Start from selection", &StartFromSelection);
		ImGui::SameLine();
		ImGui::Checkbox("Warp/start", &WarpToStart);

		ImGui::Checkbox("Show all", &ShowAllTargets);
		ImGui::SameLine();
		ImGui::Checkbox("Show on map", &ShowTargetsOnMap);

		if(!routeFileError.empty())
			ImGui::TextColored(ImVec4(1.f, 0.35f, 0.3f, 1.f), "%s", routeFileError.c_str());

		static TargetType addType = TargetType::Action;
		static bool addIntermediate = false;
		const bool canBeIntermediate =
			addType == TargetType::MenuTas || addType == TargetType::TravelTas;
		const char* addTypeName = addType == TargetType::Position
			? "Target"
			: addType == TargetType::Delay
				? "Delay"
			: addType == TargetType::Action
				? "Action"
			: addType == TargetType::Toggle
				? "Toggle"
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
					TargetType::Position,
					TargetType::Delay,
					TargetType::Action,
					TargetType::Toggle,
					TargetType::ShopTas,
					TargetType::MenuTas,
					TargetType::TravelTas
				}) {
					const char* name = type == TargetType::Position
						? "Target"
						: type == TargetType::Delay
							? "Delay"
						: type == TargetType::Action
							? "Action"
						: type == TargetType::Toggle
							? "Toggle"
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
				if(addType == TargetType::Position) {
					selectedIndex = NewTarget(selectedIndex);
				} else if(addType == TargetType::Delay) {
					selectedIndex = NewDelay(selectedIndex);
				} else if(addType == TargetType::Action) {
					selectedIndex = NewAction(selectedIndex);
				} else if(addType == TargetType::Toggle) {
					selectedIndex = NewToggle(selectedIndex);
				} else {
					selectedIndex = NewSpecialStep(
						addType,
						selectedIndex,
						canBeIntermediate && addIntermediate
					);
				}
			}
			const bool validSelectedSplit = selectedIndex >= 0
				&& selectedIndex < static_cast<int>(Targets.size())
				&& IsMovementTarget(Targets[selectedIndex]);
			const bool validRecentSplit =
				mostRecentTargetToTargetOriginIndex >= 0
				&& mostRecentTargetToTargetOriginIndex
					< static_cast<int>(Targets.size());
			const int splitTargetIndex = targetToTargetTimerRunning
				&& targetToTargetOriginIndex >= 0
				? targetToTargetOriginIndex
				: !scriptTimerRunning && inspectSelectedSplit && validSelectedSplit
					? selectedIndex
					: !scriptTimerRunning && validRecentSplit
						? mostRecentTargetToTargetOriginIndex
						: !scriptTimerRunning && validSelectedSplit
							? selectedIndex
							: -1;
			const std::string splitTimeText = targetToTargetTimerRunning
				? fmt::format("{}f", targetToTargetFrameCount)
				: splitTargetIndex >= 0
					&& Targets[splitTargetIndex].lastFrames > 0
					? fmt::format("{}f", Targets[splitTargetIndex].lastFrames)
					: "--";
			const std::string splitBestText = splitTargetIndex >= 0
				&& Targets[splitTargetIndex].bestFrames > 0
				? fmt::format("{}f", Targets[splitTargetIndex].bestFrames)
				: "--";
			ImGui::SameLine(0.f, 12.f);
			ImGui::Text(
				"T2T: %s  Best: %s",
				splitTimeText.c_str(),
				splitBestText.c_str()
			);
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
			else if(!activeActions.empty()) {
				const auto& runtime = activeActions.front();
				if(runtime.index >= 0 && runtime.index < static_cast<int>(Targets.size())) {
					if(runtime.started)
						ImGui::Text(
							"Action: %s%s",
							ActionName(Targets[runtime.index]).c_str(),
							activeActions.size() > 1 ? " + others" : ""
						);
					else
						ImGui::Text(
							"Action delay: %df%s",
							runtime.delayFramesRemaining,
							activeActions.size() > 1 ? " + others" : ""
						);
				}
			}
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
		const bool selectedAction = hasSelection
			&& Targets[selectedIndex].type == TargetType::Action;
		const bool selectedToggle = hasSelection
			&& Targets[selectedIndex].type == TargetType::Toggle;
		const float editorReserve = hasSelection
			? ImGui::GetTextLineHeightWithSpacing()
				* (selectedMenuTas ? 5.5f : selectedAction ? 5.5f
					: selectedToggle ? 3.5f : 4.5f)
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
						"    Delay: {}f{}",
						std::max(0, Targets[index].delayFrames),
						index == activeTargetIndex ? "  [ACTIVE]" : ""
					);
				} else if(Targets[index].type == TargetType::Action) {
					label = fmt::format(
						"    Action: {}{}{}",
						ActionName(Targets[index]),
						Targets[index].buffered ? "  [BUFFERED]" : "",
						index == activeTargetIndex ? "  [ACTIVE]" : ""
					);
				} else if(Targets[index].type == TargetType::Toggle) {
					label = fmt::format(
						"    Toggle: Auto Cutscene Skips -> {}{}",
						Targets[index].toggleEnabled ? "Enable" : "Disable",
						index == activeTargetIndex ? "  [ACTIVE]" : ""
					);
				} else if(Targets[index].type == TargetType::ShopTas) {
					label = fmt::format(
						"    ShopTAS: {}{}",
						Targets[index].name.empty() ? "<select recording>" : Targets[index].name,
						index == activeTargetIndex ? "  [ACTIVE]" : ""
					);
				} else if(Targets[index].type == TargetType::MenuTas) {
					label = fmt::format(
						"    MenuTAS: {}{}{}",
						Targets[index].name.empty() ? "<select recording>" : Targets[index].name,
						Targets[index].intermediate ? "  [INTERMEDIATE]" : "",
						index == activeTargetIndex ? "  [ACTIVE]" : ""
					);
				} else if(Targets[index].type == TargetType::TravelTas) {
					label = fmt::format(
						"    TravelTAS: {}{}{}",
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
				if(ImGui::Selectable(label.c_str(), selectedIndex == index)) {
					selectedIndex = index;
					inspectSelectedSplit = true;
				}
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
			} else if(target.type == TargetType::Action) {
				const char* inputTypeName = target.actionInputType == ActionInputType::Buttons
					? "Buttons"
					: target.actionInputType == ActionInputType::LeftStick
						? "Left Stick"
						: "Right Stick";
				ImGui::TextUnformatted("Type:");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1.f);
				if(ImGui::BeginCombo("##ActionInputType", inputTypeName)) {
					for(const auto type : {
						ActionInputType::Buttons,
						ActionInputType::LeftStick,
						ActionInputType::RightStick
					}) {
						const char* name = type == ActionInputType::Buttons
							? "Buttons"
							: type == ActionInputType::LeftStick
								? "Left Stick"
								: "Right Stick";
						if(ImGui::Selectable(name, target.actionInputType == type)) {
							target.actionInputType = type;
							if(type != ActionInputType::Buttons)
								target.buffered = false;
						}
					}
					ImGui::EndCombo();
				}

				if(target.actionInputType == ActionInputType::Buttons) {
				ImGui::TextUnformatted("Input:");
				ImGui::SameLine();
				const std::string preview = ActionName(target.buttonMask);
				ImGui::SetNextItemWidth(-1.f);
				if(ImGui::BeginCombo("##ActionButtons", preview.c_str())) {
					for(const auto& choice : ButtonChoices) {
						const bool selected = (target.buttonMask & choice.mask) != 0;
						if(ImGui::Selectable(
							choice.name,
							selected,
							ImGuiSelectableFlags_DontClosePopups
						)) {
							if(selected)
								target.buttonMask &= ~choice.mask;
							else
								target.buttonMask |= choice.mask;
						}
					}
					ImGui::EndCombo();
				}
				} else {
					const float axisWidth = 92.f;
					ImGui::TextUnformatted("X:");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(axisWidth);
					if(ImGui::DragFloat("##ActionStickX", &target.stickX, 0.01f, -1.f, 1.f, "%.2f"))
						target.stickX = std::clamp(target.stickX, -1.f, 1.f);
					ImGui::SameLine();
					ImGui::TextUnformatted("Y:");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(axisWidth);
					if(ImGui::DragFloat("##ActionStickY", &target.stickY, 0.01f, -1.f, 1.f, "%.2f"))
						target.stickY = std::clamp(target.stickY, -1.f, 1.f);
				}
				const float valueWidth = 72.f;
				ImGui::TextUnformatted("Delay:");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(valueWidth);
				if(ImGui::DragInt(
					"##ActionDelay",
					&target.delayFrames,
					1.f,
					0,
					std::numeric_limits<int>::max(),
					"%df"
				))
					target.delayFrames = std::max(0, target.delayFrames);
				ImGui::SameLine();
				ImGui::TextUnformatted("Hold for:");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(valueWidth);
				if(ImGui::DragInt(
					"##ActionHold",
					&target.holdFrames,
					1.f,
					1,
					std::numeric_limits<int>::max(),
					"%df"
				))
					target.holdFrames = std::max(1, target.holdFrames);
				if(target.actionInputType == ActionInputType::Buttons)
					ImGui::Checkbox("Buffer", &target.buffered);
			} else if(target.type == TargetType::Toggle) {
				ImGui::TextUnformatted("Toggle:");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(180.f);
				if(ImGui::BeginCombo("##ToggleSetting", "Auto Cutscene Skips")) {
					if(ImGui::Selectable(
						"Auto Cutscene Skips",
						target.toggleSetting == ToggleSetting::AutoCutsceneSkips
					))
						target.toggleSetting = ToggleSetting::AutoCutsceneSkips;
					ImGui::EndCombo();
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(90.f);
				if(ImGui::BeginCombo(
					"##ToggleState",
					target.toggleEnabled ? "Enable" : "Disable"
				)) {
					if(ImGui::Selectable("Enable", target.toggleEnabled))
						target.toggleEnabled = true;
					if(ImGui::Selectable("Disable", !target.toggleEnabled))
						target.toggleEnabled = false;
					ImGui::EndCombo();
				}
			} else if(target.type == TargetType::Position) {
				ImGui::Text("Name: %s", target.name.c_str());
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
				NormalizeTargetNames(Targets);
				selectedIndex = Targets.empty()
					? -1
					: std::min(selectedIndex, static_cast<int>(Targets.size()) - 1);
			}
		}

		SaveTargetsIfChanged();

		ImGui::End();
	}

	void Targeting::Initialize() {
		UpdatableModule::Initialize();
		InitializeRouteFiles();
		LoadTargetsFromFile();
		// A missing or malformed file must not be replaced until the user actually
		// changes the in-memory route.
		lastSavedTargets = SerializeTargets();
		lastBackupTick = nn::os::GetSystemTick();
		g_Menu->RegisterRenderCallback(&MenuWindow, true);
	}

	void Targeting::Update(fw::UpdateInfo*) {
		if(scriptTimerRunning)
			scriptFrameCount++;
		if(targetToTargetTimerRunning)
			targetToTargetFrameCount++;
		SaveTargetsIfChanged();
		UpdateTargetsBackup();
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
				const bool highlighted = index == activeTargetIndex
					|| (
						!RouteActive
						&& !scriptTimerRunning
						&& index == selectedTargetIndex
					);
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
						highlighted ? mm::Col4::yellow : mm::Col4::white,
						target.type == TargetType::TravelTas
							? "TravelTAS: {}"
							: "MenuTAS: {}",
						target.name
					);
				} else {
					debug::drawFontFmtShadow3D(
						target.position,
						highlighted ? mm::Col4::yellow : mm::Col4::white,
						"{}: {}",
						RouteNumber(index),
						target.name
					);
				}
			}
		}
		const bool nativeControlFree = gf::GfGameManager::isControlFree();
		const bool tutorialMovementAllowed =
			detail::IsModuleRegistered(STRINGIFY(DebugStuff))
			&& DebugStuff::ShouldBypassControlLockForTutorial(nativeControlFree);
		const bool actionControlFree = PlayerMovement::GetPartyPosition() != nullptr
			&& CameraTools::HasCameraState
			&& (nativeControlFree || tutorialMovementAllowed);
		UpdateActiveAction(actionControlFree);
		if(
			targetingMenuTasPlayback
			&& MenuHelper::IsMenuPlaybackPendingOrActive()
		) {
			InputBuffer::SetLeftStickOverride(false);
			ResetTargetApproach();
			return;
		}
		targetingMenuTasPlayback = false;
		TryCompleteScriptTimer();

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
		if(!nativeControlFree && !tutorialMovementAllowed) {
			waitingForPlayer = true;
			ResetTargetApproach();
			InputBuffer::SetLeftStickOverride(false);
			return;
		}
		waitingForPlayer = false;
		// Route delays obey the same control lock as movement and Actions. The
		// sole exception is the confirmed tutorial lock above; collection-point
		// animations and every other loss of field control pause the countdown.
		if(Targets[activeTargetIndex].type == TargetType::Delay) {
			if(ConsumeActiveDelaySteps(mapId))
				return;
		}
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
		constexpr float completionDistance = 0.005f;
		bool crossedExactTarget =
			hasPreviousTargetDelta
			&& trackedTargetIndex == movementTargetIndex
			&& glm::dot(previousTargetDelta, delta) <= 0.f;
		while(distance <= completionDistance || crossedExactTarget) {
			const auto& reachedTarget = Targets[movementTargetIndex];
			CompleteTargetSplit(movementTargetIndex, mapId);
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
					MarkRouteMovementComplete();
				}
				return;
			}
			activeTargetIndex = FindNextTargetForMap(activeTargetIndex, mapId);
			ResetTargetApproach();
			if(activeTargetIndex < 0) {
				MarkRouteMovementComplete();
				TryCompleteScriptTimer();
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
		if(scriptTimerRunning) {
			scriptFrameCount = 0;
			targetToTargetFrameCount = 0;
			targetToTargetOriginIndex = -1;
		}
		if(
			RouteActive
			&& (
				activeTargetIndex < 0
				|| activeTargetIndex >= static_cast<int>(Targets.size())
				|| Targets[activeTargetIndex].mapId != mapId
			)
		)
			activeTargetIndex = FindFirstTargetForMap(mapId);
		if(scriptTimerRunning && !scriptMovementComplete)
			targetToTargetTimerRunning = false;
	}

#if XENOMODS_CODENAME(bf2)
	XENOMODS_REGISTER_MODULE(Targeting);
#endif

} // namespace xenomods
