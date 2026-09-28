extends SceneTree
## Minimal test runner. Usage (from the repository root):
##   godot --headless --path example --script ../tests/run_tests.gd
## Runs every `test_*` method of every `tests/test_*.gd` file; exits with 1 on any failure.

func _initialize() -> void:
	var dir := ProjectSettings.globalize_path("res://").path_join("../tests")
	var passed := 0
	var failed := 0
	for file in DirAccess.get_files_at(dir):
		if not (file.begins_with("test_") and file.ends_with(".gd")):
			continue
		var script: GDScript = load(dir.path_join(file))
		var suite: Object = script.new()
		for m in suite.get_method_list():
			var name: String = m["name"]
			if not name.begins_with("test_"):
				continue
			suite.reset()
			suite.call(name)
			var failures: PackedStringArray = suite.get_failures()
			if failures.is_empty():
				passed += 1
				print("ok    %s::%s" % [file, name])
			else:
				failed += 1
				for f in failures:
					print("FAIL  %s::%s - %s" % [file, name, f])
	print("%d passed, %d failed" % [passed, failed])
	quit(1 if failed > 0 else 0)
