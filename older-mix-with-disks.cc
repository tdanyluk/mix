#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

// Platform checks:
#if __cplusplus < 201703L
#error "This code requires at least C++17"
#endif
static_assert(sizeof(int) >= 4, "This code requires at least 32 bit int.");

class MixException : public std::runtime_error {
 public:
  template <typename... Args>
  explicit MixException(const Args&... args)
      : std::runtime_error([&] {
          std::stringstream stream;
          (stream << ... << args);
          return stream.str();
        }()) {}

  explicit MixException(const char* message) : std::runtime_error(message) {}
};

// clang-format off
enum OpCode {
  kNop, kAdd, kSub, kMul, kDiv, kSpec, kShift, kMove, kLda, kLd1, kLd2, kLd3,
  kLd4, kLd5, kLd6, kLdx, kLdan, kLd1n, kLd2n, kLd3n, kLd4n, kLd5n, kLd6n,
  kLdxn, kSta, kSt1, kSt2, kSt3, kSt4, kSt5, kSt6, kStx, kStj, kStz, kJbus,
  kIoc, kIn, kOut, kJred, kJump, kJa, kJ1, kJ2, kJ3, kJ4, kJ5, kJ6, kJx,
  kAddrOpA, kAddrOp1, kAddrOp2, kAddrOp3, kAddrOp4, kAddrOp5, kAddrOp6,
  kAddrOpX, kCmpa, kCmp1, kCmp2, kCmp3, kCmp4, kCmp5, kCmp6, kCmpx, kNumOpCodes
};

enum ArithmeticField {
  kFloatField = 6,
};

enum SpecialField {
  kNumField, kCharField, kHltField,
};

enum ShiftField {
  kSlaField, kSraField, kSlaxField, kSraxField, kSlcField, kSrcField,
};

enum JumpField {
  kJmpField, kJsjField, kJovField, kJnovField, kJlField, kJeField, kJgField,
  kJgeField, kJneField, kJleField,
};

enum RegJumpField {
  kJnField, kJzField, kJpField, kJnnField, kJnzField, kJnpField,
};

enum AddrOpField {
  kIncField, kDecField, kEntField, kEnnField,
};

enum IoDeviceField {
  kTape0Field, kTape1Field, kTape2Field, kTape3Field, kTape4Field,
  kTape5Field, kTape6Field, kTape7Field, kDisk0Field, kDisk1Field,
  kDisk2Field, kDisk3Field, kDisk4Field, kDisk5Field, kDisk6Field,
  kDisk7Field, kCardReaderField, kCardPunchField, kLinePrinterField,
  KTerminalField, kPaperTapeField, kNumDevices
};

enum IoDirection {
  kRead = 1,
  kWrite = 2,
  kReadWrite = kRead | kWrite,
};

enum DeviceEncoding {
  // Full word
  kBinary,
  kCharacter,
};

enum IoControlType {
  kNoControl, kSeekX, kRewindOnly, kWind, kNewPage
};
// clang-format on

struct DeviceDesc {
  IoDeviceField device_field = kTape0Field;
  int block_size_words = 0;
  IoDirection direction = kRead;
  DeviceEncoding encoding = kBinary;
  IoControlType control_type = kNoControl;
  std::string file_name;
};

const std::array<DeviceDesc, kNumDevices> device_table = {
    {{kTape0Field, 100, kReadWrite, kBinary, kWind, "tape0.bin"},
     {kTape1Field, 100, kReadWrite, kBinary, kWind, "tape1.bin"},
     {kTape2Field, 100, kReadWrite, kBinary, kWind, "tape2.bin"},
     {kTape3Field, 100, kReadWrite, kBinary, kWind, "tape3.bin"},
     {kTape4Field, 100, kReadWrite, kBinary, kWind, "tape4.bin"},
     {kTape5Field, 100, kReadWrite, kBinary, kWind, "tape5.bin"},
     {kTape6Field, 100, kReadWrite, kBinary, kWind, "tape6.bin"},
     {kTape7Field, 100, kReadWrite, kBinary, kWind, "tape7.bin"},
     {kDisk0Field, 100, kReadWrite, kBinary, kSeekX, "disk0.bin"},
     {kDisk1Field, 100, kReadWrite, kBinary, kSeekX, "disk1.bin"},
     {kDisk2Field, 100, kReadWrite, kBinary, kSeekX, "disk2.bin"},
     {kDisk3Field, 100, kReadWrite, kBinary, kSeekX, "disk3.bin"},
     {kDisk4Field, 100, kReadWrite, kBinary, kSeekX, "disk4.bin"},
     {kDisk5Field, 100, kReadWrite, kBinary, kSeekX, "disk5.bin"},
     {kDisk6Field, 100, kReadWrite, kBinary, kSeekX, "disk6.bin"},
     {kDisk7Field, 100, kReadWrite, kBinary, kSeekX, "disk7.bin"},
     {kCardReaderField, 16, kRead, kCharacter, kNoControl, ""},
     {kCardPunchField, 16, kRead, kCharacter, kNoControl, ""},
     {kLinePrinterField, 24, kWrite, kCharacter, kNewPage, ""},
     {KTerminalField, 14, kReadWrite, kCharacter, kNoControl, ""},
     {kPaperTapeField, 14, kRead, kCharacter, kRewindOnly, "paper.txt"}}};

int Field(int left, int right) { return left * 8 + right; }

struct Word {
  static const int kSignBit = (1 << 30);
  static const int kAbsMask = (1 << 30) - 1;
  static const int kByteMask = (1 << 6) - 1;

  Word() = default;

  explicit Word(int value) {
    if (value < -kAbsMask || value > kAbsMask)
      throw MixException("Word value outside of range: ", value);
    data = value < 0 ? (kSignBit | -value) : value;
  }

  Word(int sign, int abs) {
    if (abs < 0 || abs > kAbsMask)
      throw MixException("Word absolute value outside of range: ", abs);
    data = sign < 0 ? (kSignBit | abs) : abs;
  }

