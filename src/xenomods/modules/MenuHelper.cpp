#include "MenuHelper.hpp"
#include "SkipTravelProfiler.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <skylaunch/hookng/Hooks.hpp>
#include <toml++/toml.hpp>

#include "xenomods/HidInput.hpp"
#include "xenomods/engine/gf/Manager.hpp"
#include "xenomods/engine/gf/MenuObject.hpp"
#include "xenomods/engine/gf/PlayFactory.hpp"
#include "xenomods/engine/gf/SaveGame.hpp"
#include "xenomods/Logger.hpp"
#include "xenomods/NnFile.hpp"
#include "xenomods/State.hpp"
#include "xenomods/menu/Menu.hpp"

namespace {
	bool tutorialBladeBondPending = false;
	bool tutorialBladeBondOpen = false;
	bool auxCoreRecipeOpen = false;
	bool auxCoreShopWaitingForInput = false;
	std::uint32_t auxCoreListInputHandle = 0;
	std::uint32_t auxCoreRecipeInputHandle = 0;
	bool acceptingAuxCoreAmountPlayback = false;

	void OnInputLayerUpdating(
		std::uint32_t object,
		const void* inputLayer,
		std::uint32_t layerHandle,
		bool afterUpdate,
		bool accepted,
		std::uint16_t emittedEvent,
		std::uint32_t heldButtons,
		std::uint32_t downButtons
	) {
		xenomods::SkipTravelProfiler::OnAuxInputLayerUpdate(
			object,
			inputLayer,
			layerHandle,
			afterUpdate,
			accepted,
			emittedEvent,
			heldButtons,
			downButtons
		);
		if(
			!afterUpdate
			&&
			auxCoreShopWaitingForInput
			&& auxCoreListInputHandle != 0
			&& layerHandle == auxCoreListInputHandle
		)
			xenomods::MenuHelper::OnAuxCoreShopInputReady();
	}

	struct AuxCoreShopOpenHandlerHook
		: skylaunch::hook::Trampoline<AuxCoreShopOpenHandlerHook> {
		static void Hook(void* listener, const void* eventData, unsigned int object) {
			const std::uint16_t event = eventData == nullptr
				? 0
				: *reinterpret_cast<const std::uint16_t*>(eventData);
			const std::uint32_t requestedShop = event == 0x21
				? *reinterpret_cast<const std::uint32_t*>(
					reinterpret_cast<const std::uint8_t*>(eventData) + 8
				)
				: 0;
			if(event == 0x21 && requestedShop == 212)
				xenomods::MenuHelper::OnAuxCoreShopOpening();
			xenomods::SkipTravelProfiler::OnAuxShopRequest(
				event, requestedShop, object, eventData
			);
			Orig(listener, eventData, object);
		}
	};

	struct ShopInitializeHook : skylaunch::hook::Trampoline<ShopInitializeHook> {
		static void Hook(void* shop) {
			auxCoreRecipeOpen = false;
			xenomods::InputBuffer::ResetAuxCoreAmountInput();
			xenomods::MenuHelper::OnSandboxMenuOpened();
			Orig(shop);
			xenomods::MenuHelper::OnShopOpened();
		}
	};

	struct AuxCoreRecipeListenerHook
		: skylaunch::hook::Trampoline<AuxCoreRecipeListenerHook> {
		static void Hook(void* listener, const void* eventData, unsigned int object) {
			const std::uint16_t event = eventData == nullptr
				? 0
				: *reinterpret_cast<const std::uint16_t*>(eventData);
			const std::uint32_t action = eventData == nullptr
				? 0
				: *reinterpret_cast<const std::uint32_t*>(
					reinterpret_cast<const std::uint8_t*>(eventData) + 0xc
				);
			xenomods::SkipTravelProfiler::OnAuxRecipeEvent(
				false, event, action, object, listener, eventData
			);
			if(event != 1 && object != 0) {
				auxCoreRecipeInputHandle = object;
				xenomods::InputBuffer::SetAuxCoreAmountMenu(true, object);
			}
			if(event == 0x40 || event == 0x42) {
				xenomods::InputBuffer::ResetAuxCoreAmountInput();
			} else if(event == 1) {
				auxCoreRecipeOpen = false;
				xenomods::InputBuffer::SetAuxCoreAmountMenu(false);
				auxCoreRecipeInputHandle = 0;
				xenomods::InputBuffer::ResetAuxCoreAmountInput();
			}
			if(event == 8 && eventData != nullptr) {
				if(action == 9 || action == 10)
					auxCoreRecipeOpen = true;
				if(
					(action == 9 || action == 10)
					&& !xenomods::InputBuffer::AcceptAuxCoreAmountInput()
				) {
					xenomods::SkipTravelProfiler::OnAuxRecipeEvent(
						true, event, action, object, listener, eventData, false
					);
					return;
				}
			}
			Orig(listener, eventData, object);
			if(event == 8 && (action == 9 || action == 10)) {
				acceptingAuxCoreAmountPlayback = true;
				xenomods::InputBuffer::AcceptAuxCoreAmountPlayback();
				acceptingAuxCoreAmountPlayback = false;
			}
			xenomods::SkipTravelProfiler::OnAuxRecipeEvent(
				true, event, action, object, listener, eventData
			);
		}
	};

	struct ShopSetDisplayHook : skylaunch::hook::Trampoline<ShopSetDisplayHook> {
		static void Hook(void* shopManager, bool visible) {
			if(visible)
				xenomods::MenuHelper::OnSandboxMenuOpened();
			Orig(shopManager, visible);
			if(visible)
				xenomods::MenuHelper::OnShopOpened();
		}
	};

	struct ShopOpenHook : skylaunch::hook::Trampoline<ShopOpenHook> {
		static void Hook(void* shop, void* callContext, const void* openParam) {
			xenomods::MenuHelper::OnSandboxMenuOpened();
			Orig(shop, callContext, openParam);
			xenomods::MenuHelper::OnShopOpened();
		}
	};

	struct MainMenuCreateHook : skylaunch::hook::Trampoline<MainMenuCreateHook> {
		static bool Hook(
			unsigned int type,
			gf::MAINMENU menu,
			gf::GfSequentialPlaySignal* signal
		) {
			if(menu == gf::MAINMENU::SkipTravel) {
				xenomods::SkipTravelProfiler::OnOpenCommandStarted();
				xenomods::MenuHelper::OnTravelOpening();
			} else if(menu == gf::MAINMENU::Main || menu == gf::MAINMENU::System)
				xenomods::MenuHelper::OnMenuOpening();
			const bool created = Orig(type, menu, signal);
			if(menu == gf::MAINMENU::SkipTravel)
				xenomods::SkipTravelProfiler::OnOpenCommandResult(created);
			if(
				created
				&& (menu == gf::MAINMENU::Main || menu == gf::MAINMENU::System)
			) {
				xenomods::MenuHelper::OnSandboxMenuOpened();
				xenomods::MenuHelper::OnMenuOpened();
			} else if(created && menu == gf::MAINMENU::SkipTravel) {
				xenomods::MenuHelper::OnSandboxMenuOpened();
				xenomods::MenuHelper::OnTravelOpened();
			} else if(!created && menu == gf::MAINMENU::SkipTravel) {
				xenomods::MenuHelper::OnTravelClosed();
			}
			return created;
		}
	};

