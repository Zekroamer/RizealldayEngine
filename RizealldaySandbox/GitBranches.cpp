#include "GitBranches.h"
#include <algorithm>
#include <sstream>

namespace GitBranches {

List Parse(const std::string& output)
{
	List list;
	std::vector<std::string> remotes;
	std::istringstream lines(output);
	std::string line;
	while (std::getline(lines, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		const bool isCurrent = line[0] == '*';
		const std::string ref = line.substr(1);
		if (ref.starts_with("refs/heads/")) {
			list.local.push_back(ref.substr(11));
			if (isCurrent) list.current = list.local.back();
		} else if (ref.starts_with("refs/remotes/")) {
			const std::string name = ref.substr(13);
			if (name.ends_with("/HEAD")) continue; // origin/HEAD only points at another remote branch
			remotes.push_back(name);
		}
		// anything else ("(HEAD detached at ...)") is not a branch to switch to
	}
	for (const std::string& remote : remotes) {
		const std::string local = LocalNameOf(remote);
		const auto taken = [&](const std::vector<std::string>& names, const std::string& n, bool viaLocalName) {
			return std::any_of(names.begin(), names.end(), [&](const std::string& x) { return (viaLocalName ? LocalNameOf(x) : x) == n; });
		};
		// Switching to a local name that exists already (locally or on another remote) is not a new tracking branch
		if (local.empty() || taken(list.local, local, false) || taken(list.remoteOnly, local, true)) continue;
		list.remoteOnly.push_back(remote);
	}
	return list;
}

std::string LocalNameOf(const std::string& remoteBranch)
{
	const size_t slash = remoteBranch.find('/');
	return slash == std::string::npos ? std::string{} : remoteBranch.substr(slash + 1);
}

}
