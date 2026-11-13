#pragma once
#include <nlohmann/json.hpp>
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "math/Vector4.h"
#include "math/Quaternion.h"
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

/// JSON helpers for editor files (scenes, project settings, script fields).
/// ordered_json keeps keys in insertion order so saved files stay readable and diff-friendly.
namespace SceneJson {

using Json = nlohmann::ordered_json;

/// Shortest decimal form of a float stored as a double, so 0.15f is written as 0.15
/// rather than 0.15000000596046448. Non-finite values are written as 0 (JSON has no NaN/Inf).
inline double Num(float v)
{
	if (!std::isfinite(v)) return 0.0;
	char buf[32];
	const auto res = std::to_chars(buf, buf + sizeof(buf), v);
	*res.ptr = '\0';
	return std::strtod(buf, nullptr);
}

inline Json Arr(const Vector2& v) { return Json::array({ Num(v.x), Num(v.y) }); }
inline Json Arr(const Vector3& v) { return Json::array({ Num(v.x), Num(v.y), Num(v.z) }); }
inline Json Arr(const Vector4& v) { return Json::array({ Num(v.x), Num(v.y), Num(v.z), Num(v.w) }); }
inline Json Arr(const Quaternion& q) { return Json::array({ Num(q.x), Num(q.y), Num(q.z), Num(q.w) }); }

// ---- Reading: every Read() leaves `out` untouched when the key is missing or has the wrong type,
// ---- so older files that lack newer fields keep the C++ default values.

inline const Json* Find(const Json& obj, const char* key)
{
	if (!obj.is_object()) return nullptr;
	const auto it = obj.find(key);
	return (it != obj.end()) ? &*it : nullptr;
}

inline bool Read(const Json& obj, const char* key, bool& out)
{
	const Json* v = Find(obj, key);
	if (!v) return false;
	if (v->is_boolean()) { out = v->get<bool>(); return true; }
	if (v->is_number()) { out = v->get<double>() != 0.0; return true; } // legacy 0/1
	return false;
}

inline bool Read(const Json& obj, const char* key, int& out)
{
	const Json* v = Find(obj, key);
	if (!v || !v->is_number()) return false;
	out = static_cast<int>(v->get<double>());
	return true;
}

inline bool Read(const Json& obj, const char* key, uint32_t& out)
{
	const Json* v = Find(obj, key);
	if (!v || !v->is_number()) return false;
	const double d = v->get<double>();
	if (d < 0.0) return false;
	out = static_cast<uint32_t>(d);
	return true;
}

inline bool Read(const Json& obj, const char* key, float& out)
{
	const Json* v = Find(obj, key);
	if (!v || !v->is_number()) return false;
	out = static_cast<float>(v->get<double>());
	return true;
}

inline bool Read(const Json& obj, const char* key, std::string& out)
{
	const Json* v = Find(obj, key);
	if (!v || !v->is_string()) return false;
	out = v->get<std::string>();
	return true;
}

/// Reads up to `count` numbers from an array into dst; extra/missing elements are ignored.
inline bool ReadFloats(const Json& obj, const char* key, float* dst, size_t count)
{
	const Json* v = Find(obj, key);
	if (!v || !v->is_array()) return false;
	for (size_t i = 0; i < count && i < v->size(); ++i) {
		if ((*v)[i].is_number()) dst[i] = static_cast<float>((*v)[i].get<double>());
	}
	return true;
}

inline bool Read(const Json& obj, const char* key, Vector2& out)
{
	float f[2] = { out.x, out.y };
	if (!ReadFloats(obj, key, f, 2)) return false;
	out = { f[0], f[1] };
	return true;
}
inline bool Read(const Json& obj, const char* key, Vector3& out)
{
	float f[3] = { out.x, out.y, out.z };
	if (!ReadFloats(obj, key, f, 3)) return false;
	out = { f[0], f[1], f[2] };
	return true;
}
inline bool Read(const Json& obj, const char* key, Vector4& out)
{
	float f[4] = { out.x, out.y, out.z, out.w };
	if (!ReadFloats(obj, key, f, 4)) return false;
	out = { f[0], f[1], f[2], f[3] };
	return true;
}
inline bool Read(const Json& obj, const char* key, Quaternion& out)
{
	float f[4] = { out.x, out.y, out.z, out.w };
	if (!ReadFloats(obj, key, f, 4)) return false;
	out = { f[0], f[1], f[2], f[3] };
	return true;
}

/// Enum stored as an int.
template <typename E>
inline bool ReadEnum(const Json& obj, const char* key, E& out)
{
	int v = 0;
	if (!Read(obj, key, v)) return false;
	out = static_cast<E>(v);
	return true;
}

/// Older scene files were written without escaping, so Windows paths such as "D:\Projects\a.png"
/// contain invalid escapes. Turn every backslash that is not a valid JSON escape into '/'.
inline std::string RepairLegacyBackslashes(std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if (c == '\\' && i + 1 < text.size()) {
			const char n = text[i + 1];
			if (n == '"' || n == '\\') { out.push_back(c); out.push_back(n); ++i; continue; }
			out.push_back('/');
			continue;
		}
		out.push_back(c);
	}
	return out;
}

