extends Button

func _ready() -> void:
	button_up.connect(_on_release)

func _on_release():
	Global.go_to("res://scenes/world_list.tscn")
