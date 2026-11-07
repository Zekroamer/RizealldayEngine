#pragma once
#include "NodeGraph.h"
#include <optional>
#include <string>
#include <vector>

// What the graph editor window does to a graph, without ImGui (testable): undo / redo, adding a node already
// connected to the pin a link was dragged from, duplicating, deleting (the output node is protected),
// searching the node menu, and the starting templates.

class GraphEditorModel {
public:
	NodeGraph graph;
	std::vector<uint32_t> selection;
	bool dirty = false; // changed since the last save

	/// Starts editing `g`: empty history, nothing selected, not dirty
	void Reset(const NodeGraph& g);
	/// Records the current graph as one undo step (call after a change: a drag ends, a value is set)
	void Commit();
	bool Undo();
	bool Redo();
	bool CanUndo() const { return current_ > 0; }
	bool CanRedo() const { return current_ + 1 < history_.size(); }
	void MarkSaved() { dirty = false; }
	/// Changes with every recorded step, undo and redo (the window regenerates its code when it changes)
	uint64_t Revision() const { return revision_; }

	uint32_t FindOutputNode() const;
	uint32_t AddNode(const std::string& type, float x, float y);
	/// A link was dragged from a pin and dropped on empty space, and `type` was picked: the new node is
	/// connected through its first pin that fits. `fromIsOutput` = the drag started at an output. 0 + why when nothing fits.
	uint32_t AddNodeConnected(const std::string& type, float x, float y, uint32_t fromNode, const std::string& fromPin, bool fromIsOutput, std::string* why);
	bool Connect(uint32_t fromNode, const std::string& fromPin, uint32_t toNode, const std::string& toPin, std::string* why);
	void Disconnect(uint32_t toNode, const std::string& toPin);
	/// Deletes the selected nodes (never the output node) and their links
	void DeleteSelection();
	/// Copies of the selected nodes with the links between them, placed next to them; they become the selection
	std::vector<uint32_t> Duplicate();

private:
	std::vector<std::string> history_; // graph JSON per step
	size_t current_ = 0;
	uint64_t revision_ = 0;
};

/// While a link is dragged, the node menu only offers nodes that can take part in it
struct NodeSearchFilter {
	std::optional<PinType> fromOutput; // the node needs an input that takes this
	std::optional<PinType> intoInput;  // the node needs an output that fits this
};
/// Nodes of `domain` matching `query` (every word in the name, category or description; names first). Output nodes are left out.
std::vector<const NodeDef*> SearchNodes(const NodeLibrary& library, const std::string& query, uint32_t domain, const NodeSearchFilter& filter = {});

/// Starting points of a new material ("Blank" = only the output)
inline constexpr const char* kMaterialTemplates[] = { "Soft Glow", "Scrolling Fire", "Dissolve", "Flipbook Smoke", "Energy Ring", "Blank" };
NodeGraph MakeMaterialTemplate(const std::string& name);
/// Starting points of a new particle graph ("Blank" = only Spawn Output and Update Output)
inline constexpr const char* kParticleTemplates[] = { "Fountain", "Magic Swirl", "Burst", "Rising Embers", "Blank" };
NodeGraph MakeParticleTemplate(const std::string& name);
