#include "animated_door.hpp"
#include "cutaway_building.hpp"
#include "game_sim.hpp"
#include "player_controller.hpp"
#include "tone_director.hpp"
#include "voxel_model.hpp"
#include "world_director.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

static void initialize_module(ModuleInitializationLevel level) {
	if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(GameSim);
	GDREGISTER_CLASS(VoxelModel);
	GDREGISTER_CLASS(CutawayBuilding);
	GDREGISTER_CLASS(AnimatedDoor);
	GDREGISTER_CLASS(WorldDirector);
	GDREGISTER_CLASS(PlayerController);
	GDREGISTER_CLASS(ToneDirector);
}

static void uninitialize_module(ModuleInitializationLevel level) {
	if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
GDExtensionBool GDE_EXPORT incel_library_init(GDExtensionInterfaceGetProcAddress get_proc_address, GDExtensionClassLibraryPtr library, GDExtensionInitialization *initialization) {
	GDExtensionBinding::InitObject init_obj(get_proc_address, library, initialization);
	init_obj.register_initializer(initialize_module);
	init_obj.register_terminator(uninitialize_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}
