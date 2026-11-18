// SPDX-License-Identifier: GPL-3.0-only
// MR::talkNodeOrEnd and MR::talkBranchNodeOrEnd (Game/NPC/TalkFlow.hpp): a flow's reference of none ends the flow.
// N-3036: TalkNodeCtrl took the node at cNoNode (past the nodes) from a flow's last message and for a branch of none.
// The data stands in for MessageData: three nodes, and branches to node 2 and to none.
#include "Game/NPC/TalkFlow.hpp"

#include <stdio.h>

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    struct Flow {
        static const u32 cNoNode = 0xFFFFFFFFu;
        int mNodes[3];
        u32 mBranches[2];

        int* getNode(u32 index) const {
            return const_cast< int* >(mNodes) + index;
        }
        int* getBranchNode(u32 branch) const {
            return getNode(mBranches[branch]);
        }
        bool isValidBranchNode(u32 branch) const {
            return mBranches[branch] != cNoNode;
        }
    };
}  // namespace

int main() {
    const Flow flow = {{0, 1, 2}, {2, Flow::cNoNode}};
    expect(MR::talkNodeOrEnd(flow, 1) == &flow.mNodes[1], "a message's next node");
    expect(MR::talkNodeOrEnd(flow, Flow::cNoNode) == nullptr, "a flow's last message goes on to no node");
    expect(MR::talkBranchNodeOrEnd(flow, 0) == &flow.mNodes[2], "a branch's node");
    expect(MR::talkBranchNodeOrEnd(flow, 1) == nullptr, "a branch of none is no node");
    if (sFailures == 0) {
        printf("native_talk_flow_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
