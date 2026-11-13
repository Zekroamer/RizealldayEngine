#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

// Sibling order (pure): one number per object, saved with it, so a file can list objects in id order and the
// Hierarchy is rebuilt by sorting on it. Keys already in order are kept (the longest increasing run), and the
// others get a value between their neighbours (a fractional key), so moving one object changes only that object.
namespace SiblingOrder {

/// `current[i]` = the key the object at Hierarchy position i had (nullopt = no key yet). Returns strictly increasing keys.
inline std::vector<double> AssignKeys(const std::vector<std::optional<double>>& current)
{
	const size_t n = current.size();
	const auto known = [&](size_t i) { return current[i].has_value() && std::isfinite(*current[i]); };

	// Longest strictly increasing run of the known keys (patience sorting)
	std::vector<size_t> tails;
	std::vector<ptrdiff_t> previous(n, -1);
	for (size_t i = 0; i < n; ++i) {
		if (!known(i)) continue;
		const auto it = std::lower_bound(tails.begin(), tails.end(), *current[i], [&](size_t t, double v) { return *current[t] < v; });
		if (it != tails.end() && *current[*it] == *current[i]) continue; // a duplicate key: the earlier object keeps it
		if (it != tails.begin()) previous[i] = static_cast<ptrdiff_t>(*(it - 1));
		if (it == tails.end()) tails.push_back(i);
		else *it = i;
	}
	std::vector<bool> kept(n, false);
	for (ptrdiff_t i = tails.empty() ? -1 : static_cast<ptrdiff_t>(tails.back()); i >= 0; i = previous[static_cast<size_t>(i)]) kept[static_cast<size_t>(i)] = true;

	std::vector<double> keys(n, 0.0);
	std::optional<double> low;
	for (size_t i = 0; i < n;) {
		if (kept[i]) {
			keys[i] = *current[i];
			low = keys[i];
			++i;
			continue;
		}
		size_t end = i;
		while (end < n && !kept[end]) ++end;
		const double m = static_cast<double>(end - i);
		const std::optional<double> high = end < n ? current[end] : std::nullopt;
		for (size_t k = 1; i + k - 1 < end; ++k) {
			const double step = static_cast<double>(k);
			double v = step;
			if (low && high) v = (*low + m < *high) ? *low + step : *low + (*high - *low) * step / (m + 1.0); // whole numbers while they fit
			else if (low) v = *low + step;
			else if (high) v = *high - (m + 1.0 - step);
			keys[i + k - 1] = v;
		}
		i = end;
	}
	// Bisecting the same gap again and again runs out of precision: number them all again (rare)
	for (size_t j = 1; j < n; ++j) {
		if (!(keys[j - 1] < keys[j])) {
			for (size_t r = 0; r < n; ++r) keys[r] = static_cast<double>(r + 1);
			break;
		}
	}
	return keys;
}

} // namespace SiblingOrder