	struct MenuInputEnableHook
		: skylaunch::hook::Trampoline<MenuInputEnableHook> {
		static void Hook(unsigned int input) {
			xenomods::SkipTravelProfiler::OnAuxEnableInput(false, input);
			Orig(input);
			xenomods::SkipTravelProfiler::OnAuxEnableInput(true, input);
			xenomods::MenuHelper::OnMenuInputEnabled();
		}
	};

	struct TutorialBladeBondRequestHook
		: skylaunch::hook::Trampoline<TutorialBladeBondRequestHook> {
		static int Hook(unsigned int driver) {
			xenomods::MenuHelper::OnTutorialBladeBondRequested();
			return Orig(driver);
		}
	};

	struct TutorialBladeBondInitializeHook
		: skylaunch::hook::Trampoline<TutorialBladeBondInitializeHook> {
		static void Hook(void* menu) {
			Orig(menu);
			const bool openedFromEvent =
				reinterpret_cast<const std::uint8_t*>(menu)[0x11b4] != 0;
			if(!openedFromEvent)
				return;
			if(!tutorialBladeBondPending)
				xenomods::MenuHelper::OnTutorialBladeBondRequested();
			tutorialBladeBondOpen = true;
			tutorialBladeBondPending = false;
			xenomods::MenuHelper::OnMenuOpened();
		}
	};

	struct TutorialBladeBondInputReadyHook
		: skylaunch::hook::Trampoline<TutorialBladeBondInputReadyHook> {
		static bool Hook(void* menu) {
			const bool ready = Orig(menu);
			if(ready && tutorialBladeBondOpen)
				xenomods::MenuHelper::OnMenuInputEnabled();
			return ready;
		}
	};

	struct TutorialBladeBondFinalizeHook
		: skylaunch::hook::Trampoline<TutorialBladeBondFinalizeHook> {
		static void Hook(void* menu) {
			if(tutorialBladeBondOpen) {
				xenomods::MenuHelper::OnMenuClosed();
				tutorialBladeBondOpen = false;
			}
			Orig(menu);
		}
	};

	struct MainMenuSequenceEndHook
		: skylaunch::hook::Trampoline<MainMenuSequenceEndHook> {
		static bool Hook(void* mainMenu) {
			const bool finished = Orig(mainMenu);
			if(finished) {
				xenomods::MenuHelper::OnMenuClosed();
				xenomods::MenuHelper::OnTravelClosed();
				xenomods::MenuHelper::OnSandboxMenuClosed();
			}
			return finished;
		}
	};

	struct TravelButtonOpenHook
		: skylaunch::hook::Trampoline<TravelButtonOpenHook> {
		static void Hook() {
			xenomods::SkipTravelProfiler::OnOpenButtonPressed();
			xenomods::MenuHelper::OnTravelButtonPressed();
			Orig();
			xenomods::MenuHelper::OnTravelButtonFinished();
			xenomods::SkipTravelProfiler::OnOpenButtonFinished();
		}
	};

	struct PlayerControlEventHook
		: skylaunch::hook::Trampoline<PlayerControlEventHook> {
		static void* Hook(void* menuManager, unsigned int event) {
			if(event == 0x0e)
				xenomods::MenuHelper::OnShopClosed();
			return Orig(menuManager, event);
		}
	};

	struct SandboxDataStoreBuildHook
		: skylaunch::hook::Trampoline<SandboxDataStoreBuildHook> {
		static void Hook(const void* setupParam) {
			xenomods::MenuHelper::OnSandboxMenuClosed();
			Orig(setupParam);
		}
	};

	struct SandboxDataStoreDestroyHook
		: skylaunch::hook::Trampoline<SandboxDataStoreDestroyHook> {
		static void Hook() {
			xenomods::MenuHelper::OnSandboxMenuClosed();
			Orig();
		}
	};

	struct SandboxSaveOptionHook
		: skylaunch::hook::Trampoline<SandboxSaveOptionHook> {
		static std::uint32_t Hook() {
			return xenomods::MenuHelper::IsSandboxDataActive()
				? std::numeric_limits<std::uint32_t>::max()
				: Orig();
		}
	};

	struct SandboxSaveGameHook : skylaunch::hook::Trampoline<SandboxSaveGameHook> {
		static std::uint32_t Hook(unsigned int slot) {
			return xenomods::MenuHelper::IsSandboxDataActive()
				? std::numeric_limits<std::uint32_t>::max()
				: Orig(slot);
		}
	};

	struct SandboxAutoSaveHook : skylaunch::hook::Trampoline<SandboxAutoSaveHook> {
		static std::uint32_t Hook() {
			return xenomods::MenuHelper::IsSandboxDataActive()
				? std::numeric_limits<std::uint32_t>::max()
				: Orig();
		}
	};

} // namespace

namespace gf::GfEvent {
	void callOpenMeinMenu();
}

namespace xenomods {

	bool MenuHelper::ShowWindow = false;
	bool MenuHelper::SandboxMode = false;

	namespace {
		enum class RecordingKind {
			Shop,
			Menu,
			Travel
		};

		enum class HelperState {
			Idle,
			ArmedRecord,
			Recording,
			DraftReady,
			ArmedPlayback,
			Playing,
			WaitingForShopClose,
			Complete,
			Cancelled
		};

		struct Recording {
			std::string name;
			std::vector<InputBuffer::AcceptedAction> actions;
			std::uint64_t lastPlaybackFrames = 0;
			RecordingKind kind = RecordingKind::Shop;
		};

		constexpr std::size_t MaxRecordings = 64;
		constexpr std::size_t MaxActions = 65536;
		constexpr std::uint64_t AbortMask =
			(1ull << static_cast<unsigned>(nn::hid::NpadButton::L))
			| (1ull << static_cast<unsigned>(nn::hid::NpadButton::R))
			| (1ull << static_cast<unsigned>(nn::hid::NpadButton::Plus));

		std::vector<Recording> recordings;
		std::vector<InputBuffer::AcceptedAction> draftActions;
		std::vector<InputBuffer::AcceptedAction> playbackActions;
		std::string draftName = "Shop route";
		RecordingKind draftKind = RecordingKind::Shop;
		RecordingKind activeKind = RecordingKind::Shop;
		HelperState state = HelperState::Idle;
		int selectedRecording = -1;
		int playbackRecording = -1;
		std::size_t playbackIndex = 0;
		std::uint64_t updateFrame = 0;
		std::uint64_t playbackFrames = 0;
		std::uint64_t lastRecordedFrame = std::numeric_limits<std::uint64_t>::max();
		InputBuffer::AcceptedAction lastRecordedAction {};
		std::uint32_t playbackNeutralFrames = 0;
		bool shopOpen = false;
		bool shopSessionDeferred = false;
		std::uint64_t shopSessionDeferredFrame = 0;
		bool mainMenuOpen = false;
		bool mainMenuOpening = false;
		bool mainMenuInputReady = false;
		bool travelMenuOpen = false;
		bool travelMenuOpening = false;
		constexpr std::size_t DataStoreSize = 0x137bb0;
		constexpr const char* DataStoreSingletonSymbol =
			"_ZZN2mm3mtl12PtrSingletonIN2gf11GfDataStoreEE3sysEvE10s_instance";
		std::vector<std::uint64_t> sandboxDataStore;
		void** dataStoreSingleton = nullptr;
		void* realDataStore = nullptr;
		bool sandboxDataActive = false;

