#include "AudioControls.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>

#include <fmt/format.h>
#include <imgui.h>
#include <skylaunch/hookng/Hooks.hpp>
#include <toml++/toml.hpp>

#include <xenomods/Logger.hpp>
#include <xenomods/NnFile.hpp>
#include <xenomods/engine/gf/MenuObject.hpp>

namespace xenomods {

	bool AudioControls::BackgroundMusic = true;
	bool AudioControls::DisableErrorSound = false;

#if XENOMODS_CODENAME(bf2)
	namespace {
		bool LastSavedDisableErrorSound = false;

		std::string PlaybackSettingsPath() {
			return fmt::format(
				XENOMODS_CONFIG_PATH "/{}/tasSettings.toml",
				XENOMODS_CODENAME_STR
			);
		}

		void SavePlaybackSettingsIfChanged() {
			if(AudioControls::DisableErrorSound == LastSavedDisableErrorSound)
				return;
			const auto path = PlaybackSettingsPath();
			toml::table root;
			toml::parse_result existing = toml::parse_file(path);
			if(existing)
				root = std::move(existing).table();
			toml::table settings;
			settings.insert_or_assign("enabled", AudioControls::DisableErrorSound);
			root.insert_or_assign("disable_error_sound", std::move(settings));
			std::stringstream stream;
			stream << root;
			const std::string contents = stream.str();
			if(NnFile::Preallocate(path, contents.size())) {
				NnFile file(path, nn::fs::OpenMode_Write);
				file.Write(contents.c_str(), contents.size());
				file.Flush();
				LastSavedDisableErrorSound = AudioControls::DisableErrorSound;
			}
		}

		struct MenuErrorSoundHook : skylaunch::hook::Trampoline<MenuErrorSoundHook> {
			static void Hook(unsigned int index) {
				if(
					AudioControls::DisableErrorSound
					&& index == gf::GfMenuObjUtil::SEIndex::error
				)
					return;
				Orig(index);
			}
		};

		using SetVolumeFn = void (*)(void* masterVolume, float volume);

		constexpr const char* GfSoundSingletonSymbol =
			"_ZZN2mm3mtl12PtrSingletonIN2gf7GfSoundEE3sysEvE10s_instance";
		constexpr const char* SetEventBgmVolumeSymbol =
			"_ZN2gf14GfMasterVolume17setEventBgmVolumeEf";
		constexpr const char* SetGameBgmVolumeSymbol =
			"_ZN2gf14GfMasterVolume16setGameBgmVolumeEf";
		constexpr const char* SetMenuBgmVolumeSymbol =
			"_ZN2gf14GfMasterVolume16setMenuBgmVolumeEf";

		// GfMasterVolume is embedded in GfSound. These offsets come directly from
		// GfSound::setupGame and the three native BGM volume setters.
		constexpr std::size_t MasterVolumeOffset = 0x6690;
		constexpr std::size_t EventBgmVolumeOffset = 0x10;
		constexpr std::size_t GameBgmVolumeOffset = 0x18;
		constexpr std::size_t MenuBgmVolumeOffset = 0x38;

		void** GfSoundSingleton = nullptr;
		SetVolumeFn SetEventBgmVolume = nullptr;
		SetVolumeFn SetGameBgmVolume = nullptr;
		SetVolumeFn SetMenuBgmVolume = nullptr;

		void* LastMasterVolume = nullptr;
		void* ReadyGfSound = nullptr;
		void* CandidateGfSound = nullptr;
		int CandidateStableFrames = 0;
		constexpr int ExistingSoundReadyFrames = 120;
		bool Muted = false;
		bool EnabledStateAppliedForMaster = false;
		int EnabledSilentFrames = 0;
		constexpr int SilentRecoveryFrames = 15;
		float SavedEventBgmVolume = 1.f;
		float SavedGameBgmVolume = 1.f;
		float SavedMenuBgmVolume = 1.f;