  Word(int sign, int b1, int b2, int b3, int b4, int b5) {
    for (int b : {b1, b2, b3, b4, b5})
      if (b < 0 || b > 63)
        throw MixException("Byte value outside of range: ", b);

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
    assert(i >= 0 && i <= 5);
    if (i == 0) {
      return sign();
    }
    const int shift = (5 - i) * 6;
    return (data >> shift) & kByteMask;
  }

  Word part(int field) const {
    int left = field / 8;
    int right = field % 8;
    assert(left >= 0 && left <= 5);
    assert(right >= left && right <= 5);

    Word result;
    if (left == 0) {
      result.set_sign(sign());
      left++;
    }
    for (int i = left; i <= right; i++) {
      result.set_byte(i + (5 - right), byte(i));
    }
    return result;
  }

  void set_part(int field, Word value) {
    int left = field / 8;
    int right = field % 8;
    assert(left >= 0 && left <= 5);
    assert(right >= left && right <= 5);

    if (left == 0) {
      set_sign(value.sign());
      left = 1;
    }
    for (int i = left; i <= right; i++) {
      set_byte(i, value.byte(5 - (right - i)));
    }
  }

  void set_byte(int i, int value) {
    assert(i >= 0 && i <= 5);
    if (i == 0) {
      return set_sign(value);
    }
    assert(value >= 0 && value <= 63);
    const int shift = (5 - i) * 6;
    data = (data & ~(kByteMask << shift)) | (value << shift);
  }

  int address() const { return part(Field(0, 2)).value(); }

  void set_address(int address) {
    if (address <= -64 * 64 || address >= 64 * 64)
      throw MixException("Address outside of range: ", address);
    set_part(Field(0, 2), Word(address));
  }

  int index() const { return byte(3); }
  void set_index(int index) { set_byte(3, index); }

  int field() const { return byte(4); }
  void set_field(int field) { set_byte(4, field); }

  int code() const { return byte(5); }
  void set_code(int code) { set_byte(5, code); }

  bool operator==(Word other) const { return data == other.data; }

  Word operator-() const { return Word(-sign(), abs()); }

  // 1 bit sign, 30 bit absolute value
  int data = 0;
};

const Word kNegZero(-1, 0);

std::ostream& operator<<(std::ostream& out, const Word& word) {
  if (word == kNegZero) {
    out << "-0";
    return out;
  }
  out << word.value();
  return out;
}

struct WordOverflow {
  Word word;
  bool overflow = false;

  bool operator==(WordOverflow other) const {
    return word == other.word && overflow == other.overflow;
  }
};

struct HighLow {
  Word high;
  Word low;

  bool operator==(HighLow other) const {
    return high == other.high && low == other.low;
  }
};

struct DivRemOverflow {
  Word div;
  Word rem;
  bool overflow = false;

  bool operator==(DivRemOverflow other) const {
    return div == other.div && rem == other.rem && overflow == other.overflow;
  }
};

WordOverflow add(Word a, Word b) {
  int sign;
  int abs;

  if (a.sign() == b.sign()) {
    sign = a.sign();
    abs = a.abs() + b.abs();
  } else {
    sign = a.sign();
    abs = a.abs() - b.abs();
    if (abs < 0) {
      abs = -abs;
      sign = -sign;
    }
  }

  return {Word(sign, abs & Word::kAbsMask), bool(abs & ~Word::kAbsMask)};
}

HighLow mul(Word a, Word b) {
  int sign = a.sign() * b.sign();
  int64_t abs = int64_t(a.abs()) * b.abs();
  return {Word(sign, abs >> 30), Word(sign, abs & Word::kAbsMask)};
}

DivRemOverflow div(Word high, Word low, Word divisor) {
  if (divisor.abs() == 0) {
    return {Word(-0xbeef), Word(-0xbeef), true};
  }
  int64_t abs = (int64_t(high.abs()) << 30) | low.abs();
  int64_t div_abs = abs / divisor.abs();
  if (div_abs & ~Word::kAbsMask) {
    return {Word(-0xbeef), Word(-0xbeef), true};
  }
  int64_t rem_abs = abs % divisor.abs();
  return {Word(high.sign() * divisor.sign(), div_abs),
          Word(high.sign(), rem_abs), false};
}

Word shift_left(Word a, int shift) {
  Word result;
  result.set_sign(a.sign());
  for (int i = 1; i <= 5; i++) {
    int from_ind = i + shift;
    result.set_byte(i, from_ind >= 1 && from_ind <= 5 ? a.byte(from_ind) : 0);
  }
  return result;
}

int get_non_sign_byte(Word high, Word low, int i) {
  assert(i >= 0 && i < 10);
  if (i < 5) {
    return high.byte(i + 1);
  }
  return low.byte(i - 5 + 1);
}

void set_non_sign_byte(HighLow& hl, int i, int value) {
  assert(i >= 0 && i < 10);
  assert(value >= 0 && value <= 63);
  if (i < 5) {
    hl.high.set_byte(i + 1, value);
    return;
  }
  hl.low.set_byte(i - 5 + 1, value);
}

int non_neg_mod(int a, int m) {
  assert(m > 0);
  int rem = a % m;
  if (rem < 0) {
    rem += m;
  }
  return rem;
}

HighLow shift_left(Word high, Word low, int shift, bool cyclic) {
  HighLow result;
  result.high.set_sign(high.sign());
  result.low.set_sign(low.sign());
  for (int i = 0; i < 10; i++) {
    int from_ind = i + shift;
    if (cyclic) {
      from_ind = non_neg_mod(from_ind, 10);
    }
    set_non_sign_byte(result, i,
                      from_ind >= 0 && from_ind < 10
                          ? get_non_sign_byte(high, low, from_ind)
                          : 0);
  }
  return result;
}

WordOverflow num(Word high, Word low) {
  WordOverflow result;
  result.word.set_sign(high.sign());
  int64_t n = 0;
  for (int i = 1; i <= 5; i++) {
    n *= 10;
    n += (high.byte(i) % 10);
  }
  for (int i = 1; i <= 5; i++) {
    n *= 10;
    n += (low.byte(i) % 10);
  }
  result.word.set_abs(n & Word::kAbsMask);
  result.overflow = bool(n & ~Word::kAbsMask);
  return result;
}