		bool BeginSandboxData() {
			if(sandboxDataActive)
				return true;
			if(dataStoreSingleton == nullptr || *dataStoreSingleton == nullptr)
				return false;

			realDataStore = *dataStoreSingleton;
			sandboxDataStore.resize(DataStoreSize / sizeof(std::uint64_t));
			std::memcpy(sandboxDataStore.data(), realDataStore, DataStoreSize);
			*dataStoreSingleton = sandboxDataStore.data();
			sandboxDataActive = true;
			return true;
		}

		void DiscardSandboxData() {
			if(!sandboxDataActive)
				return;
			if(
				dataStoreSingleton != nullptr
				&& *dataStoreSingleton == sandboxDataStore.data()
			)
				*dataStoreSingleton = realDataStore;
			sandboxDataActive = false;
			realDataStore = nullptr;
			sandboxDataStore.clear();
		}

		void FreezeSandboxData() {
			if(
				!sandboxDataActive
				|| dataStoreSingleton == nullptr
				|| realDataStore == nullptr
				|| *dataStoreSingleton != sandboxDataStore.data()
			)
				return;
			std::memcpy(sandboxDataStore.data(), realDataStore, DataStoreSize);
		}

		std::string RecordingsPath() {
			return fmt::format(
				XENOMODS_CONFIG_PATH "/{}/menuHelper.toml",
				XENOMODS_CODENAME_STR
			);
		}

		std::string EscapeBasicTomlString(std::string_view value) {
			std::string escaped;
			escaped.reserve(value.size());
			for(const char character : value) {
				switch(character) {
					case '\\': escaped += "\\\\"; break;
					case '"': escaped += "\\\""; break;
					case '\b': escaped += "\\b"; break;
					case '\t': escaped += "\\t"; break;
					case '\n': escaped += "\\n"; break;
					case '\f': escaped += "\\f"; break;
					case '\r': escaped += "\\r"; break;
					default: escaped += character; break;
				}
			}
			return escaped;
		}

		// Older or manually edited recording files may contain names such as
		// name = 'Tora's House'. A TOML literal string cannot contain an apostrophe,
		// and one such name otherwise prevents every recording from loading. Convert
		// only name fields to escaped basic strings before retrying the parse.
		std::string RepairRecordingNameQuotes(std::string_view contents) {
			std::string repaired;
			repaired.reserve(contents.size());
			std::size_t lineStart = 0;
			while(lineStart < contents.size()) {
				const auto lineEnd = contents.find('\n', lineStart);
				const auto length = lineEnd == std::string_view::npos
					? contents.size() - lineStart
					: lineEnd - lineStart;
				const auto line = contents.substr(lineStart, length);
				const auto nameKey = line.find("name = '");
				if(nameKey != std::string_view::npos && line.back() == '\'') {
					const auto valueStart = nameKey + 8;
					repaired.append(line.substr(0, valueStart));
					repaired.back() = '"';
					repaired += EscapeBasicTomlString(
						line.substr(valueStart, line.size() - valueStart - 1)
					);
					repaired += '"';
				} else {
					repaired.append(line);
				}
				if(lineEnd == std::string_view::npos)
					break;
				repaired += '\n';
				lineStart = lineEnd + 1;
			}
			return repaired;
		}

		const char* StateName() {
			switch(state) {
				case HelperState::Idle: return "Idle";
				case HelperState::ArmedRecord:
					return activeKind == RecordingKind::Shop
						? "Armed: enter a shop"
						: activeKind == RecordingKind::Travel
							? "Armed: open skip travel"
							: "Armed: open the menu";
				case HelperState::Recording:
					return activeKind == RecordingKind::Shop
						? "Recording shop inputs"
						: activeKind == RecordingKind::Travel
							? "Recording skip-travel inputs"
							: "Recording menu inputs";
				case HelperState::DraftReady: return "Recording ready to save";
				case HelperState::ArmedPlayback:
					return activeKind == RecordingKind::Shop
						? "Armed: enter a shop"
						: activeKind == RecordingKind::Travel
							? "Armed: open skip travel"
							: "Armed: open the menu";
				case HelperState::Playing:
					return activeKind == RecordingKind::Travel && playbackIndex == 0
						? "Waiting for skip-travel controls"
						: "Playing";
				case HelperState::WaitingForShopClose: return "Inputs complete; waiting for shop to close";
				case HelperState::Complete: return "Playback complete";
				case HelperState::Cancelled: return "Playback cancelled";
			}
			return "Unknown";
		}

		const char* ButtonName(InputBuffer::AcceptedAction action) {
			const std::uint32_t button = action.input & 0x3fffffffu;
			if((action.input & 0xc0000000u) == 0x40000000u) {
				switch(button) {
					case 1u << 4: return "L";
					case 1u << 5: return "R";
					case 1u << 6: return "ZL";
					case 1u << 7: return "ZR";
					default: break;
				}
			}
			switch(button) {
				case 1u << 0: return "A";
				case 1u << 1: return "B";
				case 1u << 2: return "X";
				case 1u << 3: return "Y";
				case 1u << 4: return "Stick L";
				case 1u << 5: return "Stick R";
				case 1u << 6: return "L";
				case 1u << 7: return "R";
				case 1u << 8: return "ZL";
				case 1u << 9: return "ZR";
				case 1u << 10: return "Plus";
				case 1u << 11: return "Minus";
				case 1u << 12: return "Left";
				case 1u << 13: return "Up";
				case 1u << 14: return "Right";
				case 1u << 15: return "Down";
				default: return "Stick/navigation input";
			}
		}

		bool IsPlaybackActive() {
			return state == HelperState::Playing
				|| state == HelperState::WaitingForShopClose;
		}

		void StopPlayback(HelperState nextState) {
			InputBuffer::SetAcceptedActionCapture(false);
			InputBuffer::SetPlaybackOverride(false);
			playbackActions.clear();
			playbackIndex = 0;
			playbackNeutralFrames = 0;
			playbackRecording = -1;
			state = nextState;
		}

		void QueueNextPlaybackAction() {
			if(
				state != HelperState::Playing
				|| playbackNeutralFrames != 0
				|| InputBuffer::PlaybackActionPending()
			)
				return;
			if(playbackIndex >= playbackActions.size()) {
				state = HelperState::WaitingForShopClose;
				return;
			}
			InputBuffer::SetPlaybackAction(playbackActions[playbackIndex]);
		}