		struct GfSoundSetupGameHook
			: skylaunch::hook::Trampoline<GfSoundSetupGameHook> {
			static void Hook(void* sound) {
				Orig(sound);
				// setupGame is the point at which XC2 has created the master category
				// handle and copied the user's saved volume options into it. A mute
				// restored from toolWindows.toml must not touch the setters before this.
				ReadyGfSound = sound;
				CandidateGfSound = sound;
				CandidateStableFrames = ExistingSoundReadyFrames;
				LastMasterVolume = nullptr;
				EnabledStateAppliedForMaster = false;
				EnabledSilentFrames = 0;
			}
		};

		bool ResolveAudioFunctions() {
			const auto singleton = skylaunch::hook::detail::ResolveSymbolBase(
				GfSoundSingletonSymbol
			);
			const auto eventSetter = skylaunch::hook::detail::ResolveSymbolBase(
				SetEventBgmVolumeSymbol
			);
			const auto gameSetter = skylaunch::hook::detail::ResolveSymbolBase(
				SetGameBgmVolumeSymbol
			);
			const auto menuSetter = skylaunch::hook::detail::ResolveSymbolBase(
				SetMenuBgmVolumeSymbol
			);
			if(
				singleton == skylaunch::hook::INVALID_FUNCTION_PTR
				|| eventSetter == skylaunch::hook::INVALID_FUNCTION_PTR
				|| gameSetter == skylaunch::hook::INVALID_FUNCTION_PTR
				|| menuSetter == skylaunch::hook::INVALID_FUNCTION_PTR
			)
				return false;

			GfSoundSingleton = reinterpret_cast<void**>(singleton);
			SetEventBgmVolume = reinterpret_cast<SetVolumeFn>(eventSetter);
			SetGameBgmVolume = reinterpret_cast<SetVolumeFn>(gameSetter);
			SetMenuBgmVolume = reinterpret_cast<SetVolumeFn>(menuSetter);
			return true;
		}

		float ReadVolume(void* masterVolume, std::size_t offset) {
			float value = 1.f;
			std::memcpy(
				&value,
				reinterpret_cast<const std::uint8_t*>(masterVolume) + offset,
				sizeof(value)
			);
			return std::isfinite(value) ? value : 1.f;
		}

		void ApplyVolumes(
			void* masterVolume,
			float eventVolume,
			float gameVolume,
			float menuVolume
		) {
			SetEventBgmVolume(masterVolume, eventVolume);
			SetGameBgmVolume(masterVolume, gameVolume);
			SetMenuBgmVolume(masterVolume, menuVolume);
		}
	} // namespace
#endif

