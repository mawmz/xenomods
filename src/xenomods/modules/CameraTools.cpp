//
// Created by block on 6/21/2022.
//

#include "CameraTools.hpp"

#include "DebugStuff.hpp"
#include "PlayerMovement.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/matrix_decompose.hpp"
#undef GLM_ENABLE_EXPERIMENTAL
#include "glm/mat4x4.hpp"
#include "xenomods/ImGuiExtensions.hpp"
#include "xenomods/engine/apps/FrameworkLauncher.hpp"
#include "xenomods/engine/fw/Document.hpp"
#include "xenomods/engine/fw/Framework.hpp"
#include "xenomods/engine/game/MenuModelView.hpp"
#include "xenomods/engine/game/Scripts.hpp"
#include "xenomods/engine/game/Utils.hpp"
#include "xenomods/engine/gf/Party.hpp"
#include "xenomods/engine/ml/Scene.hpp"
#include "xenomods/engine/mm/MathTypes.hpp"
#include "xenomods/stuff/utils/debug_util.hpp"
#include "xenomods/stuff/utils/util.hpp"

namespace {

#if XENOMODS_OLD_ENGINE
	struct BypassPlayerCameraHitTest :
		skylaunch::hook::Trampoline<BypassPlayerCameraHitTest> {
		static bool Hook(
			void* thisPointer,
			const void* collisionConfig,
			const mm::Vec3& start,
			const mm::Vec3& end,
			mm::Vec3& hitPosition
		) {
			if(xenomods::CameraTools::Settings.ignoreWorldGeometry)
				return false;

			return Orig(
				thisPointer,
				collisionConfig,
				start,
				end,
				hitPosition
			);
		}
	};

	struct BypassPlayerCameraCollision :
		skylaunch::hook::Trampoline<BypassPlayerCameraCollision> {
		static void Hook(
			void* thisPointer,
			mm::Vec3& cameraPosition,
			mm::Vec3& targetPosition
		) {
			if(xenomods::CameraTools::Settings.ignoreWorldGeometry)
				return;

			Orig(thisPointer, cameraPosition, targetPosition);
		}
	};

	struct BypassPlayerCameraCollision2 :
		skylaunch::hook::Trampoline<BypassPlayerCameraCollision2> {
		static void Hook(
			void* thisPointer,
			mm::Vec3& cameraPosition,
			mm::Vec3& targetPosition
		) {
			if(xenomods::CameraTools::Settings.ignoreWorldGeometry)
				return;

			Orig(thisPointer, cameraPosition, targetPosition);
		}
	};

	struct BypassPlayerCameraCollisionSide :
		skylaunch::hook::Trampoline<BypassPlayerCameraCollisionSide> {
		static void Hook(
			void* thisPointer,
			mm::Vec3& cameraPosition,
			mm::Vec3& targetPosition
		) {
			if(xenomods::CameraTools::Settings.ignoreWorldGeometry)
				return;

			Orig(thisPointer, cameraPosition, targetPosition);
		}
	};

	struct BypassPlayerCameraCollisionUp :
		skylaunch::hook::Trampoline<BypassPlayerCameraCollisionUp> {
		static void Hook(
			void* thisPointer,
			const mm::Vec3& referencePosition,
			mm::Vec3& cameraPosition,
			mm::Vec3& targetPosition
		) {
			if(xenomods::CameraTools::Settings.ignoreWorldGeometry)
				return;

			Orig(
				thisPointer,
				referencePosition,
				cameraPosition,
				targetPosition
			);
		}
	};
#endif

