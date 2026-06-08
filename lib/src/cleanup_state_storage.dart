abstract class CleanupStateStorage {
  Future<Map<String, dynamic>?> load(String key);

  Future<void> save(String key, Map<String, dynamic> value);

  Future<void> delete(String key);
}

class MemoryCleanupStateStorage implements CleanupStateStorage {
  final Map<String, Map<String, dynamic>> _values = {};

  @override
  Future<Map<String, dynamic>?> load(String key) async {
    final value = _values[key];
    if (value == null) return null;
    return _deepCopyMap(value);
  }

  @override
  Future<void> save(String key, Map<String, dynamic> value) async {
    _values[key] = _deepCopyMap(value);
  }

  @override
  Future<void> delete(String key) async {
    _values.remove(key);
  }

  Map<String, dynamic> _deepCopyMap(Map<String, dynamic> value) {
    return value.map((key, value) => MapEntry(key, _deepCopyValue(value)));
  }

  Object? _deepCopyValue(Object? value) {
    if (value is Map) {
      return value.map(
        (key, value) => MapEntry(key.toString(), _deepCopyValue(value)),
      );
    }
    if (value is List) {
      return value.map(_deepCopyValue).toList();
    }
    return value;
  }
}
