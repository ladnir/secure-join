#pragma once
#include "secure-join/Defines.h"
#include "macoro/variant.h"

namespace secJoin
{

    template <typename... Fs>
    struct match : Fs... {
        using Fs::operator()...;
        // constexpr match(Fs &&... fs) : Fs{fs}... {}
    };
    template<class... Ts> match(Ts...)->match<Ts...>;

    template<typename V>
    struct isVariant : std::false_type {};
    template<typename... Ts>
    struct isVariant<macoro::variant<Ts...>> : std::true_type {};

    template <typename Var, typename... Fs, typename G = isVariant<Var>>
    constexpr decltype(auto) operator| (Var&& v, match<Fs...> const& match) {
        return std::visit(match, v);
    }

}