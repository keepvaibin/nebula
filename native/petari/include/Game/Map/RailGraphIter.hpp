#pragma once

#include "Game/Map/RailGraph.hpp"
#include "Game/Map/RailGraphEdge.hpp"
#include "Game/Map/RailGraphNode.hpp"

class RailGraphIter {
public:
    RailGraphIter(const RailGraph*);

    RailGraphIter(const RailGraphIter& rIter) {
        *this = rIter;
    }

    void moveNodeNext();
    void setNode(s32);
    void watchStartEdge();
    void watchNextEdge();
    bool isWatchEndEdge() const;
    void selectEdge();
    void selectEdge(s32);
    bool isWatchedPrevEdge() const;
    bool isSelectedEdge() const;
    RailGraphNode* getCurrentNode() const;
    RailGraphNode* getNextNode() const;
    RailGraphNode* getWatchNode() const;
    RailGraphEdge* getCurrentEdge() const;
    RailGraphEdge* getWatchEdge() const;

    const RailGraph* mGraph;
    s32 _4;
    s32 mSelectedEdge;
    s32 mNextEdge;
    s32 _10;
};
