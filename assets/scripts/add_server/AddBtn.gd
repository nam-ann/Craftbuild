extends Button

func _ready() -> void:
	button_up.connect(_on_release)

func _on_release() -> void:
	var server_name: String = $"../ServerName".text
	var server_socket: String = $"../IP".text
	
	if not server_socket.contains(":"): return
	
	var data: Dictionary[String, String]
	var file = FileAccess.open(Global.server_list, FileAccess.READ)
	if file:
		var json_string = file.get_as_text()
		file.close()
		
		data = JSON.parse_string(json_string)
		
	data[server_name] = server_socket
		
	file = FileAccess.open(Global.server_list, FileAccess.WRITE)
	if file:
		file.seek_end()
		
		var json_string: String = JSON.stringify(data, "\t")
		file.store_string(json_string)
		file.close()
	
	Global.teleport_to("res://scenes/server_list.tscn")