	struct PilotCameraLayers : skylaunch::hook::Trampoline<PilotCameraLayers> {
#if XENOMODS_NEW_ENGINE
		static void Hook(fw::CameraLayer* this_pointer, const fw::Document& document, const fw::UpdateInfo& updateInfo) {
#else
		static void Hook(fw::CameraLayer* this_pointer, const fw::UpdateInfo& updateInfo) {
#endif
			// The list can be transiently incomplete while CameraLayer is being
			// initialized. It is only needed for freecam, so do not traverse it
			// during normal gameplay or while using the visual-orbit camera.
			if(
				xenomods::CameraTools::Settings.freecamOn
				&& this_pointer->listCamera.head != nullptr
				&& reinterpret_cast<void*>(this_pointer->listCamera.head)
					!= &this_pointer->listCamera
			) {
				constexpr size_t CameraNodeOffset = 0x10;
				constexpr size_t MaxCameraLayers = 64;
				auto node = this_pointer->listCamera.head;
				const auto sentinel = reinterpret_cast<void*>(
					&this_pointer->listCamera
				);
				const auto cameraCount = std::min(
					this_pointer->listCamera.count,
					MaxCameraLayers
				);

				for(size_t index = 0; index < cameraCount; index++) {
					if(node == nullptr || reinterpret_cast<void*>(node) == sentinel)
						break;

					auto camera = reinterpret_cast<fw::Camera*>(
						reinterpret_cast<u8*>(node) - CameraNodeOffset
					);
					node = node->next;

					camera->matrix = glm::inverse(
						static_cast<const glm::mat4&>(
							xenomods::CameraTools::CamState.matrix
						)
					);
					camera->fov =
						xenomods::CameraTools::CamState.fov;
				}
			}

	if(xenomods::CameraTools::Settings.freecamOn) {
		this_pointer->willLerp = true;
		this_pointer->lerpProgress = 999.f;
		this_pointer->matTarget = xenomods::CameraTools::CamState.matrix;
		this_pointer->matCurrent = xenomods::CameraTools::CamState.matrix;
	}

#if XENOMODS_NEW_ENGINE
	Orig(this_pointer, document, updateInfo);
#else
			Orig(this_pointer, updateInfo);
#endif

	if(
		xenomods::CameraTools::Settings.freecamOn
		&& this_pointer->objCam != nullptr
		&& this_pointer->objCam->AttrTransformPtr != nullptr
	) {
		this_pointer->objCam->AttrTransformPtr->fov = xenomods::CameraTools::CamState.fov;
#if !XENOMODS_CODENAME(bf3)
		this_pointer->objCam->updateFovNearFar();
#endif
	}

#if !XENOMODS_CODENAME(bf3)
	if(
		xenomods::CameraTools::Settings.cameraUnlockOn
		&& this_pointer->objCam != nullptr
		&& this_pointer->objCam->AttrTransformPtr != nullptr
		&& this_pointer->objCam->ScnPtr != nullptr
		&& this_pointer->objCam
			== this_pointer->objCam->ScnPtr->getCam(-1)
	) {
		// CameraLayer and its fw::Camera objects remain untouched. Movement
		// therefore keeps XC2's normal camera basis. Only the final scene
		// camera shown by the renderer receives the visual-orbit matrix.
		xenomods::CameraTools::CamState.matrix =
			this_pointer->objCam->AttrTransformPtr->viewMatInverse;
		xenomods::CameraTools::CamState.fov =
			this_pointer->objCam->AttrTransformPtr->fov;
		xenomods::CameraTools::NormalCamTarget =
			this_pointer->objCam->AttrTransformPtr->target;
		xenomods::CameraTools::HasNormalCamTarget = true;
		xenomods::CameraTools::UpdateMeta();
		xenomods::CameraTools::HasCameraState = true;

		this_pointer->objCam->setViewMatrix(
			glm::inverse(static_cast<const glm::mat4&>(
				xenomods::CameraTools::VisualCamState.matrix
			))
		);
	}
#endif
} // namespace
}
;

struct CopyCurrentCameraState : skylaunch::hook::Trampoline<CopyCurrentCameraState> {
	static void Hook(ml::ScnObjCam* this_pointer) {
		Orig(this_pointer);

#if !XENOMODS_CODENAME(bf3)
		if(this_pointer->ScnPtr != nullptr && this_pointer->AttrTransformPtr != nullptr && this_pointer == this_pointer->ScnPtr->getCam(-1)) {
#else
			if(this_pointer->AttrTransformPtr != nullptr) {
#endif
			if(
				!xenomods::CameraTools::Settings.freecamOn
				&& !xenomods::CameraTools::Settings.cameraUnlockOn
			) {
				// CamState and its menu values always follow XC2's normal
				// camera. Camera unlock has a separate VisualCamState.
				xenomods::CameraTools::CamState.matrix =
					this_pointer->AttrTransformPtr->viewMatInverse;
				xenomods::CameraTools::CamState.fov =
					this_pointer->AttrTransformPtr->fov;
				xenomods::CameraTools::NormalCamTarget =
					this_pointer->AttrTransformPtr->target;
				xenomods::CameraTools::HasNormalCamTarget = true;
				xenomods::CameraTools::UpdateMeta();
				xenomods::CameraTools::HasCameraState = true;
			}
		}
	}
};
}
; // namespace

namespace xenomods {

