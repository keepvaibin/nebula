#pragma once

#include <nebula_types.h>

namespace MR {
    /// @brief The node a flow reference names, or none: a message's next node is cNoNode at the end of its flow. The cook
    /// refuses any other reference outside the file's nodes (crates/nebula-import cook/message.rs check_flow). N-3036:
    /// the console took the node at cNoNode, past the nodes, when a talk went on from a flow's last message.
    template < class Data >
    auto talkNodeOrEnd(const Data& rData, u32 index) -> decltype(rData.getNode(index)) {
        return index != Data::cNoNode ? rData.getNode(index) : nullptr;
    }

    /// @brief The node a branch of the flow names (an event's, a branch node's choice), or none: a branch may name no node
    /// (cNoNode). N-3036: the console took the node at cNoNode for a branch of none.
    template < class Data >
    auto talkBranchNodeOrEnd(const Data& rData, u32 branch) -> decltype(rData.getBranchNode(branch)) {
        return rData.isValidBranchNode(branch) ? rData.getBranchNode(branch) : nullptr;
    }
}  // namespace MR
