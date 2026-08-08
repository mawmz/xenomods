#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "UpdatableModule.hpp"
#include "xenomods/InputBuffer.hpp"

namespace xenomods {

	struct MenuHelper : public UpdatableModule {
		static bool ShowWindow;
		static bool SandboxMode;

		static void TopBarButton();
		static void MenuWindow();
		static void StatusOverlay();
		static void OnShopOpened();
		static void OnShopClosed();
		static void OnMenuOpening();
		static void OnMenuOpened();
		static void OnMenuInputEnabled();
		static void OnMenuClosed();
		static void OnTravelButtonPressed();
		static void OnTravelButtonFinished();
		static void OnTravelOpening();
		static void OnTravelOpened();
		static void OnTravelClosed();
		static void OnSandboxMenuOpened();
		static void OnSandboxMenuClosed();
		static bool IsSandboxDataActive();
		static std::vector<std::string> SavedShopRecordingNames();
		static std::vector<std::string> SavedMenuRecordingNames();
		static std::vector<std::string> SavedTravelRecordingNames();
		static bool ArmShopPlayback(const std::string& recordingName);
		static bool ArmMainMenuPlayback(const std::string& recordingName);
		static bool ArmTravelMenuPlayback(const std::string& recordingName);
		static bool StartMainMenuPlayback(const std::string& recordingName);
		static bool StartTravelMenuPlayback(const std::string& recordingName);
		static bool IsMenuPlaybackPendingOrActive();
		static void OnAcceptedAction(
			InputBuffer::AcceptedAction action,
			InputBuffer::ActionSource source
		);

		void Initialize() override;
		bool NeedsUpdate() const override {
			return true;
		}
		void Update(fw::UpdateInfo* updateInfo) override;
		void OnSceneTransition() override;
	};

} // namespace xenomods
