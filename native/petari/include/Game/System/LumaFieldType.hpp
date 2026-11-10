#pragma once

// The field rows' types, decided by the compiler from each field's declared type (generated/lumacore_fields.cpp), so a
// `long` or an enum gets the size it has on this platform.

#include "Game/System/LumaCore.hpp"
#include <type_traits>

class HitSensor;

namespace lumacore {
    template < typename F >
    constexpr u16 fieldTypeOf() {
        using T = std::remove_cv_t< std::remove_all_extents_t< F > >;
        if constexpr (std::is_same_v< T, bool >) {
            return FieldType_Bool;
        } else if constexpr (std::is_enum_v< T >) {
            return fieldTypeOf< std::underlying_type_t< T > >();
        } else if constexpr (std::is_integral_v< T > && std::is_signed_v< T > && sizeof(T) == 1) {
            return FieldType_S8;
        } else if constexpr (std::is_integral_v< T > && sizeof(T) == 1) {
            return FieldType_U8;
        } else if constexpr (std::is_integral_v< T > && std::is_signed_v< T > && sizeof(T) == 2) {
            return FieldType_S16;
        } else if constexpr (std::is_integral_v< T > && sizeof(T) == 2) {
            return FieldType_U16;
        } else if constexpr (std::is_integral_v< T > && std::is_signed_v< T > && sizeof(T) == 4) {
            return FieldType_S32;
        } else if constexpr (std::is_integral_v< T > && sizeof(T) == 4) {
            return FieldType_U32;
        } else if constexpr (std::is_same_v< T, float >) {
            return FieldType_F32;
        } else if constexpr (std::is_same_v< T, double >) {
            return FieldType_F64;
        } else if constexpr (std::is_class_v< T > && sizeof(T) == 12 && alignof(T) == 4) {
            return FieldType_Vec3;  // the generator lets only TVec3f, Vec3f and Vec through
        } else if constexpr (std::is_pointer_v< T > &&
                             std::is_same_v< std::remove_cv_t< std::remove_pointer_t< T > >, char >) {
            return FieldType_Text;
        } else if constexpr (std::is_pointer_v< T > &&
                             std::is_same_v< std::remove_cv_t< std::remove_pointer_t< T > >, HitSensor >) {
            return FieldType_Sensor;
        } else if constexpr (std::is_pointer_v< T > && std::is_class_v< std::remove_pointer_t< T > >) {
            return FieldType_Object;
        } else {
            return 0;  // 64-bit integers and anything else: no row reads it
        }
    }

    template < typename F >
    constexpr u32 fieldFlagsOf() {
        using T = std::remove_all_extents_t< F >;
        return std::is_const_v< T > || std::is_pointer_v< std::remove_cv_t< T > > ? FieldFlag_ReadOnly : 0u;
    }
}  // namespace lumacore

// One row: class, its id, a field. Arrays give their element count and stride.
#define LUMACORE_FIELD(T, id, field)                                                                                    \
    {static_cast< u16 >(id),                                                                                            \
     ::lumacore::fieldTypeOf< decltype(T::field) >(),                                                                   \
     static_cast< u32 >(offsetof(T, field)),                                                                            \
     static_cast< u32 >(sizeof(decltype(T::field)) / sizeof(std::remove_all_extents_t< decltype(T::field) >)),          \
     static_cast< u32 >(sizeof(std::remove_all_extents_t< decltype(T::field) >)),                                       \
     ::lumacore::fieldFlagsOf< decltype(T::field) >(),                                                                  \
     #field}
