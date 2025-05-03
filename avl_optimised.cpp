#include <cstdint>
#include <algorithm>

struct OptimizedAVLNode {
    int8_t balance = 0;  // Height difference: left - right (can be -1, 0, 1 for valid AVL tree)
    uint32_t cnt = 1;    // Size of the subtree (for rank operations)
    
    OptimizedAVLNode *left = nullptr;
    OptimizedAVLNode *right = nullptr;
    OptimizedAVLNode *parent = nullptr;
    
    // Value stored directly in the node (avoid container_of macro)
    uint32_t val = 0;
};

// Initialize a node
static void avl_init(OptimizedAVLNode *node, uint32_t value) {
    node->balance = 0;
    node->cnt = 1;
    node->left = node->right = node->parent = nullptr;
    node->val = value;
}

// Get the size of a subtree
static uint32_t avl_cnt(OptimizedAVLNode *node) {
    return node ? node->cnt : 0;
}

// Update node metadata after structural changes
static void avl_update(OptimizedAVLNode *node) {
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

// Single left rotation - optimized with fewer pointer updates
static OptimizedAVLNode *rotate_left(OptimizedAVLNode *node) {
    OptimizedAVLNode *right_child = node->right;
    OptimizedAVLNode *right_left = right_child->left;
    
    // Update structure
    right_child->left = node;
    node->right = right_left;
    
    // Update parent pointers
    right_child->parent = node->parent;
    node->parent = right_child;
    if (right_left) {
        right_left->parent = node;
    }
    
    // Update balance factors
    // After a left rotation:
    // node->balance = node->balance + 1 - std::min(0, right_child->balance);
    // right_child->balance = right_child->balance + 1 + std::max(0, node->balance);
    
    // Simplified balance calculation for standard case
    node->balance += 1;
    if (right_child->balance < 0) {
        node->balance -= right_child->balance;
    }
    
    right_child->balance += 1;
    if (node->balance > 0) {
        right_child->balance += node->balance;
    }
    
    // Update counts
    avl_update(node);
    avl_update(right_child);
    
    return right_child;
}

// Single right rotation - optimized with fewer pointer updates
static OptimizedAVLNode *rotate_right(OptimizedAVLNode *node) {
    OptimizedAVLNode *left_child = node->left;
    OptimizedAVLNode *left_right = left_child->right;
    
    // Update structure
    left_child->right = node;
    node->left = left_right;
    
    // Update parent pointers
    left_child->parent = node->parent;
    node->parent = left_child;
    if (left_right) {
        left_right->parent = node;
    }
    
    // Update balance factors
    // After a right rotation:
    // node->balance = node->balance - 1 - std::max(0, left_child->balance);
    // left_child->balance = left_child->balance - 1 + std::min(0, node->balance);
    
    // Simplified balance calculation for standard case
    node->balance -= 1;
    if (left_child->balance > 0) {
        node->balance -= left_child->balance;
    }
    
    left_child->balance -= 1;
    if (node->balance < 0) {
        left_child->balance += node->balance;
    }
    
    // Update counts
    avl_update(node);
    avl_update(left_child);
    
    return left_child;
}

// Balance a node
static OptimizedAVLNode *avl_balance(OptimizedAVLNode *node) {
    if (node->balance > 1) {
        // Left subtree is higher
        if (node->left && node->left->balance < 0) {
            // LR case
            node->left = rotate_left(node->left);
        }
        // LL case
        return rotate_right(node);
    }
    else if (node->balance < -1) {
        // Right subtree is higher
        if (node->right && node->right->balance > 0) {
            // RL case
            node->right = rotate_right(node->right);
        }
        // RR case
        return rotate_left(node);
    }
    
    // Node is already balanced
    return node;
}

// Insert a value into the tree
static OptimizedAVLNode *avl_insert(OptimizedAVLNode *root, OptimizedAVLNode *new_node) {
    // Base case: empty tree or reached leaf
    if (!root) {
        return new_node;
    }
    
    // Insert into appropriate subtree
    if (new_node->val < root->val) {
        OptimizedAVLNode *left = avl_insert(root->left, new_node);
        root->left = left;
        left->parent = root;
        
        // Update balance factor
        root->balance++;
    } else {
        OptimizedAVLNode *right = avl_insert(root->right, new_node);
        root->right = right;
        right->parent = root;
        
        // Update balance factor
        root->balance--;
    }
    
    // Update count
    avl_update(root);
    
    // Rebalance if needed
    return avl_balance(root);
}

// Find the minimum value node in a subtree
static OptimizedAVLNode *find_min(OptimizedAVLNode *node) {
    while (node && node->left) {
        node = node->left;
    }
    return node;
}

// Delete a node from the tree
static OptimizedAVLNode *avl_delete(OptimizedAVLNode *root, uint32_t val) {
    if (!root) {
        return nullptr;
    }
    
    // Find the node
    if (val < root->val) {
        root->left = avl_delete(root->left, val);
        if (root->left) {
            root->left->parent = root;
        }
        root->balance--;
    } 
    else if (val > root->val) {
        root->right = avl_delete(root->right, val);
        if (root->right) {
            root->right->parent = root;
        }
        root->balance++;
    }
    else {
        // Node found, handle deletion
        if (!root->left) {
            OptimizedAVLNode *right = root->right;
            delete root;
            return right;
        }
        else if (!root->right) {
            OptimizedAVLNode *left = root->left;
            delete root;
            return left;
        }
        else {
            // Node has two children
            // Find successor (min value in right subtree)
            OptimizedAVLNode *successor = find_min(root->right);
            
            // Copy successor's value
            root->val = successor->val;
            
            // Delete successor
            root->right = avl_delete(root->right, successor->val);
            if (root->right) {
                root->right->parent = root;
            }
            root->balance++;
        }
    }
    
    // Update count
    avl_update(root);
    
    // Balance the tree if needed
    return avl_balance(root);
}

// Search for a value in the tree
static OptimizedAVLNode *avl_search(OptimizedAVLNode *root, uint32_t val) {
    if (!root) {
        return nullptr;
    }
    
    if (val == root->val) {
        return root;
    }
    
    if (val < root->val) {
        return avl_search(root->left, val);
    } else {
        return avl_search(root->right, val);
    }
}