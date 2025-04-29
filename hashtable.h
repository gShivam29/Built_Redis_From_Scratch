#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstdlib>
struct HNode
{
    HNode *next = nullptr;
    uint64_t hcode = 0;
};

struct HTab
{
    HNode **tab = nullptr;
    size_t mask = 0;
    size_t size = 0;
};

struct HMap
{
    HTab ht1;
    HTab ht2;
    size_t resizing_pos = 0;
};

void h_init(HTab *htab, size_t n);

void h_insert(HTab *htab, HNode *node);

HNode **h_lookup(HTab *htab, HNode *key, bool (*cmp)(HNode *, HNode *));

HNode *h_detach(HTab *htab, HNode **from);

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *));
void hm_help_resizing(HMap *hmap);
void hm_insert(HMap *hmap, HNode *node);
void hm_start_resizing(HMap *hmap);