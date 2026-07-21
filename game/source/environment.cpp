module;

#pragma warning(push, 0)
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/procedural_sky_material.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector3.hpp>
#pragma warning(pop)

#include <includes.hpp>

module game.environment;

namespace craftbuild {
    void Sun::_ready() {
        set_color(Color(1.0f, 0.93f, 0.78f));
        set_param(Light3D::PARAM_ENERGY, 1.2f);
        set_param(Light3D::PARAM_INDIRECT_ENERGY, 0.55f);
        set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, 512.0f);
        set_shadow(true);
        set_shadow_mode(DirectionalLight3D::SHADOW_PARALLEL_4_SPLITS);
        set_sky_mode(DirectionalLight3D::SKY_MODE_LIGHT_AND_SKY);
        set_rotation(Vector3(day_angle, -0.75f, 0.0f));
    }

    void Sun::_process(float64 delta) {
        day_angle += day_speed * static_cast<float32>(delta);
        if (day_angle > static_cast<float32>(Math_TAU)) day_angle -= static_cast<float32>(Math_TAU);

        set_rotation(Vector3(day_angle, -0.75f, 0.0f));
    }

    void Sun::_bind_methods() {}

    void CraftSky::_ready() {
        sky_material.instantiate();
        sky_material->set_sky_top_color(Color(0.00f, 0.85f, 0.90f));
        sky_material->set_sky_horizon_color(Color(0.45f, 1.00f, 1.00f));
        sky_material->set_sky_curve(0.18f);
        sky_material->set_sky_energy_multiplier(0.75f);
        sky_material->set_ground_bottom_color(Color(0.08f, 0.16f, 0.18f));
        sky_material->set_ground_horizon_color(Color(0.18f, 0.30f, 0.32f));
        sky_material->set_ground_curve(0.3f);
        sky_material->set_ground_energy_multiplier(0.18f);
        sky_material->set_sun_angle_max(14.0f);
        sky_material->set_sun_curve(0.08f);
        sky_material->set_use_debanding(true);

        Ref<Sky> sky;
        sky.instantiate();
        sky->set_radiance_size(Sky::RADIANCE_SIZE_256);
        sky->set_process_mode(Sky::PROCESS_MODE_REALTIME);
        sky->set_material(sky_material);

        environment.instantiate();
        environment->set_background(Environment::BG_SKY);
        environment->set_sky(sky);
        environment->set_bg_energy_multiplier(0.7f);
        environment->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
        environment->set_ambient_light_energy(0.25f);
        environment->set_reflection_source(Environment::REFLECTION_SOURCE_SKY);
        environment->set_tonemapper(Environment::TONE_MAPPER_ACES);
        environment->set_tonemap_exposure(0.85f);
        set_environment(environment);
    }

    void CraftSky::_bind_methods() {}
}