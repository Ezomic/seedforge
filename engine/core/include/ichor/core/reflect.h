#pragma once

#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ichor {

// Minimal field reflection: a type lists its members once and every generic
// visitor (serialisation, hashing, the editor later) walks that list.
//
//   template <> struct Reflect<Transform> {
//       static constexpr auto fields() { return std::make_tuple(
//           field("position", &Transform::position), field("rotation", &Transform::rotation)); }
//   };

template <class T, class M>
struct Field {
    std::string_view name;
    M T::*member;
};

template <class T, class M>
constexpr Field<T, M> field(std::string_view name, M T::*member)
{
    return {name, member};
}

template <class T>
struct Reflect;

template <class T, class = void>
struct IsReflected : std::false_type {};

template <class T>
struct IsReflected<T, std::void_t<decltype(Reflect<T>::fields())>> : std::true_type {};

template <class T>
constexpr bool isReflected = IsReflected<T>::value;

template <class T, class Fn>
void forEachField(T& object, Fn&& fn)
{
    std::apply([&](const auto&... f) { (fn(f.name, object.*(f.member)), ...); }, Reflect<std::remove_const_t<T>>::fields());
}

}