		void LoadRecordings() {
			recordings.clear();
			const std::string path = RecordingsPath();
			toml::parse_result result = toml::parse_file(path);
			if(!result) {
				NnFile file(path, nn::fs::OpenMode_Read);
				if(file.Ok() && file.Size() > 0) {
					std::string contents(static_cast<std::size_t>(file.Size()), '\0');
					if(file.Read(contents.data(), file.Size()))
						result = toml::parse(RepairRecordingNameQuotes(contents), path);
				}
			}
			if(!result)
				return;
			const int version = result.table()["version"].value_or(0);
			if(version != 11)
				return;

			const auto entries = result.table().get_as<toml::array>("recordings");
			if(entries == nullptr)
				return;
			for(const auto& element : *entries) {
				if(recordings.size() >= MaxRecordings)
					break;
				const auto entry = element.as_table();
				if(entry == nullptr)
					continue;
				Recording recording;
				recording.name = (*entry)["name"].value_or<std::string>("");
				const auto kind = (*entry)["kind"].value_or<std::string>("shop");
				recording.kind = kind == "menu"
					? RecordingKind::Menu
					: kind == "travel"
						? RecordingKind::Travel
						: RecordingKind::Shop;
				const auto lastPlaybackFrames =
					(*entry)["last_elapsed_frames"].value<std::int64_t>();
				if(lastPlaybackFrames && *lastPlaybackFrames > 0)
					recording.lastPlaybackFrames = static_cast<std::uint64_t>(
						*lastPlaybackFrames
					);
				const auto actions = entry->get_as<toml::array>("actions");
				if(recording.name.empty() || actions == nullptr)
					continue;
				for(const auto& actionNode : *actions) {
					if(recording.actions.size() >= MaxActions)
						break;
					const auto action = actionNode.as_table();
					if(action == nullptr)
						continue;
					const auto input = (*action)["input"].value<std::int64_t>();
					const auto layer = (*action)["layer"].value<std::int64_t>();
					const auto display = (*action)["display"].value<std::int64_t>();
					if(
						input && layer && display
						&& *input > 0
						&& *input <= std::numeric_limits<std::uint32_t>::max()
						&& *layer > 0
						&& *layer <= std::numeric_limits<std::uint32_t>::max()
						&& *display >= 0
						&& *display <= std::numeric_limits<std::uint32_t>::max()
					)
						recording.actions.push_back({
							static_cast<std::uint32_t>(*input),
							static_cast<std::uint32_t>(*layer),
							static_cast<std::uint32_t>(*display)
						});
				}
				if(!recording.actions.empty())
					recordings.push_back(std::move(recording));
			}
			selectedRecording = recordings.empty() ? -1 : 0;
		}

		bool SaveRecordings() {
			toml::array entries;
			for(const auto& recording : recordings) {
				toml::array actions;
				for(const auto action : recording.actions) {
					toml::table actionEntry;
					actionEntry.emplace("input", static_cast<std::int64_t>(action.input));
					actionEntry.emplace("layer", static_cast<std::int64_t>(action.layer));
					actionEntry.emplace("display", static_cast<std::int64_t>(action.display));
					actions.emplace_back(std::move(actionEntry));
				}
				toml::table entry;
				entry.emplace("name", recording.name);
				entry.emplace(
					"kind",
					std::string(recording.kind == RecordingKind::Menu
						? "menu"
						: recording.kind == RecordingKind::Travel ? "travel" : "shop")
				);
				if(recording.lastPlaybackFrames != 0)
					entry.emplace(
						"last_elapsed_frames",
						static_cast<std::int64_t>(recording.lastPlaybackFrames)
					);
				entry.emplace("actions", std::move(actions));
				entries.emplace_back(std::move(entry));
			}
			toml::table root;
			root.emplace("version", 11);
			root.emplace("recordings", std::move(entries));
			std::stringstream stream;
			stream << root;
			const std::string contents = stream.str();
			const std::string path = RecordingsPath();
			if(!NnFile::Preallocate(path, contents.size())) {
				g_Logger->LogError("Couldn't create Menu Helper file {}", path);
				return false;
			}
			NnFile file(path, nn::fs::OpenMode_Write);
			if(!file.Ok())
				return false;
			file.Write(contents.c_str(), contents.size());
			file.Flush();
			return true;
		}

		void ArmRecording() {
			if(IsPlaybackActive())
				StopPlayback(HelperState::Idle);
			draftActions.clear();
			lastRecordedFrame = std::numeric_limits<std::uint64_t>::max();
			lastRecordedAction = {};
			activeKind = draftKind;
			state = HelperState::ArmedRecord;
			g_Logger->ToastInfo(
				"menu-helper",
				activeKind == RecordingKind::Shop
					? "Recording armed; enter a shop"
					: activeKind == RecordingKind::Travel
						? "Recording armed; open skip travel"
						: "Recording armed; open the menu"
			);
		}

		void ArmPlayback() {
			if(selectedRecording < 0 || selectedRecording >= static_cast<int>(recordings.size()))
				return;
			playbackActions = recordings[selectedRecording].actions;
			activeKind = recordings[selectedRecording].kind;
			playbackRecording = selectedRecording;
			playbackIndex = 0;
			playbackNeutralFrames = 0;
			playbackFrames = 0;
			InputBuffer::SetPlaybackOverride(false);
			state = HelperState::ArmedPlayback;
			g_Logger->ToastInfo(
				"menu-helper",
				activeKind == RecordingKind::Shop
					? "Playback armed; enter a shop"
					: activeKind == RecordingKind::Travel
						? "Playback armed; open skip travel"
						: "Playback armed; open the menu"
			);
			// Targeting can reach its ShopTAS step on the same frame that the
			// player opens the shop. In that case the open callback has already
			// run, so begin playback here instead of waiting for another open.
			if(activeKind == RecordingKind::Shop && shopOpen)
				MenuHelper::OnShopOpened();
			else if(
				activeKind == RecordingKind::Menu
				&& mainMenuOpen
				&& mainMenuInputReady
			)
				MenuHelper::OnMenuInputEnabled();
			else if(activeKind == RecordingKind::Travel && travelMenuOpen)
				MenuHelper::OnTravelOpened();
		}

		void SaveDraft() {
			if(
				state != HelperState::DraftReady
				|| draftName.empty()
				|| draftActions.empty()
			)
				return;

			auto existing = std::find_if(
				recordings.begin(),
				recordings.end(),
				[](const Recording& recording) {
					return recording.name == draftName && recording.kind == draftKind;
				}
			);
			if(existing == recordings.end()) {
				recordings.push_back({draftName, draftActions, 0, draftKind});
				selectedRecording = static_cast<int>(recordings.size()) - 1;
			} else {
				existing->actions = draftActions;
				existing->lastPlaybackFrames = 0;
				selectedRecording = static_cast<int>(existing - recordings.begin());
			}
			if(SaveRecordings()) {
				g_Logger->ToastInfo(
					"menu-helper",
					"Saved {} recording {}",
					draftKind == RecordingKind::Menu
						? "menu"
						: draftKind == RecordingKind::Travel ? "travel" : "shop",
					draftName
				);
				draftActions.clear();
				state = HelperState::Idle;
			}
		}

		void BeginActiveSession(RecordingKind kind, const char* label) {
			if(activeKind != kind)
				return;
			if(state == HelperState::ArmedRecord) {
				state = HelperState::Recording;
				InputBuffer::SetAcceptedActionCapture(true);
				g_Logger->ToastInfo("menu-helper", "{} recording started", label);
			} else if(state == HelperState::ArmedPlayback) {
				state = HelperState::Playing;
				InputBuffer::SetPlaybackOverride(
					true,
					activeKind == RecordingKind::Travel
						? InputBuffer::PlaybackMode::TravelUiBuffered
						: InputBuffer::PlaybackMode::StandardMenu
				);
				QueueNextPlaybackAction();
				g_Logger->ToastInfo("menu-helper", "{} playback started", label);
			}
		}

