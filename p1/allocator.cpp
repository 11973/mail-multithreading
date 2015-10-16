#include "allocator.h"
#include <cstring>


bool operator < (Allocator::AllocUnit a, Allocator::AllocUnit b) { return a.link < b.link; }

Allocator::AllocUnit::AllocUnit(char *link, size_t size, size_t idx):
    link(link), size(size), idx(idx) {};

void *
Pointer::get() const
{
    if (base == nullptr) {
        return nullptr;
    }
    if (this->idx >= this->base->parted.size()) {
        return nullptr;
    }
    if (Allocator::it(*this) == this->base->allocated.end()) {
        return nullptr;
    }
    return this->base->parted[idx]->link;
}

Allocator::Allocator(void *base, size_t size) : parted(size, this->allocated.end())
{
    this->base = (char *) base;
    this->next = (char *) base;
    this->free_space = size;
    this->end = (char *) base + size;
    this->freed.reserve(size / 16);
    for (size_t i = this->freed.capacity() - 1; i != (size_t) -1; i--) {
        this->freed.push_back(i);
    }
}

Pointer
Allocator::alloc(size_t N)
{
    if (this->free_space < N) {
        throw AllocError(AllocErrorType::NoMemory, "");
    }

    if (this->freed.empty()) {
        this->freed.push_back(this->freed.capacity());
        this->parted.resize(this->freed.capacity(), this->allocated.end());
    }
    
    if (this->next + N <= this->end) {
        auto tmp = this->allocated.insert(AllocUnit(this->next, N, this->freed.back()));
        this->parted[this->freed.back()] = tmp.first;
        Pointer res(this->freed.back(), this);
        this->freed.pop_back();

        this->free_space -= N;
        this->next += N;
        
        return res;
    }

    this->defrag();
    return this->alloc(N);    
}

void
Allocator::free(Pointer &p)
{
    if (p.idx >= this->parted.size() || this->parted[p.idx] == this->allocated.end()) {
        throw AllocError(AllocErrorType::InvalidFree, "");
    }

    auto it = this->parted[p.idx];
    this->parted[p.idx] = this->allocated.end();
    this->free_space += it->size;
    this->freed.push_back(it->idx);
    this->allocated.erase(it);    
}

void
Allocator::defrag()
{
    if (this->allocated.begin() == this->allocated.end()) {
        this->next = this->base;
        return;
    }
    if (this->allocated.begin()->link != this->base) {
        auto it = this->allocated.begin();
        std::memmove(this->base, it->link, it->size);
        auto tmp = this->allocated.insert(AllocUnit(this->base, it->size, it->idx));
        this->parted[it->idx] = tmp.first;
        this->allocated.erase(it);
    }

    auto it = this->allocated.begin();
    it++;
    
    auto pred = this->allocated.begin();
    while (it != allocated.end()) {
        if (it->link != pred->link + pred->size) {
            std::memmove(pred->link + pred->size, it->link, it->size);

            auto tmp = this->allocated.insert(AllocUnit(pred->link + pred->size, it->size, it->idx));
            this->parted[it->idx] = tmp.first;
            auto to_del = it;
            it++;
            this->allocated.erase(to_del);
            it--;
        }
        it++;
        pred++;
    }
    this->next = pred->link + pred->size;
}

void
Allocator::realloc(Pointer &p, size_t N)
{
    if (p.get() == nullptr) {
        p = this->alloc(N);
        return;
        throw AllocError(AllocErrorType::InvalidFree, "");
    }
    if (N < it(p)->size) {
        this->free_space -= p.base->parted[p.idx]->size - N;
        AllocUnit tmp(it(p)->link, N, it(p)->idx);
        this->allocated.erase(it(p));
        this->allocated.insert(tmp);
        return;
    }
    
    if (N - it(p)->size > this->free_space) {
        throw AllocError(AllocErrorType::NoMemory, "");
    }

    char *buf = (char *) ::calloc(it(p)->size, sizeof(*buf));
    size_t buf_size = it(p)->size;
    std::memcpy(buf, it(p)->link, it(p)->size);
    this->allocated.erase(it(p));
    this->defrag();
    p = this->alloc(N);
    std::memcpy(it(p)->link, buf, buf_size);
    ::free(buf);
}
