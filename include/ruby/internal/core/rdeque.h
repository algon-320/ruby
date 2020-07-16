#ifndef RBIMPL_RDEQUE_H /*-*-C++-*-vi:se ft=cpp:*/
#define RBIMPL_RDEQUE_H

#include "ruby/internal/arithmetic/long.h"
#include "ruby/internal/attr/pure.h"
#include "ruby/internal/core/rbasic.h"
#include "ruby/internal/fl_type.h"
#include "ruby/internal/rgengc.h"
#include "ruby/internal/value.h"
#include "ruby/internal/value_type.h"
#include "ruby/assert.h"

struct RDequeChunkTable {
    unsigned long first_chunk_idx;
    unsigned long front;
    unsigned long last_chunk_idx;
    unsigned long back;
    VALUE *chunks[];
};

struct RDeque {
    struct RBasic basic;
    unsigned long len;
    unsigned long table_cap;
    struct RDequeChunkTable *table;
};

#define RDEQUE(obj) RBIMPL_CAST((struct RDeque *)(obj))
#define RDEQUE_CHUNK_SIZE_LOG2 6
#define RDEQUE_CHUNK_SIZE (1 << RDEQUE_CHUNK_SIZE_LOG2)
#define RDEQUE_MAX_SIZE (long)(LONG_MAX / sizeof(VALUE))

static inline unsigned long
RDEQUE_LEN(VALUE deq)
{
    RBIMPL_ASSERT_TYPE(deq, RUBY_T_DEQUE);
    return RDEQUE(deq)->len;
}

static inline unsigned long
RDEQUE_CHUNK_TABLE_CAP(VALUE deq)
{
    RBIMPL_ASSERT_TYPE(deq, RUBY_T_DEQUE);
    return RDEQUE(deq)->table_cap;
}

static inline struct RDequeChunkTable *
RDEQUE_CHUNK_TABLE(VALUE deq)
{
    RBIMPL_ASSERT_TYPE(deq, RUBY_T_DEQUE);
    return RDEQUE(deq)->table;
}

static inline unsigned long
RDEQUE_TABLE_USED_CHUNK_NUM(VALUE deq)
{
    RBIMPL_ASSERT_TYPE(deq, RUBY_T_DEQUE);
    return RDEQUE(deq)->table->last_chunk_idx - RDEQUE(deq)->table->first_chunk_idx + 1;
}

void
rb_deq_free(VALUE deq);

RUBY_FUNC_EXPORTED size_t
rb_deq_memsize(VALUE ary);

#endif