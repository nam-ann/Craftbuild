extends Button

func _ready() -> void:
	pressed.connect(_on_release)

func _on_release():
	Global.go_to("res://scenes/setting.tscn")
