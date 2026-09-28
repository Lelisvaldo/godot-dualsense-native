extends RefCounted
## Base class for tests: each `test_*.gd` extends this and defines `test_*` methods.
## Assertions record failures instead of aborting, so every failure is reported.

var _failures := PackedStringArray()


func reset() -> void:
	_failures.clear()


func get_failures() -> PackedStringArray:
	return _failures


func assert_true(value: bool, message := "expected true") -> void:
	if not value:
		_failures.append(message)


func assert_eq(actual: Variant, expected: Variant, message := "") -> void:
	if actual != expected:
		_failures.append("%s: expected=%s got=%s" % [message, str(expected), str(actual)])