HighLow chars(Word word) {
  HighLow result;
  int n = word.abs();
  for (int i = 5; i >= 1; i--) {
    result.low.set_byte(i, 30 + n % 10);
    n /= 10;
  }
  for (int i = 5; i >= 1; i--) {
    result.high.set_byte(i, 30 + n % 10);
    n /= 10;
  }
  assert(n == 0);
  return result;
}

char ToAsciiChar(int val) {
  static const char s[] =
      " ABCDEFGHI~JKLMNOPQR[#STUVWXYZ0123456789.,()+-*/=$<>@;:'";
  static_assert(sizeof(s) - 1 == 56);
  assert(val >= 0 && val <= 56);
  return s[val];
}

int ToMixChar(char c) {
  static const int8_t val[] = {
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      0,  -1, -1, 21, 49, -1, -1, 55, 42, 43, 46, 44, 41, 45, 40, 47,
      30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 54, 53, 50, 48, 51, -1,
      52, 1,  2,  3,  4,  5,  6,  7,  8,  9,  11, 12, 13, 14, 15, 16,
      17, 18, 19, 22, 23, 24, 25, 26, 27, 28, 29, 20, -1, -1, -1, -1,
      -1, 1,  2,  3,  4,  5,  6,  7,  8,  9,  11, 12, 13, 14, 15, 16,
      17, 18, 19, 22, 23, 24, 25, 26, 27, 28, 29, -1, -1, -1, 10, -1};
  static_assert(sizeof(val) == 128);

  auto uc = (unsigned char)c;
  if (uc >= 128) {
    return -1;
  }
  return val[uc];
}

std::optional<int64_t> FileSize(std::fstream& fstream) {
  if (!fstream.good()) return std::nullopt;
  std::fstream::pos_type pos = fstream.tellp();
  fstream.seekp(0, std::ios_base::end);
  if (!fstream.good()) return std::nullopt;
  auto size = (int64_t)fstream.tellp();
  fstream.seekp(pos);
  if (!fstream.good()) return std::nullopt;
  return size;
}

bool OpenFileAt0RW(std::fstream& fstream, const std::string& file_name,
                   bool binary) {
  std::ios_base::openmode open_mode = std::ios_base::in | std::ios_base::out;
  if (binary) {
    open_mode |= std::ios_base::binary;
  }
  fstream.open(file_name, open_mode);
  if (fstream.good()) {
    std::cerr << "opened " << file_name << " (rw)" << std::endl;
  } else {
    open_mode |= std::ios_base::trunc;
    fstream.open(file_name, open_mode);
    if (fstream.good()) {
      std::cerr << "created " << file_name << " (rw)" << std::endl;
    }
  }
  return fstream.good();
}

bool OpenFileAt0R(std::fstream& fstream, const std::string& file_name,
                  bool binary) {
  std::ios_base::openmode open_mode = std::ios_base::in;
  if (binary) {
    open_mode |= std::ios_base::binary;
  }
  fstream.open(file_name, open_mode);
  if (fstream.good()) {
    std::cerr << "opened " << file_name << " (r)" << std::endl;
  } else {
    std::cerr << "missing " << file_name << " (r)" << std::endl;
  }
  return fstream.good();
}

bool SeekWithExpand(std::fstream& fstream, const std::string& file_name,
                    int64_t location) {
  std::optional<int64_t> opt_size = FileSize(fstream);
  if (!opt_size.has_value()) return false;
  int64_t size = opt_size.value();

  if (location <= size) {
    // Equivalent to seekg for files.
    fstream.seekp(location);
    return fstream.good();
  }

  std::cerr << "expanding " << file_name << " " << size << " -> " << location
            << std::endl;
  fstream.seekp(0, std::ios_base::end);
  if (!fstream.good()) return false;
  while (size < location) {
    fstream.put(0);
    size++;
  }
  return fstream.good();
}

struct State {
  std::array<Word, 10> registers;

  Word& rA() { return registers[0]; }

  Word rA() const { return registers[0]; }

  Word& rI(int i) {
    assert(i >= 1 && i <= 6);
    return registers[i];
  }

  Word rI(int i) const {
    assert(i >= 1 && i <= 6);
    return registers[i];
  }

  Word& rX() { return registers[7]; }

  Word rX() const { return registers[7]; }

  Word& rJ() { return registers[8]; }

  Word rJ() const { return registers[8]; }

  int next_instr = 0;
  //<0: lt, =0: eq, >0: gt
  int cmp_result = 0;
  bool overflow = false;
  int64_t time = 0;
  // Maybe not store in stack?
  std::array<Word, 4000> mem;
  bool halt = false;

  std::array<std::fstream, kNumDevices> file_streams;
};

// calculate M
int get_address(const State& state, Word instr) {
  int addr = instr.address();
  if (instr.index() != 0) {
    addr += state.rI(instr.index()).value();
  }
  return addr;
}

// load V
Word load_mem_operand_part(const State& state, Word instr) {
  return state.mem.at(get_address(state, instr)).part(instr.field());
}

void store_to_mem_operand_part(State& state, Word instr, Word value) {
  state.mem.at(get_address(state, instr)).set_part(instr.field(), value);
}

void cycle(State&, Word) {
  // empty
}

void add(State& state, Word instr) {
  assert(instr.field() != kFloatField);
  Word mem_op = load_mem_operand_part(state, instr);
  WordOverflow wo = add(state.rA(), mem_op);
  state.rA() = wo.word;
  state.overflow |= wo.overflow;
}

void sub(State& state, Word instr) {
  assert(instr.field() != kFloatField);
  Word mem_op = load_mem_operand_part(state, instr);
  WordOverflow wo = add(state.rA(), -mem_op);
  state.rA() = wo.word;
  state.overflow |= wo.overflow;
}

void mul(State& state, Word instr) {
  assert(instr.field() != kFloatField);
  Word mem_op = load_mem_operand_part(state, instr);
  HighLow hl = mul(state.rA(), mem_op);
  state.rA() = hl.high;
  state.rX() = hl.low;
}

