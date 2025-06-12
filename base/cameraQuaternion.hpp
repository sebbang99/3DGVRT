/*
 * Sogang Univ, Graphics Lab, 2024
 *
 * Abura Soba, 2025
 *
 * Camera(Quaternion)
 *
 */

#define USE_CORRECT_VULKAN_PERSPECTIVE_IHM

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "LoadCamera.hpp"
#include "Define.h"

class QuaternionCamera {
public:
    glm::vec3 position;
    glm::quat rotation;
    glm::mat4 perspective;
    float fov;
    float znear;
    float zfar;
    float deltaTime;

    bool updated;

    CameraLoader cameraLoader;
    DatasetType dataType;
    glm::mat4 viewMatrix;

    QuaternionCamera()
        : position(0.0f, 0.0f, 0.0f), rotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)), perspective(glm::mat4(0.0f)) {
    }

    void setDeltaTime(float deltaTime) {
        this->deltaTime = deltaTime;
    }

    glm::mat4 getViewMatrix() const {
        // 쿼터니언을 행렬로 변환하고, 역행렬 적용 (View matrix)
        glm::mat4 rotMat = glm::toMat4(glm::conjugate(rotation));
        glm::mat4 transMat = glm::translate(glm::mat4(1.0f), -position);
#if LOAD_NERF_CAMERA
        return viewMatrix;
#else
        return rotMat * transMat;
#endif
    }

    glm::mat4 getProjectionMatrix() const {
        return perspective;
    }

    void printMat4(glm::mat4 mat) {
        cout << "{\n";
        for (int i = 0; i < 2; i++) {
            cout << "\t\t" << mat[i][0] << ", " << mat[i][1] << ",    " << mat[i][2] << ",    " << mat[i][3] << ",    ";
            cout << "\t\t" << mat[i][4] << ", " << mat[i][5] << ",    " << mat[i][6] << ",    " << mat[i][7] << ",\n";
        }
        cout << "\t};\n\tbreak;\n";
    }

    void setPerspective(float fov, float aspect, float znear, float zfar)
    {
        glm::mat4 currentMatrix = perspective;
#ifdef USE_CORRECT_VULKAN_PERSPECTIVE_IHM
        this->fov = fov;
        this->znear = znear;
        this->zfar = zfar;
        perspective = glm::perspective_Vulkan_no_depth_reverse(glm::radians(fov), aspect, znear, zfar);

        if (perspective != currentMatrix) {
            updated = true;
        }
#else
        glm::mat4 currentMatrix = matrices.perspective;
        this->fov = fov;
        this->znear = znear;
        this->zfar = zfar;
        matrices.perspective = glm::perspective(glm::radians(fov), aspect, znear, zfar);
        if (flipY) {
            matrices.perspective[1][1] *= -1.0f;
        }
        if (matrices.view != currentMatrix) {
            updated = true;
        }
#endif
    };

    void setTranslation(glm::vec3 position) {
        this->position = position;
    }

    void setRotation(glm::quat radians) {
        rotation = radians;
    }

    void rotate(const glm::vec3& localAxis, float angleRadians) {
        glm::vec3 worldAxis = rotation * localAxis;
        glm::quat q = glm::angleAxis(angleRadians, glm::normalize(worldAxis));
        rotation = glm::normalize(q * rotation);
    }

    void move(const glm::vec3& delta) {
        // 로컬 방향으로 이동하려면 쿼터니언 회전을 적용
        position += rotation * delta;
    }

    /* load nerf camera */
    const vector<string> getCamNames() {
        return cameraLoader.camNames;
    }

    unsigned int getNumOfCams() {
        return cameraLoader.camNames.size();
    }

    void setNerfCamera(uint32_t idx, bool debugMsg) {
        CameraFrame* frame = &cameraLoader.nerfCameras.frames[idx];
        viewMatrix = frame->transformMatrix;

        if (debugMsg) {
            //cout << "perspective mat:\n";
            //printMat4(perspective);
            cout << "\tm_frameInfo.viewMatrix = glm::mat4";
            printMat4(viewMatrix);
        }
    }

    void setDatasetCamera(DatasetType type, uint32_t idx, float aspect, bool debugMsg) {
        if (type == nerf) {
            setPerspective(FOV_Y, aspect, znear, zfar);
            setNerfCamera(idx, debugMsg);
        }
    }

    void loadDatasetCamera(DatasetType type, string path, uint32_t width, uint32_t height) {
        if (type == nerf) {
            cameraLoader.loadNerfCameraData(path, width, height, znear, zfar);
        }
        dataType = type;
    }
};