	CameraTools::FreecamSettings CameraTools::Settings = {
		.freecamOn = false,
		.ignoreWorldGeometry = false,
		.cameraUnlockOn = false,
		.relativeToPlayer = false,
		.moveAxis = FreecamSettings::MoveAxis::XZ,
		.comboMoveAxis = FreecamSettings::MoveAxis::XY,
		.isFreezePos = { false, false, false },
		.isGlobalPos = { false, false, false },
		.isGlobalRot = { false, true, false },
		.camSpeed = 8.f,
		.enableTargeting = false,
		.targetPos = {},
		.unlockYaw = 0.f,
		.unlockPitch = 0.f,
		.unlockDistance = 8.f,
		.unlockTargetHeight = 1.f,
		.unlockRotateSpeed = 90.f,
		.unlockZoomSpeed = 8.f
	};

	CameraTools::CameraState CameraTools::CamState = {
		.matrix = glm::identity<glm::mat4>(),
		.fov = 40.f
	};
	CameraTools::CameraState CameraTools::VisualCamState = {
		.matrix = glm::identity<glm::mat4>(),
		.fov = 40.f
	};
	glm::vec3 CameraTools::NormalCamTarget = {};
	glm::vec3 CameraTools::VisualCamTarget = {};
	glm::vec3 CameraTools::UnlockLastPlayerPosition = {};
	bool CameraTools::HasNormalCamTarget = false;

	CameraTools::CameraMeta CameraTools::CamMeta = {};
	bool CameraTools::HasCameraState = false;

	glm::vec3 lastPlayerPos = {};
	glm::vec3 relativePlayerDelta = {};

	void CameraTools::UpdateMeta() {
		glm::vec3 pos {};
		glm::quat rot {};
		glm::vec3 scale {};
		glm::vec3 skew {};
		glm::vec4 perspective {};

		// decompose existing matrix
		glm::decompose(static_cast<glm::mat4&>(CamState.matrix), scale, rot, pos, skew, perspective);

		CamMeta.pos = pos;
		CamMeta.rot = rot;
		CamMeta.euler = glm::degrees(glm::eulerAngles(rot));

		glm::vec3 forward = { 0, 0, 1 };
		glm::vec3 up = { 0, 1, 0 };
		CamMeta.forward = rot * forward;
		CamMeta.up = rot * up;

		CamMeta.fov = CamState.fov;
	}

	void InitializeCameraUnlockFromState() {
		auto playerPosition = PlayerMovement::GetPartyPosition();
		if(playerPosition == nullptr || !CameraTools::HasCameraState)
			return;

		CameraTools::VisualCamState = CameraTools::CamState;
		CameraTools::UpdateMeta();
		const auto cameraPosition = CameraTools::CamMeta.pos;

		// XC2 already stores the normal camera's exact target. Use it directly
		// so enabling unlock begins at the normal camera's current transform with
		// no inferred target or activation jump.
		if(CameraTools::HasNormalCamTarget) {
			CameraTools::VisualCamTarget =
				CameraTools::NormalCamTarget;
		} else {
			CameraTools::VisualCamTarget =
				cameraPosition
				+ CameraTools::CamMeta.forward
					* CameraTools::Settings.unlockDistance;
		}

		CameraTools::UnlockLastPlayerPosition = *playerPosition;
		const glm::vec3 offset =
			cameraPosition - CameraTools::VisualCamTarget;
		const float distance = glm::length(offset);
		if(distance <= 0.0001f)
			return;

		CameraTools::Settings.unlockYaw =
			glm::degrees(std::atan2(offset.x, offset.z));
		CameraTools::Settings.unlockPitch =
			glm::degrees(std::asin(
				std::clamp(offset.y / distance, -1.f, 1.f)
			));
		CameraTools::Settings.unlockDistance = distance;
		CameraTools::Settings.unlockTargetHeight =
			CameraTools::VisualCamTarget.y - playerPosition->y;
	}

