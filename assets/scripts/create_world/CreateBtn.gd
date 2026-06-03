extends Button

func _ready() -> void:
	button_up.connect(_on_pressed)
	var dir = DirAccess.open(Global.world_saves)
	if dir == null: return
		
	dir.list_dir_begin()
	var folder_name = dir.get_next()
	var i = 1

	while folder_name != "":
		if (folder_name.contains("My World")): i += 1
		folder_name = dir.get_next()
	dir.list_dir_end()
	
	if (i > 1): $"../WorldName".text = "My World " + str(i)

func _on_pressed():
	if not check_name(Global.world_name): return
	
	Global.world_name = $"../WorldName".text
	Global.world_seed = int($"../Seed".text)
	Global.go_to("res://scenes/game.tscn")
	
func check_name(world_name: String) -> bool:
	var targets = ["\\", "/", ":", "*", "?", "\"", "<", ">", "|"]

	for t in targets:
		if world_name.contains(t):
			return false
	
	var dir = DirAccess.open(world_name)
	return dir != null
