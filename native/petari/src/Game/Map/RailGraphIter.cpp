#include "Game/Map/RailGraphIter.hpp"

RailGraphIter::RailGraphIter(const RailGraph* pGraph) {
    mGraph = pGraph;
    mCurrentNode = -1;
    mSelectedEdge = -1;
    mNextEdge = -1;
    mPrevNode = -1;
}

void RailGraphIter::moveNodeNext() {
    s32 selected_edge = mSelectedEdge;
    mPrevNode = mCurrentNode;
    RailGraphEdge* edge = mGraph->getEdge(selected_edge);
    s32 next_node = edge->getNextNode(mPrevNode);
    mCurrentNode = next_node;
}

void RailGraphIter::setNode(s32 nodeIndex) {
    mCurrentNode = nodeIndex;
    mPrevNode = -1;
    mSelectedEdge = -1;
    mNextEdge = -1;
}

void RailGraphIter::watchStartEdge() {
    RailGraphNode* node = mGraph->getNode(mCurrentNode);
    mNextEdge = node->mFirstEdge;
}

void RailGraphIter::watchNextEdge() {
    RailGraphEdge* edge = mGraph->getEdge(mNextEdge);
    s32 next_edge = edge->getNextEdge(mCurrentNode);
    mNextEdge = next_edge;
}

bool RailGraphIter::isWatchEndEdge() const {
    return mNextEdge == -1;
}

void RailGraphIter::selectEdge() {
    mSelectedEdge = mNextEdge;
}

void RailGraphIter::selectEdge(s32 edge) {
    mSelectedEdge = edge;
}

bool RailGraphIter::isWatchedPrevEdge() const {
    RailGraphEdge* edge = mGraph->getEdge(mNextEdge);
    return edge->getNextNode(mCurrentNode) == mPrevNode;
}

bool RailGraphIter::isSelectedEdge() const {
    return mGraph->isValidEdge(mSelectedEdge);
}

RailGraphNode* RailGraphIter::getCurrentNode() const {
    return mGraph->getNode(mCurrentNode);
}

RailGraphNode* RailGraphIter::getNextNode() const {
    RailGraphEdge* edge = mGraph->getEdge(mSelectedEdge);
    s32 next = edge->getNextNode(mCurrentNode);
    return mGraph->getNode(next);
}

RailGraphNode* RailGraphIter::getWatchNode() const {
    RailGraphEdge* edge = mGraph->getEdge(mNextEdge);
    s32 next_node = edge->getNextNode(mCurrentNode);
    return mGraph->getNode(next_node);
}

RailGraphEdge* RailGraphIter::getCurrentEdge() const {
    return mGraph->getEdge(mSelectedEdge);
}

RailGraphEdge* RailGraphIter::getWatchEdge() const {
    return mGraph->getEdge(mNextEdge);
}