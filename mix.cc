#if __cplusplus < 201703L
#error "This code requires at least C++17"
#endif
static_assert(sizeof(int) >= 4, "This code requires at least 32 bit int.");

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "SDL.h"

#define INLINE [[gnu::always_inline]] inline
#define NOINLINE [[gnu::noinline]]

namespace {

template <typename... Args>
std::string Format(const Args&... args) {
  std::stringstream stream;
  (stream << ... << args);
  return stream.str();
}

struct MixException : public std::runtime_error {
  explicit MixException(const char* message) : std::runtime_error(message) {}

  template <typename... Args>
  explicit MixException(const Args&... args)
      : std::runtime_error(Format(args...)) {}
};

// It is important not to throw directly, because that makes the throwing
// function large and prevents optimizations/inlining.
template <typename... Args>
NOINLINE [[noreturn]] void ThrowMixException(const Args&... args) {
  throw MixException(args...);
}

// clang-format off
enum OpCode {
  kNop, kAdd, kSub, kMul, kDiv, kSpec, kShift, kMove, kLda, kLd1, kLd2, kLd3,
  kLd4, kLd5, kLd6, kLdx, kLdan, kLd1n, kLd2n, kLd3n, kLd4n, kLd5n, kLd6n,
  kLdxn, kSta, kSt1, kSt2, kSt3, kSt4, kSt5, kSt6, kStx, kStj, kStz, kJbus,
  kIoc, kIn, kOut, kJred, kJump, kJa, kJ1, kJ2, kJ3, kJ4, kJ5, kJ6, kJx,
  kAddrOpA, kAddrOp1, kAddrOp2, kAddrOp3, kAddrOp4, kAddrOp5, kAddrOp6,
  kAddrOpX, kCmpa, kCmp1, kCmp2, kCmp3, kCmp4, kCmp5, kCmp6, kCmpx, kNumOpCodes
};

enum FieldValues {
  /* Arithmetic: */ kFloatField = 6,
  /* Special:    */ kNumField = 0, kCharField, kHltField,
  /* Shift:      */ kSlaField = 0, kSraField, kSlaxField, kSraxField,
                    kSlcField, kSrcField, kSlbField, kSrbField,
  /* Jump:       */ kJmpField = 0, kJsjField, kJovField, kJnovField, kJlField,
                    kJeField, kJgField, kJgeField, kJneField, kJleField,
  /* RegJump:    */ kJnField = 0, kJzField, kJpField, kJnnField, kJnzField,
                    kJnpField, kJevenField, kJoddField,
  /* AddrOp:     */ kIncField = 0, kDecField, kEntField, kEnnField,
  /* Devices:    */ kCardReaderField = 16, kCardPunchField=17, kLinePrinterField=18,
                    KTerminalField=19, kSysCallFieldEx=21
};

enum BlockSizes { bsCard = 16, bsLinePrinter = 24, bsTerminal = 14 };
// clang-format on

int Field(int left, int right) { return left * 8 + right; }

using LeftRight = std::pair<int, int>;

LeftRight ToLeftRight(int field) {
  int left = field / 8;
  int right = field % 8;
  return {left, right};
}

void CheckPartSpec(int left, int right) {
  if (left < 0 || left > 5 || right < left || right > 5)
    ThrowMixException("Invalid part: ", left, ":", right);
}

void CheckByteValue(int value) {
  if (value < 0 || value > 63)
    ThrowMixException("Byte outside range: ", value);
}

void CheckByteIndex(int i) {
  if (i < 0 || i > 5)
    ThrowMixException("Byte index outside [0, 5]: ", i);
}

void Check(bool good, const char* expr_str) {
  if (!good)
    ThrowMixException("Assertion failed: ", expr_str);
}

#define CHECK(expr) Check(expr, #expr)

struct Word {
  static const int kSignBit = (1 << 30);
  static const int kAbsMask = (1 << 30) - 1;
  static const int kByteMask = (1 << 6) - 1;

  Word() = default;

  explicit Word(int value) {
    if (value < -kAbsMask || value > kAbsMask)
      ThrowMixException("Value outside range of Word: ", value);
    data = value < 0 ? (kSignBit | -value) : value;
  }

  Word(int sign, int abs) {
    if (abs < 0 || abs > kAbsMask)
      ThrowMixException("Invalid abs value: ", abs);
    data = sign < 0 ? (kSignBit | abs) : abs;
  }

  Word(int sign, int b1, int b2, int b3, int b4, int b5) {
    for (int b : {b1, b2, b3, b4, b5})
      CheckByteValue(b);
    data = (sign < 0 ? kSignBit : 0) | (b1 << 24) | (b2 << 18) | (b3 << 12) |
           (b4 << 6) | b5;
  }

  int sign() const { return (data & kSignBit) ? -1 : 1; }
  void set_sign(int sign) { *this = Word(sign, abs()); }

  int abs() const { return data & kAbsMask; }
  void set_abs(int abs) { *this = Word(sign(), abs); }

  int value() const { return sign() * abs(); }
  void set_value(int value) { *this = Word(value); }

  int byte(int i) const {
    CheckByteIndex(i);
    if (i == 0)
      return sign();
    const int shift = (5 - i) * 6;
    return (data >> shift) & kByteMask;
  }

  NOINLINE Word part_slow(int field) const {
    auto [left, right] = ToLeftRight(field);
    CheckPartSpec(left, right);
    Word result(left == 0 ? sign() : 0, 0);
    for (int i = std::max(left, 1); i <= right; i++)
      result.set_byte(i + (5 - right), byte(i));
    return result;
  }

  INLINE Word part(int field) const {
    if (field == 5) {
      return *this;
    }
    return part_slow(field);
  }

  NOINLINE void set_part_slow(int field, Word value) {
    auto [left, right] = ToLeftRight(field);
    CheckPartSpec(left, right);
    if (left == 0)
      set_sign(value.sign());
    for (int i = std::max(left, 1); i <= right; i++)
      set_byte(i, value.byte(5 - (right - i)));
  }

  INLINE void set_part(int field, Word value) {
    if (field == 5) {
      data = value.data;
      return;
    }
    set_part_slow(field, value);
  }

  void set_byte(int i, int value) {
    CheckByteIndex(i);
    if (i == 0)
      return set_sign(value);
    CheckByteValue(value);
    const int shift = (5 - i) * 6;
    data = (data & ~(kByteMask << shift)) | (value << shift);
  }

  int address() const {
    const int val = (data & kAbsMask) >> 18;
    return (data & kSignBit) ? -val : val;
  }

  void set_address(int address) {
    if (address <= -64 * 64 || address >= 64 * 64)
      ThrowMixException("Address outside of range: ", address);

    set_part(Field(0, 2), Word(address));
  }

  int index() const { return byte(3); }
  void set_index(int index) { set_byte(3, index); }

  int field() const { return byte(4); }
  void set_field(int field) { set_byte(4, field); }

  int code() const { return byte(5); }
  void set_code(int code) { set_byte(5, code); }

  bool operator==(Word other) const { return data == other.data; }
  bool operator!=(Word other) const { return data != other.data; }
  Word operator-() const { return Word(-sign(), abs()); }

