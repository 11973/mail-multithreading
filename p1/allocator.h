#include <stdexcept>
#include <string>
#include <set>
#include <vector>

enum class AllocErrorType {
    InvalidFree,
    NoMemory,
};

class AllocError: std::runtime_error {
private:
    AllocErrorType type;

public:
    AllocError(AllocErrorType _type, std::string message):
            runtime_error(message),
            type(_type)
    {}

    AllocErrorType getType() const { return type; }
};

class Allocator;

class Pointer {
public:
    void *get() const;
    Pointer() { idx = (size_t) -1; base = nullptr; };
private:
    friend class Allocator;
    Pointer(size_t idx, Allocator *base): idx(idx), base(base) {};
    size_t idx;
    Allocator *base;
};

class Allocator {
public:
    Allocator(void *base, size_t size);
    
    Pointer alloc(size_t N);
    void realloc(Pointer &p, size_t N);
    void free(Pointer &p);
    void defrag();
    std::string dump() { return ""; };
    
private:
    friend class Pointer;
    char *base;
    char *next;
    size_t free_space;
    char *end;
    struct AllocUnit
    {
        char *link;
        size_t size;
        size_t idx;
        AllocUnit(char *link, size_t size, size_t idx);
    };
    friend bool operator < (AllocUnit a, AllocUnit b);
    std::set < AllocUnit > allocated;
    std::vector < size_t > freed;
    std::vector < std::set <AllocUnit>::iterator > parted;
    static std::set <AllocUnit>::iterator it(Pointer p) { return p.base->parted[p.idx]; };
};