void div(State& state, Word instr) {
  assert(instr.field() != kFloatField);
  Word mem_op = load_mem_operand_part(state, instr);
  DivRemOverflow dro = div(state.rA(), state.rX(), mem_op);
  state.rA() = dro.div;
  state.rX() = dro.rem;
  state.overflow |= dro.overflow;
}

void spec(State& state, Word instr) {
  switch (instr.field()) {
    case kNumField: {
      WordOverflow wo = num(state.rA(), state.rX());
      state.rA() = wo.word;
      state.overflow |= wo.overflow;
    } break;
    case kCharField: {
      HighLow hl = chars(state.rA());
      // keep signs
      state.rA().set_abs(hl.high.abs());
      state.rX().set_abs(hl.low.abs());
    } break;
    case kHltField:
      state.halt = true;
      break;
    default:
      assert(false);
  }
}

void shift(State& state, Word instr) {
  int shift = get_address(state, instr);
  assert(shift >= 0);
  switch (instr.field()) {
    case kSlaField:
      state.rA() = shift_left(state.rA(), shift);
      break;
    case kSraField:
      state.rA() = shift_left(state.rA(), -shift);
      break;
    case kSlaxField: {
      HighLow hl = shift_left(state.rA(), state.rX(), shift, /*cyclic=*/false);
      state.rA() = hl.high;
      state.rX() = hl.low;
    } break;
    case kSraxField: {
      HighLow hl = shift_left(state.rA(), state.rX(), -shift,
                              /*cyclic=*/false);
      state.rA() = hl.high;
      state.rX() = hl.low;
    } break;
    case kSlcField: {
      HighLow hl = shift_left(state.rA(), state.rX(), shift, /*cyclic=*/true);
      state.rA() = hl.high;
      state.rX() = hl.low;
    } break;
    case kSrcField: {
      HighLow hl = shift_left(state.rA(), state.rX(), -shift, /*cyclic=*/true);
      state.rA() = hl.high;
      state.rX() = hl.low;
    } break;
    default:
      assert(false);
  }
}

void move(State& state, Word instr) {
  int from_address = get_address(state, instr);
  int num_words = instr.field();
  assert(num_words >= 0);
  Word& i1 = state.rI(1);

  for (int i = 0; i < num_words; i++) {
    state.mem.at(i1.value()) = state.mem.at(from_address + i);
    assert(i1.value() < 64 * 64 - 1);
    i1.set_value(i1.value() + 1);
  }

  state.time += num_words * 2;
}

void load(State& state, Word instr) {
  int code = instr.code();
  Word& target_reg = state.registers.at(code - kLda);
  target_reg = load_mem_operand_part(state, instr);
  if (code >= kLd1 && code <= kLd6) {
    assert(target_reg.abs() < 64 * 64);
  }
}

void loadn(State& state, Word instr) {
  int code = instr.code();
  Word& target_reg = state.registers.at(code - kLdan);
  target_reg = -load_mem_operand_part(state, instr);
  if (code >= kLd1 && code <= kLd6) {
    assert(target_reg.abs() < 64 * 64);
  }
}

void store(State& state, Word instr) {
  int reg_index = instr.code() - kSta;
  store_to_mem_operand_part(state, instr, state.registers.at(reg_index));
}

void ioc(State& state, Word instr) {
  int device = instr.field();
  const DeviceDesc& device_desc = device_table.at(device);
  int m = get_address(state, instr);
  std::fstream* fstream = nullptr;
  if (device_desc.file_name != "") {
    fstream = &state.file_streams.at(device);
    if (!fstream->is_open()) {
      if (device_desc.direction & kWrite) {
        if (!OpenFileAt0RW(*fstream, device_desc.file_name,
                           device_desc.encoding == kBinary)) {
          throw MixException("Cannot open file: ", device_desc.file_name,
                             " (rw)");
        }
      } else {
        if (!OpenFileAt0R(*fstream, device_desc.file_name,
                          device_desc.encoding == kBinary)) {
          throw MixException("Cannot open file: ", device_desc.file_name,
                             " (r)");
        }
      }
    }
  }

  if (device_desc.control_type == kNoControl) {
    throw MixException("IOC doesn't work for device: ", device);
  }
  if (device_desc.control_type == kSeekX) {
    if (m != 0) {
      throw MixException("IOC: M must be zero for device: ", device);
    }
    assert(device_desc.encoding == kBinary);
    assert(device_desc.direction == kReadWrite);
    assert(fstream != nullptr);
    int seek_location = 4 * device_desc.block_size_words * state.rX().value();
    if (!SeekWithExpand(*fstream, device_desc.file_name, seek_location)) {
      throw MixException("Cannot seek to ", seek_location,
                         " in file: ", device_desc.file_name, " (rw)");
    }
    return;
  }
  if (device_desc.control_type == kRewindOnly) {
    assert(fstream != nullptr);
    if (m != 0) {
      throw MixException("IOC: M must be zero for device: ", device);
    }
    fstream->seekg(0);
    if (!fstream->good()) {
      throw MixException("Could not rewind device: ", device);
    }
    return;
  }
  if (device_desc.control_type == kWind) {
    assert(fstream != nullptr);
    assert(device_desc.encoding == kBinary);
    int64_t current = fstream->tellp();
    int64_t target = 0;
    if (m != 0) {
      target = current + 4 * device_desc.block_size_words * m;
      if (target < 0) {
        target = 0;
      }
    }

    std::optional<int64_t> opt_size = FileSize(*fstream);
    if (!opt_size.has_value()) {
      throw MixException("Could not check file size for: ",
                         device_desc.file_name);
    }
    int64_t size_in_blocks =
        opt_size.value() / 4 / device_desc.block_size_words;
    int64_t target_in_blocks = target / 4 / device_desc.block_size_words;
    if (target > opt_size.value()) {
      throw MixException("Can not wind device ", device,
                         " past end: ", target_in_blocks, " > ",
                         size_in_blocks);
    }
    fstream->seekg(target);
    if (!fstream->good()) {
      throw MixException("Could not rewind device: ", device);
    }
    return;
  }
  if (device_desc.control_type == kNewPage) {
    if (m != 0) {
      throw MixException("IOC: M must be zero for device: ", device);
    }
    return;
  }
  assert(false);
}

