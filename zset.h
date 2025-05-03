#include "AVL.cpp"
#include "hashtable.h"
struct Zset
{
    AVLNode *tree = NULL;
    HMap hmap;
};

struct Znode
{
    AVLNode tree;
    HNode hmap;
    double score = 0;
    size_t len = 0;
    char name[0];
};

Znode *znode_new(const char *name, size_t len, double score);

void tree_add(Zset *zset, Znode *node);

bool zless(AVLNode *lhs, double score, const char *name, size_t len);

bool zless(AVLNode *lhs, AVLNode *rhs);

void zset_update(Zset *zset, Znode *node, double score);

bool zset_add(Zset *zset, const char *name, size_t len, double score);

bool znode_cmp(HNode *node, HNode *key);

Znode *zset_lookup(Zset *zset, const char *name, size_t len);

Znode *zset_query(Zset *zset, double score, const char *name, size_t len, int64_t offset);