		void EndActiveSession(RecordingKind kind, const char* label) {
			if(activeKind != kind)
				return;
			if(state == HelperState::Recording) {
				InputBuffer::SetAcceptedActionCapture(false);
				state = draftActions.empty() ? HelperState::Idle : HelperState::DraftReady;
				g_Logger->ToastInfo(
					"menu-helper",
					"{} recording stopped with {} input(s)",
					label,
					draftActions.size()
				);
				if(state == HelperState::DraftReady)
					SaveDraft();
			} else if(IsPlaybackActive()) {
				if(
					playbackRecording >= 0
					&& playbackRecording < static_cast<int>(recordings.size())
				) {
					recordings[playbackRecording].lastPlaybackFrames = playbackFrames;
					SaveRecordings();
				}
				StopPlayback(HelperState::Complete);
				g_Logger->ToastInfo(
					"menu-helper",
					"{} playback complete in {} frame(s)",
					label,
					playbackFrames
				);
			}
		}
	} // namespace

	void MenuHelper::TopBarButton() {
		if(ImGui::MenuItem("Menu Helper", nullptr, ShowWindow))
			ShowWindow = !ShowWindow;
	}

	std::vector<std::string> MenuHelper::SavedShopRecordingNames() {
		std::vector<std::string> names;
		names.reserve(recordings.size());
		for(const auto& recording : recordings) {
			if(recording.kind == RecordingKind::Shop)
				names.push_back(recording.name);
		}
		return names;
	}

	std::vector<std::string> MenuHelper::SavedMenuRecordingNames() {
		std::vector<std::string> names;
		names.reserve(recordings.size());
		for(const auto& recording : recordings) {
			if(recording.kind == RecordingKind::Menu)
				names.push_back(recording.name);
		}
		return names;
	}

	std::vector<std::string> MenuHelper::SavedTravelRecordingNames() {
		std::vector<std::string> names;
		names.reserve(recordings.size());
		for(const auto& recording : recordings) {
			if(recording.kind == RecordingKind::Travel)
				names.push_back(recording.name);
		}
		return names;
	}

	bool MenuHelper::ArmShopPlayback(const std::string& recordingName) {
		const auto recording = std::find_if(
			recordings.begin(),
			recordings.end(),
			[&recordingName](const Recording& candidate) {
				return candidate.kind == RecordingKind::Shop
					&& candidate.name == recordingName;
			}
		);
		if(recording == recordings.end()) {
			g_Logger->ToastWarning(
				"menu-helper",
				"Shop recording '{}' was not found",
				recordingName
			);
			return false;
		}
		selectedRecording = static_cast<int>(recording - recordings.begin());
		ArmPlayback();
		return state == HelperState::ArmedPlayback;
	}

	bool MenuHelper::ArmMainMenuPlayback(const std::string& recordingName) {
		const auto recording = std::find_if(
			recordings.begin(),
			recordings.end(),
			[&recordingName](const Recording& candidate) {
				return candidate.kind == RecordingKind::Menu
					&& candidate.name == recordingName;
			}
		);
		if(recording == recordings.end()) {
			g_Logger->ToastWarning(
				"menu-helper",
				"Menu recording '{}' was not found",
				recordingName
			);
			return false;
		}
		selectedRecording = static_cast<int>(recording - recordings.begin());
		ArmPlayback();
		return state == HelperState::ArmedPlayback;
	}

	bool MenuHelper::ArmTravelMenuPlayback(const std::string& recordingName) {
		const auto recording = std::find_if(
			recordings.begin(),
			recordings.end(),
			[&recordingName](const Recording& candidate) {
				return candidate.kind == RecordingKind::Travel
					&& candidate.name == recordingName;
			}
		);
		if(recording == recordings.end()) {
			g_Logger->ToastWarning(
				"menu-helper",
				"Travel recording '{}' was not found",
				recordingName
			);
			return false;
		}
		selectedRecording = static_cast<int>(recording - recordings.begin());
		ArmPlayback();
		return state == HelperState::ArmedPlayback;
	}

	bool MenuHelper::StartMainMenuPlayback(const std::string& recordingName) {
		if(
			!gf::GfGameManager::isEnableOpenMainMenu()
			|| !gf::GfGameManager::isEnableOpenMainMenuSequence()
		) {
			gf::GfMenuObjUtil::playSE(gf::GfMenuObjUtil::error);
			g_Logger->ToastWarning("menu-helper", "Main menu cannot open here");
			return false;
		}

		const auto recording = std::find_if(
			recordings.begin(),
			recordings.end(),
			[&recordingName](const Recording& candidate) {
				return candidate.kind == RecordingKind::Menu
					&& candidate.name == recordingName;
			}
		);
		if(recording == recordings.end()) {
			g_Logger->ToastWarning(
				"menu-helper",
				"Menu recording '{}' was not found",
				recordingName
			);
			return false;
		}
		selectedRecording = static_cast<int>(recording - recordings.begin());
		ArmPlayback();
		if(state != HelperState::ArmedPlayback)
			return false;

		gf::GfEvent::callOpenMeinMenu();
		gf::GfMenuObjUtil::playSE(gf::GfMenuObjUtil::menuopen);
		if(!gf::GfPlayFactory::createOpenMenu2(1, gf::MAINMENU::Main, nullptr)) {
			StopPlayback(HelperState::Cancelled);
			g_Logger->ToastWarning("menu-helper", "Main menu creation failed");
			return false;
		}
		return true;
	}

	bool MenuHelper::StartTravelMenuPlayback(const std::string& recordingName) {
		if(
			!gf::GfGameManager::isEnableOpenMainMenu()
			|| !gf::GfGameManager::isEnableOpenMainMenuSequence()
		) {
			gf::GfMenuObjUtil::playSE(gf::GfMenuObjUtil::error);
			g_Logger->ToastWarning("menu-helper", "Skip travel cannot open here");
			return false;
		}

		const auto recording = std::find_if(
			recordings.begin(),
			recordings.end(),
			[&recordingName](const Recording& candidate) {
				return candidate.kind == RecordingKind::Travel
					&& candidate.name == recordingName;
			}
		);
		if(recording == recordings.end()) {
			g_Logger->ToastWarning(
				"menu-helper",
				"Travel recording '{}' was not found",
				recordingName
			);
			return false;
		}
		selectedRecording = static_cast<int>(recording - recordings.begin());
		ArmPlayback();
		if(state != HelperState::ArmedPlayback)
			return false;

		gf::GfEvent::callOpenMeinMenu();
		gf::GfMenuObjUtil::playSE(gf::GfMenuObjUtil::menuopen);
		if(!gf::GfPlayFactory::createOpenMenu2(1, gf::MAINMENU::SkipTravel, nullptr)) {
			StopPlayback(HelperState::Cancelled);
			g_Logger->ToastWarning("menu-helper", "Skip-travel menu creation failed");
			return false;
		}
		return true;
	}

	bool MenuHelper::IsMenuPlaybackPendingOrActive() {
		return activeKind != RecordingKind::Shop
			&& (state == HelperState::ArmedPlayback || IsPlaybackActive());
	}

	bool MenuHelper::IsPlaybackPendingOrActive() {
		return state == HelperState::ArmedPlayback || IsPlaybackActive();
	}

