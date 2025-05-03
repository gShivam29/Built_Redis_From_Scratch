#include "AVL.cpp"
#include <cstddef>
#include <cassert>
#include <set>
#include <cstdlib>
#include <iostream>
#define container_of(ptr, type, member) ({ \
    const typeof( ((type *)0)->member ) *__mptr = (ptr); \
    (type *)( (char *)__mptr - offsetof(type, member) ); })

struct Data {
    AVLNode node;
    uint32_t val = 0;
};

struct Container {
    AVLNode* root = nullptr;
};

// Normal add function
static void add(Container &c, uint32_t val) {
    Data *data = new Data();
    avl_init(&data->node);
    data->val = val;

    if (!c.root) {
        c.root = &data->node;
        return;
    }

    AVLNode *curr = c.root;
    while (true) {
        AVLNode **from = (val < container_of(curr, Data, node)->val) ? &curr->left : &curr->right;

        if (!*from) {
            *from = &data->node;
            data->node.parent = curr;
            c.root = avl_fix(&data->node);
            break;
        }
        curr = *from;
    }
}

// Broken add function that will fail verification
static void add_broken(Container &c, uint32_t val) {
    Data *data = new Data();
    avl_init(&data->node);
    data->val = val;

    if (!c.root) {
        c.root = &data->node;
        return;
    }

    AVLNode *curr = c.root;
    while (true) {
        // BROKEN: Always go left regardless of value
        AVLNode **from = &curr->left;

        if (!*from) {
            *from = &data->node;
            data->node.parent = curr;
            // Skip the AVL fix to break balance property
            // c.root = avl_fix(&data->node);
            break;
        }
        curr = *from;
    }
}

// Broken delete function
static bool del_broken(Container &c, uint32_t val) {
    AVLNode *curr = c.root;
    while (curr) {
        uint32_t node_val = container_of(curr, Data, node)->val;
        if (val == node_val) {
            break;
        }
        curr = val < node_val ? curr->left : curr->right;
    }
    if (!curr) {
        return false;
    }
    
    // BROKEN: Just remove node without fixing parent pointers
    if (curr->parent) {
        if (curr->parent->left == curr) {
            curr->parent->left = nullptr;
        } else {
            curr->parent->right = nullptr;
        }
    } else {
        c.root = nullptr;
    }
    
    delete container_of(curr, Data, node);
    return true;
}

static bool del(Container &c, uint32_t val) {
    AVLNode *curr = c.root;
    while (curr) {
        uint32_t node_val = container_of(curr, Data, node)->val;
        if (val == node_val) {
            break;
        }
        curr = val < node_val ? curr->left : curr->right;
    }
    if (!curr) {
        return false;
    }
    c.root = avl_del(curr);
    delete container_of(curr, Data, node);
    return true;
}

static void avl_verify(AVLNode *parent, AVLNode *node) {
    if (!node) return;

    assert(node->parent == parent);
    avl_verify(node, node->left);
    avl_verify(node, node->right);

    assert(node->cnt == 1 + avl_cnt(node->left) + avl_cnt(node->right));

    uint32_t l = avl_depth(node->left);
    uint32_t r = avl_depth(node->right);
    assert(l == r || l+1 == r || l == r+1);
    assert(node->depth == 1 + max(l, r));

    uint32_t val = container_of(node, Data, node)->val;
    if (node->left) {
        assert(node->left->parent == node);
        assert(container_of(node->left, Data, node)->val <= val);
    }
    if (node->right) {
        assert(node->right->parent == node);
        assert(container_of(node->right, Data, node)->val >= val);
    }
}

static void extract(AVLNode *node, std::multiset<uint32_t> &extracted) {
    if (!node) {
        return;
    }

    extract(node->left, extracted);
    extracted.insert(container_of(node, Data, node)->val);
    extract(node->right, extracted);
}

static void container_verify(Container &c, const std::multiset<uint32_t> &ref) {
    if (c.root) {
        avl_verify(NULL, c.root);
    }
    assert(avl_cnt(c.root) == ref.size());
    std::multiset<uint32_t> extracted;
    extract(c.root, extracted);
    assert(extracted == ref);
}

static void dispose(Container &c) {
    while (c.root) {
        AVLNode *node = c.root;
        c.root = avl_del(c.root);
        delete container_of(node, Data, node);
    }
}

// Test that breaks the BST property
void test_break_bst_property() {
    std::cout << "Breaking BST property..." << std::endl;
    Container c;
    std::multiset<uint32_t> ref;
    
    // Insert values in the wrong order to break BST property
    add(c, 10);
    ref.insert(10);
    
    // Manually corrupt the tree
    Data *data = new Data();
    avl_init(&data->node);
    data->val = 20;  // Larger value should go right
    
    // But we'll put it on the left to break BST property
    data->node.parent = c.root;
    c.root->left = &data->node;
    
    ref.insert(20);
    
    // This should fail when it checks that left child value <= parent value
    container_verify(c, ref);
}

// Test that breaks the AVL balance property
void test_break_balance_property() {
    std::cout << "Breaking AVL balance property..." << std::endl;
    Container c;
    std::multiset<uint32_t> ref;
    
    // Create a very unbalanced tree
    for (int i = 0; i < 10; i++) {
        add_broken(c, i);
        ref.insert(i);
    }
    
    // This should fail when it checks balance factor
    container_verify(c, ref);
}

// Test that breaks parent/child pointer consistency
void test_break_pointer_consistency() {
    std::cout << "Breaking pointer consistency..." << std::endl;
    Container c;
    std::multiset<uint32_t> ref;
    
    add(c, 10);
    add(c, 5);
    add(c, 15);
    ref.insert(10);
    ref.insert(5);
    ref.insert(15);
    
    // Corrupt parent pointer
    c.root->left->parent = nullptr;
    
    // This should fail parent pointer check
    container_verify(c, ref);
}

// Test that breaks count property
void test_break_count_property() {
    std::cout << "Breaking count property..." << std::endl;
    Container c;
    std::multiset<uint32_t> ref;
    
    add(c, 10);
    add(c, 5);
    add(c, 15);
    ref.insert(10);
    ref.insert(5);
    ref.insert(15);
    
    // Corrupt count
    c.root->cnt = 10; // Should be 3
    
    // This should fail count check
    container_verify(c, ref);
}

// Test that breaks depth property
void test_break_depth_property() {
    std::cout << "Breaking depth property..." << std::endl;
    Container c;
    std::multiset<uint32_t> ref;
    
    add(c, 10);
    add(c, 5);
    add(c, 15);
    ref.insert(10);
    ref.insert(5);
    ref.insert(15);
    
    // Corrupt depth
    c.root->depth = 10; // Should be 2
    
    // This should fail depth check
    container_verify(c, ref);
}

int main() {
    try {
        test_break_bst_property();
    } catch (const std::exception& e) {
        std::cout << "Test failed as expected: BST property violated\n";
    }
    
    try {
        test_break_balance_property();
    } catch (const std::exception& e) {
        std::cout << "Test failed as expected: AVL balance property violated\n";
    }
    
    try {
        test_break_pointer_consistency();
    } catch (const std::exception& e) {
        std::cout << "Test failed as expected: Parent/child pointer consistency violated\n";
    }
    
    try {
        test_break_count_property();
    } catch (const std::exception& e) {
        std::cout << "Test failed as expected: Count property violated\n";
    }
    
    try {
        test_break_depth_property();
    } catch (const std::exception& e) {
        std::cout << "Test failed as expected: Depth property violated\n";
    }

    std::cout << "All tests completed\n";
    return 0;
}