	void UpdateCameraUnlock(float deltaTime) {
		auto playerPosition = PlayerMovement::GetPartyPosition();
		if(playerPosition == nullptr)
			return;

		auto& settings = CameraTools::Settings;
		HidInput* secondController = HidInput::GetPlayer(2);
		if(secondController->padConnected) {
			glm::vec2 look = secondController->stateCur.RAxis;
			glm::vec2 move = secondController->stateCur.LAxis;
			constexpr float StickDeadzone = 0.15f;
			if(glm::length(look) < StickDeadzone)
				look = {};
			if(glm::length(move) < StickDeadzone)
				move = {};

			settings.unlockYaw -=
				look.x * settings.unlockRotateSpeed * deltaTime;
			settings.unlockPitch -=
				look.y * settings.unlockRotateSpeed * deltaTime;
			settings.unlockDistance -=
				move.y * settings.unlockZoomSpeed * deltaTime;
			settings.unlockTargetHeight +=
				move.x * settings.unlockZoomSpeed * 0.25f * deltaTime;
		}

		settings.unlockPitch =
			std::clamp(settings.unlockPitch, -85.f, 85.f);
		settings.unlockDistance =
			std::max(settings.unlockDistance, 0.25f);

		// Follow the controlled character without borrowing any subsequent
		// normal-camera rotation. The visual target receives only the player's
		// world-space displacement and the explicit height adjustment below.
		const glm::vec3 playerDelta =
			*playerPosition - CameraTools::UnlockLastPlayerPosition;
		CameraTools::VisualCamTarget += playerDelta;
		CameraTools::UnlockLastPlayerPosition = *playerPosition;

		const float yaw = glm::radians(settings.unlockYaw);
		const float pitch = glm::radians(settings.unlockPitch);
		const float horizontalDistance =
			settings.unlockDistance * std::cos(pitch);
		const glm::vec3 offset(
			horizontalDistance * std::sin(yaw),
			settings.unlockDistance * std::sin(pitch),
			horizontalDistance * std::cos(yaw)
		);
		CameraTools::VisualCamTarget.y =
			playerPosition->y + settings.unlockTargetHeight;
		const glm::vec3 target = CameraTools::VisualCamTarget;
		const glm::vec3 cameraPosition = target + offset;

		CameraTools::VisualCamState.matrix =
			glm::inverse(glm::lookAt(
				cameraPosition,
				target,
				glm::vec3(0.f, 1.f, 0.f)
			));
		// FOV remains tied to the normal camera just like the camera-tools values.
		CameraTools::VisualCamState.fov = CameraTools::CamState.fov;
	}

