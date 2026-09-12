extends Button

@onready var world = $"../../Main"

func _ready() -> void:
	button_up.connect(_on_release)

func _on_release():
	world.resume()
