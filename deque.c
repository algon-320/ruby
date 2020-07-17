#include "internal.h"
#include "internal/gc.h"
#include "internal/numeric.h"
#include "internal/object.h"
#include "ruby/encoding.h"
#include "ruby/util.h"
#include "builtin.h"

#if !DEQUE_DEBUG
#undef NDEBUG
#define NDEBUG
#endif
#include "ruby_assert.h"

#define DEQ_LEN(deq) (RDEQUE(deq)->len)
#define DEQ_TABLE_CAP(deq) (RDEQUE(deq)->table_cap)
#define DEQ_TABLE(deq) (RDEQUE(deq)->table)

static inline unsigned long
deq_chunk_table_size_bytes(unsigned long cap)
{
    return sizeof(struct RDequeChunkTable) + sizeof(VALUE*) * cap;
}

static void
deq_heap_free(VALUE deq);
void
rb_deq_free(VALUE deq)
{
    deq_heap_free(deq);
}

RUBY_FUNC_EXPORTED size_t
rb_deq_memsize(VALUE deq)
{
    if (DEQ_TABLE(deq) == NULL) return 0;
    return deq_chunk_table_size_bytes(DEQ_TABLE_CAP(deq))
            + RDEQUE_TABLE_USED_CHUNK_NUM(deq) * RDEQUE_CHUNK_SIZE * sizeof(VALUE);
}

static inline void
memfill(register VALUE *mem, register long size, register VALUE val)
{
    while (size--) {
        *mem++ = val;
    }
}

static inline unsigned long
deq_calc_new_table_cap(VALUE deq)
{
    unsigned long old_capa = DEQ_TABLE_CAP(deq);
    unsigned long new_capa = old_capa * 2;
    if (old_capa == 1) new_capa = 3;
    if (new_capa > RDEQUE_MAX_SIZE / RDEQUE_CHUNK_SIZE) {
        new_capa = RDEQUE_MAX_SIZE / RDEQUE_CHUNK_SIZE;
    }
    return new_capa;
}

static inline void
rb_deq_modify_check(VALUE ary)
{
    rb_check_frozen(ary);
}

static VALUE
empty_deq_alloc(VALUE klass)
{
    NEWOBJ_OF(deq, struct RDeque, klass, T_DEQUE);
    RDEQUE(deq)->len = 0;
    RDEQUE(deq)->table_cap = 0;
    RDEQUE(deq)->table = NULL;
    return (VALUE)deq;
}

static inline VALUE *
deq_heap_alloc_chunk()
{
    return ALLOC_N(VALUE, RDEQUE_CHUNK_SIZE);
}

static inline struct RDequeChunkTable *
deq_heap_alloc_table(long cap)
{
    return (struct RDequeChunkTable *)ALLOC_N(VALUE, deq_chunk_table_size_bytes(cap) / sizeof(VALUE));
}

static void
deq_heap_realloc_table(VALUE deq, long new_cap)
{
    assert(!OBJ_FROZEN(deq));
    long old_cap = DEQ_TABLE_CAP(deq);
    struct RDequeChunkTable *old_table = DEQ_TABLE(deq);
    long used_chunk = RDEQUE_TABLE_USED_CHUNK_NUM(deq);
    struct RDequeChunkTable *new_table = deq_heap_alloc_table(new_cap);

    // copy header
    *new_table = *old_table;
    long offset = (new_cap - used_chunk) / 2 - old_table->first_chunk_idx;  // centering
    MEMCPY(new_table->chunks + offset, DEQ_TABLE(deq)->chunks, VALUE, old_cap);

    new_table->first_chunk_idx += offset;
    new_table->last_chunk_idx += offset;

    DEQ_TABLE_CAP(deq) = new_cap;
    DEQ_TABLE(deq) = new_table;
    ruby_sized_xfree(old_table, deq_chunk_table_size_bytes(old_cap));
}

static void
deq_heap_free(VALUE deq)
{
    long len = DEQ_LEN(deq);
    long table_cap = DEQ_TABLE_CAP(deq);
    struct RDequeChunkTable *table = DEQ_TABLE(deq);
    DEQ_LEN(deq) = 0;
    DEQ_TABLE_CAP(deq) = 0;
    DEQ_TABLE(deq) = (struct RDequeChunkTable *)NULL;
    for (unsigned long i = table->first_chunk_idx; i <= table->first_chunk_idx; i++) {
        ruby_sized_xfree((void *)table->chunks[i], len * sizeof(VALUE));
        table->chunks[i] = (VALUE *)NULL;
    }
    ruby_sized_xfree(table, deq_chunk_table_size_bytes(table_cap));
}

