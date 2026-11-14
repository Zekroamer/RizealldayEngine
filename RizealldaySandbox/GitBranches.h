#pragma once
#include <string>
#include <vector>

/// Branch list for the editor's branch menu (EditorSuite_GitBranch.cpp). Pure: no git process here.
namespace GitBranches {
	/// The command whose output Parse reads: one "<*| ><full ref name>" line per branch
	inline constexpr const wchar_t* kListCommand = L"git branch -a --format=%(HEAD)%(refname)";

	struct List {
		std::string current;                 ///< empty when HEAD is detached
		std::vector<std::string> local;      ///< in git's order (sorted by name)
		std::vector<std::string> remoteOnly; ///< "origin/x" for each remote branch without a local branch "x"
	};

	/// Remote HEAD pointers (origin/HEAD) and remote branches that have a local branch of the same name are dropped
	List Parse(const std::string& output);

	/// "origin/feature/x" -> "feature/x": the name `git switch` takes to create the local tracking branch
	std::string LocalNameOf(const std::string& remoteBranch);
}