	void MenuHelper::MenuWindow() {
		if(!ShowWindow)
			return;

		ImGui::SetNextWindowSize(ImVec2(390.f, 330.f), ImGuiCond_Appearing);
		if(!ImGui::Begin("Menu Helper", &ShowWindow)) {
			ImGui::End();
			return;
		}

		if(state == HelperState::Recording) {
			if(ImGui::Button("Stop recording")) {
				InputBuffer::SetAcceptedActionCapture(false);
				state = draftActions.empty() ? HelperState::Idle : HelperState::DraftReady;
			}
		} else if(ImGui::Button("Record")) {
			ArmRecording();
		}
		ImGui::SameLine();
		const bool cannotSave =
			state != HelperState::DraftReady
			|| draftName.empty()
			|| draftActions.empty();
		if(cannotSave)
			ImGui::BeginDisabled();
		if(ImGui::Button("Save"))
			SaveDraft();
		if(cannotSave)
			ImGui::EndDisabled();
		ImGui::SameLine();
		if(state == HelperState::ArmedPlayback || IsPlaybackActive()) {
			if(ImGui::Button("Stop"))
				StopPlayback(HelperState::Cancelled);
		} else {
			if(selectedRecording < 0)
				ImGui::BeginDisabled();
			if(ImGui::Button("Start"))
				ArmPlayback();
			if(selectedRecording < 0)
				ImGui::EndDisabled();
		}
		const bool modeLocked = state == HelperState::ArmedRecord
			|| state == HelperState::Recording
			|| state == HelperState::ArmedPlayback
			|| IsPlaybackActive();
		if(modeLocked)
			ImGui::BeginDisabled();
		ImGui::SetNextItemWidth(110.f);
		const char* kindName = draftKind == RecordingKind::Menu
			? "Menu"
			: draftKind == RecordingKind::Travel ? "Travel" : "Shop";
		if(ImGui::BeginCombo("Type", kindName)) {
			if(ImGui::Selectable("Shop", draftKind == RecordingKind::Shop)) {
				draftKind = RecordingKind::Shop;
				if(draftName == "Menu route" || draftName == "Travel route")
					draftName = "Shop route";
			}
			if(ImGui::Selectable("Menu", draftKind == RecordingKind::Menu)) {
				draftKind = RecordingKind::Menu;
				if(draftName == "Shop route" || draftName == "Travel route")
					draftName = "Menu route";
			}
			if(ImGui::Selectable("Travel", draftKind == RecordingKind::Travel)) {
				draftKind = RecordingKind::Travel;
				if(draftName == "Shop route" || draftName == "Menu route")
					draftName = "Travel route";
			}
			ImGui::EndCombo();
		}
		if(modeLocked)
			ImGui::EndDisabled();
		ImGui::InputText("Name", &draftName);
		if(ImGui::Checkbox("Sandbox", &SandboxMode)) {
			if(!SandboxMode)
				DiscardSandboxData();
			else if(shopOpen || mainMenuOpen || travelMenuOpen)
				BeginSandboxData();
		}
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Use disposable menu data so menu changes never touch the real game state."
			);
		if(sandboxDataActive)
			ImGui::TextColored(
				ImVec4(1.f, 0.8f, 0.2f, 1.f),
				"Sandbox active"
			);

		ImGui::Text("State: %s", StateName());
		if(state == HelperState::Recording)
			ImGui::Text("Captured inputs: %zu", draftActions.size());
		if(
			state == HelperState::Recording
			&& InputBuffer::AcceptedActionCaptureWaitingForNeutral()
		)
			ImGui::TextDisabled("Waiting for controller neutral...");
		if(IsPlaybackActive()) {
			const std::size_t remaining = playbackActions.size() - playbackIndex;
			ImGui::Text("Elapsed: %llu frame(s)", playbackFrames);
			ImGui::Text("Remaining inputs: %zu", remaining);
			if(state == HelperState::Playing && playbackIndex < playbackActions.size())
				ImGui::Text("Waiting to send: %s", ButtonName(playbackActions[playbackIndex]));
			ImGui::TextDisabled("Abort: hold L + R + Plus");
		}

		if(state == HelperState::DraftReady) {
			ImGui::SeparatorText("Unsaved recording");
			ImGui::Text("Inputs: %zu", draftActions.size());
		}

		ImGui::SeparatorText("Saved recordings");
		if(ImGui::BeginChild("MenuHelperRecordings", ImVec2(0.f, 115.f), true)) {
			for(int index = 0; index < static_cast<int>(recordings.size()); index++) {
				const auto& recording = recordings[index];
				const char* kind = recording.kind == RecordingKind::Menu
					? "Menu"
					: recording.kind == RecordingKind::Travel ? "Travel" : "Shop";
				const std::string label = recording.lastPlaybackFrames == 0
					? fmt::format(
						"[{}] {} ({} inputs)",
						kind,
						recording.name,
						recording.actions.size()
					)
					: fmt::format(
						"[{}] {} ({} inputs, {}f last)",
						kind,
						recording.name,
						recording.actions.size(),
						recording.lastPlaybackFrames
					);
				if(ImGui::Selectable(label.c_str(), selectedRecording == index))
					selectedRecording = index;
			}
		}
		ImGui::EndChild();
		if(selectedRecording >= 0 && selectedRecording < static_cast<int>(recordings.size())) {
			if(ImGui::Button("Delete selected")) {
				recordings.erase(recordings.begin() + selectedRecording);
				selectedRecording = recordings.empty()
					? -1
					: std::min(selectedRecording, static_cast<int>(recordings.size()) - 1);
				SaveRecordings();
			}
		}