void out(State& state, Word instr) {
  int device = instr.field();
  const DeviceDesc& device_desc = device_table.at(device);
  assert(device_desc.direction & kWrite);

  std::ostream* ostream = &std::cout;
  if (device_desc.file_name != "") {
    std::fstream* fstream = &state.file_streams.at(device);
    ostream = fstream;
    if (!fstream->is_open()) {
      if (!OpenFileAt0RW(*fstream, device_desc.file_name,
                         device_desc.encoding == kBinary)) {
        throw MixException("Cannot open file: ", device_desc.file_name,
                           " (rw)");
      }
    }
    if (device_desc.control_type == kSeekX) {
      assert(device_desc.encoding == kBinary);
      assert(device_desc.direction == kReadWrite);
      int seek_location = 4 * device_desc.block_size_words * state.rX().value();
      if (!SeekWithExpand(*fstream, device_desc.file_name, seek_location)) {
        throw MixException("Cannot seek to ", seek_location,
                           " in file: ", device_desc.file_name, " (rw)");
      }
    }
  }

  int num_words = device_desc.block_size_words;
  int begin_addr = get_address(state, instr);

  if (device_desc.encoding == kBinary) {
    for (int i = begin_addr; i < begin_addr + num_words; i++) {
      Word word = state.mem.at(i);
      // network byte order: big endian
      for (int j = 0; j < 4; j++) {
        char c = (word.data >> (24 - j * 8)) & 0xff;
        ostream->put(c);
      }
    }
    if (!ostream->good()) {
      throw MixException("Could not write to ", device_desc.file_name);
    }
    return;
  }

  int last_non_zero_addr = begin_addr - 1;
  for (int i = begin_addr; i < begin_addr + num_words; i++) {
    if (state.mem.at(i).abs() != 0) {
      last_non_zero_addr = i;
    }
  }
  if (last_non_zero_addr < begin_addr) {
    return;
  }
  int last_len = 0;
  Word last_word = state.mem.at(last_non_zero_addr);
  for (int i = 1; i <= 5; i++) {
    if (last_word.byte(i) != 0) {
      last_len = i;
    }
  }
  std::string s;
  int expected_len = (last_non_zero_addr - begin_addr) * 5 + last_len + 1;
  s.reserve(expected_len);
  for (int i = begin_addr; i <= last_non_zero_addr; i++) {
    int max_byte = i == last_non_zero_addr ? last_len : 5;
    Word word = state.mem.at(i);
    for (int j = 1; j <= max_byte; j++) {
      s.push_back(ToAsciiChar(word.byte(j)));
    }
  }
  s.push_back('\n');
  assert((int)s.size() == expected_len);
  (*ostream) << s;
}

void in(State& state, Word instr) {
  int device = instr.field();
  const DeviceDesc& device_desc = device_table.at(device);
  assert(device_desc.direction & kRead);

  std::istream* istream = &std::cin;
  if (device_desc.file_name != "") {
    std::fstream* fstream = &state.file_streams.at(device);
    istream = fstream;
    if (!fstream->is_open()) {
      if (device_desc.direction & kWrite) {
        if (!OpenFileAt0RW(*fstream, device_desc.file_name,
                           device_desc.encoding == kBinary)) {
          throw MixException("Cannot open file: ", device_desc.file_name,
                             " (rw)");
        }
      } else {
        if (!OpenFileAt0R(*fstream, device_desc.file_name,
                          device_desc.encoding == kBinary)) {
          throw MixException("Cannot open file: ", device_desc.file_name,
                             " (r)");
        }
      }
    }
    if (device_desc.control_type == kSeekX) {
      assert(device_desc.encoding == kBinary);
      assert(device_desc.direction == kReadWrite);
      int seek_location =
          4 * device_desc.block_size_words * (state.rX().value() + 1);
      // Ensure we can read enough.
      if (!SeekWithExpand(*fstream, device_desc.file_name, seek_location)) {
        throw MixException("Cannot seek to ", seek_location,
                           " in file: ", device_desc.file_name, " (rw)");
      }
      seek_location = 4 * device_desc.block_size_words * state.rX().value();
      if (!SeekWithExpand(*fstream, device_desc.file_name, seek_location)) {
        throw MixException("Cannot seek to ", seek_location,
                           " in file: ", device_desc.file_name, " (rw)");
      }
    }
  }

  int num_words = device_table.at(device).block_size_words;
  int begin_addr = get_address(state, instr);
  if (device_desc.encoding == kBinary) {
    for (int i = begin_addr; i < begin_addr + num_words; i++) {
      int& data = state.mem.at(i).data;
      data = 0;
      // network byte order: big endian
      for (int j = 0; j < 4; j++) {
        int value = istream->get();
        if (value == EOF) {
          throw MixException("End of file while reading: ",
                             device_desc.file_name);
        }
        auto uc = (unsigned char)value;
        data <<= 8;
        data |= uc;
      }
    }
    if (!istream->good()) {
      throw MixException("Could not read from ", device_desc.file_name);
    }
    return;
  }

  const int max_size = num_words * 5;
  std::string input_str;
  if (!istream->good()) {
    throw MixException("Could not read line from ", device_desc.file_name);
  }
  std::getline(*istream, input_str);
  if (istream->fail() || istream->bad()) {
    throw MixException("Could not read line from ", device_desc.file_name);
  }
  if ((int)input_str.size() > max_size) {
    std::cerr << "Line too long?\n";
    input_str = input_str.substr(0, max_size);
  }
  for (int i = 0; i < num_words; i++) {
    for (int j = 1; j <= 5; j++) {
      int ind = i * 5 + j - 1;
      int val = 0;
      if (ind < (int)input_str.size()) {
        val = ToMixChar(input_str.at(ind));
        if (val == -1) {
          std::cerr << "Warning: unsupported character: '" << input_str[i]
                    << "' (" << (int)(unsigned char)input_str[i] << ")\n";
          val = 0;
        }
      }
      state.mem.at(begin_addr + i).set_byte(j, val);
    }
  }
}

