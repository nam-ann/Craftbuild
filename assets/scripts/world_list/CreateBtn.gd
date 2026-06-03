extends Button

func _ready() -> void:
	button_up.connect(_on_pressed)

func _on_pressed():
	Global.go_to("res://scenes/create_world.tscn")
