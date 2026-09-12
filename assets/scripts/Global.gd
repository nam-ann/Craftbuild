extends Node

enum GameType { SINGLEPLAYER, MULTIPLAYER }
var game_type: GameType

var world_name: String
var world_seed: int
var server_socket: String
var render_distance: int = 32
var sensitivity: float = 0.0043

const world_saves: String = "user://game/saves"
const server_list: String = "user://game/server_list.json"

var stack_scene: Array[String] = []

func go_to(path: String):
	stack_scene.push_back(get_tree().current_scene.scene_file_path)
	get_tree().change_scene_to_file(path)
	
func teleport_to(path: String):
	get_tree().change_scene_to_file(path)

func ret_last_scene():
	if stack_scene.is_empty(): return
	get_tree().change_scene_to_file(stack_scene.pop_back())
