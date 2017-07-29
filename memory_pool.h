#ifndef __MEMORY_POOL_H__
#define __MEMORY_POOL_H__

#include <climits>
#include <cstddef>
#include <atomic>
#include <type_traits>
#include <utility>

template <class T> class spin_lock {
    T &lock_obj;
public:
    spin_lock(T &obj) : lock_obj(obj) { lock(); }
    ~spin_lock() { unlock(); }

    void lock() {
        while (lock_obj.test_and_set(std::memory_order_acquire)) { asm volatile("pause\n": : :"memory"); }
    }
    void unlock() {
        lock_obj.clear(std::memory_order_release);
    }
};

template <typename T, std::size_t block_size = 4096>
class MemoryPool
{
  public:
    /* Member types */
    typedef T               value_type;
    typedef T*              pointer;
    typedef T&              reference;
    typedef const T*        const_pointer;
    typedef const T&        const_reference;
    typedef size_t          size_type;
    typedef char *          data_pointer;

    // Constructor / destructor
    MemoryPool() noexcept;
    ~MemoryPool() noexcept;

    std::size_t max_size() const noexcept { return m_max_size * sizeof(slot_t); }

    // Public member functions
    pointer address(reference x) const noexcept { return &x; };
    const pointer address(const_reference x) const noexcept { return &x; };

    // Can only allocate one object at a time. n and hint are ignored
    pointer allocate(std::size_t n = 1, const_pointer hint = 0);
    void deallocate(pointer p, size_type n = 1);


    template <class U, class... Args> void construct(U* p, Args&&... args);
    template <class U>  void destroy(U* p);

    template <class... Args> pointer new_element(Args&&... args);
    void delete_element(T* p);

  private:
    // Private types
    struct slot_t {
        T element;
        slot_t *next = nullptr;
    };

    struct slot_head_t {
        uintptr_t aba = 0;
        slot_t *node = nullptr;
    };

    struct allocated_block_t {
        char *buffer = nullptr;
        allocated_block_t *next = nullptr;

        ~allocated_block_t() { operator delete(buffer); }
    };

    // Private variables
    uint64_t m_max_size = 0;
    slot_t *m_last_slot = nullptr;
    allocated_block_t *m_allocated_block_head = nullptr;
    std::atomic<slot_head_t> m_free;
    std::atomic_flag m_lock = ATOMIC_FLAG_INIT;

    // Private functions
    size_type pad_pointer(char *p, std::size_t align) const noexcept;

    void allocate_block();

    MemoryPool(const MemoryPool& memoryPool) noexcept = delete;
    MemoryPool(MemoryPool&& memoryPool) noexcept = delete;
    MemoryPool& operator=(const MemoryPool& memoryPool) {};
    MemoryPool& operator=(MemoryPool&& memoryPool) {};
};

template <typename T, std::size_t block_size>
inline typename MemoryPool<T, block_size>::size_type
MemoryPool<T, block_size>::pad_pointer(data_pointer p, size_type align) const noexcept {
    uintptr_t result = reinterpret_cast<uintptr_t>(p);
    return ((align - result) % align);
}

template <typename T, std::size_t block_size>
MemoryPool<T, block_size>::MemoryPool() noexcept { }

template <typename T, std::size_t block_size>
MemoryPool<T, block_size>::~MemoryPool() noexcept {
    allocated_block_t *curr = m_allocated_block_head;
    allocated_block_t *next = nullptr;
    while (curr != nullptr) {
        next = curr->next;
        operator delete(curr);
        curr = next;
    }
}

template <typename T, std::size_t block_size>
inline typename MemoryPool<T, block_size>::pointer
MemoryPool<T, block_size>::allocate(size_type n, const_pointer hint) {
    slot_head_t next, orig = m_free.load();
    do {
        if (orig.node == nullptr) {
            allocate_block();
            orig = m_free.load();
        }
        next.aba = orig.aba + 1;
        next.node = orig.node->next;
    }
    while (!atomic_compare_exchange_weak(&m_free, &orig, next));

    return reinterpret_cast<pointer>(orig.node);
}

template <typename T, std::size_t block_size>
inline void
MemoryPool<T, block_size>::deallocate(pointer p, size_type n)
{
    slot_head_t next, orig = m_free.load();
    slot_t *tp = reinterpret_cast<slot_t *>(p);
    do {
        tp->next = orig.node;
        next.aba = orig.aba + 1;
        next.node = tp;
    }
    while (!atomic_compare_exchange_weak(&m_free, &orig, next));
}

template <typename T, std::size_t block_size>
template <class U, class... Args>
inline void
MemoryPool<T, block_size>::construct(U* p, Args&&... args) {
    new (p) U (std::forward<Args>(args)...);
}

template <typename T, std::size_t block_size>
template <class U>
inline void
MemoryPool<T, block_size>::destroy(U* p) {
    p->~U();
}

template <typename T, std::size_t block_size>
template <class... Args>
inline typename MemoryPool<T, block_size>::pointer
MemoryPool<T, block_size>::new_element(Args&&... args) {
    pointer result = allocate();
    construct<value_type>(result, std::forward<Args>(args)...);
    return result;
}

template <typename T, std::size_t block_size>
inline void
MemoryPool<T, block_size>::delete_element(pointer p) {
    if (p != nullptr) {
        p->~value_type();
        deallocate(p);
    }
}

template <typename T, std::size_t block_size>
inline void
MemoryPool<T, block_size>::allocate_block() {
    spin_lock<std::atomic_flag> lock(m_lock);
    // After coming out of the lock, if the condition that got us here is now false, we can safely return
    // and do nothing.  This means another thread beat us to the allocation.  If we don't do this, we could
    // potentially allocate an entire block_size of memory that would never get used.
    if (m_free.load().node != nullptr) { return; }

    allocated_block_t *new_block = new allocated_block_t();
    new_block->next = m_allocated_block_head;
    m_allocated_block_head = new_block;

    new_block->buffer = reinterpret_cast<char *>(operator new(block_size * sizeof(slot_t)));

    // Pad block body to satisfy the alignment requirements for elements
    char *body = new_block->buffer + sizeof(slot_t *);
    std::size_t body_padding = pad_pointer(body, alignof(slot_t));
    char *start = body + body_padding;
    char *end = (new_block->buffer + (block_size * sizeof(slot_t)));

    // Update the old last slot's next ptr to point to the first slot of the new block
    if (m_last_slot)
        m_last_slot->next = reinterpret_cast<slot_t *>(start);

    // We'll never get exactly the number of objects requested, but it should be close.
    for (size_t i = 0; (start + sizeof(slot_t)) < end; i++) {
        reinterpret_cast<slot_t *>(start)->next = reinterpret_cast<slot_t *>(start + sizeof(slot_t));
        start += sizeof(slot_t);
        m_max_size++;
    }

    // "start" should now point to one byte past the end of the last slot.  Subtract the size of slot_t from it to
    // get a pointer to the beginning of the last slot.
    m_last_slot = reinterpret_cast<slot_t *>(start - sizeof(slot_t));
    m_last_slot->next = nullptr;

    // Update the head of the free list to point to the start of the new block
    slot_head_t first;
    first.aba = 0;
    first.node = reinterpret_cast<slot_t *>(body + body_padding);
    m_free.store(first);
}
#endif