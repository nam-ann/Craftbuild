#include <defs.hpp>

NO_WARNING
#include <godot_cpp/classes/engine.hpp>
DO_WARNING

import misc.pos;
import misc.number;
import misc.format;
import game.core;
import game.environment;
import game.main;
import game.server;
import game.player;
import game.logger;
import game.thread;

using namespace godot;
using namespace craftbuild;

void initialize_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;

    log<LogType::VERBOSE>("Hello from the DLL!");
    log<LogType::VERBOSE>(format{} << "Game version: " << full_version);

    ClassDB::register_class<Main>();
    ClassDB::register_class<Player>();
    ClassDB::register_class<Sun>();
    ClassDB::register_class<CraftSky>();
}

void initialize_server(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;

	log<LogType::VERBOSE>("Hello from the Server DLL!");
	log<LogType::VERBOSE>(format{} << "Game version: " << full_version);

	ClassDB::register_class<Server>();
}

void uninitialize_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
}

void uninitialize_server(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
}

extern "C" {
    GDExtensionBool __declspec(dllexport) craftbuild_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization* r_initialization) {
        ThreadRegistry::register_thread("Main");

        GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.register_initializer(initialize_module);
        init_obj.register_terminator(uninitialize_module);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
    GDExtensionBool __declspec(dllexport) craftbuild_server(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization* r_initialization) {
        ThreadRegistry::register_thread("Main");

        GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.register_initializer(initialize_server);
        init_obj.register_terminator(uninitialize_server);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
}