	void AudioControls::MenuSection() {
		ImGui::Checkbox("Background music", &BackgroundMusic);
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Toggles field, event, and menu music without muting sound effects or voices."
			);
	}

	void AudioControls::PlaybackMenuSection() {
		if(ImGui::Checkbox("Disable Error Sound", &DisableErrorSound))
			SavePlaybackSettingsIfChanged();
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip("Disables common/MNU_SoundSe sound ID 7 only.");
	}

	void AudioControls::Initialize() {
		UpdatableModule::Initialize();
#if XENOMODS_CODENAME(bf2)
		const toml::parse_result settings = toml::parse_file(PlaybackSettingsPath());
		if(settings) {
			DisableErrorSound =
				settings["disable_error_sound"]["enabled"].value_or(false);
		}
		LastSavedDisableErrorSound = DisableErrorSound;
		if(!ResolveAudioFunctions())
			g_Logger->LogError("Background-music control symbols are unavailable");
		else
			g_Logger->LogInfo("Background-music controls installed");
		GfSoundSetupGameHook::HookAt("_ZN2gf7GfSound9setupGameEv");
		MenuErrorSoundHook::HookAt("_ZN2gf13GfMenuObjUtil6playSEEj");
#endif
	}

	void AudioControls::Update(fw::UpdateInfo*) {
#if XENOMODS_CODENAME(bf2)
		if(
			GfSoundSingleton == nullptr
			|| SetEventBgmVolume == nullptr
			|| SetGameBgmVolume == nullptr
			|| SetMenuBgmVolume == nullptr
		)
			return;

		void* const sound = *GfSoundSingleton;
		if(sound == nullptr) {
			LastMasterVolume = nullptr;
			ReadyGfSound = nullptr;
			CandidateGfSound = nullptr;
			CandidateStableFrames = 0;
			EnabledStateAppliedForMaster = false;
			EnabledSilentFrames = 0;
			return;
		}
		if(sound != ReadyGfSound) {
			// Eden can reload the plugin after setupGame has already run. In that
			// case the definitive hook cannot fire for the existing instance. Require
			// the singleton to remain stable for several seconds before treating it as
			// initialized; this retains the startup crash guard while allowing hot reload.
			if(sound != CandidateGfSound) {
				CandidateGfSound = sound;
				CandidateStableFrames = 1;
			} else {
				CandidateStableFrames++;
			}
			if(CandidateStableFrames < ExistingSoundReadyFrames)
				return;
			ReadyGfSound = sound;
		}

		void* const masterVolume =
			reinterpret_cast<std::uint8_t*>(sound) + MasterVolumeOffset;
		if(masterVolume != LastMasterVolume) {
			LastMasterVolume = masterVolume;
			EnabledStateAppliedForMaster = false;
			EnabledSilentFrames = 0;
		}

		if(!BackgroundMusic) {
			EnabledSilentFrames = 0;
			if(!Muted) {
				SavedEventBgmVolume = ReadVolume(
					masterVolume,
					EventBgmVolumeOffset
				);
				SavedGameBgmVolume = ReadVolume(masterVolume, GameBgmVolumeOffset);
				SavedMenuBgmVolume = ReadVolume(masterVolume, MenuBgmVolumeOffset);
				Muted = true;
				EnabledStateAppliedForMaster = false;
			}

			// Native option/setup paths may restore these fields during loads. Only
			// call the setters when that happens; normal frames remain untouched.
			if(
				ReadVolume(masterVolume, EventBgmVolumeOffset) != 0.f
				|| ReadVolume(masterVolume, GameBgmVolumeOffset) != 0.f
				|| ReadVolume(masterVolume, MenuBgmVolumeOffset) != 0.f
			)
				ApplyVolumes(masterVolume, 0.f, 0.f, 0.f);
		}
		else {
			const float eventVolume = ReadVolume(
				masterVolume,
				EventBgmVolumeOffset
			);
			const float gameVolume = ReadVolume(masterVolume, GameBgmVolumeOffset);
			const float menuVolume = ReadVolume(masterVolume, MenuBgmVolumeOffset);
			const bool allSilent =
				eventVolume == 0.f && gameVolume == 0.f && menuVolume == 0.f;
			EnabledSilentFrames = allSilent ? EnabledSilentFrames + 1 : 0;
			if(
				Muted
				|| !EnabledStateAppliedForMaster
				|| EnabledSilentFrames >= SilentRecoveryFrames
			) {
				// If this process inherited the zeroed master categories from an older
				// build, there is no live Saved* state to recover. One is the native
				// full-volume fallback and immediately makes an enabled toggle audible.
				const bool savedVolumesAreSilent = SavedEventBgmVolume == 0.f
					&& SavedGameBgmVolume == 0.f
					&& SavedMenuBgmVolume == 0.f;
				ApplyVolumes(
					masterVolume,
					savedVolumesAreSilent ? 1.f : SavedEventBgmVolume,
					savedVolumesAreSilent ? 1.f : SavedGameBgmVolume,
					savedVolumesAreSilent ? 1.f : SavedMenuBgmVolume
				);
				EnabledSilentFrames = 0;
			}
			Muted = false;
			EnabledStateAppliedForMaster = true;
		}
#endif
	}

	void AudioControls::OnMapChange(unsigned short) {
#if XENOMODS_CODENAME(bf2)
		// Cross-region loads can rebuild or zero the native BGM categories without
		// replacing the GfSound singleton. Invalidate the enabled-state cache so the
		// newly loaded region receives the user's saved levels on its first safe update.
		EnabledStateAppliedForMaster = false;
		EnabledSilentFrames = 0;
#endif
	}

#if XENOMODS_CODENAME(bf2)
	XENOMODS_REGISTER_MODULE(AudioControls);
#endif

} // namespace xenomods
