#include "types.h"

struct LiveRange {
  struct Iterator {
    Iterator(Event* events, size_t index)
      : events(events), index(index), skipUntil(index) {}
    Event& operator*() const { return events[index]; }
    Iterator& operator++() {
      if (events[index].code == GOTO_HEADER)
        skipUntil = events[index].data;
      else if (events[index].code == WALK_HEADER && index <= skipUntil)
        index = events[index].data;
      index--;
      return *this;
    }
    bool operator!=(const Iterator& a) const { return index != a.index; }

  private:
    Event* events;
    size_t index;
    size_t skipUntil;
  };

  LiveRange(Event* events, size_t def, size_t use)
    : events(events), def(def), use(use) {}
  Iterator begin() const { return Iterator(events, use - 1); }
  Iterator end() const { return Iterator(nullptr, def); }

private:
  Event* events;
  size_t def;
  size_t use;
};

struct Work {
  explicit Work(unsigned index) : index(index) {}
  bool operator <(const Work& a) const { return count < a.count; }
  unsigned count;
  unsigned index;
};

struct Interaction {
  unsigned first;
  unsigned second;
};

struct IntermediateState {
  unsigned preferred;
  unsigned invalid;
  //unsigned countDown;
};

struct RegisterAllocator {
  void findLastUses();
  void commuteOperations();
  void linkCopies();
  void traverseKeys();
  void markConflicts();

  Event* events;
  Work* work;
  IntermediateState* state;
  Interaction* interactions;
  Block* blocks;
  size_t numInteractions;
  size_t numEvents;
  size_t numWorkItems;
  size_t numBlocks;
};

void RegisterAllocator::findLastUses() {
  for (size_t i = 0; i < numEvents; i++) {
    if (events[i].code != USE) continue;
    size_t target = events[i].data;
    for (auto other : LiveRange(events, target, i)) {
      if (other.code == USE && other.data == target) {
        other.code = MUTED_USE;
        // TODO: terminate here if we can.
      }
    }
  }
}

void RegisterAllocator::commuteOperations() {
  for (size_t i = 0; i < numEvents; i++) {
    if (events[i].code != ADD) continue;
    if (events[i - 3].code == MUTED_USE && events[i - 4].code == USE) {
      std::swap(events[i - 3].code, events[i - 4].code);
      std::swap(events[i - 3].data, events[i - 4].data);
    }
  }
}

void RegisterAllocator::linkCopies() {
  for (size_t i = 0; i < numEvents; i++) {
    auto event = events[i];
    if ((event.code & VALUE_MASK) == COPY) {
      auto use = events[i - 1];
      if (use.code == MUTED_USE) continue;
      event.data = use.data;
      use.code = MUTED_USE;
      event.code = MUTED_USE;
    }
    else if ((event.code & VALUE_MASK) == PHI_COPY) {
      auto use = events[i - 1];
      if (use.code == MUTED_USE) continue;
      auto phi = events[event.data];
      if (phi.data == event.data || phi.data > use.data) phi.data = use.data;
    }
  }
}

void RegisterAllocator::traverseKeys() {
  for (size_t i = 0; i < numEvents; i++) {
    auto event = events[i];
    if (!(event.code & VALUE)) continue;
    size_t key = i;
    do {
      key = events[key].data;
    } while (events[key].data != key);
    events[i].data = (Data)key;
  }
  for (size_t i = 0; i < numEvents; i++) {
    auto event = events[i];
    if (event.code != USE && event.code != MUTED_USE) continue;
    // Note: this is a bit tricky...
    if ((events[event.data].code & VALUE_MASK) == PHI) continue;
    event.data = events[event.data].data;
  }
}


void RegisterAllocator::markConflicts() {
  std::vector<std::pair<Data, Data>> conflicts;
  std::vector<std::pair<Data, Data>> fixed_conflicts; //< can be elimintated, I think
  for (size_t i = 0; i < numEvents; i++) {
    if (events[i].code != USE) continue;
    size_t j = events[i].data;
    auto key = events[events[j].data].data; //< one more level of indirection because of phis
    for (auto other : LiveRange(events, j, i)) {
      if (other.code < VALUE) continue;
      if (other.code & IS_FIXED) {
        if ((events[j].code & 0x7) != (other.code & 0x7)) continue;
        fixed_conflicts.push_back(
          std::make_pair(key, 1 << ((other.code >> 3) & 0x7)));
      }
      else {
        //printf("%d : %d : %d %d\n", i, &other.code - events.codes, key, other.data);
        assert(other.data != key);
        auto other_key = other.data;
        conflicts.push_back(
          std::make_pair(std::min(key, other_key), std::max(key, other_key)));
        //printf("conflict: %d : %d\n", key, other_key);
      }
    }
  }
}