#pragma once
#include <string_view>

/// Engine release version (major.minor.patch). Bump it for every zip handed to the team;
/// the updater and project.json compare against it.
inline constexpr const char* kEngineVersion = "2.5.3";

/// Compares dotted version strings part by part as numbers ("2.10.0" > "2.9.0", "2.1" == "2.1.0").
/// A leading 'v' is ignored. Returns <0, 0 or >0 like strcmp.
inline int CompareEngineVersions(std::string_view a, std::string_view b)
{
	auto next = [](std::string_view& s) {
		if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) s.remove_prefix(1);
		int value = 0;
		while (!s.empty() && s.front() >= '0' && s.front() <= '9') {
			value = value * 10 + (s.front() - '0');
			s.remove_prefix(1);
		}
		while (!s.empty() && s.front() != '.') s.remove_prefix(1); // ignore suffixes such as "-beta"
		if (!s.empty()) s.remove_prefix(1);
		return value;
	};
	while (!a.empty() || !b.empty()) {
		const int x = next(a);
		const int y = next(b);
		if (x != y) return x < y ? -1 : 1;
	}
	return 0;
}
