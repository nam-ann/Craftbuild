module;

#pragma warning(push, 0)
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/procedural_sky_material.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#pragma warning(pop)

#include <includes.hpp>

export module game.environment;

import misc.number;

using namespace godot;

export namespace craftbuild {
    class Sun : public DirectionalLight3D {
        GDCLASS(Sun, DirectionalLight3D)

    private:
        float32 day_angle = -0.8f;
        float32 day_speed = 0.0001f;

    protected:
        static none _bind_methods();

    public:
        none _ready() override;
        none _process(float64 delta) override;
    };

    class CraftSky : public WorldEnvironment {
        GDCLASS(CraftSky, WorldEnvironment)

    private:
        Ref<Environment> environment;
        Ref<ProceduralSkyMaterial> sky_material;

    protected:
        static none _bind_methods();

    public:
        none _ready() override;
    };
}