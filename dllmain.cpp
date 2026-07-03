#pragma warning(push, 0)
#include <windows.h>
#include <godot_cpp/classes/engine.hpp>
#pragma warning(pop)

#include <includes.hpp>

import misc.pos;
import misc.number;
import misc.format;
import game.core;
import game.environment;
import game.main;
import game.server_ptr;
import game.player;
import game.logger;
import game.thread;

using namespace godot;
using namespace craftbuild;

none initialize_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;

    log<LogType::VERBOSE>(format{} << "Hello from the DLL! Process ID: " << (uint32)GetCurrentProcessId() << ", Thread ID: " << (uint32)GetCurrentThreadId());
    log<LogType::VERBOSE>(format{} << "Game version: " << full_version);

    ClassDB::register_class<Main>();
    ClassDB::register_class<Player>();
    ClassDB::register_class<Sun>();
    ClassDB::register_class<CraftSky>();
}

none initialize_server(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;

	log<LogType::VERBOSE>(format{} << "Hello from the Server DLL! Process ID: " << (uint32)GetCurrentProcessId() << ", Thread ID: " << (uint32)GetCurrentThreadId());
	log<LogType::VERBOSE>(format{} << "Game version: " << full_version);

	ClassDB::register_class<Server>();
}

none uninitialize_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
}

none uninitialize_server(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
}

extern "C" {
    GDExtensionBool __declspec(dllexport) craftbuild_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization* r_initialization) {
        ThreadRegistry::register_thread("Main Thread");

        GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.register_initializer(initialize_module);
        init_obj.register_terminator(uninitialize_module);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
    GDExtensionBool __declspec(dllexport) craftbuild_server(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization* r_initialization) {
        ThreadRegistry::register_thread("Main Thread");

        GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.register_initializer(initialize_server);
        init_obj.register_terminator(uninitialize_server);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
}