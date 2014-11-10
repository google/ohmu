//===- event.cpp -----------------------------------------------*- C++ --*-===//
// Copyright 2014  Google
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//===----------------------------------------------------------------------===//

// Note: approach for handling pressure that exceeds the number of bits we have
// in our "registerSet" bitfields:  Given that we use a bitmask to store the
// remaining valid registers for a given live range, if that set becomes all 0
// we cannot allocate a register for this register and a "spill" must occur.  I
// use "spill" in quotes because we may have more registers than we do bits.
// Once we've completed a pass of register allocation we will have allocated
// registers up to the bitwidth of our registerSet bit set.  Everything that
// couldn't have been allocated will have a 0 in their register field.  We can
// run another pass to allocate these independantly, ignoring the already
// allocated liveRanges.  These passes can continue to allocate registers if
// registers exist or they can allocate spill slots.  After some number of
// finite passes, the allocator can assign fixed slots to the remaining live
// ranges.

// Copying at an instruction:  Sometimes it's necessary to copy arguments (e.g.
// because they're destroyed by the instruction but still live) and results
// (because they are used multiple times).  Sometimes they instruction will need
// additional registers to perform these copies e.g. c = (a - b) + a;  The sub
// will destroy a, but the add needs it.  However, if the register allocator
// allocates a and b to the same registers as the continuing a and c then we
// must destroy the register that contains b.  However the semantics of x86 say
// we must destroy the register contining a.  Given that we can only see two
// registers and we need 3 (storage for a, result (c) and argument b) we must
// allocate a register or spill slot.  This occurs after general register
// allocation and require us knowing which registers are free at point in which
// the instruction executes.

// Live ranges:  Each instruction result (SSA value) can have multiple live
// ranges.  These live ranges can, in general, overlap.  Overlapping must occur
// at a point when a destructive operation consumes a value in the middle of its
// live range.  

// Live range splitting/merging: Using the model that each use corresponds to a live
// range leads to an inefficiency if we use the same argument twice for an
// instruction.  Each use constitutes a 'different' live range even though it
// shouldn't.

// Literal hoisting:  In general, it's not necessary to allocate registers for
// literals.  They can use one

#include "types.h"
#include "interface.h"
#include <vector>

using namespace clang::threadSafety::til;
//using namespace Jagger;

#ifdef _MSC_VER
extern "C" unsigned char _BitScanForward(unsigned long * Index, unsigned long Mask);
#endif

static int lowIndex(unsigned x) {
  unsigned long a;
  _BitScanReverse(&a, (unsigned long)x);
  return (int)a;
}

template <typename Type>
struct RangeAdaptor {
  RangeAdaptor(Type* data, size_t size) : _begin(data), _end(data + size) {}
  RangeAdaptor(Type* begin, Type* end) : _begin(begin), _end(end) {}
  Type* begin() const { return _begin; }
  Type* end() const { return _end; }

 private:
  Type* _begin;
  Type* _end;
};

template <typename Type>
RangeAdaptor<Type> AdaptRange(Type* data, size_t size) {
  return RangeAdaptor<Type>(data, size);
}

template <typename Type>
RangeAdaptor<Type> AdaptRange(Type* begin, Type* end) {
  return RangeAdaptor<Type>(begin, end);
}

struct LiveRange {
  struct Iterator {
    Iterator(EventStream events, size_t index)
        : events(events), index(index), skipUntil(index) {}
    EventRef operator*() const { return events[index]; }
    Iterator& operator++() {
      if (events.codes[index] == HEADER_DOMINATES)
        skipUntil = events.data[index];
      else if (events.codes[index] == HEADER && index <= skipUntil)
        index = events.data[index];
      index--;
      return *this;
    }
    bool operator!=(const Iterator& a) const { return index != a.index; }

   private:
     EventStream events;
    size_t index;
    size_t skipUntil;
  };

