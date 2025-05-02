#include "AVL.cpp"
#include <cstddef>
#define container_of(ptr, type, member) ({ \
    const typeof( ((type *)0)->member ) *__mptr = (ptr); \
    (type *)( (char *)__mptr - offsetof(type, member) ); })

struct Data{
    AVLNode node;
    uint32_t val = 0;
};

struct Container{
    AVLNode* root = nullptr;
};


static void add(Container &c, uint32_t val){
    Data *data = new Data();
    avl_init(&data-> node);
    data -> val = val;

    if (!c.root){
        c.root = &data -> node;
        return;
    }

    AVLNode *curr = c.root;
    while(true){
        AVLNode **from = (val < container_of(curr, Data, node) -> val) ? &curr -> left : &curr -> right;

        if (!*from){
            *from = &data -> node;
            data -> node.parent = curr;
            c.root = avl_fix(&data -> node);
            break;
        }
        curr = *from;
    }
}

static bool del(Container &c, uint32_t val){
    AVLNode *curr = c.root;
    while(curr){
        uint32_t node_val = container_of(curr, Data, node) -> val;
        if(val == node_val){
            break;
        }
        curr = val < node_val ? curr -> left : curr -> right;
    }
    if (!curr){
        return false;
    }
    c.root = avl_del(curr);
    delete container_of(curr, Data, node);
    return true;
}