	void DoFreeCameraMovement(float deltaTime) {
		// for future reference:
		//auto seconds = nn::os::GetSystemTick()/19200000.;

		// The debug-input selection has already been handled.
		HidInput* debugInput = HidInput::GetDebugInput();

		auto set = &CameraTools::Settings;
		auto cs = &CameraTools::CamState;
		auto cm = &CameraTools::CamMeta;

		glm::vec3 pos {};
		glm::quat rot {};
		glm::vec3 scale {};
		glm::vec3 skew {};
		glm::vec4 perspective {};

		// decompose existing matrix
		glm::decompose(static_cast<glm::mat4&>(cs->matrix), scale, rot, pos, skew, perspective);

		glm::vec2 lStick = debugInput->stateCur.LAxis;
		glm::vec2 rStick = debugInput->stateCur.RAxis;

		// deadzone
		const float STICK_DEADZONE = 0.15f;
		if(glm::length(lStick) < STICK_DEADZONE)
			lStick = glm::zero<glm::vec2>();
		if(glm::length(rStick) < STICK_DEADZONE)
			rStick = glm::zero<glm::vec2>();

		// fov changing
		float fovMult = 30.f * deltaTime;

		// double the deadzone for the right stick X here to prevent accidental FOV changing when rolling
		bool shouldRoll = glm::abs(rStick.x) >= (STICK_DEADZONE * 2.f);
		bool shouldChangeFOV = !shouldRoll;

		// slow the zoom at lower fovs
		if(cs->fov != 0.0f && std::abs(cs->fov) < 20.f)
			fovMult *= std::lerp(0.01f, 1.0f, std::abs(cs->fov) / 20.f);

		if(debugInput->InputHeld(Keybind::CAMERA_COMBO) && shouldChangeFOV) {
			// holding down the combo, so modify fov
			// note: game hard crashes during rendering when |fov| >= ~179.5 or == 0, it needs clamping
			cs->fov = std::clamp(cs->fov + -rStick.y * fovMult, -179.f, 179.f);
			if(cs->fov == 0)
				cs->fov = 0.001f;
		}

		// movement
		glm::vec3 move { 0, 0, 0 };
		glm::vec3 perAxisMove { 0, 0, 0 };

		CameraTools::FreecamSettings::MoveAxis moveAxis = debugInput->InputHeld(Keybind::CAMERA_COMBO) ? set->comboMoveAxis : set->moveAxis;

		switch(moveAxis) {
			case CameraTools::FreecamSettings::MoveAxis::XZ:
				move = { lStick.x, 0, -lStick.y };
				break;
			case CameraTools::FreecamSettings::MoveAxis::XY:
				move = { lStick.x, lStick.y, 0 };
				break;
			case CameraTools::FreecamSettings::MoveAxis::YZ:
				move = { 0, lStick.y, -lStick.x };
				break;
		}

		perAxisMove += (set->isGlobalPos[0] ? glm::identity<glm::quat>() : rot) * glm::vec3(move.x, 0, 0);
		perAxisMove += (set->isGlobalPos[1] ? glm::identity<glm::quat>() : rot) * glm::vec3(0, move.y, 0);
		perAxisMove += (set->isGlobalPos[2] ? glm::identity<glm::quat>() : rot) * glm::vec3(0, 0, move.z);

		if (set->isFreezePos[0])
			perAxisMove.x = 0;
		if (set->isFreezePos[1])
			perAxisMove.y = 0;
		if (set->isFreezePos[2])
			perAxisMove.z = 0;

		move = perAxisMove * deltaTime; // rotate movement to local space
		move *= set->camSpeed;          // multiply by cam speed
		if (set->relativeToPlayer)      // add player movement
			move = move + relativePlayerDelta;

		// rotation
		if (set->enableTargeting) {
			// if targeting, just look at our target
			if (set->targetPos != pos + move)
				rot = glm::inverse(glm::lookAt(pos + move, set->targetPos, {0, 1, 0}));

			// i hope this fixes the crashes
			if (isnan(rot)[0])
				rot = glm::identity<glm::quat>();
		}
		else {
			glm::vec3 look {};
			float lookMult = 70.f * deltaTime;
			float rollMult = 20.f * deltaTime;

			// slow the camera down at lower fovs
			if(cs->fov != 0.0f && std::abs(cs->fov) < 40.f)
				lookMult *= cs->fov / 40.f;

			if(debugInput->InputHeld(Keybind::CAMERA_COMBO)) {
				if (shouldRoll)
					look = { 0, 0, -rStick.x * rollMult }; // only roll
			}
			else
				look = { rStick.y * lookMult, -rStick.x * lookMult, 0 }; // pitch and yaw

			// pitch is default in local space
			// yaw is default in world space
			// roll is default in local space
			float pitchDeg = glm::radians(look.x);
			glm::quat pitchRot = glm::angleAxis(pitchDeg, (set->isGlobalRot[0] ? glm::identity<glm::quat>() : rot) * glm::vec3(1, 0, 0));
			float yawDeg = glm::radians(look.y);
			glm::quat yawRot = glm::angleAxis(yawDeg, (set->isGlobalRot[1] ? glm::identity<glm::quat>() : rot) * glm::vec3(0, 1, 0));
			float rollDeg = glm::radians(look.z);
			glm::quat rollRot = glm::angleAxis(rollDeg, (set->isGlobalRot[2] ? glm::identity<glm::quat>() : rot) * glm::vec3(0, 0, 1));

			// apply rotations
			rot = yawRot * pitchRot * rollRot * rot;
		}

		// get angle+axis to rotate the matrix by
		float angle = glm::angle(rot);
		glm::vec3 axis = glm::axis(rot);

		glm::mat4 newmat = glm::mat4(1.f);
		newmat = glm::translate(newmat, pos + move);
		newmat = glm::rotate(newmat, angle, axis);

		cs->matrix = newmat;
		CameraTools::UpdateMeta();
	}