static VALUE
rb_deq_initialize(int argc, VALUE *argv, VALUE deq)
{
    rb_deq_modify_check(deq);
    if (argc >= 3) {
        rb_raise(rb_eArgError, "too many arguments");
    }

    VALUE size, fill_val;
    if (argc == 0) {
        size = LONG2FIX(0);
        fill_val = Qnil;
    } else {
        rb_scan_args(argc, argv, "02", &size, &fill_val);
    }

    long len = NUM2LONG(size);
    if (len < 0) rb_raise(rb_eArgError, "negative size");
    if (len > RDEQUE_MAX_SIZE) rb_raise(rb_eArgError, "too large deque");

    if (DEQ_TABLE(deq) != NULL) {
        deq_heap_free(deq);
    }

    long slot_cap = len + 2;
    long first_half = slot_cap / 2;
    long second_half = slot_cap - first_half;
    long chunk_num = (slot_cap + RDEQUE_CHUNK_SIZE - 1) / RDEQUE_CHUNK_SIZE;
    long center = chunk_num * RDEQUE_CHUNK_SIZE / 2;
    // [begin, end)
    long begin = center - first_half;
    long end = center + second_half;

    // allocate chunk table
    struct RDequeChunkTable *table = deq_heap_alloc_table(chunk_num);
    for (long i = 0; i < chunk_num; i++) {
        table->chunks[i] = deq_heap_alloc_chunk();
    }
    
    if (chunk_num == 1) {
        memfill(table->chunks[0] + begin, end - begin, fill_val);
        table->front = begin;
        table->back = end - 1;
        table->first_chunk_idx = table->last_chunk_idx = 0;
    } else {
        // greater than or equals to 3
        memfill(table->chunks[0] + begin, RDEQUE_CHUNK_SIZE - begin, fill_val);
        for (long i = 1; i + 1 < chunk_num; i++) {
            memfill(table->chunks[i], RDEQUE_CHUNK_SIZE, fill_val);
        }
        memfill(table->chunks[chunk_num - 1], (end - 1) % RDEQUE_CHUNK_SIZE, fill_val);
        table->front = begin;
        table->back = (end - 1) % RDEQUE_CHUNK_SIZE;
        table->first_chunk_idx = 0;
        table->last_chunk_idx = (end - 1) / RDEQUE_CHUNK_SIZE;
    }
    DEQ_LEN(deq) = len;
    DEQ_TABLE_CAP(deq) = chunk_num;
    DEQ_TABLE(deq) = table;
    return deq;
}

static inline VALUE *
rb_deq_ref_ptr(VALUE deq, long idx)
{
    struct RDequeChunkTable *table = DEQ_TABLE(deq);
    long len = DEQ_LEN(deq);
    if (idx >= len || idx < -len) {
        return NULL; // out of range
    }
    if (idx < 0) {
        idx += len;
    }

    idx += table->front + 1;
    if (idx < RDEQUE_CHUNK_SIZE) {
        return &table->chunks[table->first_chunk_idx][idx];
    } else {
        return &table->chunks[table->first_chunk_idx + idx / RDEQUE_CHUNK_SIZE][idx % RDEQUE_CHUNK_SIZE];
    }
}
static inline VALUE
rb_deq_ref(VALUE deq, long idx)
{
    VALUE *ptr = rb_deq_ref_ptr(deq, idx);
    if (ptr) return *ptr;
    return Qnil;
}

static VALUE
rb_deq_at(VALUE deq, VALUE offset)
{
    return rb_deq_ref(deq, NUM2LONG(offset));
}

static VALUE
rb_deq_at_write(VALUE deq, VALUE offset, VALUE value)
{
    rb_deq_modify_check(deq);
    VALUE *ptr = rb_deq_ref_ptr(deq, NUM2LONG(offset));
    if (ptr) {
        *ptr = value;
    } else {
        rb_raise(rb_eArgError, "index ouf of range");
    }
    return value;
}

static VALUE
inspect_deq(VALUE deq, VALUE dummy, int recur)
{
    unsigned long i;
    VALUE s, str;

    if (recur) return rb_usascii_str_new2(">[...]<");
    str = rb_str_buf_new2(">[");
    for (i = 0; i < RDEQUE_LEN(deq); i++) {
        s = rb_inspect(rb_deq_ref(deq, i));
        if (i > 0) rb_str_buf_cat2(str, ", ");
        else rb_enc_copy(str, s);
        rb_str_buf_append(str, s);
    }
    rb_str_buf_cat2(str, "]<");
    return str;
}

