extends Label

@onready var main: Main = $"../../Main"

func _process(delta: float) -> void:
	var ppos: Vector3 = main.get_player_position()
	ppos += Vector3(0.5, -0.901, -0.5)
	text = "Player pos: (x: " + "%.3f" % ppos.x + ", y: " + "%.3f" % ppos.y + ", z: " + "%.3f" % ppos.z + ")";
