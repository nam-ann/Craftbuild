extends Button

func _ready() -> void:
	button_up.connect(_on_pressed)

func _on_pressed():
	get_tree().quit()
