#include <cstdint>
struct AVLNode
{
    uint32_t depth = 0; // height of the tree
    uint32_t cnt = 0;   // size of the tree
    AVLNode *left = nullptr;
    AVLNode *right = nullptr;
    AVLNode *parent = nullptr;
};

static void avl_init(AVLNode *node)
{
    node->depth = 1;
    node->cnt = 1;
    node->left = node->right = node->parent = nullptr;
}

static uint32_t avl_depth(AVLNode *node)
{
    return node ? node->depth : 0;
}

static uint32_t avl_cnt(AVLNode *node)
{
    return node ? node->cnt : 0;
}

static uint32_t max(uint32_t lhs, uint32_t rhs)
{
    return lhs < rhs ? rhs : lhs;
}

// maintain the depth and cnt fields
static void avl_update(AVLNode *node)
{
    node->depth = 1 + max(avl_depth(node->left), avl_depth(node->right));
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

// LL rotation
static AVLNode *left_rotation(AVLNode *node)
{
    AVLNode *new_node = node->right;
    if (new_node->left)
    {
        new_node->left->parent = node;
    }
    node->right = new_node->left;
    new_node->left = node;
    new_node->parent = node->parent;
    node->parent = new_node;
    avl_update(node);
    avl_update(new_node);
    return new_node;
}

// RR rotation
static AVLNode *right_rotation(AVLNode *node)
{
    AVLNode *new_node = node->left;
    if (new_node->right)
    {
        new_node->right->parent = node;
    }
    node->left = new_node->right;
    new_node->right = node;
    new_node->parent = node->parent;
    node->parent = new_node;
    avl_update(node);
    avl_update(new_node);
    return new_node;
}

// LR rotation
static AVLNode *avl_fix_left(AVLNode *root)
{
    if (avl_depth(root->left->left) < avl_depth(root->left->right))
    {
        root->left = left_rotation(root->left);
    }

    return right_rotation(root);
}

// RL rotation
static AVLNode *avl_fix_right(AVLNode *root)
{
    if (avl_depth(root->right->right) < avl_depth(root->right->left))
    {
        root->right = right_rotation(root->right);
    }

    return left_rotation(root);
}

static AVLNode *avl_fix(AVLNode *node)
{
    while (true)
    {
        avl_update(node);
        uint32_t l = avl_depth(node->left);
        uint32_t r = avl_depth(node->right);
        AVLNode **from = nullptr;
        if (node->parent)
        {
            from = (node->parent->left == node) ? &node->parent->left : &node->parent->right;
        }

        if (l == r + 2)
        {
            node = avl_fix_left(node);
        }
        else if (l + 2 == r)
        {
            node = avl_fix_right(node);
        }

        if (!from)
        {
            return node;
        }
        *from = node;
        node = node->parent;
    }
}

static AVLNode *avl_del(AVLNode *node)
{
    AVLNode *parent = node->parent;

    if (node->right == nullptr)
    {
        // no right subtree, replace the node with the left subtree
        // link the left subtree to the parent
        if (node->left)
        {
            node->left->parent = parent;
        }
        if (parent)
        {
            // attach the left subtree to the parent
            (parent->left == node ? parent->left : parent->right) = node->left;
            return avl_fix(parent);
        }
        else
        {
            // removing root
            return node->left;
        }
    }
    else
    {
        // swap the node with its right sibling
        AVLNode *victim = node->right;
        while (victim->left)
        {
            victim = victim->left;
        }
        AVLNode *root = avl_del(victim);
        *victim = *node;
        if (victim->left)
        {
            victim->left->parent = victim;
        }
        if (victim->right)
        {
            victim->right->parent = victim;
        }

        AVLNode *parent = node->parent;

        if (parent)
        {
            (parent->left == node ? parent->left : parent->right) = victim;
            return root;
        }
        else
        {
            return victim;
        }
    }
}

// offset into the succeeding or preceding node.
// note: the worst-case is O(log(n)) regardless of how long the offset is.
AVLNode *avl_offset(AVLNode *node, int64_t offset)
{
    int64_t pos = 0; // relative to the starting node
    while (offset != pos)
    {
        if (pos < offset && pos + avl_cnt(node->right) >= offset)
        {
            // the target is inside the right subtree
            node = node->right;
            pos += avl_cnt(node->left) + 1;
        }
        else if (pos > offset && pos - avl_cnt(node->left) <= offset)
        {
            // the target is inside the left subtree
            node = node->left;
            pos -= avl_cnt(node->right) + 1;
        }
        else
        {
            // go to the parent
            AVLNode *parent = node->parent;
            if (!parent)
            {
                return nullptr;
            }
            if (parent->right == node)
            {
                pos -= avl_cnt(node->left) + 1;
            }
            else
            {
                pos += avl_cnt(node->right) + 1;
            }
            node = parent;
        }
    }
    return node;
}
