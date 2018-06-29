#ifndef TREE_REFERENCE_H
#define TREE_REFERENCE_H

#include "tree_pool.h"
#include <stdio.h>

static inline int min(int i, int j) { return i < j ? i : j; }
static inline int max(int i, int j) { return i > j ? i : j; }

class Cursor;

template <typename T>
class TreeReference {
  friend class Cursor;
  template <typename U>
  friend class TreeReference;
  template <typename U>
  friend class ExpressionReference;
  template <typename U>
  friend class LayoutReference;
public:
  TreeReference(const TreeReference & tr) { setTo(tr); }
  TreeReference(TreeReference&& tr) { setTo(tr); }
  TreeReference& operator=(const TreeReference& tr) {
    setTo(tr);
    return *this;
  }
  TreeReference& operator=(TreeReference&& tr) {
    setTo(tr);
    return *this;
  }

  inline bool operator==(TreeReference<TreeNode> t) { return m_identifier == t.identifier(); }

  void setTo(const TreeReference & tr) {
    setIdentifierAndRetain(tr.identifier());
  }

  TreeReference<T> clone() const {
    TreeNode * myNode = node();
    if (myNode->isAllocationFailure()) {
      int allocationFailureNodeId = myNode->allocationFailureNodeIdentifier();
      return TreeReference<T>(TreePool::sharedPool()->node(allocationFailureNodeId));
    }
    TreeNode * nodeCopy = TreePool::sharedPool()->deepCopy(myNode);
    return TreeReference<T>(nodeCopy);
  }

  ~TreeReference() {
    if (m_identifier >= 0) {
      assert(node());
      assert(node()->identifier() == m_identifier);
      node()->release();
    }
  }

  bool isDefined() const { return m_identifier >= 0 && node() != nullptr; }
  bool isAllocationFailure() const { return node()->isAllocationFailure(); }

  int nodeRetainCount() const { return node()->retainCount(); }
  void incrementNumberOfChildren() { return node()->incrementNumberOfChildren(); }
  void decrementNumberOfChildren() { return node()->decrementNumberOfChildren(); }

  operator TreeReference<TreeNode>() const {
    return TreeReference<TreeNode>(this->node());
  }

  T * castedNode() const {
    // TODO: Here, assert that the node type is indeed T
    // ?? Might be allocation failure, not T
    return static_cast<T*>(TreePool::sharedPool()->node(m_identifier));
  }

  TreeNode * node() const {
    return TreePool::sharedPool()->node(m_identifier);
  }

  int identifier() const { return m_identifier; }

  // Hierarchy
  int numberOfChildren() const {
    return node()->numberOfChildren();
  }

  TreeReference<T> parent() const {
    return TreeReference(node()->parentTree());
  }

  TreeReference<T> treeChildAtIndex(int i) const {
    return TreeReference(node()->childTreeAtIndex(i));
  }

  // Hierarchy operations

  void addChild(TreeReference<TreeNode> t) {
    return addChildAtIndex(t, 0);
  }

  void addChildAtIndex(TreeReference<TreeNode> t, int index) {
    if (node()->isAllocationFailure()) {
      return;
    }
    // TODO detach t
    assert(index >= 0 && index <= numberOfChildren());
    t.node()->retain();
    TreeNode * newChildPosition = node()->next();
    for (int i = 0; i < index; i++) {
      newChildPosition = newChildPosition->nextSibling();
    }
    TreePool::sharedPool()->move(t.node(), newChildPosition);
    node()->incrementNumberOfChildren();
  }

  void removeChild(TreeReference<TreeNode> t) {
    TreePool::sharedPool()->move(t.node(), TreePool::sharedPool()->last());
    t.node()->release();
    node()->decrementNumberOfChildren();
  }

  void replaceWith(TreeReference<TreeNode> t) {
    TreeReference<TreeNode> p = parent();
    if (p.isDefined()) {
      p.replaceChildAtIndex(p.node()->indexOfChildByIdentifier(identifier()), t);
    }
  }

  void replaceChildAtIndex(int oldChildIndex, TreeReference<TreeNode> newChild) {
    if (newChild.isAllocationFailure()) {
      replaceWithAllocationFailure();
      return;
    }
    TreeReference<TreeNode> p = newChild.parent();
    if (p.isDefined()) {
      p.decrementNumberOfChildren();
    }
    assert(oldChildIndex >= 0 && oldChildIndex < numberOfChildren());
    TreeReference<T> oldChild = treeChildAtIndex(oldChildIndex);
    TreePool::sharedPool()->move(newChild.node(), oldChild.node()->next());
    newChild.node()->retain();
    TreePool::sharedPool()->move(oldChild.node(), TreePool::sharedPool()->last());
    oldChild.node()->release();
  }

  void replaceWithAllocationFailure() {
    TreeReference<TreeNode> p = parent();
    int indexInParentNode = node()->indexInParent();
    int currentRetainCount = node()->retainCount();
    TreeNode * staticAllocFailNode = castedNode()->failedAllocationStaticNode();

    // Move the node to the end of the pool and decrease children count of parent
    TreePool::sharedPool()->move(node(), TreePool::sharedPool()->last());
    if (p.isDefined()) {
      p.decrementNumberOfChildren();
    }

    // Release all children and delete the node in the pool
    node()->releaseChildrenAndDestroy();

    /* Create an allocation failure node with the previous node id. We know
     * there is room in the pool as we deleted the previous node and an
     * AllocationFailure nodes size is smaller or equal to any other node size.*/
    //TODO static assert that the size is smaller
    TreeNode * newAllocationFailureNode = TreePool::sharedPool()->deepCopy(staticAllocFailNode);
    newAllocationFailureNode->rename(m_identifier);
    if (p.isDefined()) {
      assert(indexInParentNode >= 0);
      /* Set the refCount to previousRefCount-1 because the previous parent is
       * no longer retaining the node. When we add this node to the parent, it
       * will retain it and increment the retain count. */
      newAllocationFailureNode->setReferenceCounter(currentRetainCount - 1);
      p.addChildAtIndex(newAllocationFailureNode, indexInParentNode);
    } else {
      newAllocationFailureNode->setReferenceCounter(currentRetainCount);
    }
  }

  void swapChildren(int i, int j) {
    assert(i >= 0 && i < numberOfChildren());
    assert(j >= 0 && j < numberOfChildren());
    if (i == j) {
      return;
    }
    int firstChildIndex = min(i, j);
    int secondChildIndex = max(i, j);
    TreeReference<T> firstChild = treeChildAtIndex(firstChildIndex);
    TreeReference<T> secondChild = treeChildAtIndex(secondChildIndex);
    TreeNode * firstChildNode = firstChild.node();
    TreePool::sharedPool()->move(firstChildNode, secondChild.node()->next());
    TreePool::sharedPool()->move(secondChild.node(), firstChildNode);
  }

protected:
  TreeReference() {
    TreeNode * node = TreePool::sharedPool()->createTreeNode<T>();
    m_identifier = node->identifier();
  }

  TreeReference(TreeNode * node) {
    if (node == nullptr) {
      m_identifier = -1;
    } else {
      setIdentifierAndRetain(node->identifier());
    }
  }
  void setIdentifierAndRetain(int newId) {
    m_identifier = newId;
    node()->retain();
  }
private:
  int m_identifier;
};

typedef TreeReference<TreeNode> TreeRef;

#endif