		ImGui::End();
	}

	void MenuHelper::StatusOverlay() {
		if(!g_Menu->IsOpen())
			return;
		const bool armed = state == HelperState::ArmedRecord
			|| state == HelperState::ArmedPlayback;
		const bool recording = state == HelperState::Recording;
		const bool playing = IsPlaybackActive();
		if(!armed && !recording && !playing)
			return;

		const ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(
			ImVec2(io.DisplaySize.x * 0.5f, 24.f),
			ImGuiCond_Always,
			ImVec2(0.5f, 0.f)
		);
		ImGui::SetNextWindowBgAlpha(0.78f);
		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoInputs
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoFocusOnAppearing;
		if(ImGui::Begin("Menu Helper Status", nullptr, flags)) {
			if(recording) {
				ImGui::TextColored(
					ImVec4(1.f, 0.25f, 0.2f, 1.f),
					"MENU HELPER - RECORDING"
				);
				ImGui::Text("Captured inputs: %zu", draftActions.size());
				if(InputBuffer::AcceptedActionCaptureWaitingForNeutral())
					ImGui::TextDisabled("Waiting for controller neutral...");
			} else if(playing) {
				ImGui::TextColored(
					ImVec4(0.3f, 1.f, 0.4f, 1.f),
					"MENU HELPER - %s",
					state == HelperState::Playing ? "PLAYING" : "FINISHING"
				);
				ImGui::Text(
					"Remaining inputs: %zu",
					playbackActions.size() - playbackIndex
				);
			} else {
				ImGui::TextColored(
					ImVec4(1.f, 0.8f, 0.2f, 1.f),
					activeKind == RecordingKind::Shop
						? "MENU HELPER - ARMED: ENTER SHOP"
						: activeKind == RecordingKind::Travel
							? "MENU HELPER - ARMED: OPEN SKIP TRAVEL"
							: "MENU HELPER - ARMED: OPEN MENU"
				);
			}
		}
		ImGui::End();
	}

	void MenuHelper::OnShopOpened() {
		if(
			shopOpen
			&& state != HelperState::ArmedRecord
			&& state != HelperState::ArmedPlayback
		)
			return;
		shopOpen = true;
		OnSandboxMenuOpened();
		if(auxCoreShopWaitingForInput)
			return;
		if(
			activeKind == RecordingKind::Shop
			&& (state == HelperState::ArmedRecord || state == HelperState::ArmedPlayback)
		)
		{
			shopSessionDeferred = true;
			shopSessionDeferredFrame = updateFrame;
		}
	}

	void MenuHelper::OnAuxCoreShopOpening() {
		auxCoreShopWaitingForInput = true;
		auxCoreListInputHandle = 0;
		auxCoreRecipeInputHandle = 0;
		shopSessionDeferred = false;
		shopSessionDeferredFrame = 0;
		if(activeKind != RecordingKind::Shop)
			return;
		if(IsPlaybackActive()) {
			InputBuffer::SetPlaybackOverride(false);
			playbackIndex = 0;
			playbackNeutralFrames = 0;
			playbackFrames = 0;
			state = HelperState::ArmedPlayback;
		} else if(state == HelperState::Recording) {
			InputBuffer::SetAcceptedActionCapture(false);
			draftActions.clear();
			lastRecordedFrame = std::numeric_limits<std::uint64_t>::max();
			lastRecordedAction = {};
			state = HelperState::ArmedRecord;
		}
	}

	void MenuHelper::OnAuxCoreListInputEnabled(std::uint32_t layerHandle) {
		if(!auxCoreShopWaitingForInput || layerHandle == 0)
			return;
		auxCoreListInputHandle = layerHandle;
	}

	void MenuHelper::OnAuxCoreShopInputReady() {
		if(!auxCoreShopWaitingForInput)
			return;
		auxCoreShopWaitingForInput = false;
		auxCoreListInputHandle = 0;
		shopSessionDeferred = false;
		shopSessionDeferredFrame = 0;
		shopOpen = true;
		BeginActiveSession(RecordingKind::Shop, "Shop");
	}

	void MenuHelper::OnShopClosed() {
		auxCoreRecipeOpen = false;
		InputBuffer::SetAuxCoreAmountMenu(false);
		auxCoreShopWaitingForInput = false;
		auxCoreListInputHandle = 0;
		auxCoreRecipeInputHandle = 0;
		shopSessionDeferred = false;
		shopSessionDeferredFrame = 0;
		InputBuffer::ResetAuxCoreAmountInput();
		if(activeKind != RecordingKind::Shop && !shopOpen)
			return;
		if(
			!shopOpen
			&& state != HelperState::Recording
			&& !IsPlaybackActive()
		)
			return;
		shopOpen = false;
		OnSandboxMenuClosed();
		EndActiveSession(RecordingKind::Shop, "Shop");
	}

	void MenuHelper::OnMenuOpening() {
		mainMenuOpening = true;
		mainMenuOpen = false;
		mainMenuInputReady = false;
	}

	void MenuHelper::OnMenuOpened() {
		if(
			mainMenuOpen
			&& state != HelperState::ArmedRecord
			&& state != HelperState::ArmedPlayback
		)
			return;
		mainMenuOpen = true;
		mainMenuOpening = false;
		OnSandboxMenuOpened();
		if(mainMenuInputReady)
			BeginActiveSession(RecordingKind::Menu, "Menu");
	}

	void MenuHelper::OnMenuInputEnabled() {
		if(!mainMenuOpen && !mainMenuOpening)
			return;
		mainMenuInputReady = true;
		if(mainMenuOpen)
			BeginActiveSession(RecordingKind::Menu, "Menu");
	}

	void MenuHelper::OnMenuClosed() {
		if(activeKind != RecordingKind::Menu && !mainMenuOpen)
			return;
		if(
			!mainMenuOpen
			&& state != HelperState::Recording
			&& !IsPlaybackActive()
		)
			return;
		mainMenuOpen = false;
		mainMenuOpening = false;
		mainMenuInputReady = false;
		OnSandboxMenuClosed();
		EndActiveSession(RecordingKind::Menu, "Menu");
	}

	void MenuHelper::OnTutorialBladeBondRequested() {
		tutorialBladeBondPending = true;
		OnMenuOpening();
	}

	void MenuHelper::OnTravelButtonPressed() {
		OnTravelOpening();
	}

	void MenuHelper::OnTravelButtonFinished() {
		// The native handler has no return value. A successful X-button open
		// synchronously passes through createOpenMenu2 and OnTravelOpened().
		if(!travelMenuOpen) {
			travelMenuOpening = false;
		}
	}

	void MenuHelper::OnTravelOpening() {
		travelMenuOpening = true;
		travelMenuOpen = false;
	}

	void MenuHelper::OnTravelOpened() {
		if(
			travelMenuOpen
			&& state != HelperState::ArmedRecord
			&& state != HelperState::ArmedPlayback
		)
			return;
		travelMenuOpen = true;
		travelMenuOpening = false;
		OnSandboxMenuOpened();
		// The action is queued now, but InputBuffer exposes it only from the exact
		// stable input-layer handle captured in the version-11 recording.
		BeginActiveSession(RecordingKind::Travel, "Travel");
	}

	void MenuHelper::OnTravelClosed() {
		if(activeKind != RecordingKind::Travel && !travelMenuOpen)
			return;
		if(
			!travelMenuOpen
			&& state != HelperState::Recording
			&& !IsPlaybackActive()
		)
			return;
		travelMenuOpen = false;
		travelMenuOpening = false;
		OnSandboxMenuClosed();
		EndActiveSession(RecordingKind::Travel, "Travel");
	}

	void MenuHelper::OnSandboxMenuOpened() {
		if(!SandboxMode || sandboxDataActive)
			return;
		if(!BeginSandboxData())
			g_Logger->ToastWarning("menu-helper", "Sandbox data unavailable");
	}

	void MenuHelper::OnSandboxMenuClosed() {
		DiscardSandboxData();
	}

	bool MenuHelper::IsSandboxDataActive() {
		return SandboxMode && sandboxDataActive;
	}

	void MenuHelper::OnAcceptedAction(
		InputBuffer::AcceptedAction action,
		InputBuffer::ActionSource source
	) {
		SkipTravelProfiler::OnAuxAcceptedAction(action, source);
		if(
			state == HelperState::Recording
			&& (
				(activeKind == RecordingKind::Shop && shopOpen)
				|| (activeKind == RecordingKind::Menu && mainMenuOpen)
				|| (activeKind == RecordingKind::Travel && travelMenuOpen)
			)
			&& source == InputBuffer::ActionSource::Physical
		) {
			if(
				lastRecordedFrame != updateFrame
					|| lastRecordedAction != action
			) {
				if(draftActions.size() < MaxActions)
					draftActions.push_back(action);
				lastRecordedFrame = updateFrame;
				lastRecordedAction = action;
			}
		} else if(
			state == HelperState::Playing
			&& source == InputBuffer::ActionSource::Playback
			&& playbackIndex < playbackActions.size()
			&& playbackActions[playbackIndex].input == action.input
		) {
			const std::uint32_t acceptedInput = action.input;
			playbackIndex++;
			const bool repeatedInput = playbackIndex < playbackActions.size()
				&& playbackActions[playbackIndex].input == acceptedInput;
			const bool auxCoreAmountInput = acceptingAuxCoreAmountPlayback
				&& activeKind == RecordingKind::Shop
				&& ((acceptedInput & 0x3fffffffu) & ((1u << 4) | (1u << 5))) != 0;
			if(
				activeKind == RecordingKind::Travel
				|| repeatedInput
			)
				playbackNeutralFrames = auxCoreAmountInput ? 2 : 1;
		}
	}

	void MenuHelper::Initialize() {
		UpdatableModule::Initialize();
#if XENOMODS_CODENAME(bf2)
		const std::uintptr_t dataStoreSingletonAddress =
			skylaunch::hook::detail::ResolveSymbolBase(DataStoreSingletonSymbol);
		if(dataStoreSingletonAddress != skylaunch::hook::INVALID_FUNCTION_PTR)
			dataStoreSingleton = reinterpret_cast<void**>(dataStoreSingletonAddress);
		else
			g_Logger->LogError("Menu Helper couldn't resolve the data-store singleton");
		LoadRecordings();
		InputBuffer::SetAcceptedActionCallback(&OnAcceptedAction);
		InputBuffer::SetInputLayerUpdateCallback(&OnInputLayerUpdating);
		ShopInitializeHook::HookAt("_ZN2gf13GfMenuObjShop10initializeEv");
		ShopSetDisplayHook::HookAt("_ZN2gf10GfMenuShop7setDispEb");
		ShopOpenHook::HookAt(
			"_ZN2gf13GfMenuObjShop4openEPNS_17GfMenuCallContextERKNS0_9OpenParamE"
		);
		AuxCoreShopOpenHandlerHook::HookAt(
			"_ZN2gf13GfMenuObjShop12MainListener11reciveEventERKN2ui9EventDataEj"
		);
		AuxCoreRecipeListenerHook::HookAt(
			"_ZN2gf13GfMenuObjShop17OrbRecipeListener11reciveEventERKN2ui9EventDataEj"
		);
		MainMenuCreateHook::HookAt(
			"_ZN2gf13GfPlayFactory15createOpenMenu2EjNS_8MAINMENUEPNS_22GfSequentialPlaySignalE"
		);
		MenuInputEnableHook::HookAt("_ZN2gf12GfMenuObject11enableInputEj");
		TutorialBladeBondRequestHook::HookAt(
			"_ZN2gf13GfMenuManager24openBladeCreateFromEventEj"
		);
		TutorialBladeBondInitializeHook::HookAt(
			"_ZN2gf20GfMenuObjBladeCreate10initializeEv"
		);
		TutorialBladeBondInputReadyHook::HookAt(
			"_ZN2gf20GfMenuObjBladeCreate6seqEndEv"
		);
		TutorialBladeBondFinalizeHook::HookAt(
			"_ZN2gf20GfMenuObjBladeCreate8finalizeEv"
		);
		MainMenuSequenceEndHook::HookAt(
			"_ZN2gf18GfMenuObjMainMenu211seqExecTermEv"
		);
		TravelButtonOpenHook::HookAt(
			"_ZN2gf9GfGamePad29eventInGameOpenSkipTravelMenuEv"
		);
		PlayerControlEventHook::HookAt(
			"_ZN2gf13GfMenuManager16gevPlayerControlENS_3GEVE"
		);
		SandboxDataStoreBuildHook::HookAt(
			"_ZN2gf11GfDataStore5buildERKNS_12GfSetupParamE"
		);
		SandboxDataStoreDestroyHook::HookAt("_ZN2gf11GfDataStore7destroyEv");
		SandboxSaveOptionHook::HookAt("_ZN2gf12GfReqCommand13reqSaveOptionEv");
		SandboxSaveGameHook::HookAt(
			"_ZN2gf12GfReqCommand7reqSaveENS_8SAVESLOTE"
		);
		SandboxAutoSaveHook::HookAt("_ZN2gf12GfReqCommand11reqAutoSaveEv");
		g_Menu->RegisterTopBarCallback(&TopBarButton);
		g_Menu->RegisterRenderCallback(&MenuWindow, true);
		g_Menu->RegisterRenderCallback(&StatusOverlay, false);
		g_Logger->LogInfo("Menu Helper shop hooks installed");
#endif
	}

	void MenuHelper::Update(fw::UpdateInfo*) {
		updateFrame++;
		InputBuffer::SetFrameIndex(updateFrame);
		FreezeSandboxData();
		if(
			shopSessionDeferred
			&& !auxCoreShopWaitingForInput
			&& updateFrame >= shopSessionDeferredFrame + 2
		) {
			shopSessionDeferred = false;
			shopSessionDeferredFrame = 0;
			BeginActiveSession(RecordingKind::Shop, "Shop");
		}
		if(IsPlaybackActive()) {
			if((HidInput::GetPlayer(1)->stateCur.Buttons & AbortMask) == AbortMask) {
				StopPlayback(HelperState::Cancelled);
				g_Logger->ToastInfo(
					"menu-helper",
					"{} playback cancelled",
					activeKind == RecordingKind::Menu
						? "Menu"
						: activeKind == RecordingKind::Travel ? "Travel" : "Shop"
				);
				return;
			}
			playbackFrames++;
			if(playbackNeutralFrames > 0)
				playbackNeutralFrames--;
			else
				QueueNextPlaybackAction();
		}
	}

	void MenuHelper::OnSceneTransition() {
		auxCoreRecipeOpen = false;
		InputBuffer::SetAuxCoreAmountMenu(false);
		auxCoreShopWaitingForInput = false;
		auxCoreListInputHandle = 0;
		auxCoreRecipeInputHandle = 0;
		shopSessionDeferred = false;
		shopSessionDeferredFrame = 0;
		InputBuffer::ResetAuxCoreAmountInput();
		const bool armedMenu = activeKind == RecordingKind::Menu
			&& (state == HelperState::ArmedPlayback
				|| state == HelperState::ArmedRecord);
		const bool activeTutorialBladeBond = tutorialBladeBondOpen
			&& activeKind == RecordingKind::Menu
			&& (state == HelperState::Recording || IsPlaybackActive());
		const bool preserveMenuTAS = armedMenu || activeTutorialBladeBond;
		// ShopTAS is explicitly armed for the next shop. Loading, scripted
		// tutorials, and scene transitions are not cancellation signals; only an
		// actual shop open or an explicit stop consumes that armed state.
		const bool preserveArmedShopTAS = activeKind == RecordingKind::Shop
			&& (state == HelperState::ArmedPlayback
				|| state == HelperState::ArmedRecord);
		const bool preserveHelperSession =
			preserveMenuTAS || preserveArmedShopTAS;
		DiscardSandboxData();
		shopOpen = false;
		if(!activeTutorialBladeBond) {
			mainMenuOpen = false;
			mainMenuOpening = false;
			mainMenuInputReady = false;
		}
		travelMenuOpen = false;
		travelMenuOpening = false;
		if(!activeTutorialBladeBond && !preserveArmedShopTAS)
			InputBuffer::SetAcceptedActionCapture(false);
		if(
			(IsPlaybackActive() || state == HelperState::ArmedPlayback)
			&& !preserveHelperSession
		)
			StopPlayback(HelperState::Cancelled);
		if(
			(state == HelperState::Recording || state == HelperState::ArmedRecord)
			&& !preserveHelperSession
		)
			state = draftActions.empty() ? HelperState::Idle : HelperState::DraftReady;
	}

#if XENOMODS_CODENAME(bf2)
	XENOMODS_REGISTER_MODULE(MenuHelper);
#endif

} // namespace xenomods