void jbus(State&, Word) {
  // never busy
}

void jred(State& state, Word instr) {
  // always ready
  state.next_instr = get_address(state, instr);
}

bool should_jump(int field, bool overflow, int cmp_result) {
  switch (field) {
    case kJmpField:
    case kJsjField:
      return true;
    case kJovField:
      return overflow;
    case kJnovField:
      return !overflow;
    case kJlField:
      return cmp_result < 0;
    case kJeField:
      return cmp_result == 0;
    case kJgField:
      return cmp_result > 0;
    case kJgeField:
      return cmp_result >= 0;
    case kJneField:
      return cmp_result != 0;
    case kJleField:
      return cmp_result <= 0;
    default:
      throw MixException("Unexpected field for jump instruction.\n");
  }
}

void jump(State& state, Word instr) {
  int field = instr.field();

  if (should_jump(field, state.overflow, state.cmp_result)) {
    if (field != kJsjField) {
      state.rJ() = Word(state.next_instr);
    }
    state.next_instr = get_address(state, instr);
  }

  switch (field) {
    case kJovField:
    case kJnovField:
      state.overflow = false;
    default:;
  }
}

bool should_reg_jump(int value, int field) {
  switch (field) {
    case kJnField:
      return value < 0;
    case kJzField:
      return value == 0;
    case kJpField:
      return value > 0;
    case kJnnField:
      return value >= 0;
    case kJnzField:
      return value != 0;
    case kJnpField:
      return value <= 0;
    default:
      assert(false);
  }
}

void reg_jump(State& state, Word instr) {
  int reg_index = instr.code() - kJa;
  int reg_value = state.registers.at(reg_index).value();
  int field = instr.field();

  if (should_reg_jump(reg_value, field)) {
    state.rJ() = Word(state.next_instr);
    state.next_instr = get_address(state, instr);
  }
}

void addr_op(State& state, Word instr) {
  int code = instr.code();
  Word& target_reg = state.registers.at(code - kAddrOpA);
  Word value = Word(get_address(state, instr));
  WordOverflow wo;
  switch (instr.field()) {
    case kIncField:
      wo = add(target_reg, value);
      target_reg = wo.word;
      state.overflow |= wo.overflow;
      break;
    case kDecField:
      wo = add(target_reg, -value);
      target_reg = wo.word;
      state.overflow |= wo.overflow;
      break;
    case kEntField:
      target_reg = Word(get_address(state, instr));
      break;
    case kEnnField:
      target_reg = -Word(get_address(state, instr));
      break;
    default:
      assert(false);
  }
  if (code >= kAddrOp1 && code <= kAddrOp6) {
    if (target_reg.abs() >= 64 * 64) {
      throw MixException("Index register overflow.");
    }
  }
}

void compare(State& state, Word instr) {
  assert(instr.field() != kFloatField);
  int a = state.registers.at(instr.code() - kCmpa).part(instr.field()).value();
  int b = load_mem_operand_part(state, instr).value();
  state.cmp_result = a - b;
}

using OpFun = void(State&, Word instr);

struct OpDesc {
  OpCode code = kNop;
  OpFun* fun = nullptr;
  int time = 0;
};

const std::array<OpDesc, kNumOpCodes> op_table = {{
    {kNop, cycle, 1},         {kAdd, add, 2},         {kSub, sub, 2},
    {kMul, mul, 10},        {kDiv, div, 12},        {kSpec, spec, 10},
    {kShift, shift, 2},     {kMove, move, 1},       {kLda, load, 2},
    {kLd1, load, 2},        {kLd2, load, 2},        {kLd3, load, 2},
    {kLd4, load, 2},        {kLd5, load, 2},        {kLd6, load, 2},
    {kLdx, load, 2},        {kLdan, loadn, 2},   {kLd1n, loadn, 2},
    {kLd2n, loadn, 2},   {kLd3n, loadn, 2},   {kLd4n, loadn, 2},
    {kLd5n, loadn, 2},   {kLd6n, loadn, 2},   {kLdxn, loadn, 2},
    {kSta, store, 2},       {kSt1, store, 2},       {kSt2, store, 2},
    {kSt3, store, 2},       {kSt4, store, 2},       {kSt5, store, 2},
    {kSt6, store, 2},       {kStx, store, 2},       {kStj, store, 2},
    {kStz, store, 2},       {kJbus, jbus, 1},       {kIoc, ioc, 1},
    {kIn, in, 1},           {kOut, out, 1},         {kJred, jred, 1},
    {kJump, jump, 1},       {kJa, reg_jump, 1},     {kJ1, reg_jump, 1},
    {kJ2, reg_jump, 1},     {kJ3, reg_jump, 1},     {kJ4, reg_jump, 1},
    {kJ5, reg_jump, 1},     {kJ6, reg_jump, 1},     {kJx, reg_jump, 1},
    {kAddrOpA, addr_op, 1}, {kAddrOp1, addr_op, 1}, {kAddrOp2, addr_op, 1},
    {kAddrOp3, addr_op, 1}, {kAddrOp4, addr_op, 1}, {kAddrOp5, addr_op, 1},
    {kAddrOp6, addr_op, 1}, {kAddrOpX, addr_op, 1}, {kCmpa, compare, 2},
    {kCmp1, compare, 2},     {kCmp2, compare, 2},     {kCmp3, compare, 2},
    {kCmp4, compare, 2},     {kCmp5, compare, 2},     {kCmp6, compare, 2},
    {kCmpx, compare, 2},
}};

Word Instruction(int code, int address = 0, int index = 0,
                 int field = Field(0, 5)) {
  Word word;
  word.set_address(address);
  word.set_byte(3, index);
  word.set_byte(4, field);
  word.set_byte(5, code);
  return word;
}

