// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: test.proto

#ifndef GOOGLE_PROTOBUF_INCLUDED_test_2eproto
#define GOOGLE_PROTOBUF_INCLUDED_test_2eproto

#include <limits>
#include <string>

#include <google/protobuf/port_def.inc>
#if PROTOBUF_VERSION < 3013000
#error This file was generated by a newer version of protoc which is
#error incompatible with your Protocol Buffer headers. Please update
#error your headers.
#endif
#if 3013000 < PROTOBUF_MIN_PROTOC_VERSION
#error This file was generated by an older version of protoc which is
#error incompatible with your Protocol Buffer headers. Please
#error regenerate this file with a newer version of protoc.
#endif

#include <google/protobuf/port_undef.inc>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/arena.h>
#include <google/protobuf/arenastring.h>
#include <google/protobuf/generated_message_table_driven.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/inlined_string_field.h>
#include <google/protobuf/metadata_lite.h>
#include <google/protobuf/generated_message_reflection.h>
#include <google/protobuf/message.h>
#include <google/protobuf/repeated_field.h>  // IWYU pragma: export
#include <google/protobuf/extension_set.h>  // IWYU pragma: export
#include <google/protobuf/unknown_field_set.h>
// @@protoc_insertion_point(includes)
#include <google/protobuf/port_def.inc>
#define PROTOBUF_INTERNAL_EXPORT_test_2eproto
PROTOBUF_NAMESPACE_OPEN
namespace internal {
class AnyMetadata;
}  // namespace internal
PROTOBUF_NAMESPACE_CLOSE

