extends Main

@onready var pause_btn = $ResumeBtn
@onready var leave_btn = $LeaveBtn
@onready var setting_btn = $SettingBtn
@onready var chat_box = $ChatBox
@onready var output = $Output

var pausing = true
var chat_history = [""]
var current_history = 0

func _ready() -> void:
	chat_output.connect(_on_chat_output)
	chat_box.visible = false
	init()
	set_seed_and_world_name(Global.world_seed, Global.world_name)
	set_render_distance(int(Global.render_distance))

func _input(_event):
	if Input.is_action_just_pressed("enter"):
		chat_box.visible = false
		chat(chat_box.text)
		chat_history.insert(1, chat_box.text)
		current_history = 0
		
	if Input.is_action_just_pressed("view_history"):
		if not chat_box.visible: return
		if Input.is_key_pressed(KEY_UP):
			if current_history >= len(chat_history) - 1: return
			current_history += 1
		else:
			if current_history <= 0: return
			current_history -= 1
		chat_box.text = chat_history[current_history]
		chat_box.set_caret_column(len(chat_history[current_history]))
	
	if Input.is_action_just_pressed("open_chat") and not pausing:
		if chat_box.visible: return
		
		chat_box.text = ""
		if Input.is_key_pressed(KEY_SLASH):
			chat_box.text = "/"
			chat_box.set_caret_column(1)
		chat_box.visible = true
		chat_box.grab_focus()
		start_chat()
		get_viewport().set_input_as_handled()
		
	if Input.is_action_just_pressed("ui_cancel"):
		if chat_box.visible:
			chat_box.visible = false
			chat("")
			current_history = 0
		elif not pausing: pause()
		else: resume()

func _notification(what: int) -> void:
	if (what == NOTIFICATION_APPLICATION_FOCUS_OUT):
		if not chat_box.visible: pause()

func _on_chat_output(msg: String):
	output.text += msg + "\n"

func pause():
	pause_btn.visible = true
	leave_btn.visible = true
	setting_btn.visible = true
	pausing = true
	pause_game()

func resume():
	pause_btn.visible = false
	leave_btn.visible = false
	setting_btn.visible = false
	pausing = false
	resume_game()