  LiveRange(EventStream events, size_t def, size_t use)
      : events(events), def(def), use(use) {}
  Iterator begin() const { return Iterator(events, use - 1); }
  Iterator end() const { return Iterator(EventStream(), def); }

 private:
  EventStream events;
  size_t def;
  size_t use;
};

namespace Jagger {
Opcode countedMarker;
}  // namespace Jagger

//namespace {
struct RegisterAllocator {
  void encode(SCFG* const* cfg, size_t numCFGs);

  static size_t countEvents(SExpr* expr);
  static size_t emitBlockHeader(EventStream events, size_t index, Block* block);
  static size_t emitArgument   (EventStream events, size_t index, Phi* phi);
  static size_t emitEvents     (EventStream events, size_t index, SExpr* expr);
  static size_t emitTerminator (EventStream events, size_t index, Terminator* term, BasicBlock* basicBlock);

  Block* blocks;
  size_t numBlocks;
  EventStream events;
  size_t numEvents;
};
//}  // namespace

void RegisterAllocator::encode(SCFG* const* cfgs, size_t numCFGs) {
  if (!numCFGs) return;

  {
    // Count the blocks.
    size_t numBlocks = 0;
    for (auto cfg : AdaptRange(cfgs, numCFGs))
      numBlocks += cfg->numBlocks();
    this->numBlocks = numBlocks;
  }

  assert(numBlocks);
  blocks = new Block[numBlocks];

  {
    // Initialize blocks and count instructions.
    Block* nextBlock = blocks;
    size_t numEvents = 0;
    for (auto cfg : AdaptRange(cfgs, numCFGs))
      for (auto basicBlock : *cfg) {
      basicBlock->setBackendID(nextBlock);
      nextBlock->dominator = nullptr;
      nextBlock->head = nextBlock;
      size_t size = 1;
      if (BasicBlock* parent = basicBlock->parent()) {
        nextBlock->dominator = (Block*)parent->getBackendID();
        if (basicBlock->PostDominates(*parent) ||
          nextBlock->dominator + 1 == nextBlock)
          nextBlock->head = nextBlock->dominator->head;
      }
      for (auto arg : basicBlock->arguments()) size += countEvents(arg);
      for (auto instr : basicBlock->instructions())
        size += countEvents(instr);
      size += countEvents(basicBlock->terminator());
      nextBlock->firstEvent = numEvents;
      nextBlock->numEvents = size;
      numEvents += size;
      nextBlock++;
      }
    this->numEvents = numEvents;
  }

  assert(numEvents);
  events.codes = new Opcode[numEvents];
  events.data = new Data[numEvents];

  {
    // Emit instructions.
    size_t index = 0;
    Block* block = blocks;
    for (auto cfg : AdaptRange(cfgs, numCFGs))
      for (auto basicBlock : *cfg) {
      assert(block->firstEvent == index);
      index = emitBlockHeader(events, index, block);
      for (auto arg : basicBlock->arguments())
        index = emitArgument(events, index, cast<Phi>(arg));
      for (auto instr : basicBlock->instructions())
        index = emitEvents(events, index, instr);
      index = emitTerminator(events, index, basicBlock->terminator(),
        basicBlock);
      assert(index == block->firstEvent + block->numEvents);
      block++;
      }
  }

  // Verify integrety
  for (size_t i = 0; i < numEvents; i++) {
    auto event = events[i];
    if (event.code == USE) {
      auto target = events[event.data];
      //printf("%d : %d %d\n", i, event.data, target.data);
      assert(target.data == event.data);
      //printf("%02x\n", target.code);
      assert((target.code & VALUE_MASK) == VALUE ||
             (target.code & VALUE_MASK) == COPY ||
             (target.code & VALUE_MASK) == PHI);
    } else if ((event.code & VALUE_MASK) == COPY) {
      assert(events[i - 1].code == USE);
    } else if (event.code == PHI_COPY) {
      assert(events[event.data].code == PHI);
    }
    if (event.code >= VALUE) assert(event.code & REGS_MASK);
  }

  // Determine last uses.
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

  //Commute commutable operations to save copies.
  for (size_t i = 0; i < numEvents; i++) {
    if (events[i].code != ADD) continue;
    if (events[i - 3].code == MUTED_USE && events[i - 4].code == USE) {
      std::swap(events[i - 3].code, events[i - 4].code);
      std::swap(events[i - 3].data, events[i - 4].data);
    }
  }

  // Link copies.
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

  // Traverse the keys.
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

  // Mark conflicts.
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
      } else {
        //printf("%d : %d : %d %d\n", i, &other.code - events.codes, key, other.data);
        assert(other.data != key);
        auto other_key = other.data;
        conflicts.push_back(
            std::make_pair(std::min(key, other_key), std::max(key, other_key)));
        //printf("conflict: %d : %d\n", key, other_key);
      }
    }
  }

  // Traverse the keys.
  for (size_t i = 0; i < numEvents; i++) {
    auto event = events[i];
    if (!(event.code == USE || event.code == MUTED_USE ||
          (event.code & VALUE) && (event.code & VALUE_MASK) != VALUE))
      continue;
    events[i].data = events[events[i].data].data;
  }

  // Clean conflicts? (otherwise they confuse the goal marker)
  for (size_t i = 0; i < numEvents; i++)
    if (events[i].code >= USE_FIXED && events[i].data == i)
      events[i].code = NOP;

  // Mark goals.
  std::vector<std::pair<Data, Data>> goals;
  for (size_t i = 0; i < numEvents; i++) {
    auto event = events[i];
    if ((event.code & VALUE_MASK) != PHI_COPY) continue;
    auto use = events[i - 1];
    if (use.code == MUTED_USE) continue;
    if (event.data == use.data) continue;
    goals.push_back(std::make_pair(std::min(event.data, use.data),
                                   std::max(event.data, use.data)));
  }

  std::sort(conflicts.begin(), conflicts.end());
  if (conflicts.size() > 1) {
    size_t j = 1;
    for (size_t i = 1, e = conflicts.size(); i != e; ++i)
      if (conflicts[i - 1] != conflicts[i]) conflicts[j++] = conflicts[i];
    conflicts.resize(j);
  }

  std::sort(goals.begin(), goals.end());
  if (goals.size() > 1) {
    size_t j = 1;
    for (size_t i = 1, e = goals.size(); i != e; ++i)
      if (goals[i - 1] != goals[i]) goals[j++] = goals[i];
    goals.resize(j);
  }

  //for (auto i : goals)
  //  printf("- %d %d\n", i.first, i.second);
  //for (auto i : conflicts)
  //  printf("X %d %d\n", i.first, i.second);
  //for (auto i : fixed_conflicts)
  //  printf("* %d %d\n", i.first, i.second);
  //return;

  std::vector<Work> work;
  std::vector<Sidecar> sidecar;
  for (size_t i = 0; i < numEvents; i++) {
    auto event = events[i];
    if ((event.code & (VALUE | IS_FIXED)) != VALUE || event.data != i) continue;
    event.data = 0;
    work.push_back(Work(i));
    sidecar.push_back(Sidecar());
  }

  for (auto i : conflicts) {
    events[i.first].data++;
    events[i.second].data++;
  }

  for (auto& i : work)
    i.count = events[i.index].data;
  std::stable_sort(work.begin(), work.end());
  for (size_t i = 0, e = work.size(); i != e; ++i)
    events[work[i].index].data = i;

  for (auto& i : conflicts) {
    auto a = i.first;
    auto b = i.second;
    i.first = events[i.first].data;
    i.second = events[i.second].data;
    //if (i.first == 49 || i.second == 49)
    //  printf(">>$? %d->%d, %d->%d\n", a, i.first, b, i.second);
    if (i.first > i.second)
      std::swap(i.first, i.second);
  }
  std::sort(conflicts.begin(), conflicts.end());

  for (auto& i : goals) {
    i.first = events[i.first].data;
    i.second = events[i.second].data;
    if (i.first > i.second)
      std::swap(i.first, i.second);
  }
  std::sort(goals.begin(), goals.end());

  for (auto i : goals)
    printf("- %d %d\n", i.first, i.second);
  for (auto i : conflicts)
    printf("X %d %d\n", i.first, i.second);
  //for (auto i : fixed_conflicts)
  //  printf("* %d %d\n", i.first, i.second);

  // Mark invalid.
  for (auto i : fixed_conflicts)
    sidecar[events[i.first].data].invalid |= i.second;

  // Mark preferred.
  for (size_t i = 0; i < numEvents; i++) {
    auto event = events[i];
    if (event.code < USE_FIXED) continue;
    sidecar[events[event.data].data].preferred |= 1 << ((event.code >> 3) & 7);
  }

  printf("work.size = %d\n", work.size());
  for (size_t i = 0, e = work.size(); i != e; ++i) {
    printf("%3d %x : %02x %02x\n", work[i].index, work[i].count,
      sidecar[i].invalid, sidecar[i].preferred);
  }
  printf("\n");

  for (size_t i = 0, c = 0, g = 0, e = work.size(), c_end = conflicts.size(),
              g_end = goals.size();
       i != e; ++i) {
    auto preferred = sidecar[i].preferred;
    auto invalid = sidecar[i].invalid;
    auto unpreferred = 0;
    for (size_t j = g; j < g_end && goals[j].first == i; j++)
      preferred |= sidecar[goals[j].second].preferred;
    for (size_t j = c; j < c_end && conflicts[j].first == i; j++)
      unpreferred |= sidecar[conflicts[j].second].preferred;
    auto x = ~unpreferred & preferred & ~invalid;
    if (!x) x = preferred & ~invalid;
    if (!x) x = ~unpreferred & ~invalid;
    if (!x) x = ~invalid;
    x = x & -x;
    work[i].count = x;
    events[work[i].index].data = x;
    for (; g < g_end && goals[g].first == i; g++)
      sidecar[goals[g].second].preferred |= x;
    for (; c < c_end && conflicts[c].first == i; c++) {
      printf(">>? %d\n", conflicts[c].second);
      sidecar[conflicts[c].second].invalid |= x;
    }
  }

  printf("work.size = %d\n", work.size());
  for (size_t i = 0, e = work.size(); i != e; ++i) {
    printf("%3d %x : %02x %02x\n", work[i].index, work[i].count,
      sidecar[i].invalid, sidecar[i].preferred);
  }
  printf("\n");

