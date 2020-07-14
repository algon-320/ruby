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

struct RDequeChunkTableHeader {
    unsigned long first_chunk_idx;
    unsigned long first_chunk_size;
    unsigned long last_chunk_idx;
    unsigned long last_chunk_size;
};

struct RDeque {
    struct RBasic basic;
    unsigned long len;
    unsigned long table_cap;
    struct RDequeChunkTableHeader *ptr;
};

#define RDEQUE_CHUNK_TABLE_SIZE(cap) (sizeof(struct RDequeChunkTableHeader) + sizeof(VALUE) * cap)
#define RDEQUE_CHUNK_SIZE_LOG2 6
#define RDEQUE_CHUNK_SIZE (1 << RDEQUE_CHUNK_SIZE_LOG2)
#define RDEQUE_MAX_LEN (LONG_MAX / (int)sizeof(VALUE))

#define RDEQUE(obj) RBIMPL_CAST((struct RDeque *)(obj))

static inline unsigned long
RDEQUE_LEN(VALUE deq)
{
    RBIMPL_ASSERT_TYPE(deq, RUBY_T_DEQUE);
    return RDEQUE(deq)->len;
}
static inline unsigned long
RDEQUE_TABLE_CAP(VALUE deq)
{
    RBIMPL_ASSERT_TYPE(deq, RUBY_T_DEQUE);
    return RDEQUE(deq)->table_cap;
}
static inline struct RDequeChunkTableHeader *
RDEQUE_TABLE_HEADER_PTR(VALUE deq)
{
    RBIMPL_ASSERT_TYPE(deq, RUBY_T_DEQUE);
    return RDEQUE(deq)->ptr;
}

static inline VALUE **
RDEQUE_TABLE_FROM_HEADER(struct RDequeChunkTableHeader *header)
{
    RBIMPL_ASSERT_TYPE(deq, RUBY_T_DEQUE);
    return (VALUE **)((char *)header + sizeof(struct RDequeChunkTableHeader));
}

static inline VALUE **
RDEQUE_TABLE_PTR(VALUE deq)
{
    RBIMPL_ASSERT_TYPE(deq, RUBY_T_DEQUE);
    return (VALUE **)((char *)(RDEQUE(deq)->ptr) + sizeof(struct RDequeChunkTableHeader));
}

static inline VALUE *
RDEQUE_CHUNK_PTR(VALUE deq, long chunk_idx)
{
    RBIMPL_ASSERT_TYPE(deq, RUBY_T_DEQUE);
    return RDEQUE_TABLE_PTR(deq)[chunk_idx];
}

static inline int
RDEQUE_DURING_INIT(VALUE deq)
{
    return RDEQUE_TABLE_CAP(deq) == 0;
}

void
rb_deq_free(VALUE deq);

RUBY_FUNC_EXPORTED size_t
rb_deq_memsize(VALUE ary);

#endif