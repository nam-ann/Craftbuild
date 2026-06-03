extends Button

@onready var world = $"../../Main"

func _ready() -> void:
	button_up.connect(_on_resume)

func _on_resume():
	world.resume()
