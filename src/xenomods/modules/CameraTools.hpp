//
// Created by block on 6/21/2022.
//

#pragma once

#include "UpdatableModule.hpp"
#include "xenomods/engine/mm/MathTypes.hpp"

namespace xenomods {

	struct CameraTools : public xenomods::UpdatableModule {
		struct FreecamSettings {
			enum class MoveAxis {
				XZ,
				XY,
				YZ
			};

			bool freecamOn;
			bool ignoreWorldGeometry;
			bool cameraUnlockOn;
			bool relativeToPlayer;
			MoveAxis moveAxis;
			MoveAxis comboMoveAxis;
			bool isFreezePos[3];
			bool isGlobalPos[3];
			bool isGlobalRot[3];
			float camSpeed;

			bool enableTargeting;
			bool targetFollowPlayer;
			glm::vec3 targetPos;

			float unlockYaw;
			float unlockPitch;
			float unlockDistance;
			float unlockTargetHeight;
			float unlockRotateSpeed;
			float unlockZoomSpeed;
		};
		static FreecamSettings Settings;

		struct CameraState {
			mm::Mat44 matrix;
			float fov;
		};
		static CameraState CamState;
		static CameraState VisualCamState;
		static glm::vec3 NormalCamTarget;
		static glm::vec3 VisualCamTarget;
		static glm::vec3 UnlockLastPlayerPosition;
		static bool HasNormalCamTarget;

		struct CameraMeta {
			glm::vec3 pos;
			glm::quat rot;
			glm::vec3 euler;
			glm::vec3 forward;
			glm::vec3 up;
			float fov;
		};
		static CameraMeta CamMeta;
		static bool HasCameraState;

		static void UpdateMeta();

		static void MenuSection();
		static void MenuSettings();

		void Initialize() override;
		bool NeedsUpdate() const override {
			return true;
		}
		void Update(fw::UpdateInfo* updateInfo) override;
		void OnMapChange(unsigned short mapId) override;
	};

} // namespace xenomods