#if 0
  // Patch up all of the jump targets.
  for (auto block : *cfg) {
    if (!block->terminator())
      continue;
    switch (block->terminator()->opcode()) {
    case COP_Goto:
      instrs[block->VX64BlockEnd].arg1 =
        cast<Goto>(block->terminator())->targetBlock()->VX64BlockStart - block->VX64BlockEnd;
      break;
    case COP_Branch:
      instrs[block->VX64BlockEnd].arg1 =
        cast<Branch>(block->terminator())->elseBlock()->VX64BlockStart - block->VX64BlockEnd;
      break;
    }
  }
#endif
}

size_t countedMarker = -1;

size_t RegisterAllocator::countEvents(SExpr* expr) {
  if (expr->getBackendID()) return 0;
  expr->setBackendID((void*)countedMarker);
  switch (expr->opcode()) {
    case COP_Literal:
      return 2;
    case COP_Variable:
      return countEvents(cast<Variable>(expr)->definition());
    case COP_BinaryOp: {
      int size = countEvents(cast<BinaryOp>(expr)->expr0()) +
                 countEvents(cast<BinaryOp>(expr)->expr1());
      switch (cast<BinaryOp>(expr)->binaryOpcode()) {
      case BOP_Add: return size + 5;
      case BOP_Sub: return size + 5;
      case BOP_Mul: return size + 10;
      case BOP_Eq: return size + 4;
      case BOP_Lt: return size + 4;
      case BOP_Leq: return size + 4;
      }
      assert(false);
      return 0;
    }
    case COP_Phi:
      return 1;
    case COP_Goto:
      return cast<Goto>(expr)->targetBlock()->arguments().size() * 2 + 1;
    case COP_Branch:
      return countEvents(cast<Branch>(expr)->condition()) + 2;
    case COP_Return:
      return countEvents(cast<Return>(expr)->returnValue()) + 3;
    default:
      printf("unknown opcode: %d\n", expr->opcode());
      assert(false);
      return 0;
  }
}

