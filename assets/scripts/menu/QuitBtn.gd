extends Button

func _ready() -> void:
	button_up.connect(_on_release)

func _on_release():
	get_tree().quit()
