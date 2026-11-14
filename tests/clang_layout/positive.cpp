// SPDX-License-Identifier: MIT
// Original expectations checked against AArch64 object sizes and field offsets.
#if __ATLANTIC_XNU_LAYOUT__ != 2 || __ATLANTIC_XNU_CPP_LAYOUT__ != 1
#error Both independent AtlanticOS layout API versions are required
#endif
#define SIG(T) __atlantic_cpp_layout_signature(T)
#define SUM(T) __atlantic_cpp_layout_summary(T)
#define COMPAT(A, B) __atlantic_cpp_layout_compatible(A, B)
#define SIGNATURE(T, S) static_assert(__builtin_strcmp(SIG(T), S) == 0, #T " signature")
#define SUMMARY(T, S) static_assert(SUM(T) == (S), #T " summary")
using u64 = __UINT64_TYPE__;
static_assert(sizeof(void *) == 8 && sizeof(u64) == 8, "target LP64");
SIGNATURE(u64, "2");
SIGNATURE(__int128, "22");
SIGNATURE(double, "2");
SIGNATURE(void *, "1");
SIGNATURE(void (*)(void), "1");
SIGNATURE(decltype(nullptr), "1");
using block_t = void (^)(void);
SIGNATURE(block_t, "1");
SIGNATURE(_Atomic(void *), "1");
SIGNATURE(_Atomic(u64), "2");

struct PointerData { void *pointer; u64 data; };
struct SameShape { int *pointer; double data; };
struct Reversed { u64 data; void *pointer; };
static_assert(__is_trivial(PointerData) && __is_standard_layout(PointerData));
static_assert(sizeof(PointerData) == 16 && __builtin_offsetof(PointerData, data) == 8);
SIGNATURE(PointerData, "12");
SUMMARY(PointerData, 6);
static_assert(COMPAT(const PointerData, SameShape));
static_assert(!COMPAT(PointerData, Reversed));
static_assert(COMPAT(void, const void) && !COMPAT(void, u64));
static_assert(__is_same(decltype(SIG(PointerData)), const char (&)[3]), "C++ const literal type");

struct Empty {};
struct alignas(16) AlignedEmpty {};
static_assert(sizeof(Empty) == 1 && sizeof(AlignedEmpty) == 16);
SIGNATURE(Empty, "0");
SUMMARY(Empty, 1);
SIGNATURE(AlignedEmpty, "00");
SUMMARY(AlignedEmpty, 1);
struct NestedEmpty { Empty empty; void *pointer; };
static_assert(__builtin_offsetof(NestedEmpty, pointer) == 8 && sizeof(NestedEmpty) == 16);
SIGNATURE(NestedEmpty, "01");

struct Packed { unsigned char data; void *pointer; } __attribute__((packed));
static_assert(sizeof(Packed) == 9 && __builtin_offsetof(Packed, pointer) == 1);
SIGNATURE(Packed, "31");
SUMMARY(Packed, 10);
using PackedArray = Packed[2];
SIGNATURE(PackedArray, "331");
using PointerArray = void *[3];
SIGNATURE(PointerArray, "111");
struct Padded { alignas(16) void *pointer; };
static_assert(sizeof(Padded) == 16);
SIGNATURE(Padded, "10");
SUMMARY(Padded, 3);
union Mixed { void *pointer; u64 data; };
SIGNATURE(Mixed, "3");
SUMMARY(Mixed, 8);
struct Anonymous { union { void *pointer; u64 data; }; unsigned char tail; };
SIGNATURE(Anonymous, "32");
struct Bits { unsigned data : 1; unsigned : 0; void *pointer; };
static_assert(__builtin_offsetof(Bits, pointer) == 8);
SIGNATURE(Bits, "21");
using Double3 = double __attribute__((ext_vector_type(3)));
static_assert(sizeof(Double3) == 32);
SIGNATURE(Double3, "2220");
enum class Tag : u64 { value };
SIGNATURE(Tag, "2");
struct StaticMembers { static void *ignored; void method(); u64 data; };
SIGNATURE(StaticMembers, "2");

#define PTR_USE __attribute__((annotate("atlantic.layout.pointer")))
#define DATA_USE __attribute__((annotate("atlantic.layout.data")))
#define DUAL_USE __attribute__((annotate("atlantic.layout.dual")))
typedef u64 PointerUsage PTR_USE;
typedef PointerUsage PointerAlias;
typedef PointerUsage DataOverride DATA_USE;
typedef u64 DualUsage DUAL_USE;
using ConstAlias = const PointerAlias;
SIGNATURE(ConstAlias, "1");
SIGNATURE(DataOverride, "2");
SIGNATURE(DualUsage, "3");
static_assert(!COMPAT(PointerUsage, u64) && !COMPAT(u64, PointerUsage));
static_assert(COMPAT(PointerUsage, void *));
struct UsageFields {
  PointerUsage address DATA_USE;
  void *opaque DATA_USE;
  u64 encoded PTR_USE;
  PointerUsage array[2] DATA_USE;
};
SIGNATURE(UsageFields, "22122");
struct UsageBits { unsigned address : 16 PTR_USE; unsigned flags : 16; };
SIGNATURE(UsageBits, "3");
SUMMARY(UsageBits, 8);

// The type operand parser accepts qualified and nondependent template types.
namespace fixture { struct Record { void *pointer; u64 data; }; }
template <class T> struct Array { T items[2]; };
SIGNATURE(fixture::Record, "12");
SIGNATURE(Array<PointerData>, "1212");
extern "C" {
const char *atlantic_cpp_fixture_signature = SIG(PointerData);
unsigned atlantic_cpp_fixture_summary = SUM(Mixed);
}
