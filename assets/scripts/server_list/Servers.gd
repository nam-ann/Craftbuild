extends VBoxContainer

var game: String = "res://scenes/game.tscn"

func _ready() -> void:
	read_json()
	
func create_button(server_name: String, server_socket: String) -> void:
	var btn: Button = Button.new()
	
	btn.add_theme_font_size_override("font_size", 20)
	btn.text = server_name
	btn.pressed.connect(func() -> void:
		Global.game_type = Global.GameType.MULTIPLAYER
		Global.server_socket = server_socket
		Global.go_to(game)
	)
	add_child(btn)

func read_json() -> void:
	var file = FileAccess.open(Global.server_list, FileAccess.READ)
	
	if not file:
		Global.teleport_to("res://scenes/add_server.tscn")
		return
		
	var json_string = file.get_as_text()
	file.close()
	
	var data: Dictionary = JSON.parse_string(json_string)
	for k in data: create_button(k, data[k])