size_t RegisterAllocator::emitBlockHeader(EventStream events, size_t index,
                                          Block* block) {
  if (!block->dominator)
    events[index++] = Event(NOP, 0);
  else if (block->head != block)
    events[index++] = Event(HEADER_DOMINATES, block->head->firstEvent);
  else
    events[index++] = Event(
        HEADER, block->dominator->firstEvent + block->dominator->numEvents);
  return index;
}

size_t RegisterAllocator::emitArgument(EventStream events, size_t index, Phi* phi) {
  size_t result = index;
  events[index++] = Event(PHI | GP_REGS, result);
  phi->setBackendID((void*)result);
  return index;
}

size_t RegisterAllocator::emitEvents(EventStream events, size_t index,
                                     SExpr* expr) {
  if ((size_t)expr->getBackendID() != countedMarker) return index;
  size_t result = 0;
  switch (expr->opcode()) {
    case COP_Literal: {
      Literal* literal = cast<Literal>(expr);
      switch (literal->valueType().Base) {
        case ValueType::BT_Int:
          result = index;
          events[index++] = Event(VALUE | GP_REGS, result);
          events[index++] = Event(INT32, (Data)literal->as<int>().value());
          break;
        default:
          assert(false);
      }
      break;
    }
    case COP_Variable: {
      auto definition = cast<Variable>(expr)->definition();
      index = emitEvents(events, index, definition);
      result = (size_t)definition->getBackendID();
      break;
    }
    case COP_BinaryOp: {
      auto binaryOp = cast<BinaryOp>(expr);
      index = emitEvents(events, index, binaryOp->expr0());
      index = emitEvents(events, index, binaryOp->expr1());
      //printf("?? %d : %d %d\n", binaryOp->binaryOpcode(),
      //       binaryOp->expr0()->opcode(),
      //       binaryOp->expr1()->opcode());
      auto arg0 = (size_t)binaryOp->expr0()->getBackendID();
      auto arg1 = (size_t)binaryOp->expr1()->getBackendID();
      switch (binaryOp->binaryOpcode()) {
        case BOP_Add:
          // TODO: add cases for types
          // TODO: sort out the lea/add issue
          result = index + 2;
          events[index++] = Event(USE, arg1);
          events[index++] = Event(USE, arg0);
          events[index++] = Event(COPY | GP_REGS, result);
          events[index++] = Event(USE_EFLAGS, result + 1);
          events[index++] = Event(ADD, 0);
          break;
        case BOP_Sub:
          // TODO: add cases for types
          result = index + 1;
          events[index++] = Event(USE, arg0);
          events[index++] = Event(COPY | GP_REGS, result);
          events[index++] = Event(USE, arg1);
          events[index++] = Event(USE_EFLAGS, result + 2);
          events[index++] = Event(SUB, 0);
          break;
        case BOP_Mul:
          result = index + 7;
          events[index++] = Event(USE, arg0);
          events[index++] = Event(USE, arg1);
          events[index++] = Event(USE_EAX, arg0);
          events[index++] = Event(USE_EDX, arg0);
          events[index++] = Event(USE_EAX, arg1);
          events[index++] = Event(USE_EDX, arg1);
          events[index++] = Event(USE_EAX, result);
          events[index++] = Event(VALUE | GP_REGS, result);
          events[index++] = Event(USE_EFLAGS, result + 1);
          events[index++] = Event(MUL, 0);
          break;
        case BOP_Eq:
          result = index + 2;
          events[index++] = Event(USE, arg0);
          events[index++] = Event(USE, arg1);
          events[index++] = Event(VALUE | FLAGS_REGS, result);
          events[index++] = Event(EQ, 0);
          break;
        case BOP_Lt:
          result = index + 2;
          events[index++] = Event(USE, arg0);
          events[index++] = Event(USE, arg1);
          events[index++] = Event(VALUE | FLAGS_REGS, result);
          events[index++] = Event(LT, 0);
          break;
        case BOP_Leq:
          result = index + 2;
          events[index++] = Event(USE, arg0);
          events[index++] = Event(USE, arg1);
          events[index++] = Event(VALUE | FLAGS_REGS, result);
          events[index++] = Event(LE, 0);
          break;
        default:
          assert(false);
      }
      break;
    }
    default:
      printf("unknown opcode: %d\n", expr->opcode());
      assert(false);
      return 0;
  }
  expr->setBackendID((void*)result);
  return index;
}