  // 1 bit sign, 30 bit absolute value
  int data = 0;
};

Word ToWordOr(int value, int orValue) {
  return (value >= -Word::kAbsMask && value <= Word::kAbsMask) ? Word(value)
                                                               : Word(orValue);
}

const Word kNegZero(-1, 0);

std::ostream& operator<<(std::ostream& out, const Word& word) {
  return word == kNegZero ? (out << "-0") : (out << word.value());
}

using WordOverflow = std::pair<Word, bool>;
using HighLow = std::pair<Word, Word>;
using DivRemOverflow = std::tuple<Word, Word, bool>;

INLINE WordOverflow add(Word a, Word b) {
  int value = a.value() + b.value();
  int sign = value == 0 ? a.sign() : value;
  int abs = std::abs(value);
  return {Word(sign, abs & Word::kAbsMask), bool(abs & ~Word::kAbsMask)};
}

INLINE HighLow mul(Word a, Word b) {
  int sign = a.sign() * b.sign();
  int64_t abs = int64_t(a.abs()) * b.abs();
  return {Word(sign, abs >> 30), Word(sign, abs & Word::kAbsMask)};
}

INLINE DivRemOverflow div(Word high, Word low, Word divisor) {
  if (divisor.abs() == 0) {
    // MIX26 V1
    return {Word(1, 14, 9, 27, 32, 36), Word(1, 0, 0, 0, 25, 31), true};
  }
  if (high.abs() == 0) {
    int32_t abs = low.abs();
    int32_t div_abs = abs / divisor.abs();
    int32_t rem_abs = abs % divisor.abs();
    return {Word(high.sign() * divisor.sign(), div_abs),
            Word(high.sign(), rem_abs), false};
  }
  int64_t abs = (int64_t(high.abs()) << 30) | low.abs();
  int64_t div_abs = abs / divisor.abs();
  if (div_abs & ~int64_t(Word::kAbsMask))
    return {Word(-0xbeef), Word(-0xbeef), true};
  int64_t rem_abs = abs % divisor.abs();
  return {Word(high.sign() * divisor.sign(), div_abs),
          Word(high.sign(), rem_abs), false};
}

Word shift_left(Word a, int shift) {
  Word result(a.sign(), 0);
  for (int i = 1; i <= 5; i++) {
    int source_ind = i + shift;
    result.set_byte(
        i, source_ind >= 1 && source_ind <= 5 ? a.byte(source_ind) : 0);
  }
  return result;
}

int get_non_sign_byte(Word high, Word low, int i) {
  CHECK(i >= 0 && i < 10);
  return (i < 5) ? high.byte(i + 1) : low.byte(i - 5 + 1);
}

void set_non_sign_byte(Word& high, Word& low, int i, int value) {
  CHECK(i >= 0 && i < 10);
  if (i < 5)
    return high.set_byte(i + 1, value);
  low.set_byte(i - 5 + 1, value);
}

int non_neg_mod(int a, int m) {
  CHECK(m > 0);
  int rem = a % m;
  return rem < 0 ? rem + m : rem;
}

HighLow shift_left(Word high, Word low, int shift, bool cyclic) {
  Word res_high(high.sign(), 0);
  Word res_low(low.sign(), 0);
  for (int i = 0; i < 10; i++) {
    int source_ind = i + shift;
    if (cyclic)
      source_ind = non_neg_mod(source_ind, 10);
    set_non_sign_byte(res_high, res_low, i,
                      source_ind >= 0 && source_ind < 10
                          ? get_non_sign_byte(high, low, source_ind)
                          : 0);
  }
  return {res_high, res_low};
}

HighLow shift_left_binary(Word high, Word low, int shift) {
  int64_t abs = (int64_t)high.abs() << 30 | low.abs();
  abs = (shift >= 0) ? (abs << shift) : (abs >> -shift);
  return {Word(high.sign(), (abs >> 30) & Word::kAbsMask),
          Word(low.sign(), abs & Word::kAbsMask)};
}

WordOverflow num(Word high, Word low) {
  int64_t n = 0;
  for (Word word : {high, low}) {
    for (int i = 1; i <= 5; i++) {
      n *= 10;
      n += (word.byte(i) % 10);
    }
  }
  return {Word(high.sign(), n & Word::kAbsMask), bool(n & ~Word::kAbsMask)};
}

HighLow chars(Word word) {
  Word high, low;
  int n = word.abs();
  for (Word* word : {&low, &high}) {
    for (int i = 5; i >= 1; i--) {
      word->set_byte(i, 30 + n % 10);
      n /= 10;
    }
  }
  CHECK(n == 0);
  return {high, low};
}

int ToAsciiChar(int val) {
  static const char s[] =
      " ABCDEFGHI~JKLMNOPQR[#STUVWXYZ0123456789.,()+-*/=$<>@;:'";
  static_assert(sizeof(s) - 1 == 56);
  return (val >= 0 && val < 56) ? s[val] : -1;
}

int ToMixChar(int c) {
  static const int8_t val[] = {
      0,  -1, -1, 21, 49, -1, -1, 55, 42, 43, 46, 44, 41, 45, 40, 47,
      30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 54, 53, 50, 48, 51, -1,
      52, 1,  2,  3,  4,  5,  6,  7,  8,  9,  11, 12, 13, 14, 15, 16,
      17, 18, 19, 22, 23, 24, 25, 26, 27, 28, 29, 20, -1, -1, -1, -1,
      -1, 1,  2,  3,  4,  5,  6,  7,  8,  9,  11, 12, 13, 14, 15, 16,
      17, 18, 19, 22, 23, 24, 25, 26, 27, 28, 29, -1, -1, -1, 10, -1};
  static_assert(sizeof(val) == 128 - 32);
  return c >= 32 && c < 128 ? val[c - 32] : -1;
}

class Window;
struct Surface;

struct State {
  Word& rA() { return registers[0]; }
  Word rA() const { return registers[0]; }

  Word& rI(int i) {
    CHECK(i >= 1 && i <= 6);
    return registers[i];
  }
  Word rI(int i) const {
    CHECK(i >= 1 && i <= 6);
    return registers[i];
  }

  Word& rX() { return registers[7]; }
  Word rX() const { return registers[7]; }

  Word& rJ() { return registers[8]; }
  Word rJ() const { return registers[8]; }

  std::vector<Word> registers = std::vector<Word>(10);
  int next_instr = 0;
  // <0: lt, =0: eq, >0: gt
  int cmp_result = 0;
  bool overflow = false;
  int64_t time = 0;
  std::vector<Word> mem = std::vector<Word>(4000);
  bool halt = false;