// Internal implementation detail -- do not use these members.
struct TableStruct_test_2eproto {
  static const ::PROTOBUF_NAMESPACE_ID::internal::ParseTableField entries[]
    PROTOBUF_SECTION_VARIABLE(protodesc_cold);
  static const ::PROTOBUF_NAMESPACE_ID::internal::AuxiliaryParseTableField aux[]
    PROTOBUF_SECTION_VARIABLE(protodesc_cold);
  static const ::PROTOBUF_NAMESPACE_ID::internal::ParseTable schema[2]
    PROTOBUF_SECTION_VARIABLE(protodesc_cold);
  static const ::PROTOBUF_NAMESPACE_ID::internal::FieldMetadata field_metadata[];
  static const ::PROTOBUF_NAMESPACE_ID::internal::SerializationTable serialization_table[];
  static const ::PROTOBUF_NAMESPACE_ID::uint32 offsets[];
};
extern const ::PROTOBUF_NAMESPACE_ID::internal::DescriptorTable descriptor_table_test_2eproto;
namespace test {
class SumArgs;
class SumArgsDefaultTypeInternal;
extern SumArgsDefaultTypeInternal _SumArgs_default_instance_;
class SumResult;
class SumResultDefaultTypeInternal;
extern SumResultDefaultTypeInternal _SumResult_default_instance_;
}  // namespace test
PROTOBUF_NAMESPACE_OPEN
template<> ::test::SumArgs* Arena::CreateMaybeMessage<::test::SumArgs>(Arena*);
template<> ::test::SumResult* Arena::CreateMaybeMessage<::test::SumResult>(Arena*);
PROTOBUF_NAMESPACE_CLOSE
namespace test {

// ===================================================================

class SumArgs PROTOBUF_FINAL :
    public ::PROTOBUF_NAMESPACE_ID::Message /* @@protoc_insertion_point(class_definition:test.SumArgs) */ {
 public:
  inline SumArgs() : SumArgs(nullptr) {}
  virtual ~SumArgs();

  SumArgs(const SumArgs& from);
  SumArgs(SumArgs&& from) noexcept
    : SumArgs() {
    *this = ::std::move(from);
  }

  inline SumArgs& operator=(const SumArgs& from) {
    CopyFrom(from);
    return *this;
  }
  inline SumArgs& operator=(SumArgs&& from) noexcept {
    if (GetArena() == from.GetArena()) {
      if (this != &from) InternalSwap(&from);
    } else {
      CopyFrom(from);
    }
    return *this;
  }

  static const ::PROTOBUF_NAMESPACE_ID::Descriptor* descriptor() {
    return GetDescriptor();
  }
  static const ::PROTOBUF_NAMESPACE_ID::Descriptor* GetDescriptor() {
    return GetMetadataStatic().descriptor;
  }
  static const ::PROTOBUF_NAMESPACE_ID::Reflection* GetReflection() {
    return GetMetadataStatic().reflection;
  }
  static const SumArgs& default_instance();

  static void InitAsDefaultInstance();  // FOR INTERNAL USE ONLY
  static inline const SumArgs* internal_default_instance() {
    return reinterpret_cast<const SumArgs*>(
               &_SumArgs_default_instance_);
  }
  static constexpr int kIndexInFileMessages =
    0;

  friend void swap(SumArgs& a, SumArgs& b) {
    a.Swap(&b);
  }
  inline void Swap(SumArgs* other) {
    if (other == this) return;
    if (GetArena() == other->GetArena()) {
      InternalSwap(other);
    } else {
      ::PROTOBUF_NAMESPACE_ID::internal::GenericSwap(this, other);
    }
  }
  void UnsafeArenaSwap(SumArgs* other) {
    if (other == this) return;
    GOOGLE_DCHECK(GetArena() == other->GetArena());
    InternalSwap(other);
  }

  // implements Message ----------------------------------------------

  inline SumArgs* New() const final {
    return CreateMaybeMessage<SumArgs>(nullptr);
  }

  SumArgs* New(::PROTOBUF_NAMESPACE_ID::Arena* arena) const final {
    return CreateMaybeMessage<SumArgs>(arena);
  }
  void CopyFrom(const ::PROTOBUF_NAMESPACE_ID::Message& from) final;
  void MergeFrom(const ::PROTOBUF_NAMESPACE_ID::Message& from) final;
  void CopyFrom(const SumArgs& from);
  void MergeFrom(const SumArgs& from);
  PROTOBUF_ATTRIBUTE_REINITIALIZES void Clear() final;
  bool IsInitialized() const final;

  size_t ByteSizeLong() const final;
  const char* _InternalParse(const char* ptr, ::PROTOBUF_NAMESPACE_ID::internal::ParseContext* ctx) final;
  ::PROTOBUF_NAMESPACE_ID::uint8* _InternalSerialize(
      ::PROTOBUF_NAMESPACE_ID::uint8* target, ::PROTOBUF_NAMESPACE_ID::io::EpsCopyOutputStream* stream) const final;
  int GetCachedSize() const final { return _cached_size_.Get(); }

  private:
  inline void SharedCtor();
  inline void SharedDtor();
  void SetCachedSize(int size) const final;
  void InternalSwap(SumArgs* other);
  friend class ::PROTOBUF_NAMESPACE_ID::internal::AnyMetadata;
  static ::PROTOBUF_NAMESPACE_ID::StringPiece FullMessageName() {
    return "test.SumArgs";
  }
  protected:
  explicit SumArgs(::PROTOBUF_NAMESPACE_ID::Arena* arena);
  private:
  static void ArenaDtor(void* object);
  inline void RegisterArenaDtor(::PROTOBUF_NAMESPACE_ID::Arena* arena);
  public:

  ::PROTOBUF_NAMESPACE_ID::Metadata GetMetadata() const final;
  private:
  static ::PROTOBUF_NAMESPACE_ID::Metadata GetMetadataStatic() {
    ::PROTOBUF_NAMESPACE_ID::internal::AssignDescriptors(&::descriptor_table_test_2eproto);
    return ::descriptor_table_test_2eproto.file_level_metadata[kIndexInFileMessages];
  }

  public:

  // nested types ----------------------------------------------------

  // accessors -------------------------------------------------------

  enum : int {
    kOp1FieldNumber = 1,
    kOp2FieldNumber = 2,
  };
  // int32 op1 = 1;
  void clear_op1();
  ::PROTOBUF_NAMESPACE_ID::int32 op1() const;
  void set_op1(::PROTOBUF_NAMESPACE_ID::int32 value);
  private:
  ::PROTOBUF_NAMESPACE_ID::int32 _internal_op1() const;
  void _internal_set_op1(::PROTOBUF_NAMESPACE_ID::int32 value);
  public:

  // int32 op2 = 2;
  void clear_op2();
  ::PROTOBUF_NAMESPACE_ID::int32 op2() const;
  void set_op2(::PROTOBUF_NAMESPACE_ID::int32 value);
  private:
  ::PROTOBUF_NAMESPACE_ID::int32 _internal_op2() const;
  void _internal_set_op2(::PROTOBUF_NAMESPACE_ID::int32 value);
  public:

  // @@protoc_insertion_point(class_scope:test.SumArgs)
 private:
  class _Internal;

  template <typename T> friend class ::PROTOBUF_NAMESPACE_ID::Arena::InternalHelper;
  typedef void InternalArenaConstructable_;
  typedef void DestructorSkippable_;
  ::PROTOBUF_NAMESPACE_ID::int32 op1_;
  ::PROTOBUF_NAMESPACE_ID::int32 op2_;
  mutable ::PROTOBUF_NAMESPACE_ID::internal::CachedSize _cached_size_;
  friend struct ::TableStruct_test_2eproto;
};
// -------------------------------------------------------------------

class SumResult PROTOBUF_FINAL :
    public ::PROTOBUF_NAMESPACE_ID::Message /* @@protoc_insertion_point(class_definition:test.SumResult) */ {
 public:
  inline SumResult() : SumResult(nullptr) {}
  virtual ~SumResult();

  SumResult(const SumResult& from);
  SumResult(SumResult&& from) noexcept
    : SumResult() {
    *this = ::std::move(from);
  }

  inline SumResult& operator=(const SumResult& from) {
    CopyFrom(from);
    return *this;
  }
  inline SumResult& operator=(SumResult&& from) noexcept {
    if (GetArena() == from.GetArena()) {
      if (this != &from) InternalSwap(&from);
    } else {
      CopyFrom(from);
    }
    return *this;
  }

  static const ::PROTOBUF_NAMESPACE_ID::Descriptor* descriptor() {
    return GetDescriptor();
  }
  static const ::PROTOBUF_NAMESPACE_ID::Descriptor* GetDescriptor() {
    return GetMetadataStatic().descriptor;
  }
  static const ::PROTOBUF_NAMESPACE_ID::Reflection* GetReflection() {
    return GetMetadataStatic().reflection;
  }
  static const SumResult& default_instance();

  static void InitAsDefaultInstance();  // FOR INTERNAL USE ONLY
  static inline const SumResult* internal_default_instance() {
    return reinterpret_cast<const SumResult*>(
               &_SumResult_default_instance_);
  }
  static constexpr int kIndexInFileMessages =
    1;

  friend void swap(SumResult& a, SumResult& b) {
    a.Swap(&b);
  }
  inline void Swap(SumResult* other) {
    if (other == this) return;
    if (GetArena() == other->GetArena()) {
      InternalSwap(other);
    } else {
      ::PROTOBUF_NAMESPACE_ID::internal::GenericSwap(this, other);
    }
  }
  void UnsafeArenaSwap(SumResult* other) {
    if (other == this) return;
    GOOGLE_DCHECK(GetArena() == other->GetArena());
    InternalSwap(other);
  }

  // implements Message ----------------------------------------------

  inline SumResult* New() const final {
    return CreateMaybeMessage<SumResult>(nullptr);
  }

  SumResult* New(::PROTOBUF_NAMESPACE_ID::Arena* arena) const final {
    return CreateMaybeMessage<SumResult>(arena);
  }
  void CopyFrom(const ::PROTOBUF_NAMESPACE_ID::Message& from) final;
  void MergeFrom(const ::PROTOBUF_NAMESPACE_ID::Message& from) final;
  void CopyFrom(const SumResult& from);
  void MergeFrom(const SumResult& from);
  PROTOBUF_ATTRIBUTE_REINITIALIZES void Clear() final;
  bool IsInitialized() const final;

  size_t ByteSizeLong() const final;
  const char* _InternalParse(const char* ptr, ::PROTOBUF_NAMESPACE_ID::internal::ParseContext* ctx) final;
  ::PROTOBUF_NAMESPACE_ID::uint8* _InternalSerialize(
      ::PROTOBUF_NAMESPACE_ID::uint8* target, ::PROTOBUF_NAMESPACE_ID::io::EpsCopyOutputStream* stream) const final;
  int GetCachedSize() const final { return _cached_size_.Get(); }

  private:
  inline void SharedCtor();
  inline void SharedDtor();
  void SetCachedSize(int size) const final;
  void InternalSwap(SumResult* other);
  friend class ::PROTOBUF_NAMESPACE_ID::internal::AnyMetadata;
  static ::PROTOBUF_NAMESPACE_ID::StringPiece FullMessageName() {
    return "test.SumResult";
  }
  protected:
  explicit SumResult(::PROTOBUF_NAMESPACE_ID::Arena* arena);
  private:
  static void ArenaDtor(void* object);
  inline void RegisterArenaDtor(::PROTOBUF_NAMESPACE_ID::Arena* arena);
  public:

  ::PROTOBUF_NAMESPACE_ID::Metadata GetMetadata() const final;
  private:
  static ::PROTOBUF_NAMESPACE_ID::Metadata GetMetadataStatic() {
    ::PROTOBUF_NAMESPACE_ID::internal::AssignDescriptors(&::descriptor_table_test_2eproto);
    return ::descriptor_table_test_2eproto.file_level_metadata[kIndexInFileMessages];
  }

  public:

  // nested types ----------------------------------------------------

  // accessors -------------------------------------------------------

  enum : int {
    kSumFieldNumber = 1,
  };
  // int32 sum = 1;
  void clear_sum();
  ::PROTOBUF_NAMESPACE_ID::int32 sum() const;
  void set_sum(::PROTOBUF_NAMESPACE_ID::int32 value);
  private:
  ::PROTOBUF_NAMESPACE_ID::int32 _internal_sum() const;
  void _internal_set_sum(::PROTOBUF_NAMESPACE_ID::int32 value);
  public:

  // @@protoc_insertion_point(class_scope:test.SumResult)
 private:
  class _Internal;

  template <typename T> friend class ::PROTOBUF_NAMESPACE_ID::Arena::InternalHelper;
  typedef void InternalArenaConstructable_;
  typedef void DestructorSkippable_;
  ::PROTOBUF_NAMESPACE_ID::int32 sum_;
  mutable ::PROTOBUF_NAMESPACE_ID::internal::CachedSize _cached_size_;
  friend struct ::TableStruct_test_2eproto;
};
// ===================================================================


// ===================================================================

#ifdef __GNUC__
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif  // __GNUC__
// SumArgs

// int32 op1 = 1;
inline void SumArgs::clear_op1() {
  op1_ = 0;
}
inline ::PROTOBUF_NAMESPACE_ID::int32 SumArgs::_internal_op1() const {
  return op1_;
}
inline ::PROTOBUF_NAMESPACE_ID::int32 SumArgs::op1() const {
  // @@protoc_insertion_point(field_get:test.SumArgs.op1)
  return _internal_op1();
}
inline void SumArgs::_internal_set_op1(::PROTOBUF_NAMESPACE_ID::int32 value) {
  
  op1_ = value;
}
inline void SumArgs::set_op1(::PROTOBUF_NAMESPACE_ID::int32 value) {
  _internal_set_op1(value);
  // @@protoc_insertion_point(field_set:test.SumArgs.op1)
}

// int32 op2 = 2;
inline void SumArgs::clear_op2() {
  op2_ = 0;
}
inline ::PROTOBUF_NAMESPACE_ID::int32 SumArgs::_internal_op2() const {
  return op2_;
}
inline ::PROTOBUF_NAMESPACE_ID::int32 SumArgs::op2() const {
  // @@protoc_insertion_point(field_get:test.SumArgs.op2)
  return _internal_op2();
}
inline void SumArgs::_internal_set_op2(::PROTOBUF_NAMESPACE_ID::int32 value) {
  
  op2_ = value;
}
inline void SumArgs::set_op2(::PROTOBUF_NAMESPACE_ID::int32 value) {
  _internal_set_op2(value);
  // @@protoc_insertion_point(field_set:test.SumArgs.op2)
}

// -------------------------------------------------------------------

// SumResult

// int32 sum = 1;
inline void SumResult::clear_sum() {
  sum_ = 0;
}
inline ::PROTOBUF_NAMESPACE_ID::int32 SumResult::_internal_sum() const {
  return sum_;
}
inline ::PROTOBUF_NAMESPACE_ID::int32 SumResult::sum() const {
  // @@protoc_insertion_point(field_get:test.SumResult.sum)
  return _internal_sum();
}
inline void SumResult::_internal_set_sum(::PROTOBUF_NAMESPACE_ID::int32 value) {
  
  sum_ = value;
}
inline void SumResult::set_sum(::PROTOBUF_NAMESPACE_ID::int32 value) {
  _internal_set_sum(value);
  // @@protoc_insertion_point(field_set:test.SumResult.sum)
}

#ifdef __GNUC__
  #pragma GCC diagnostic pop
#endif  // __GNUC__
// -------------------------------------------------------------------


// @@protoc_insertion_point(namespace_scope)

}  // namespace test

// @@protoc_insertion_point(global_scope)

#include <google/protobuf/port_undef.inc>
#endif  // GOOGLE_PROTOBUF_INCLUDED_GOOGLE_PROTOBUF_INCLUDED_test_2eproto