// the index for this block in the target's phis
// TODO: make this not a search;
static size_t getPhiIndex(BasicBlock* basicBlock, BasicBlock* targetBlock) {
  auto& predecessors = targetBlock->predecessors();
  for (size_t i = 0, e = predecessors.size(); i != e; ++i)
    if (predecessors[i] == basicBlock) return i;
  return 0;
}

size_t RegisterAllocator::emitTerminator(EventStream events,
                                         size_t index, Terminator* term,
                                         BasicBlock* basicBlock) {
  size_t result = 0;
  switch (term->opcode()) {
    case COP_Goto: {
      auto jump = cast<Goto>(term);
      auto targetBasicBlock = jump->targetBlock();
      auto phiIndex = getPhiIndex(basicBlock, targetBasicBlock);
      auto& arguments = targetBasicBlock->arguments();

      auto numArguments = (int)arguments.size();
      //auto block = (Block*)basicBlock->getBackendID();
      auto targetBlock = (Block*)targetBasicBlock->getBackendID();
      auto targetPhiIndex = targetBlock->firstEvent + 1;

      // This loop should emit nothing! TODO: validate and remove
      // for (auto arg : arguments)
      //  nextEvent = emitEvents(nextEvent, cast<Phi>(arg)->values()[phiIndex]);
      for (auto arg : arguments) {
        auto arg0 = (size_t)cast<Phi>(arg)->values()[phiIndex]->getBackendID();
        events[index++] = Event(USE, arg0);
        events[index++] = Event(PHI_COPY | GP_REGS, targetPhiIndex++);
      }
      // TODO: blockID is the wrong value, we should be getting backendID
      events[result = index++] = Event(JUMP, jump->targetBlock()->blockID());
      break;
    }
    case COP_Branch: {
      auto branch = cast<Branch>(term);
      auto condition = branch->condition();
      index = emitEvents(events, index, condition);
      events[index++] = Event(USE, (size_t)condition->getBackendID());
      // TODO: blockID is the wrong value, we should be getting backendID
      events[result = index++] = Event(BRANCH, branch->elseBlock()->blockID());
      break;
    }
    case COP_Return: {
      auto ret = cast<Return>(term);
      auto value = ret->returnValue();
      index = emitEvents(events, index, value);
      auto arg0 = (size_t)value->getBackendID();
      events[index++] = Event(USE, arg0);
      events[index++] = Event(USE_EAX, arg0);
      events[result = index++] = Event(RET, 0);
      break;
    }
  }
  term->setBackendID((void*)result);
  return index;
}

void encode(SCFG* cfg, char* output) {
  RegisterAllocator allocator;
  allocator.encode(&cfg, 1);

  print_stream(allocator.events, allocator.numEvents);
  print_asm(allocator.events, allocator.numEvents);
  make_asm(allocator.events, allocator.numEvents);
  //stream.printWalks();
  //X64Builder builder;
  //emitASM(builder, InstructionStream.events.data(), InstructionStream.events.size());
}