/// Parses editor JSON. Retries once with legacy backslash repair. Returns nullopt on failure
/// and fills `error` with the parser message.
inline std::optional<Json> Parse(std::string_view text, std::string* error = nullptr)
{
	Json result = Json::parse(text, nullptr, false);
	if (!result.is_discarded()) return result;
	result = Json::parse(RepairLegacyBackslashes(text), nullptr, false);
	if (!result.is_discarded()) return result;
	try {
		(void)Json::parse(text); // re-run with exceptions only to obtain a readable error message
	} catch (const Json::parse_error& e) {
		if (error) *error = e.what();
	}
	return std::nullopt;
}

/// Copy of the object `o` with `key` = `value` placed right after `afterKey` (at the end when it is missing);
/// ordered_json has no insert-at-position
inline Json WithKeyAfter(const Json& o, const char* afterKey, const char* key, const Json& value)
{
	Json out = Json::object();
	for (auto it = o.begin(); it != o.end(); ++it) {
		if (it.key() == key) continue;
		out[it.key()] = it.value();
		if (it.key() == afterKey) out[key] = value;
	}
	if (!out.contains(key)) out[key] = value;
	return out;
}

/// dump(2) puts every array element on its own line. Collapse arrays that contain only numbers
/// onto one line ("translation": [0, 1.5, -8]) so scene files stay compact and readable.
inline std::string DumpCompact(const Json& j)
{
	// error_handler_t::replace: paths from std::filesystem::path::string() use the ANSI code page
	// (e.g. CP932 Japanese folder names), which is not valid UTF-8 and would otherwise throw on save
	const std::string src = j.dump(2, ' ', false, Json::error_handler_t::replace);
	std::string out;
	out.reserve(src.size());
	bool inString = false;
	for (size_t i = 0; i < src.size(); ++i) {
		const char c = src[i];
		if (inString) {
			out.push_back(c);
			if (c == '\\' && i + 1 < src.size()) { out.push_back(src[++i]); continue; }
			if (c == '"') inString = false;
			continue;
		}
		if (c == '"') { inString = true; out.push_back(c); continue; }
		if (c == '[') {
			// Look ahead: only numbers, commas and whitespace until the matching ']'
			size_t k = i + 1;
			bool numeric = true;
			bool hasValue = false;
			for (; k < src.size() && src[k] != ']'; ++k) {
				const char d = src[k];
				if ((d >= '0' && d <= '9') || d == '-' || d == '+' || d == '.' || d == 'e' || d == 'E') { hasValue = true; continue; }
				if (d == ',' || d == ' ' || d == '\n' || d == '\r') continue;
				numeric = false;
				break;
			}
			if (numeric && hasValue && k < src.size()) {
				out.push_back('[');
				bool pendingSpace = false;
				for (size_t m = i + 1; m < k; ++m) {
					const char d = src[m];
					if (d == ' ' || d == '\n' || d == '\r') continue;
					if (pendingSpace) { out.push_back(' '); pendingSpace = false; }
					out.push_back(d);
					if (d == ',') pendingSpace = true;
				}
				out.push_back(']');
				i = k;
				continue;
			}
		}
		out.push_back(c);
	}
	out.push_back('\n');
	return out;
}

} // namespace SceneJson