  struct StateEx {
    std::unordered_map<int, std::function<void(State&)>> syscalls;
    // 0..2 reserved for stdin, stdout, stderr
    std::unordered_map<int, FILE*> file_ptrs;
    std::unique_ptr<Window> window;
    std::unique_ptr<Surface> surface;
    bool show_frame_time = false;
    int64_t last_update_ms = 0;
    uint32_t* pixels = nullptr;
    int w = 0;
    int h = 0;
  } ex;
};

char CmpToChar(int cmp_result) {
  if (cmp_result < 0)
    return 'L';
  if (cmp_result == 0)
    return 'E';
  return 'G';
}

INLINE int get_m(const State& state, Word instr) {
  int addr = instr.address();
  int index = instr.index();

  if (index > 6)
    ThrowMixException("Invalid index register: ", index);
  if (index != 0)
    addr += state.registers[index].value();
  return addr;
}

INLINE int get_address(const State& state, Word instr) {
  int addr = get_m(state, instr);
  if (addr < 0 || addr >= 4000)
    ThrowMixException("Invalid address: ", addr);
  return addr;
}

// load V
INLINE Word load_mem_operand_part(const State& state, Word instr) {
  return state.mem[get_address(state, instr)].part(instr.field());
}

INLINE void store_to_mem_operand_part(State& state, Word instr, Word value) {
  state.mem[get_address(state, instr)].set_part(instr.field(), value);
}

INLINE void CheckNotFloat(Word instr) {
  if (instr.field() == kFloatField)
    ThrowMixException("Floating point operations are not supported.");
}

void cycle(State&, Word) {}

void add_op(State& state, Word instr) {
  CheckNotFloat(instr);
  Word b = load_mem_operand_part(state, instr);
  auto [word, overflow] = add(state.rA(), b);
  state.rA() = word;
  state.overflow |= overflow;
}

void sub_op(State& state, Word instr) {
  CheckNotFloat(instr);
  Word b = load_mem_operand_part(state, instr);
  auto [word, overflow] = add(state.rA(), -b);
  state.rA() = word;
  state.overflow |= overflow;
}

void mul_op(State& state, Word instr) {
  CheckNotFloat(instr);
  Word b = load_mem_operand_part(state, instr);
  std::tie(state.rA(), state.rX()) = mul(state.rA(), b);
}

void div_op(State& state, Word instr) {
  CheckNotFloat(instr);
  Word divisor = load_mem_operand_part(state, instr);
  auto [div_res, rem, overflow] = div(state.rA(), state.rX(), divisor);
  state.rA() = div_res;
  state.rX() = rem;
  state.overflow |= overflow;
}

void spec(State& state, Word instr) {
  switch (instr.field()) {
    case kNumField: {
      auto [word, overflow] = num(state.rA(), state.rX());
      state.rA() = word;
      state.overflow |= overflow;
    } break;
    case kCharField: {
      auto [high, low] = chars(state.rA());
      // keep signs
      state.rA().set_abs(high.abs());
      state.rX().set_abs(low.abs());
    } break;
    case kHltField: state.halt = true; break;
    default:
      ThrowMixException("Unexpected 'spec' field value: ", instr.field());
  }
}

HighLow shift(Word a, Word x, int shift, int field) {
  if (shift < 0)
    ThrowMixException("Shift value must be non-negative: ", shift);
  switch (field) {
    case kSlaField: return {shift_left(a, shift), x};
    case kSraField: return {shift_left(a, -shift), x};
    case kSlaxField: return shift_left(a, x, shift, /*cyclic=*/false);
    case kSraxField: return shift_left(a, x, -shift, /*cyclic=*/false);
    case kSlcField: return shift_left(a, x, shift, /*cyclic=*/true);
    case kSrcField: return shift_left(a, x, -shift, /*cyclic=*/true);
    case kSlbField: return shift_left_binary(a, x, shift);
    case kSrbField: return shift_left_binary(a, x, -shift);
    default: ThrowMixException("Unexpected 'shift' field value: ", field);
  }
}

void shift(State& state, Word instr) {
  int shift_amount = get_m(state, instr);
  std::tie(state.rA(), state.rX()) =
      shift(state.rA(), state.rX(), shift_amount, instr.field());
}

void move(State& state, Word instr) {
  int source_address = get_address(state, instr);
  int num_words = instr.field();

  Word& rI1 = state.rI(1);

  for (int i = 0; i < num_words; i++) {
    state.mem.at(rI1.value()) = state.mem.at(source_address + i);
    if (rI1.value() + 1 >= 64 * 64)
      ThrowMixException("Register overflow would happen: ", rI1.value() + 1);
    rI1.set_value(rI1.value() + 1);
  }

  state.time += num_words * 2;
}

void load(State& state, Word instr) {
  int code = instr.code();
  Word value = load_mem_operand_part(state, instr);
  if (code >= kLd1 && code <= kLd6 && value.abs() >= 64 * 64)
    ThrowMixException("Index register overflow would happen: ", value);
  state.registers.at(code - kLda) = value;
}

void loadn(State& state, Word instr) {
  int code = instr.code();
  Word value = load_mem_operand_part(state, instr);
  if (code >= kLd1n && code <= kLd6n && value.abs() >= 64 * 64)
    ThrowMixException("Index register overflow would happen: ", value);
  state.registers.at(code - kLdan) = -value;
}

void store(State& state, Word instr) {
  int reg_index = instr.code() - kSta;
  store_to_mem_operand_part(state, instr, state.registers.at(reg_index));
}

std::string WordToAscii(Word word, bool skipWs = false) {
  std::string ascii;
  ascii.reserve(5);
  for (int j = 1; j <= 5; j++) {
    int mix_val = word.byte(j);
    int c = ToAsciiChar(mix_val);
    if (c == -1)
      ThrowMixException("Couldn't convert to Ascii: ", mix_val);
    if (skipWs && c == ' ')
      continue;
    ascii.push_back(c);
  }
  return ascii;
}

INLINE void SetPx(State& state);

INLINE void ioc(State& state, Word instr) {
  int field = instr.field();
  if (field == kSysCallFieldEx) {
    int address = get_address(state, instr);
    int call = state.mem[address].data;
    if (call == 370504795) {  // SETPX
      SetPx(state);
      return;
    }
    state.ex.syscalls[call](state);
    return;
  }

  if (field != kLinePrinterField)
    ThrowMixException("IOC is only supported for the line printer (=nop).");
  int m = get_m(state, instr);
  if (m != 0)
    ThrowMixException("M must be 0, got: ", m);
}

int WordsByDevice(int device) {
  switch (device) {
    case kCardReaderField:
    case kCardPunchField: return bsCard;
    case kLinePrinterField: return bsLinePrinter;
    case KTerminalField: return bsTerminal;
    default: ThrowMixException("Unsupported device: ", device);
  }
}

std::string TrimSpacesOnTheRight(std::string s) {
  s.erase(s.find_last_not_of(' ') + 1);
  return s;
}

void out(State& state, Word instr) {
  int device = instr.field();
  int num_words = WordsByDevice(device);
  int begin_addr = get_address(state, instr);

  std::string line;
  line.reserve(num_words * 5);
  for (int i = begin_addr; i < begin_addr + num_words; i++)
    for (int j = 1; j <= 5; j++) {
      int mix_val = state.mem.at(i).byte(j);
      int c = ToAsciiChar(mix_val);
      if (c == -1) {
        std::cerr << "Warning: Mix value " << mix_val
                  << " cannot be printed.\n";
        c = ' ';
      }
      line.push_back(c);
    }
  std::cout << TrimSpacesOnTheRight(std::move(line)) << '\n';
}

void in(State& state, Word instr) {
  int device = instr.field();
  int num_words = WordsByDevice(device);
  int begin_addr = get_address(state, instr);
  int max_size = num_words * 5;

  std::string input_str;
  if (std::cin.eof())
    ThrowMixException("Could not read line.");
  std::getline(std::cin, input_str);
  if ((int)input_str.size() > max_size) {
    std::cerr << "Warning: Truncating long line.\n";
    input_str = input_str.substr(0, max_size);
  }
  for (int i = 0; i < num_words; i++) {
    for (int j = 1; j <= 5; j++) {
      int source_index = i * 5 + j - 1;
      int val = 0;
      if (source_index < (int)input_str.size()) {
        char c = input_str.at(source_index);
        val = ToMixChar(c);
        if (val == -1) {
          std::cerr << "Warning: Unsupported character: '" << c << "' ("
                    << (int)(unsigned char)c << ")\n";
          val = 0;
        }
      }
      state.mem.at(begin_addr + i).set_byte(j, val);
    }
  }
}

void jbus(State&, Word) { /* never busy*/ }

void jred(State& state, Word instr) {
  state.next_instr = get_address(state, instr);  // always ready
}

INLINE bool should_jump(int field, int value, bool overflow) {
  switch (field) {
    case kJmpField:
    case kJsjField: return true;
    case kJovField: return overflow;
    case kJnovField: return !overflow;
    case kJlField: return value < 0;    // kJlField + kJnField
    case kJeField: return value == 0;   // kJlField + kJzField
    case kJgField: return value > 0;    // kJlField + kJpField
    case kJgeField: return value >= 0;  // kJlField + kJnnField
    case kJneField: return value != 0;  // kJlField + kJnzField
    case kJleField: return value <= 0;  // kJlField + kJnpField
    case kJlField + kJevenField: return value ^ 1;
    case kJlField + kJoddField: return value & 1;
    default: ThrowMixException("Bad jump field: ", field);
  }
}

void jump(State& state, Word instr) {
  int field = instr.field();

  if (field > kJleField)
    ThrowMixException("Bad jump field: ", field);

  if (should_jump(field, state.cmp_result, state.overflow)) {
    if (field != kJsjField)
      state.rJ() = Word(state.next_instr);
    state.next_instr = get_address(state, instr);
  }

  if (field == kJovField || field == kJnovField)
    state.overflow = false;
}

void reg_jump(State& state, Word instr) {
  int reg_index = instr.code() - kJa;
  int reg_value = state.registers[reg_index].value();
  int field = instr.field();

  if (field > kJoddField)
    ThrowMixException("Bad register jump field: ", field);

  if (should_jump(field + kJlField, reg_value, state.overflow)) {
    state.rJ() = Word(state.next_instr);
    state.next_instr = get_address(state, instr);
  }
}

WordOverflow addr_op(Word a, Word b, int field) {
  switch (field) {
    case kIncField: return add(a, b);
    case kDecField: return add(a, -b);
    case kEntField: return {b, false};
    case kEnnField: return {-b, false};
    default: ThrowMixException("Bad addr_op field: ", field);
  }
}

void addr_op(State& state, Word instr) {
  int code = instr.code();
  Word& target_reg = state.registers.at(code - kAddrOpA);
  Word value = Word(get_m(state, instr));

  Word word;
  bool overflow;
  std::tie(word, overflow) = addr_op(target_reg, value, instr.field());

  if (code >= kAddrOp1 && code <= kAddrOp6)
    if (overflow || word.abs() >= 64 * 64)
      ThrowMixException("Index register overflow would happen.");

  target_reg = word;
  state.overflow |= overflow;
}

void compare(State& state, Word instr) {
  int a = state.registers.at(instr.code() - kCmpa).part(instr.field()).value();
  int b = load_mem_operand_part(state, instr).value();
  state.cmp_result = a - b;
}

// clang-format off
const std::vector<int> op_time = {
    /*kNop:*/ 1, /*kAdd:*/ 2, /*kSub:*/ 2, /*kMul:*/ 10, /*kDiv:*/ 12,
    /*kSpec:*/ 10, /*kShift:*/ 2, /*kMove:*/ 1, /*kLda:*/ 2, /*kLd1:*/ 2,
    /*kLd2:*/ 2, /*kLd3:*/ 2, /*kLd4:*/ 2, /*kLd5:*/ 2, /*kLd6:*/ 2,
    /*kLdx:*/ 2, /*kLdan:*/ 2, /*kLd1n:*/ 2, /*kLd2n:*/ 2, /*kLd3n:*/ 2,
    /*kLd4n:*/ 2, /*kLd5n:*/ 2, /*kLd6n:*/ 2, /*kLdxn:*/ 2, /*kSta:*/ 2,
    /*kSt1:*/ 2, /*kSt2:*/ 2, /*kSt3:*/ 2, /*kSt4:*/ 2, /*kSt5:*/ 2,
    /*kSt6:*/ 2, /*kStx:*/ 2, /*kStj:*/ 2, /*kStz:*/ 2, /*kJbus:*/ 1,
    /*kIoc:*/ 1, /*kIn:*/ 1, /*kOut:*/ 1, /*kJred:*/ 1, /*kJump:*/ 1,
    /*kJa:*/ 1, /*kJ1:*/ 1, /*kJ2:*/ 1, /*kJ3:*/ 1, /*kJ4:*/ 1,
    /*kJ5:*/ 1, /*kJ6:*/ 1, /*kJx:*/ 1, /*kAddrOpA:*/ 1, /*kAddrOp1:*/ 1,
    /*kAddrOp2:*/ 1, /*kAddrOp3:*/ 1, /*kAddrOp4:*/ 1, /*kAddrOp5:*/ 1, /*kAddrOp6:*/ 1,
    /*kAddrOpX:*/ 1, /*kCmpa:*/ 2, /*kCmp1:*/ 2, /*kCmp2:*/ 2, /*kCmp3:*/ 2,
    /*kCmp4:*/ 2, /*kCmp5:*/ 2, /*kCmp6:*/ 2, /*kCmpx:*/ 2,
};
// clang-format on

void SimulateMix(State& state) {
  constexpr int MemSize = 4000;
  Word* mem = state.mem.data();

  int last_instruction = 0;
  try {
    while (!state.halt) {
      last_instruction = state.next_instr;
      if (state.next_instr < 0 || state.next_instr >= MemSize)
        ThrowMixException(
            "Instruction counter outside range. Did you forget to HLT?\n");
      Word instr = mem[state.next_instr++];
      int code = instr.code();
      // clang-format off
      switch (code) {
        case kNop: cycle(state, instr); break;
        case kAdd: add_op(state, instr); break;
        case kSub: sub_op(state, instr); break;
        case kMul: mul_op(state, instr); break;
        case kDiv: div_op(state, instr); break;
        case kSpec: spec(state, instr); break;
        case kShift: shift(state, instr); break;
        case kMove: move(state, instr); break;
        case kLda:
        case kLd1:
        case kLd2:
        case kLd3:
        case kLd4:
        case kLd5:
        case kLd6:
        case kLdx: load(state, instr); break;
        case kLdan:
        case kLd1n:
        case kLd2n:
        case kLd3n:
        case kLd4n:
        case kLd5n:
        case kLd6n:
        case kLdxn: loadn(state, instr); break;
        case kSta:
        case kSt1:
        case kSt2:
        case kSt3:
        case kSt4:
        case kSt5:
        case kSt6:
        case kStx:
        case kStj:
        case kStz: store(state, instr); break;
        case kJbus: jbus(state, instr); break;
        case kIoc: ioc(state, instr); break;
        case kIn: in(state, instr); break;
        case kOut: out(state, instr); break;
        case kJred: jred(state, instr); break;
        case kJump: jump(state, instr); break;
        case kJa:
        case kJ1:
        case kJ2:
        case kJ3:
        case kJ4:
        case kJ5:
        case kJ6:
        case kJx: reg_jump(state, instr); break;
        case kAddrOpA:
        case kAddrOp1:
        case kAddrOp2:
        case kAddrOp3:
        case kAddrOp4:
        case kAddrOp5:
        case kAddrOp6:
        case kAddrOpX: addr_op(state, instr); break;
        case kCmpa:
        case kCmp1:
        case kCmp2:
        case kCmp3:
        case kCmp4:
        case kCmp5:
        case kCmp6:
        case kCmpx: compare(state, instr); break;
        default: ThrowMixException("Invalid opcode: ", code);
      }
      // clang-format on
      state.time += op_time[code];
    }
  } catch (const std::exception& e) {
    std::cerr << "Exception received at instruction address "
              << last_instruction << ":\n";
    std::cerr << e.what() << "\n";
  }
}

// ---- Parser ----
using FutureRef = std::string;
using FutureRefLocation = std::pair<FutureRef, int>;

struct ParserState {
  std::unordered_map<std::string, Word> symbol_table;
  int location = 0;
  std::vector<Word> mem = std::vector<Word>(4000);
  int start_location = -1;
  int increased_local_ref = -1;
  std::vector<int> next_local_ref = std::vector<int>(10);
  std::vector<FutureRefLocation> future_ref_locations;
  bool had_error = false;
};

std::string RemainingString(std::stringstream& stream) {
  if (!stream.eof() && !stream.fail()) {
    return stream.str().substr(stream.tellg());
  } else {
    return "";
  }
}

bool IsSpaceOrEof(int c) { return std::isspace(c) || c == EOF; }

int FirstNonWhitespace(const std::string& s) {
  auto it = std::find_if_not(s.begin(), s.end(),
                             [](char c) { return std::isspace(c); });
  return it == s.end() ? EOF : *it;
}

bool IsWhitespace(const std::string& s) {
  return std::find_if_not(s.begin(), s.end(),
                          [](char c) { return std::isspace(c); }) == s.end();
}

bool IsAlnum(const std::string& s) {
  return std::find_if_not(s.begin(), s.end(),
                          [](char c) { return std::isalnum(c); }) == s.end();
}

bool IsNumber(const std::string& s) {
  return std::find_if_not(s.begin(), s.end(),
                          [](char c) { return std::isdigit(c); }) == s.end();
}

std::string ToUpper(std::string s) {
  std::for_each(s.begin(), s.end(), [](char& c) { c = std::toupper(c); });
  return s;
}

std::string ToLower(std::string s) {
  std::for_each(s.begin(), s.end(), [](char& c) { c = std::tolower(c); });
  return s;
}

bool IsLocalSymbol(const std::string& s) {
  return s.size() == 2 && std::isdigit(s[0]) &&
         (s[1] == 'H' || s[1] == 'F' || s[1] == 'B');
}

bool IsLocalSymbolDefinition(const std::string& s) {
  return s.size() == 2 && std::isdigit(s[0]) && s[1] == 'H';
}

bool IsForwardLocalSymbolReference(const std::string& s) {
  return s.size() == 2 && std::isdigit(s[0]) && s[1] == 'F';
}

bool IsBackwardLocalSymbolReference(const std::string& s) {
  return s.size() == 2 && std::isdigit(s[0]) && s[1] == 'B';
}

bool IsLocalSymbolReference(const std::string& s) {
  return s.size() == 2 && std::isdigit(s[0]) && (s[1] == 'F' || s[1] == 'B');
}

int GetLocalSymbolIndex(const std::string& s) {
  if (!IsLocalSymbol(s))
    ThrowMixException("Local symbol expected");
  return s[0] - '0';
}

std::string ParseLoc(std::stringstream& stream) {
  std::string loc;
  if (!std::isspace(stream.peek())) {
    stream >> loc;
    if (!IsAlnum(loc) || IsNumber(loc))
      ThrowMixException("Invalid loc: ", loc);
  }
  return loc;
}

void IgnoreWhitespace(std::stringstream& stream) {
  while (std::isspace(stream.peek()))
    stream.ignore();
}

Word ParseAlf(std::stringstream& stream) {
  if (stream.peek() == EOF)
    return Word(0);

  if (!std::isspace(stream.get()))
    ThrowMixException("Whitespace expected after ALF");

  // This is a GNU extension:
  bool quoted = false;
  if (FirstNonWhitespace(RemainingString(stream)) == '"') {
    IgnoreWhitespace(stream);
    stream.ignore();
    quoted = true;
  }

  std::string str(5, ' ');
  for (int i = 0; i < 5; i++) {
    if ((quoted && stream.peek() == '"') || stream.peek() == EOF)
      break;
    str[i] = stream.get();
  }

  if (quoted && stream.get() != '"')
    ThrowMixException("Expected \"");

  if (!IsSpaceOrEof(stream.peek()))
    std::cerr << "Warning: Non-space character right after ALF string\n";

  if (IsWhitespace(str) && !IsWhitespace(RemainingString(stream)))
    std::cerr
        << "Warning: Non-space character after all-whitespace ALF string\n";

  Word word;
  for (int i = 0; i < 5; i++) {
    int value = ToMixChar(str[i]);
    if (value == -1)
      ThrowMixException("Unexpected char in ALF string: ", str[i], " (",
                        (int)(unsigned char)str[i], ")");
    word.set_byte(1 + i, value);
  }
  return word;
}

std::string ParseSymbolOrNum(std::stringstream& stream) {
  std::string s;
  while (std::isalnum(stream.peek()))
    s.push_back(stream.get());
  if (s.size() > 10)
    ThrowMixException("Symbol or number longer than 10 characters: ", s);
  return s;
}

std::string ResolveBackwardLocalSymbolReference(const std::string& ref,
                                                const ParserState& state) {
  if (!IsLocalSymbol(ref))
    ThrowMixException("Local symbol expected");
  int i = GetLocalSymbolIndex(ref);
  int j = state.next_local_ref[i] - 1;
  if (state.increased_local_ref == i)
    j--;
  if (j < 0)
    ThrowMixException("Local symbol not yet defined: ", i, "H");
  return Format(i, "H#", j);
}

std::string ResolveForwardLocalSymbolReference(const std::string& ref,
                                               const ParserState& state) {
  if (!IsLocalSymbol(ref))
    ThrowMixException("Local symbol expected");
  int i = GetLocalSymbolIndex(ref);
  int j = state.next_local_ref[i];
  return Format(i, "H#", j);
}

std::string ResolveLocalSymbolDefinition(const std::string& def,
                                         ParserState& state) {
  if (!IsLocalSymbol(def))
    ThrowMixException("Local symbol expected");
  int i = GetLocalSymbolIndex(def);
  int j = state.next_local_ref[i]++;
  state.increased_local_ref = i;
  return Format(i, "H#", j);
}

Word ParseAtomicExpression(std::stringstream& stream,
                           const ParserState& state) {
  if (stream.peek() == '*') {
    stream.ignore();
    return Word(state.location);
  }
  if (std::string s = ParseSymbolOrNum(stream); !s.empty()) {
    if (IsNumber(s))
      return Word(std::stoi(s));
    if (IsLocalSymbolDefinition(s))
      ThrowMixException(
          "Local symbol definition cannot appear in an expression.");
    if (IsForwardLocalSymbolReference(s))
      ThrowMixException(
          "Forward local symbol reference cannot appear in an expression.");
    if (IsBackwardLocalSymbolReference(s))
      s = ResolveBackwardLocalSymbolReference(s, state);
    if (!state.symbol_table.count(s))
      ThrowMixException("Undefined symbol: ", s);
    return state.symbol_table.at(s);
  }
  ThrowMixException("Atomic expression expected.");
}

Word ParsePlusMinusAtomicExpression(std::stringstream& stream,
                                    const ParserState& state) {
  if (stream.peek() == '-') {
    stream.ignore();
    return -ParseAtomicExpression(stream, state);
  }
  if (stream.peek() == '+')
    stream.ignore();
  return ParseAtomicExpression(stream, state);
}

int ParseBinOp(std::stringstream& stream) {
  if (std::string("+-*:").find(stream.peek()) != std::string::npos)
    return stream.get();
  if (stream.peek() == '/') {
    stream.ignore();
    if (stream.peek() == '/') {
      stream.ignore();
      return '?';
    }
    return '/';
  }
  return EOF;
}

WordOverflow EvaluateBinaryOp(int op, Word a, Word b) {
  switch (op) {
    case '+': return add(a, b);
    case '-': return add(a, -b);
    case '*': {
      auto [high, low] = mul(a, b);
      return {low, high.value() != 0};
    }
    case '/': {
      auto [div_res, rem, overflow] = div(Word(0), a, b);
      return {div_res, overflow};
    }
    case '?': {  // "//" operation
      auto [div_res, rem, overflow] = div(a, Word(0), b);
      return {div_res, overflow};
    }
    case ':': {
      auto [high, low] = mul(a, Word(8));
      auto [c, overflow] = add(low, b);
      return {c, high.value() != 0 || overflow};
    }
    default: ThrowMixException("Unknown binary op: ", op);
  }
}

Word ParseExpressionTail(std::stringstream& stream, const ParserState& state,
                         Word head) {
  int op;
  while ((op = ParseBinOp(stream)) != EOF) {
    Word b = ParsePlusMinusAtomicExpression(stream, state);
    bool overflow;
    std::tie(head, overflow) = EvaluateBinaryOp(op, head, b);
    if (overflow)
      ThrowMixException("Overflow in binary op: ", op);
  }
  return head;
}

Word ParseExpression(std::stringstream& stream, const ParserState& state) {
  Word head = ParsePlusMinusAtomicExpression(stream, state);
  return ParseExpressionTail(stream, state, head);
}

bool IsLiteralConstant(const std::string& s) {
  return s.size() >= 2 && s.front() == '=' && s.back() == '=';
}

FutureRef ParseLiteralConstant(std::stringstream& stream) {
  if (stream.peek() != '=')
    return "";
  stream.ignore();
  std::string s;
  s.push_back('=');
  int c;
  while ((c = stream.get()) != '=') {
    if (c == EOF)
      ThrowMixException("Expected = to close literal constant");
    s.push_back(c);
  }
  s.push_back('=');
  // if (s.size() > 12)
  //   ThrowMixException("Literal constant too long: ", s);
  return s;
}

// Address field of instruction
std::variant<int, FutureRef> ParseAPart(std::stringstream& stream,
                                        ParserState& state) {
  if (std::isalnum(stream.peek())) {
    std::string s = ParseSymbolOrNum(stream);
    if (s.empty())
      ThrowMixException("Symbol should not be empty here.");
    if (IsForwardLocalSymbolReference(s))
      return ResolveForwardLocalSymbolReference(s, state);
    if (!IsNumber(s) && !IsLocalSymbol(s) && !state.symbol_table.count(s))
      return s;
    std::stringstream stream2(s);
    Word expression_head = ParseAtomicExpression(stream2, state);
    return ParseExpressionTail(stream, state, expression_head).value();
  }
  if (stream.peek() == '*' || stream.peek() == '+' || stream.peek() == '-')
    return ParseExpression(stream, state).value();
  if (std::string literal = ParseLiteralConstant(stream); !literal.empty())
    return literal;
  return 0;
}

// Index field of instruction
int ParseIndexPart(std::stringstream& stream, ParserState& state) {
  if (stream.peek() != ',')
    return 0;
  stream.ignore();
  return ParseExpression(stream, state).value();
}

// Field field of instruction
int ParseFPart(std::stringstream& stream, ParserState& state,
               int default_value) {
  if (stream.peek() != '(')
    return default_value;
  stream.ignore();
  Word w = ParseExpression(stream, state);
  if (stream.get() != ')')
    ThrowMixException("Expected ')'.");
  return w.value();
}

bool ParseComma(std::stringstream& stream) {
  if (stream.peek() != ',')
    return false;
  stream.ignore();
  return true;
}

bool IsValidPart(int field) {
  int left = field / 8;
  int right = field % 8;
  return left >= 0 && left <= 5 && right >= left && right <= 5;
}

// Full-word MIX constant
Word ParseWValue(std::stringstream& stream, ParserState& state) {
  Word a;
  do {
    Word b = ParseExpression(stream, state);
    int f = ParseFPart(stream, state, Field(0, 5));
    if (!IsValidPart(f))
      ThrowMixException("Invalid field.");
    a.set_part(f, b);
  } while (ParseComma(stream));
  if (!IsSpaceOrEof(stream.peek()))
    ThrowMixException("W-value must be followed by whitespace or EOF, found: ",
                      (char)stream.peek());
  return a;
}

Word EvaluateLiteralConstant(const std::string& s, ParserState& state) {
  if (!IsLiteralConstant(s))
    ThrowMixException("Literal expected: ", s);
  try {
    std::stringstream stream(s.substr(1, s.size() - 2));
    return ParseWValue(stream, state);
  } catch (const std::exception& e) {
    std::cerr << "Exception while parsing literal constant \"" << s << "\"\n";
    throw;
  }
}

struct SymbolicOpDesc {
  int code = 0;
  int usual_field_value = 0;
};

std::unordered_map<std::string, SymbolicOpDesc> symbolic_op_table = {
    {"NOP", {0, 0}},   {"ADD", {1, 5}},   {"FADD", {1, 6}},  {"SUB", {2, 5}},
    {"FSUB", {2, 6}},  {"MUL", {3, 5}},   {"FMUL", {3, 6}},  {"DIV", {4, 5}},
    {"FDIV", {4, 6}},  {"NUM", {5, 0}},   {"CHAR", {5, 1}},  {"HLT", {5, 2}},
    {"SLA", {6, 0}},   {"SRA", {6, 1}},   {"SLAX", {6, 2}},  {"SRAX", {6, 3}},
    {"SLC", {6, 4}},   {"SRC", {6, 5}},   {"SLB", {6, 6}},   {"SRB", {6, 7}},
    {"MOVE", {7, 1}},  {"LDA", {8, 5}},   {"LD1", {9, 5}},   {"LD2", {10, 5}},
    {"LD3", {11, 5}},  {"LD4", {12, 5}},  {"LD5", {13, 5}},  {"LD6", {14, 5}},
    {"LDX", {15, 5}},  {"LDAN", {16, 5}}, {"LD1N", {17, 5}}, {"LD2N", {18, 5}},
    {"LD3N", {19, 5}}, {"LD4N", {20, 5}}, {"LD5N", {21, 5}}, {"LD6N", {22, 5}},
    {"LDXN", {23, 5}}, {"STA", {24, 5}},  {"ST1", {25, 5}},  {"ST2", {26, 5}},
    {"ST3", {27, 5}},  {"ST4", {28, 5}},  {"ST5", {29, 5}},  {"ST6", {30, 5}},
    {"STX", {31, 5}},  {"STJ", {32, 2}},  {"STZ", {33, 5}},  {"JBUS", {34, 0}},
    {"IOC", {35, 0}},  {"IN", {36, 0}},   {"OUT", {37, 0}},  {"JRED", {38, 0}},
    {"JMP", {39, 0}},  {"JSJ", {39, 1}},  {"JOV", {39, 2}},  {"JNOV", {39, 3}},
    {"JL", {39, 4}},   {"JE", {39, 5}},   {"JG", {39, 6}},   {"JGE", {39, 7}},
    {"JNE", {39, 8}},  {"JLE", {39, 9}},  {"JAN", {40, 0}},  {"JAZ", {40, 1}},
    {"JAP", {40, 2}},  {"JANN", {40, 3}}, {"JANZ", {40, 4}}, {"JANP", {40, 5}},
    {"JAE", {40, 6}},  {"JAO", {40, 7}},  {"J1N", {41, 0}},  {"J1Z", {41, 1}},
    {"J1P", {41, 2}},  {"J1NN", {41, 3}}, {"J1NZ", {41, 4}}, {"J1NP", {41, 5}},
    {"J1E", {41, 6}},  {"J1O", {41, 7}},  {"J2N", {42, 0}},  {"J2Z", {42, 1}},
    {"J2P", {42, 2}},  {"J2NN", {42, 3}}, {"J2NZ", {42, 4}}, {"J2NP", {42, 5}},
    {"J2E", {42, 6}},  {"J2O", {42, 7}},  {"J3N", {43, 0}},  {"J3Z", {43, 1}},
    {"J3P", {43, 2}},  {"J3NN", {43, 3}}, {"J3NZ", {43, 4}}, {"J3NP", {43, 5}},
    {"J3E", {43, 6}},  {"J3O", {43, 7}},  {"J4N", {44, 0}},  {"J4Z", {44, 1}},
    {"J4P", {44, 2}},  {"J4NN", {44, 3}}, {"J4NZ", {44, 4}}, {"J4NP", {44, 5}},
    {"J4E", {44, 6}},  {"J4O", {44, 7}},  {"J5N", {45, 0}},  {"J5Z", {45, 1}},
    {"J5P", {45, 2}},  {"J5NN", {45, 3}}, {"J5NZ", {45, 4}}, {"J5NP", {45, 5}},
    {"J5E", {45, 6}},  {"J5O", {45, 7}},  {"J6N", {46, 0}},  {"J6Z", {46, 1}},
    {"J6P", {46, 2}},  {"J6NN", {46, 3}}, {"J6NZ", {46, 4}}, {"J6NP", {46, 5}},
    {"J6E", {46, 6}},  {"J6O", {46, 7}},  {"JXN", {47, 0}},  {"JXZ", {47, 1}},
    {"JXP", {47, 2}},  {"JXNN", {47, 3}}, {"JXNZ", {47, 4}}, {"JXNP", {47, 5}},
    {"JXE", {47, 6}},  {"JXO", {47, 7}},  {"INCA", {48, 0}}, {"DECA", {48, 1}},
    {"ENTA", {48, 2}}, {"ENNA", {48, 3}}, {"INC1", {49, 0}}, {"DEC1", {49, 1}},
    {"ENT1", {49, 2}}, {"ENN1", {49, 3}}, {"INC2", {50, 0}}, {"DEC2", {50, 1}},
    {"ENT2", {50, 2}}, {"ENN2", {50, 3}}, {"INC3", {51, 0}}, {"DEC3", {51, 1}},
    {"ENT3", {51, 2}}, {"ENN3", {51, 3}}, {"INC4", {52, 0}}, {"DEC4", {52, 1}},
    {"ENT4", {52, 2}}, {"ENN4", {52, 3}}, {"INC5", {53, 0}}, {"DEC5", {53, 1}},
    {"ENT5", {53, 2}}, {"ENN5", {53, 3}}, {"INC6", {54, 0}}, {"DEC6", {54, 1}},
    {"ENT6", {54, 2}}, {"ENN6", {54, 3}}, {"INCX", {55, 0}}, {"DECX", {55, 1}},
    {"ENTX", {55, 2}}, {"ENNX", {55, 3}}, {"CMPA", {56, 5}}, {"FCMP", {56, 6}},
    {"CMP1", {57, 5}}, {"CMP2", {58, 5}}, {"CMP3", {59, 5}}, {"CMP4", {60, 5}},
    {"CMP5", {61, 5}}, {"CMP6", {62, 5}}, {"CMPX", {63, 5}}};

void ReserveSpaceForImplicitSymbolsAndLiterals(ParserState& state,
                                               const std::string& ignored) {
  for (auto& [ref, location] : state.future_ref_locations) {
    if (ref == ignored)
      continue;
    if (!state.symbol_table.count(ref)) {
      bool is_literal = IsLiteralConstant(ref);
      Word w = is_literal ? EvaluateLiteralConstant(ref, state) : Word(0);
      if (!is_literal)
        std::cerr << "Warning: Implicitly defined symbol: " << ref << "\n";
      state.symbol_table[ref] = Word(state.location);
      state.mem.at(state.location++) = w;
    }
  }
}

void ResolveAddressPartOfFutureRefLocations(ParserState& state) {
  for (auto& [ref, location] : state.future_ref_locations) {
    state.mem.at(location).set_address(state.symbol_table.at(ref).value());
  }
}

Word ParseOpParam(std::stringstream& stream, ParserState& state,
                  const std::string& op) {
  const SymbolicOpDesc& desc = symbolic_op_table.at(op);
  std::variant<int, FutureRef> a_part = ParseAPart(stream, state);
  int index_part = ParseIndexPart(stream, state);
  int f_part = ParseFPart(stream, state, desc.usual_field_value);
  Word w(desc.code);
  w.set_field(f_part);
  w.set_index(index_part);
  if (std::holds_alternative<int>(a_part)) {
    w.set_address(std::get<int>(a_part));
  } else {
    FutureRef ref = std::get<FutureRef>(a_part);
    state.future_ref_locations.push_back({ref, state.location});
  }
  if (!IsSpaceOrEof(stream.peek()))
    ThrowMixException("OP field must be followed by whitespace or EOF, found: ",
                      (char)stream.peek());
  return w;
}

void ParseLine(std::stringstream& stream, ParserState& state) {
  state.increased_local_ref = -1;
  std::string loc = ParseLoc(stream);
  if (!loc.empty()) {
    if (IsLocalSymbolDefinition(loc))
      loc = ResolveLocalSymbolDefinition(loc, state);
    if (IsLocalSymbolReference(loc))
      ThrowMixException("LOC field cannot contain local symbol reference");
    if (state.symbol_table.count(loc))
      ThrowMixException("Redefining symbol: ", loc);
  }
  std::string op;
  stream >> op;
  if (op.empty())
    ThrowMixException("Empty op");
  if (op == "ALF") {
    if (!loc.empty())
      state.symbol_table[loc] = Word(state.location);
    state.mem.at(state.location++) = ParseAlf(stream);
    return;
  }
  IgnoreWhitespace(stream);
  if (symbolic_op_table.count(op)) {
    if (!loc.empty())
      state.symbol_table[loc] = Word(state.location);
    state.mem.at(state.location++) = ParseOpParam(stream, state, op);
    return;
  }
  if (op == "EQU") {
    if (!loc.empty())
      state.symbol_table[loc] = ParseWValue(stream, state);
    return;
  }
  if (op == "ORIG") {
    if (!loc.empty())
      state.symbol_table[loc] = Word(state.location);
    state.location = ParseWValue(stream, state).value();
    return;
  }
  if (op == "CON") {
    if (!loc.empty())
      state.symbol_table[loc] = Word(state.location);
    state.mem.at(state.location++) = ParseWValue(stream, state);
    return;
  }
  if (op == "END") {
    ReserveSpaceForImplicitSymbolsAndLiterals(state, /*ignored=*/loc);
    if (!loc.empty())
      state.symbol_table[loc] = Word(state.location);
    state.start_location = ParseWValue(stream, state).part(Field(4, 5)).value();
    ResolveAddressPartOfFutureRefLocations(state);
    return;
  }
  ThrowMixException("Unknown op: ", op);
}

void Parse(std::istream& istream, ParserState& state) {
  int line_no = 0;
  std::string line;
  // Only read until END
  while (state.start_location == -1 && std::getline(istream, line)) {
    line_no++;
    std::stringstream stream(ToUpper(line));
    if (!line.empty() && !IsWhitespace(line) && line.front() != '*') {
      try {
        ParseLine(stream, state);
      } catch (const std::exception& e) {
        std::cerr << "Exception received at line " << line_no << ":\n";
        std::cerr << e.what() << "\n";
        std::cerr << "Line content:\n" << line << "\n";
        state.had_error = true;
        return;
      }
    }
  }
  if (state.start_location == -1) {
    std::cerr << "Missing END\n";
    state.had_error = true;
  }
}

// ---- Syscall ----

int StringToMixInt(const std::string& s) {
  if (s.size() > 5) {
    throw MixException("StringToMixWord: too long string ", s);
  }
  int res = 0;
  for (char c : s) {
    res <<= 6;
    int mix_char = ToMixChar(c);
    if (mix_char == -1) {
      throw MixException("StringToMixWord: Unsupported char ", int(c));
    }
    res |= mix_char;
  }
  return res;
}

Word CCharToWord(char c) { return Word(uint8_t(c)); }

char WordToCChar(Word w) {
  if (w.value() < 0 || w.value() > 255) {
    ThrowMixException("C Byte outside range.");
  }
  return char(uint8_t(w.value()));
}

void AddBasicSyscalls(int argc, char** argv, State& state) {
  // DUMPR: ()->()
  // Debug: Dump registers.
  state.ex.syscalls[StringToMixInt("DUMPR")] = [argc, argv](State& state) {
    std::cout << "rA: " << state.rA() << "\n";
    std::cout << "rX: " << state.rX() << "\n";
    std::cout << "rI1: " << state.rI(1) << "\n";
    std::cout << "rI2: " << state.rI(2) << "\n";
    std::cout << "rI3: " << state.rI(3) << "\n";
    std::cout << "rI4: " << state.rI(4) << "\n";
    std::cout << "rI5: " << state.rI(5) << "\n";
    std::cout << "rI6: " << state.rI(6) << "\n";
    std::cout << "rJ: " << state.rJ() << "\n";
    std::cout << "OV: " << (state.overflow ? 1 : 0) << "\n";
    std::cout << "CMP: " << CmpToChar(state.cmp_result) << "\n";
  };

  // DUMPM: (A: buf, X: bufSize)->()
  // Debug: Dump buffer as numbers and ASCII characters.
  state.ex.syscalls[StringToMixInt("DUMPM")] = [argc, argv](State& state) {
    int buf = state.rA().value();
    int bufSize = state.rX().value();

    for (int i = 0; i < bufSize && buf + i < state.mem.size(); ++i) {
      Word w = state.mem.at(buf + i);
      std::cout << "mem[" << (buf + i) << "]=" << std::setw(3) << w << " ";
      int val = w.value();
      if (val >= 0 && val <= 255 && isgraph(val)) {
        std::cout << char(uint8_t(val));
      } else if (val == ' ') {
        std::cout << " ";
      } else if (val == '\n') {
        std::cout << "\\n";
      } else if (val == '\r') {
        std::cout << "\\r";
      } else if (val == '\t') {
        std::cout << "\\t";
      } else {
        std::cout << " ??";
      }
      std::cout << "\n";
    }
  };

  // DUMPS: (A: buf, X: bufSize)->()
  // Debug: Dump buffer as a string.
  state.ex.syscalls[StringToMixInt("DUMPS")] = [argc, argv](State& state) {
    int buf = state.rA().value();
    int bufSize = state.rX().value();

    std::cout << '\"';
    for (int i = 0; i < bufSize && buf + i < state.mem.size(); ++i) {
      Word w = state.mem.at(buf + i);
      int val = w.value();
      if (val >= 0 && val <= 255 && isgraph(val) && val != '"' && val != '\\') {
        std::cout << char(uint8_t(val));
      } else if (val == ' ') {
        std::cout << " ";
      } else if (val == '"') {
        std::cout << "\\\"";
      } else if (val == '\\') {
        std::cout << "\\\\";
      } else if (val == '\n') {
        std::cout << "\\n";
      } else if (val == '\r') {
        std::cout << "\\r";
      } else if (val == '\t') {
        std::cout << "\\t";
      } else {
        std::cout << "?";
      }
    }
    std::cout << "\"\n";
  };

  // ARGC: ()->(A: argc)
  state.ex.syscalls[StringToMixInt("ARGC")] = [argc](State& state) {
    state.rA() = Word(argc - 1);
  };

  // ARGV: (A: buf, X: bufSize, I1:argIndex)->(A: wordsWrittenToBuf)
  // Stores each C byte in a MIX word.
  state.ex.syscalls[StringToMixInt("ARGV")] = [argc, argv](State& state) {
    int buf = state.rA().value();
    int bufSize = state.rX().value();
    int argIndex = state.rI(1).value();

    if (argIndex < 0 || argIndex >= (argc - 1)) {
      ThrowMixException("Invalid argument number: ", argIndex,
                        ", allowed range: [0..", argc - 2, "]");
    }

    int length = strlen(argv[argIndex + 1]);
    for (int i = 0; i < length && i < bufSize && buf + i < state.mem.size();
         i++) {
      state.mem.at(buf + i) = CCharToWord(argv[argIndex + 1][i]);
    }
    state.rA() = Word(length);
  };

  // FOPEN: (A: nameBuf, X: nameSize, I1:modeStringLoc)->(A: fileIndex or -1)
  // Mode string is a MIX string
  state.ex.syscalls[StringToMixInt("FOPEN")] = [argc, argv](State& state) {
    int nameBuf = state.rA().value();
    int nameSize = state.rX().value();
    int modeStringLoc = state.rI(1).value();

    if (modeStringLoc < 0 || modeStringLoc >= state.mem.size())
      ThrowMixException("Invalid mode string location");
    std::string mode =
        ToLower(WordToAscii(state.mem.at(modeStringLoc), /*skipWs=*/true));

    std::string name;
    name.reserve(nameSize);
    if (nameBuf + nameSize >= state.mem.size())
      ThrowMixException("Name buffer extends outside of memory");
    for (int i = 0; i < nameSize; i++)
      name.push_back(WordToCChar(state.mem.at(nameBuf + i)));

    FILE* f = fopen(name.c_str(), mode.c_str());
    if (f == nullptr) {
      state.rA() = Word(-1);
      return;
    }

    constexpr int maxIndex = 4000;
    int i;
    for (i = 3; i < maxIndex; i++) {
      if (!state.ex.file_ptrs.count(i)) {
        break;
      }
    }
    if (i >= maxIndex)
      ThrowMixException("Too many files are open.");

    state.ex.file_ptrs[i] = f;
    state.rA() = Word(i);
  };

  // FREAD: (A: buf, X: size, I1:fileIndex)->(A: wordsWrittenToBuf)
  // Stores each C byte in a MIX word.
  // if wordsWrittenToBuf < size then it is either an error or EOF.
  // We can use FERRO to check if it's an error.
  state.ex.syscalls[StringToMixInt("FREAD")] = [argc, argv](State& state) {
    int buf = state.rA().value();
    int size = state.rX().value();
    int fileno = state.rI(1).value();

    if (!state.ex.file_ptrs.count(fileno))
      ThrowMixException("File number not found: ", fileno);
    FILE* f = state.ex.file_ptrs[fileno];

    // TODO: not allocate all the time.
    std::vector<char> cBuf(size, 0);
    int result = fread(cBuf.data(), 1, size, f);

    for (int i = 0; i < result && buf + i < state.mem.size(); i++) {
      state.mem.at(buf + i) = CCharToWord(cBuf[i]);
    }

    state.rA() = Word(result);
  };

  // FERRO: (I1:fileIndex)->(A: 0 or error)
  state.ex.syscalls[StringToMixInt("FERRO")] = [argc, argv](State& state) {
    int fileno = state.rI(1).value();

    if (!state.ex.file_ptrs.count(fileno))
      ThrowMixException("File number not found: ", fileno);
    FILE* f = state.ex.file_ptrs[fileno];

    int result = ferror(f);
    if (result < -Word::kAbsMask || result > Word::kAbsMask)
      result = -1;

    state.rA() = Word(result);
  };

  // FCLOS: (I1:fileIndex)->(A: 0 or error)
  state.ex.syscalls[StringToMixInt("FCLOS")] = [argc, argv](State& state) {
    int fileno = state.rI(1).value();

    if (!state.ex.file_ptrs.count(fileno))
      ThrowMixException("File number not found: ", fileno);

    int result = fclose(state.ex.file_ptrs[fileno]);
    state.ex.file_ptrs.erase(fileno);
    state.rA() = Word(result);
  };
}

// ---- Graphics ----

#define BGI_DIE(...)                  \
  do {                                \
    fprintf(stderr, "Fatal error: "); \
    fprintf(stderr, __VA_ARGS__);     \
    fprintf(stderr, "\n");            \
    fflush(stderr);                   \
    std::exit(1);                     \
  } while (false)

#define BGI_WARN(...)             \
  do {                            \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n");        \
    fflush(stderr);               \
  } while (false)

#define BGI_SDL_CHECK_ZERO(expr) \
  if ((expr) != 0)               \
  BGI_DIE("%s: %s", #expr, SDL_GetError())

#define BGI_SDL_CHECK_PTR(expr) \
  if ((expr) == nullptr)        \
  BGI_DIE("%s: %s", #expr, SDL_GetError())

#define BGI_WARN_FALSE(expr) \
  if (!(expr))               \
  BGI_WARN("Warning: %s failed", #expr)

using Color = uint32_t;
struct Surface {
  Surface() {}

  Surface(int w, int h) : pixels(w * h), w(w), h(h) {}

  std::vector<uint32_t> pixels;
  int w = 0;
  int h = 0;
};

class NonCopyable {
 public:
  NonCopyable() = default;
  virtual ~NonCopyable() = default;
  NonCopyable(const NonCopyable&) = delete;
  NonCopyable& operator=(const NonCopyable&) = delete;
};

struct KeyPress {
  bool should_quit = false;
  bool is_repeat = false;
  // Use this if the key label matters
  SDL_Keycode keycode = SDLK_UNKNOWN;
  // Use this if the key location matters
  // For example, you want the keys at the location of the WASD keys
  // on the US keyboard, and the label doesn't matter.
  SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
};

struct Size {
  Size() {}
  Size(int w, int h) : w(w), h(h) {}
  int w = 0;
  int h = 0;
};

class Window : private NonCopyable {
 public:
  Window(std::string_view title, int w = 800, int h = 600, int scale = 2,
         std::optional<int> pos_x = std::nullopt,
         std::optional<int> pos_y = std::nullopt);
  ~Window() override;

  // TODO: show only after update?
  void Update(const Surface& surface);

  int width() const { return size().w; }
  int height() const { return size().h; }
  Size size() const;
  bool fullscreen() const;
  void set_fullscreen(bool full_screen);

 private:
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
};

Window::Window(std::string_view title, int w, int h, int scale,
               std::optional<int> pos_x, std::optional<int> pos_y) {
  Size physical_size(scale * w, scale * h);
  BGI_SDL_CHECK_PTR(window_ = SDL_CreateWindow(
                        std::string(title).c_str(),
                        pos_x.value_or(SDL_WINDOWPOS_UNDEFINED),
                        pos_y.value_or(SDL_WINDOWPOS_UNDEFINED),
                        physical_size.w, physical_size.h, SDL_WINDOW_SHOWN));
  BGI_SDL_CHECK_PTR(
      renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_PRESENTVSYNC));
  BGI_SDL_CHECK_ZERO(SDL_RenderSetLogicalSize(renderer_, w, h));
  BGI_SDL_CHECK_PTR(texture_ =
                        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING, w, h));
}

Window::~Window() {
  SDL_DestroyWindow(window_);
  SDL_DestroyRenderer(renderer_);
  SDL_DestroyTexture(texture_);
}

void Window::Update(const Surface& surface) {
  BGI_SDL_CHECK_ZERO(SDL_UpdateTexture(texture_, NULL, surface.pixels.data(),
                                       surface.w * sizeof(Color)));
  BGI_SDL_CHECK_ZERO(SDL_RenderCopy(renderer_, texture_, NULL, NULL));
  SDL_RenderPresent(renderer_);
}

Size Window::size() const {
  Size size;
  SDL_RenderGetLogicalSize(renderer_, &size.w, &size.h);
  return size;
}
bool Window::fullscreen() const {
  return SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void Window::set_fullscreen(bool full_screen) {
  BGI_SDL_CHECK_ZERO(SDL_SetWindowFullscreen(
      window_, full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
}

INLINE void SetPx(State& state) {
  int color = state.rA().value() | 0xff000000u;
  int x = state.rI(1).value();
  int y = state.rI(2).value();
  if (x < 0 || x >= state.ex.w || y < 0 || y >= state.ex.h)
    ThrowMixException("Invalid pixel: ", x, y);
  state.ex.pixels[y * state.ex.w + x] = color;
};

// TODO: Consider: first fill the buffer and then open the window.
void AddGraphicsSyscalls(State& state) {
  state.ex.syscalls[StringToMixInt("INITG")] = [](State& state) {
    int w = state.rA().value();
    int h = state.rX().value();
    int fullscreen = state.rI(1).value();

    BGI_SDL_CHECK_ZERO(SDL_Init(SDL_INIT_VIDEO));
    BGI_WARN_FALSE(SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest"));

    state.ex.window = std::make_unique<Window>("MIX Machine", w, h, 1);
    state.ex.window->set_fullscreen(fullscreen);
    state.ex.surface = std::make_unique<Surface>(w, h);
    std::fill(state.ex.surface->pixels.begin(), state.ex.surface->pixels.end(),
              0xffffffffu);
    state.ex.last_update_ms = SDL_GetTicks();
    state.ex.pixels = state.ex.surface->pixels.data();
    state.ex.w = w;
    state.ex.h = h;
  };

  state.ex.syscalls[StringToMixInt("UPDAG")] = [](State& state) {
    state.ex.window->Update(*state.ex.surface);
    int64_t time_ms = SDL_GetTicks();
    if (state.ex.show_frame_time) {
      if (time_ms >= state.ex.last_update_ms) {
        int64_t diff_ms = time_ms - state.ex.last_update_ms;
        std::ostringstream formatted;
        formatted << std::fixed << std::setprecision(2);
        formatted << "Frame: " << diff_ms << " ms";
        formatted << " " << (1000.0f / diff_ms) << " fps";
        if (diff_ms > 1000) {
          formatted << " " << (diff_ms / 1000.0f) << " spf";
        }
        std::cout << formatted.str() << std::endl;
      } else {
        std::cout << "Frame time could not be calculated.\n";
      }
    }
    state.ex.last_update_ms = time_ms;
  };

  state.ex.syscalls[StringToMixInt("SETPX")] = SetPx;

  state.ex.syscalls[StringToMixInt("WAITK")] = [](State& state) {
    KeyPress k;

    SDL_Event e;
    while (
        !(SDL_WaitEvent(&e) && (e.type == SDL_QUIT || e.type == SDL_KEYDOWN)))
      ;
    if (e.type == SDL_QUIT) {
      k.should_quit = true;
    } else {
      k.should_quit = false;
      k.is_repeat = (e.key.repeat != 0);
      k.keycode = e.key.keysym.sym;
      k.scancode = e.key.keysym.scancode;
    };

    state.rA() = ToWordOr(k.keycode, 0);
    state.rX() = Word(k.scancode);
    state.rI(1) = Word(k.is_repeat);
    state.rI(2) = Word(k.should_quit);
  };

  state.ex.syscalls[StringToMixInt("POLLK")] = [](State& state) {
    KeyPress k;
    bool has_key = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT:
          has_key = true;
          k.should_quit = true;
          break;
        case SDL_KEYDOWN:
          has_key = true;
          k.should_quit = false;
          k.is_repeat = (e.key.repeat != 0);
          k.keycode = e.key.keysym.sym;
          k.scancode = e.key.keysym.scancode;
          break;
        default:;
      }
    };

    state.rA() = ToWordOr(k.keycode, 0);
    state.rX() = Word(k.scancode);
    state.rI(1) = Word(k.is_repeat);
    state.rI(2) = Word(k.should_quit);
    state.rI(3) = Word(has_key);
  };

  state.ex.syscalls[StringToMixInt("FRAMT")] = [](State& state) {
    state.ex.show_frame_time = (state.rA().value() != 0);
  };

  state.ex.syscalls[StringToMixInt("CLOSG")] = [](State& state) {
    SDL_Quit();
    state.ex.window.reset();
    state.ex.surface.reset();
  };
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: mix filename.mixal\n";
    return 1;
  }

  ParserState parser_state;
  std::ifstream f(argv[1]);
  if (!f.is_open()) {
    std::cerr << "error: could not open file " << argv[1] << "\n";
    return 1;
  }

  Parse(f, parser_state);
  f.close();

  if (!parser_state.had_error) {
    State state;
    state.mem = std::move(parser_state.mem);
    state.next_instr = parser_state.start_location;
    AddBasicSyscalls(argc, argv, state);
    AddGraphicsSyscalls(state);
    SimulateMix(state);
  }

  return 0;
}
