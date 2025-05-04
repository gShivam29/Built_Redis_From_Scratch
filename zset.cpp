#include "zset.h"
#include <cstring>
#include <algorithm>
Znode *znode_new(const char *name, size_t len, double score)
{
    Znode *node = (Znode *)malloc(sizeof(Znode) + len);
    assert(node); // not the best thing in production but this is not prod
    avl_init(&node->tree);
    node->hmap.next = NULL;
    node->hmap.hcode = str_hash((uint8_t *)name, len);
    node->score = score;
    node->len = len;
    memcpy(&node->name[0], name, len);
    return node;
}

void tree_add(Zset *zset, Znode *node)
{
    if (!zset->tree)
    {
        zset->tree = &node->tree;
        return;
    }

    AVLNode *curr = zset->tree;
    while (true)
    {
        AVLNode **from = zless(&node->tree, curr) ? &curr->left : &curr->right;

        if (!from)
        {
            *from = &node->tree;
            node->tree.parent = curr;
            zset->tree = avl_fix(&node->tree);
            break;
        }
        curr = *from;
    }
}

// compare by the (score, name) tuple
bool zless(AVLNode *lhs, double score, const char *name, size_t len)
{
    Znode *zl = container_of(lhs, Znode, tree);
    if (zl->score != score)
    {
        return zl->score < score;
    }
    int rv = memcmp(zl->name, name, std::min(zl->len, len));
    if (rv != 0)
    {
        return rv < 0;
    }
    return zl->len < len;
}

bool zless(AVLNode *lhs, AVLNode *rhs)
{
    Znode *zr = container_of(rhs, Znode, tree);
    return zless(lhs, zr->score, zr->name, zr->len);
}

// update the score of an existing node (AVL tree reinsertion)

void zset_update(Zset *zset, Znode *node, double score)
{
    if (node->score == score)
    {
        return;
    }
    zset->tree = avl_del(&node->tree);
    node->score = score;
    avl_init(&node->tree);
    tree_add(zset, node);
}

// add a new (score, name) tuple, or update the score of an exisiting tuple
bool zset_add(Zset *zset, const char *name, size_t len, double score)
{
    Znode *node = zset_lookup(zset, name, len);
    if (node)
    {
        zset_update(zset, node, score);
        return false;
    }
    else
    {
        node = znode_new(name, len, score);
        hm_insert(&zset->hmap, &node->hmap);
        tree_add(zset, node);
        return true;
    }
}

// Comparison function for hash lookup
bool znode_cmp(HNode *node, HNode *key)
{
    Znode *znode = container_of(node, Znode, hmap);
    Znode *zkey = container_of(key, Znode, hmap);
    if (znode->len != zkey->len)
    {
        return false;
    }
    return memcmp(znode->name, zkey->name, zkey->len) == 0;
}

// Lookup by name
Znode *zset_lookup(Zset *zset, const char *name, size_t len)
{
    if (!zset->hmap.ht1.tab && !zset->hmap.ht2.tab)
    {
        return nullptr;
    }

    // Create a temporary hash node as a key for lookup
    HNode key;
    key.hcode = str_hash((uint8_t *)name, len);

    // We need a temporary znode to store the name for comparison
    // This is allocated on stack just for comparison purposes
    char buf[sizeof(Znode) + len];
    Znode *tmp = (Znode *)buf;
    tmp->len = len;
    tmp->hmap.hcode = key.hcode;
    memcpy(&tmp->name[0], name, len);

    // Lookup the node in the hash map
    HNode *found = hm_lookup(&zset->hmap, &tmp->hmap, &znode_cmp);

    if (!found)
    {
        return nullptr;
    }

    return container_of(found, Znode, hmap);
}

Znode *zset_query(Zset *zset, double score, const char *name, size_t len, int64_t offset)
{
    AVLNode *found = NULL;
    AVLNode *curr = zset->tree;

    while (curr)
    {
        if (zless(curr, score, name, len))
        {
            curr = curr->right;
        }
        else
        {
            found = curr; // candidate
            curr = curr->left;
        }
    }

    if (found)
    {
        found = avl_offset(found, offset);
    }

    return found ? container_of(found, Znode, tree) : NULL;
}