	// Buttons and menu functionality
	void ResetState(bool resetPos = false, bool resetRot = false, bool resetFOV = false) {
		if (resetPos || resetRot) {
			glm::vec3 pos {};
			glm::quat rot {};
			glm::vec3 scale {};
			glm::vec3 skew {};
			glm::vec4 perspective {};

			// decompose existing matrix
			glm::decompose(static_cast<const glm::mat4&>(xenomods::CameraTools::CamState.matrix), scale, rot, pos, skew, perspective);

			glm::mat4 newmat = glm::identity<glm::mat4>();
			if (!resetPos)
				newmat = glm::translate(newmat, pos);

			if (!resetRot) {
				// get angle+axis to rotate the matrix by
				float angle = glm::angle(rot);
				glm::vec3 axis = glm::axis(rot);

				newmat = glm::rotate(newmat, angle, axis);
			}

			xenomods::CameraTools::CamState.matrix = newmat;
		}

		if (resetFOV) {
			xenomods::CameraTools::CamState.fov = 80.f;
		}

		xenomods::CameraTools::UpdateMeta();
	}

	void TeleportPlayerToCamera() {
		if(xenomods::detail::IsModuleRegistered(STRINGIFY(PlayerMovement)))
			PlayerMovement::SetPartyPosition(CameraTools::CamMeta.pos);
	}

	void CameraTools::MenuSettings() {
		ImGui::SeparatorText("Movement");

#if !XENOMODS_CODENAME(bf3)
		ImGui::Checkbox("Relative to Player", &Settings.relativeToPlayer);
#endif

		ImGui::PushItemWidth(64.f);
		imguiext::EnumComboBox("Move type", &Settings.moveAxis);
		ImGui::SameLine();
		imguiext::EnumComboBox("L+R Move type", &Settings.comboMoveAxis);
		ImGui::PopItemWidth();

		ImGui::Checkbox("Freeze X", &Settings.isFreezePos[0]);
		ImGui::SameLine();
		ImGui::Checkbox("Freeze Y", &Settings.isFreezePos[1]);
		ImGui::SameLine();
		ImGui::Checkbox("Freeze Z", &Settings.isFreezePos[2]);

		ImGui::Checkbox("Global X", &Settings.isGlobalPos[0]);
		ImGui::SameLine();
		ImGui::Checkbox("Global Y", &Settings.isGlobalPos[1]);
		ImGui::SameLine();
		ImGui::Checkbox("Global Z", &Settings.isGlobalPos[2]);

		imguiext::InputFloatExt("Freecam speed", &Settings.camSpeed, 1.f, 5.f, 2.f, "%.3f m/s");

		ImGui::SeparatorText("Rotation");

		//ImGui::Checkbox("Global Pitch", &Settings.isGlobalRot[0]);
		//ImGui::SameLine();
		ImGui::Checkbox("Global Roll", &Settings.isGlobalRot[1]);
		//ImGui::SameLine();
		//ImGui::Checkbox("Global Yaw", &Settings.isGlobalRot[2]);

		ImGui::SeparatorText("Targeting");

		ImGui::Checkbox("Enable Targeting", &Settings.enableTargeting);
#if !XENOMODS_CODENAME(bf3)
		ImGui::Checkbox("Follow Player Position", &Settings.targetFollowPlayer);
#endif

		if (ImGui::Button("Set target from camera position")) {
			Settings.targetPos = CamMeta.pos;
		}

		ImGui::DragFloat3("Target Pos", reinterpret_cast<float*>(&Settings.targetPos));

		ImGui::Separator();
	}

