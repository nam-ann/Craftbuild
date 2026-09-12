extends VBoxContainer

var game: String = "res://scenes/game.tscn"

func _ready() -> void:
	load_folders()
	
func create_button(world_name: String) -> void:
	var btn = Button.new()
	btn.add_theme_font_size_override("font_size", 20)
	btn.text = world_name
	btn.pressed.connect(func() -> void:
		Global.game_type = Global.GameType.SINGLEPLAYER
		Global.world_name = world_name
		Global.go_to(game)
	)
	add_child(btn)

func load_folders() -> void:
	var dir = DirAccess.open(Global.world_saves)
	if dir == null:
		Global.teleport_to("res://scenes/create_world.tscn")
		return

	dir.list_dir_begin()
	var folder_name = dir.get_next()
	if folder_name == "": Global.teleport_to(game)
	
	while folder_name != "":
		if dir.current_is_dir() and folder_name != "." and folder_name != "..":
			create_button(folder_name)
		folder_name = dir.get_next()
	dir.list_dir_end()