void SimulateMix(State& state) {
  int last_instruction = -1;
  int num_instructions_done = 0;
  try {
    while (!state.halt) {
      last_instruction = state.next_instr;
      if (state.next_instr < 0 || state.next_instr >= (int)state.mem.size()) {
        throw MixException(
            "Instruction counter outside range. Did you "
            "forget to HLT?\n");
      }
      Word instr = state.mem.at(state.next_instr++);
      OpDesc op = op_table.at(instr.code());
      assert(op.fun != nullptr);
      op.fun(state, instr);
      state.time += op.time;
      num_instructions_done++;
    }
  } catch (const std::exception& e) {
    std::cerr << "Exception received at instruction address "
              << last_instruction << ":\n";
    std::cerr << e.what() << "\n";
    std::cerr << "Instructions done so far: " << num_instructions_done << ".\n";
  }
  for (std::fstream& fstream : state.file_streams) {
    if (fstream.is_open()) {
      fstream.close();
    }
  }
}

void StoreText(State& state, int address, int max_words,
               const std::string& str) {
  if ((int)str.size() > max_words * 5) {
    std::cerr << "Warning: string truncation!\n";
  }
  for (int i = 0; i < std::min<int>(max_words * 5, (int)str.size()); i++) {
    int val = ToMixChar(str[i]);
    if (val == -1) {
      std::cerr << "Warning: unsupported character: '" << str[i] << "' ("
                << (int)(unsigned char)str[i] << ")\n";
      val = 0;
    }
    state.mem.at(address + i / 5).set_byte(1 + i % 5, val);
  }
}

void LoadGnuMixFile(State& state, const std::string& file_name) {
  // Tested with v1.2
  struct MixHeader {
    int32_t signature;
    int major_ver;
    int minor_ver;
    int16_t start;
    size_t path_len;
  };
  const int32_t kReleaseSignature = 0xdeadbeef;
  const int32_t kDebugSignature = 0xbeefdead;

  FILE* f = fopen(file_name.c_str(), "rb");
  MixHeader header;
  int count = fread(&header, sizeof(MixHeader), 1, f);
  assert(count == 1);
  assert(header.signature == kReleaseSignature ||
         header.signature == kDebugSignature);
  const bool is_debug = header.signature == kDebugSignature;
  int res = fseek(f, header.path_len, SEEK_CUR);
  assert(res == 0);
  if (is_debug) {
    // ignore symbol table
    while (getc(f) == ',') {
      fscanf(f, "%*s =%*d");
    }
  }

  state.next_instr = header.start;
  int next_addr = 0;
  uint32_t tagged_word = 0;
  while (fread(&tagged_word, sizeof(uint32_t), 1, f) == 1) {
    uint32_t addr_tag = (1ul << 31);
    uint32_t word_as_uint32 = tagged_word & ((1ul << 31) - 1);
    if (tagged_word & addr_tag) {
      next_addr = word_as_uint32;
    } else {
      Word word;
      word.data = (int)word_as_uint32;
      state.mem.at(next_addr++) = word;
      if (is_debug) {
        int res = fseek(f, sizeof(uint16_t), SEEK_CUR);
        assert(res == 0);
      }
    }
  }
  fclose(f);
}

void primitive_test() {
  assert((add(kNegZero, Word(0)) == WordOverflow{kNegZero, false}));
  assert((add(Word(0), kNegZero) == WordOverflow{Word(0), false}));
  assert((add(Word(1), Word(2)) == WordOverflow{Word(3), false}));
  assert((add(Word(-1), Word(-1)) == WordOverflow{Word(-2), false}));
  assert((add(Word(-1), Word(1)) == WordOverflow{kNegZero, false}));
  assert((add(Word(1), Word(-1)) == WordOverflow{Word(0), false}));
  assert((add(Word(1), Word(-2)) == WordOverflow{Word(-1), false}));
  assert((add(Word(2), Word(-1)) == WordOverflow{Word(1), false}));
  assert((add(Word(2), Word(-1)) == WordOverflow{Word(1), false}));
  assert((add(Word(1 << 29), Word(1 << 29)) == WordOverflow{Word(0), true}));
  assert((add(Word((1 << 29) + 1), Word((1 << 29) + 2)) ==
          WordOverflow{Word(3), true}));
  assert((add(Word(-(1 << 29) - 1), Word(-(1 << 29) - 2)) ==
          WordOverflow{Word(-3), true}));

  assert((mul(Word(6), Word(5)) == HighLow{Word(0), Word(30)}));
  assert((mul(Word(-6), Word(-5)) == HighLow{Word(0), Word(30)}));
  assert((mul(Word(-6), Word(5)) == HighLow{kNegZero, Word(-30)}));
  assert((mul(Word(6), Word(-5)) == HighLow{kNegZero, Word(-30)}));
  assert(
      (mul(Word(1 << 29), Word(1 << 29)) == HighLow{Word(1 << 28), Word(0)}));
  assert((mul(Word(1 << 29), Word((1 << 29) + 1)) ==
          HighLow{Word(1 << 28), Word(1 << 29)}));

  assert((div(Word(0), Word(11), Word(5)) ==
          DivRemOverflow{Word(2), Word(1), false}));
  assert((div(Word(5), Word(0), Word(5)) ==
          DivRemOverflow{Word(-0xbeef), Word(-0xbeef), true}));
  assert((div(Word(4), Word(0), Word(5)) ==
          DivRemOverflow{Word(858993459), Word(1), false}));
  assert((div(Word(0), -Word(11), Word(5)) ==
          DivRemOverflow{Word(2), Word(1), false}));
  assert((div(kNegZero, Word(11), Word(5)) ==
          DivRemOverflow{Word(-2), Word(-1), false}));
  assert((div(kNegZero, -Word(11), Word(5)) ==
          DivRemOverflow{Word(-2), Word(-1), false}));
  assert((div(Word(0), Word(11), -Word(5)) ==
          DivRemOverflow{Word(-2), Word(1), false}));

  assert((shift_left(Word(1, 1, 2, 3, 4, 5), 0) == Word(1, 1, 2, 3, 4, 5)));
  assert((shift_left(Word(-1, 1, 2, 3, 4, 5), 0) == Word(-1, 1, 2, 3, 4, 5)));
  assert((shift_left(Word(-1, 1, 2, 3, 4, 5), 1) == Word(-1, 2, 3, 4, 5, 0)));
  assert((shift_left(Word(-1, 1, 2, 3, 4, 5), 4) == Word(-1, 5, 0, 0, 0, 0)));
  assert((shift_left(Word(-1, 1, 2, 3, 4, 5), 5) == Word(-1, 0, 0, 0, 0, 0)));
  assert((shift_left(Word(-1, 1, 2, 3, 4, 5), 6) == Word(-1, 0, 0, 0, 0, 0)));
}