	void CameraTools::MenuSection() {
		if(ImGui::Checkbox("Freecam", &Settings.freecamOn)) {
			if(Settings.freecamOn)
				Settings.cameraUnlockOn = false;
		}
#if XENOMODS_OLD_ENGINE
		ImGui::Checkbox(
			"Ignore world geometry",
			&Settings.ignoreWorldGeometry
		);
#endif
		if(
			ImGui::Checkbox(
				"Camera Unlock",
				&Settings.cameraUnlockOn
			)
		) {
			if(Settings.cameraUnlockOn) {
				Settings.freecamOn = false;
				InitializeCameraUnlockFromState();
			}
		}
		if(Settings.cameraUnlockOn) {
			ImGui::PushItemWidth(150.f);
			ImGui::DragFloat(
				"Visual yaw",
				&Settings.unlockYaw,
				0.5f
			);
			ImGui::DragFloat(
				"Visual pitch",
				&Settings.unlockPitch,
				0.5f,
				-85.f,
				85.f
			);
			ImGui::DragFloat(
				"Visual distance",
				&Settings.unlockDistance,
				0.1f,
				0.25f,
				100.f
			);
			ImGui::DragFloat(
				"Target height",
				&Settings.unlockTargetHeight,
				0.05f
			);
			ImGui::PopItemWidth();
		}

		ImGui::PushItemWidth(250.f);

		if (Settings.freecamOn && ImGui::CollapsingHeader("Control Options")) {
			MenuSettings();
		}

		// icky short-circuit prevention...
		bool shouldUpdate = false;

		if(ImGui::DragFloat3("Pos", reinterpret_cast<float*>(&CamMeta.pos)))
			shouldUpdate = true;
		ImGui::SameLine();
		if (ImGui::Button("Reset Pos")) {
			ResetState(true, false, false);
		}

		if(ImGui::DragFloat3("Rot", reinterpret_cast<float*>(&CamMeta.euler)))
			shouldUpdate = true;
		ImGui::SameLine();
		if (ImGui::Button("Reset Rot")) {
			ResetState(false, true, false);
		}

		if(ImGui::DragFloat("FOV", &CamState.fov, 1, -179, 179))
			shouldUpdate = true;
		ImGui::SameLine();
		if (ImGui::Button("Reset FOV")) {
			ResetState(false, false, true);
		}

		ImGui::PopItemWidth();

		if(shouldUpdate) {
			glm::quat newRot = glm::quat(glm::radians(CameraTools::CamMeta.euler));
			float angle = glm::angle(newRot);
			glm::vec3 axis = glm::axis(newRot);

			glm::mat4 newmat = glm::mat4(1.f);
			newmat = glm::translate(newmat, CameraTools::CamMeta.pos);
			newmat = glm::rotate(newmat, angle, axis);

			CameraTools::CamState.matrix = newmat;

			// note: game hard crashes during rendering when |fov| >= ~179.5 or == 0, it needs clamping
			CameraTools::CamState.fov = std::clamp(CameraTools::CamState.fov, -179.f, 179.f);
			if(CameraTools::CamState.fov == 0)
				CameraTools::CamState.fov = 0.001f;

			CameraTools::Settings.freecamOn = true;
		}

#if !XENOMODS_CODENAME(bf3)
		ImGui::Separator();
		if(ImGui::Button("Teleport party lead to camera"))
			TeleportPlayerToCamera();
#endif
	}

	void CameraTools::Initialize() {
		UpdatableModule::Initialize();
		g_Logger->LogDebug("Setting up camera tools...");

#if XENOMODS_OLD_ENGINE
		BypassPlayerCameraHitTest::HookAt(
			"_ZN2gf12PlayerCamera14getHitPositionERKN2fw15ColiCheckConfigERKN2mm4Vec3ES8_RS6_"
		);
		BypassPlayerCameraCollision::HookAt(
			"_ZN2gf12PlayerCamera15updateCollisionERN2mm4Vec3ES3_"
		);
		BypassPlayerCameraCollision2::HookAt(
			"_ZN2gf12PlayerCamera16updateCollision2ERN2mm4Vec3ES3_"
		);
		BypassPlayerCameraCollisionSide::HookAt(
			"_ZN2gf12PlayerCamera19updateCollisionSideERN2mm4Vec3ES3_"
		);
		BypassPlayerCameraCollisionUp::HookAt(
			"_ZN2gf12PlayerCamera17updateCollisionUpERKN2mm4Vec3ERS2_S5_"
		);

		// intermittently reads the address as 0x0... let's just use the actual symbol for now
		// TODO: why *is* the function reference not exporting?
		PilotCameraLayers::HookAt("_ZN2fw11CameraLayer6updateERKNS_10UpdateInfoE");
#elif XENOMODS_CODENAME(bfsw)
		PilotCameraLayers::HookAt(&fw::CameraLayer::update);
#elif XENOMODS_CODENAME(bf3)
		// fw::CameraLayer::update
		if(version::RuntimeVersion() == version::SemVer::v2_0_0 || version::RuntimeVersion() == version::SemVer::v2_1_0 || version::RuntimeVersion() == version::SemVer::v2_2_0)
			PilotCameraLayers::HookFromBase(0x7100013708);
		else if(version::RuntimeVersion() == version::SemVer::v2_1_1)
			PilotCameraLayers::HookFromBase(0x7100013718);
#endif

#if !XENOMODS_CODENAME(bf3)
		CopyCurrentCameraState::HookAt(&ml::ScnObjCam::updateFovNearFar);
#else
		// ml::ScnObjCam::updateFovNearFar
		if(version::RuntimeVersion() == version::SemVer::v2_0_0)
			CopyCurrentCameraState::HookFromBase(0x71012702ec);
		else if(version::RuntimeVersion() == version::SemVer::v2_1_0)
			CopyCurrentCameraState::HookFromBase(0x710127061c);
		else if(version::RuntimeVersion() == version::SemVer::v2_1_1)
			CopyCurrentCameraState::HookFromBase(0x710127065c);
		else if(version::RuntimeVersion() == version::SemVer::v2_2_0)
			CopyCurrentCameraState::HookFromBase(0x71012711cc);
#endif

	}

