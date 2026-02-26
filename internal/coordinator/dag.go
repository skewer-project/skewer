package coordinator

import "errors"

type Node interface {
	ID() string
}

type DAG struct {
	nodes map[string]Node
	deps  map[string][]string
}

func NewDAG() *DAG {
	return &DAG{
		nodes: make(map[string]Node),
		deps:  make(map[string][]string),
	}
}

func (d *DAG) GetNode(nodeID string) (Node, error) {
	node, exists := d.nodes[nodeID]
	if !exists {
		return nil, errors.New("[ERROR] Node not found")
	}
	return node, nil
}

func (d *DAG) AddNode(node Node) {
	d.nodes[node.ID()] = node
}

// Add a dependency where "from" depends on "to"
func (d *DAG) AddDependency(from, to Node) {
	d.deps[from.ID()] = append(d.deps[from.ID()], to.ID())
}

func (d *DAG) TopologicalSort() ([]Node, error) {
	nodeDegrees := make(map[string]uint) // number of dependencies for a given node id

	// Initialize all node degrees
	for nodeId := range d.nodes {
		nodeDegrees[nodeId] = uint(len(d.deps[nodeId]))
	}

	// Populate queue with independent nodes (no deps)
	queue := []string{} // ids of all nodes with no incoming edges
	for nodeId, degree := range nodeDegrees {
		if degree == 0 {
			queue = append(queue, nodeId)
		}
	}

	// Organize nodes in topological order
	sortedOrder := []Node{}
	for len(queue) > 0 {
		current, queue := queue[0], queue[1:]
		sortedOrder = append(sortedOrder, d.nodes[current])

		// Decrement node degrees and enqueue nodes with no incoming edges
		for dependentNodeId, depList := range d.deps {
			for _, depId := range depList {
				if depId == current {
					nodeDegrees[dependentNodeId]--

					// If all deps fulfilled then just enqueue node
					if nodeDegrees[dependentNodeId] == 0 {
						queue = append(queue, dependentNodeId)
					}
				}
			}
		}
	}

	// Check if all tasks are included. If not, there's a cycle.
	if len(sortedOrder) != len(d.nodes) {
		return nil, errors.New("[SERVER] Cycle detected in DAG or missing dependencies")
	}

	return sortedOrder, nil
}
