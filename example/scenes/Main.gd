extends Node
## Entry point. Default: feature demo. `--controller-test`: raw readout. `--selftest`: IMU self-test.

const SCENES := {
	"--controller-test": "res://scenes/ControllerTest.tscn",
	"--selftest": "res://scenes/SelfTest.tscn",
}
const DEFAULT_SCENE := "res://scenes/FeatureDemo.tscn"


func _ready() -> void:
	var args := OS.get_cmdline_args() + OS.get_cmdline_user_args()
	var scene := DEFAULT_SCENE
	for arg in SCENES:
		if args.has(arg):
			scene = SCENES[arg]
	get_tree().change_scene_to_file.call_deferred(scene)