	void CameraTools::Update(fw::UpdateInfo* updateInfo) {
		HidInput* debugInput = HidInput::GetDebugInput();

		if(Settings.cameraUnlockOn) {
			UpdateCameraUnlock(updateInfo->updateDelta);
			return;
		}

		// if there's only one controller, let them freecam only when the menu is open
		if(debugInput == HidInput::GetPlayer(1) && !g_Menu->IsOpen())
			return;

		if(debugInput->InputDownStrict(Keybind::FREECAM_TOGGLE)) {
			Settings.freecamOn = !Settings.freecamOn;
			g_Logger->ToastInfo(STRINGIFY(Freecam.freecamOn), "Freecam: {}", Settings.freecamOn);
		}

		if(Settings.freecamOn) {
			bool speedChanged = false;
			if(debugInput->InputDownStrict(Keybind::FREECAM_SPEED_UP)) {
				Settings.camSpeed *= 2.f;
				speedChanged = true;
			} else if(debugInput->InputDownStrict(Keybind::FREECAM_SPEED_DOWN)) {
				Settings.camSpeed /= 2.f;
				speedChanged = true;
			}

			if(speedChanged)
				g_Logger->ToastInfo("freecamSpeed", "Freecam speed: {}m/s", Settings.camSpeed);

			if(debugInput->InputDownStrict(Keybind::FREECAM_FOVRESET))
				ResetState(false, false, true);
			if(debugInput->InputDownStrict(Keybind::FREECAM_ROTRESET)) {
				ResetState(false, true, false);
			}

#if !XENOMODS_CODENAME(bf3)
			if (xenomods::detail::IsModuleRegistered(STRINGIFY(PlayerMovement))) {
				glm::vec3* pos = PlayerMovement::GetPartyPosition();

				if (Settings.targetFollowPlayer && pos != nullptr)
					Settings.targetPos = *pos + glm::vec3(0, 1, 0);

				if (pos != nullptr) {
					relativePlayerDelta = *pos - lastPlayerPos;
				}
				else {
					// reset these, the party doesn't exist anymore
					lastPlayerPos = relativePlayerDelta = glm::zero<glm::vec3>();
				}
			}
#endif

			DoFreeCameraMovement(updateInfo->updateDelta);

#if !XENOMODS_CODENAME(bf3)
			if (xenomods::detail::IsModuleRegistered(STRINGIFY(PlayerMovement))) {
				glm::vec3* pos = PlayerMovement::GetPartyPosition();
				if (pos != nullptr)
					lastPlayerPos = *pos;
			}
#endif

			if(debugInput->InputDownStrict(Keybind::FREECAM_TELEPORT))
				TeleportPlayerToCamera();
		}
	}

	void CameraTools::OnMapChange(unsigned short) {
		// Camera state points into the outgoing scene. Do not let consumers use
		// it until CopyCurrentCameraState observes the new scene camera.
		HasCameraState = false;
		HasNormalCamTarget = false;
	}

	XENOMODS_REGISTER_MODULE(CameraTools);

} // namespace xenomods
