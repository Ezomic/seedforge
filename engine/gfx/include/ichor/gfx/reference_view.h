#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/gfx/render_view.h"

#include <glm/gtc/quaternion.hpp>

#include <string>

namespace ichor {

// The fixed view of the hoplite kit that the golden image test renders and
// the app starts from. Change it and the golden image changes with it.
inline RenderView kitReferenceView(const SceneBundle& bundle, float aspect)
{
    RenderView view;
    (void)aspect;
    // The kit exports with every piece at the origin; spread them so all three read.
    const auto offset = [](const std::string& name) {
        if (name.starts_with("helm_")) return glm::dvec3(0.85, 0.0, 0.1);
        if (name == "SM_Aspis") return glm::dvec3(-0.85, 0.0, 0.0);
        return glm::dvec3(0.0);
    };
    for (const Node& node : bundle.nodes) {
        RenderInstance instance;
        instance.mesh = node.mesh;
        instance.position = glm::dvec3(node.world[3]) + offset(node.name);
        instance.basis = glm::mat3(node.world);
        view.instances.push_back(instance);
    }

    const glm::dvec3 eye(2.2, 1.6, 2.8);
    const glm::dvec3 target(0.0, 0.55, 0.0);
    view.camera.position = eye;
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
    view.camera.fovY = glm::radians(45.0f);
    view.camera.nearPlane = 0.05f;
    view.camera.farPlane = 100.0f;
    view.sunDirection = glm::normalize(glm::vec3(-0.5f, 0.9f, 0.35f));

    for (int i = -5; i <= 5; ++i) {
        const glm::vec3 grey(0.35f);
        view.lines.push_back({glm::dvec3(i, 0.0, -5.0), glm::dvec3(i, 0.0, 5.0), grey});
        view.lines.push_back({glm::dvec3(-5.0, 0.0, i), glm::dvec3(5.0, 0.0, i), grey});
    }
    view.lines.push_back({glm::dvec3(0.0), glm::dvec3(1.0, 0.0, 0.0), glm::vec3(1.0f, 0.2f, 0.2f)});
    view.lines.push_back({glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0), glm::vec3(0.2f, 1.0f, 0.2f)});
    view.lines.push_back({glm::dvec3(0.0), glm::dvec3(0.0, 0.0, 1.0), glm::vec3(0.2f, 0.4f, 1.0f)});
    return view;
}

}