void complex_test() {
  {
    State state;
    state.mem[0] = Instruction(kNop);
    state.mem[1] = Instruction(kAdd, 10);
    state.mem[2] = Instruction(kAdd, 11);
    state.mem[3] = Instruction(kMul, 12);
    state.mem[4] = Instruction(kSpec, 0, 0, kHltField);
    state.mem[10] = Word(10);
    state.mem[11] = Word(20);
    state.mem[12] = Word(3);
    SimulateMix(state);
    assert(state.rA() == Word(0) && state.rX() == Word(90) &&
           state.overflow == false && state.halt);
  }

  {
    State state;
    // Main:
    state.mem[0] = Instruction(kJump, 3000, 0, kJmpField);
    state.mem[1] = Instruction(kSpec, 0, 0, kHltField);
    // X
    state.mem[1000] = Word(4);
    state.mem[1001] = Word(3);
    state.mem[1002] = Word(5);
    state.mem[1003] = Word(6);
    state.mem[1004] = Word(2);
    // Program M (maximum)
    // rA = m, rI1 = n, rI2 = j, rI3 = k
    state.mem[3000] =
        Instruction(kStj, 3009, 0, Field(0, 2));  // Store return addr
    state.mem[3001] = Instruction(kAddrOp3, 0, 1, kEntField);  // k = n
    state.mem[3002] = Instruction(kJump, 3005, 0, kJmpField);
    state.mem[3003] = Instruction(kCmpa, 1000, 3);
    state.mem[3004] = Instruction(kJump, 3007, 0, kJgeField);
    state.mem[3005] = Instruction(kAddrOp2, 0, 3, kEntField);
    state.mem[3006] = Instruction(kLda, 1000, 3);
    state.mem[3007] = Instruction(kAddrOp3, 1, 0, kDecField);
    state.mem[3008] = Instruction(kJ3, 3003, 0, kJpField);
    state.mem[3009] = Instruction(kJump, 3009, 0, kJmpField);
    state.rI(1) = Word(5);
    SimulateMix(state);
    assert(state.rA() == Word(6) && state.rI(2) == Word(3) && state.halt);
  }

  {
    State state;
    state.mem[3999] = Instruction(kSpec, 0, 0, kHltField);
    SimulateMix(state);
    assert(state.halt && state.time == 4009);
  }

  {
    State state;
    int i = 0;
    state.mem[i++] = Instruction(kIoc, 0, 0, kLinePrinterField);
    state.mem[i++] = Instruction(kOut, 100, 0, kLinePrinterField);
    state.mem[i++] = Instruction(kLda, 300);
    state.mem[i++] = Instruction(kSpec, 0, 0, kCharField);
    state.mem[i++] = Instruction(kSta, 300);
    state.mem[i++] = Instruction(kStx, 301);
    state.mem[i++] = Instruction(kOut, 300, 0, kLinePrinterField);
    state.mem[i++] = Instruction(kIoc, 0, 0, kLinePrinterField);
    state.mem[i++] = Instruction(kOut, 500, 0, kLinePrinterField);
    state.mem[i++] = Instruction(kSpec, 0, 0, kHltField);
    i = 100;
    state.mem[i++] = Word(-1, 8, 5, 13, 13, 16);
    state.mem[i++] = Word(+1, 0, 26, 16, 19, 13);
    state.mem[i++] = Word(+1, 4, 0, 0, 0, 0);
    i = 300;
    state.mem[i++] = Word(19920828);
    StoreText(state, 500, device_table.at(kLinePrinterField).block_size_words,
              "What' up");
    SimulateMix(state);
    assert(state.halt);
  }
}

void interactive_test() {
  State state;
  int i = 0;
  state.mem[i++] = Instruction(kOut, 100, 0, kLinePrinterField);
  state.mem[i++] = Instruction(kJbus, 1, 0, kLinePrinterField);
  state.mem[i++] = Instruction(kIn, 300, 0, kCardReaderField);
  state.mem[i++] = Instruction(kJbus, 3, 0, kCardReaderField);
  state.mem[i++] = Instruction(kLda, 401);
  state.mem[i++] = Instruction(kLdx, 300, 0, Field(1, 3));
  state.mem[i++] = Instruction(kSpec, 0, 0, kNumField);
  state.mem[i++] = Instruction(kMul, 400);
  state.mem[i++] = Instruction(kStx, 201);
  state.mem[i++] = Instruction(kLda, 201);
  state.mem[i++] = Instruction(kSpec, 0, 0, kCharField);
  state.mem[i++] = Instruction(kStx, 201);
  state.mem[i++] = Instruction(kOut, 200, 0, kLinePrinterField);
  state.mem[i++] = Instruction(kSpec, 0, 0, kHltField);

  StoreText(state, 100, device_table.at(kLinePrinterField).block_size_words,
            "I(3)=");
  StoreText(state, 200, device_table.at(kLinePrinterField).block_size_words,
            "2*I=");
  // 300: read buffer
  i = 400;
  state.mem[i++] = Word(2);
  state.mem[i++] = Word(0);

  SimulateMix(state);
  assert(state.halt);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage:\n";
    std::cerr << "mix filename.mix\n";
    std::cerr << "mix test\n";
    std::cerr << "mix interactive-test\n";
    return 0;
  }
  std::string param(argv[1]);
  if (param == "test") {
    primitive_test();
    complex_test();
    return 0;
  }
  if (param == "interactive-test") {
    interactive_test();
    return 0;
  }

  State state;
  LoadGnuMixFile(state, param);
  SimulateMix(state);
}