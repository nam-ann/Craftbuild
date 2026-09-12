extends Button

func _ready() -> void:
	pressed.connect(_on_release)

func _on_release():
	Global.ret_last_scene()