static VALUE
rb_deq_inspect(VALUE deq)
{
    if (RDEQUE_LEN(deq) == 0) rb_usascii_str_new2(">[]<");
    return rb_exec_recursive(inspect_deq, deq, 0);
}

static VALUE
rb_deq_length(VALUE deq)
{
    return LONG2NUM(RDEQUE_LEN(deq));
}

static VALUE
rb_deq_push_back(VALUE deq, VALUE item)
{
    rb_deq_modify_check(deq);
    struct RDequeChunkTable *table = DEQ_TABLE(deq);
    table->chunks[table->last_chunk_idx][table->back] = item;
    DEQ_LEN(deq) += 1;
    if (table->back + 1 == RDEQUE_CHUNK_SIZE) {
        if (table->last_chunk_idx + 1 == DEQ_TABLE_CAP(deq)) {
            long new_cap = deq_calc_new_table_cap(deq);
            deq_heap_realloc_table(deq, new_cap);
            table = DEQ_TABLE(deq);
        }
        table->last_chunk_idx += 1;
        table->back = 0;
        table->chunks[table->last_chunk_idx] = deq_heap_alloc_chunk();
    } else {
        table->back += 1;
    }
    return deq;
}
static VALUE
rb_deq_push_front(VALUE deq, VALUE item)
{
    rb_deq_modify_check(deq);
    struct RDequeChunkTable *table = DEQ_TABLE(deq);
    table->chunks[table->first_chunk_idx][table->front] = item;
    DEQ_LEN(deq) += 1;
    if (table->front == 0) {
        if (table->first_chunk_idx == 0) {
            long new_cap = deq_calc_new_table_cap(deq);
            deq_heap_realloc_table(deq, new_cap);
            table = DEQ_TABLE(deq);
        }
        table->first_chunk_idx -= 1;
        table->front = RDEQUE_CHUNK_SIZE - 1;
        table->chunks[table->first_chunk_idx] = deq_heap_alloc_chunk();
    } else {
        table->front -= 1;
    }
    return deq;
}

static VALUE
rb_deq_pop_back(VALUE deq)
{
    rb_deq_modify_check(deq);
    struct RDequeChunkTable *table = DEQ_TABLE(deq);
    DEQ_LEN(deq) -= 1;
    if (table->back == 0) {
        // TODO: shrink table
        table->last_chunk_idx -= 1;
        table->back = RDEQUE_CHUNK_SIZE - 1;
    } else {
        table->back -= 1;
    }
    return table->chunks[table->last_chunk_idx][table->back];
}
static VALUE
rb_deq_pop_front(VALUE deq)
{
    rb_deq_modify_check(deq);
    struct RDequeChunkTable *table = DEQ_TABLE(deq);
    DEQ_LEN(deq) -= 1;
    if (table->front == RDEQUE_CHUNK_SIZE - 1) {
        // TODO: shrink table
        table->first_chunk_idx += 1;
        table->front = 0;
    } else {
        table->front += 1;
    }
    return table->chunks[table->first_chunk_idx][table->front];
}

void
Init_Deque(void)
{
    VALUE rb_cDeque = rb_define_class("Deque", rb_cObject);
    rb_define_alloc_func(rb_cDeque, empty_deq_alloc);
    rb_define_method(rb_cDeque, "initialize", rb_deq_initialize, -1);
    rb_define_method(rb_cDeque, "inspect", rb_deq_inspect, 0);
    rb_define_alias(rb_cDeque,  "to_s", "inspect");
    rb_define_method(rb_cDeque, "length", rb_deq_length, 0);
    rb_define_alias(rb_cDeque,  "size", "length");
    
    rb_define_method(rb_cDeque, "push_back", rb_deq_push_back, 1);
    rb_define_method(rb_cDeque, "push_front", rb_deq_push_front, 1);
    rb_define_alias(rb_cDeque,  "push", "push_back");
    rb_define_alias(rb_cDeque,  "unshift", "push_front");
    
    rb_define_method(rb_cDeque, "pop_back", rb_deq_pop_back, 0);
    rb_define_method(rb_cDeque, "pop_front", rb_deq_pop_front, 0);
    rb_define_alias(rb_cDeque,  "pop", "pop_back");
    rb_define_alias(rb_cDeque,  "shift", "pop_front");

    rb_define_method(rb_cDeque, "at", rb_deq_at, 1);
    rb_define_method(rb_cDeque, "[]=", rb_deq_at_write, 2);
    rb_define_alias(rb_cDeque,  "[]", "at");
}
