//
// lager - library for functional interactive c++ programs
// Copyright (C) 2017 Juan Pedro Bolivar Puente
//
// This file is part of lager.
//
// lager is free software: you can redistribute it and/or modify
// it under the terms of the MIT License, as detailed in the LICENSE
// file located at the root of this source code distribution,
// or here: <https://github.com/arximboldi/lager/blob/master/LICENSE>
//

#include <catch.hpp>

#include <lager/debug/debugger.hpp>
#include <lager/event_loop/manual.hpp>
#include <lager/store.hpp>

#include "../example/counter/counter.hpp"
#include <optional>

struct dummy_debugger
{
    struct handle
    {
        template <typename Context>
        void set_context(Context)
        {}
        template <typename Model>
        void view(const Model& m)
        {}
    };

    template <typename Debugger>
    handle& enable(Debugger)
    {
        return handle_;
    }

    handle handle_;
};

TEST_CASE("basic")
{
    auto debugger = dummy_debugger{};
    auto viewed   = std::optional<counter::model>{std::nullopt};
    auto view     = [&](auto model) { viewed = model; };
    auto store =
        lager::make_store<counter::action>(counter::model{},
                                           counter::update,
                                           view,
                                           lager::with_manual_event_loop{},
                                           lager::with_debugger(debugger));

    store.dispatch(counter::increment_action{});

    CHECK(viewed);
    CHECK(viewed->value == 1);
}

TEST_CASE("effect as a result")
{
    auto debugger = dummy_debugger{};
    auto viewed   = std::optional<int>{std::nullopt};
    auto view     = [&](auto model) { viewed = model; };
    auto called   = 0;
    auto effect   = [&](lager::context<int> ctx) { ++called; };
    auto store =
        lager::make_store<int>(0,
                               [=](int model, int action) {
                                   return std::pair{model + action, effect};
                               },
                               view,
                               lager::with_manual_event_loop{},
                               lager::with_debugger(debugger));

    store.dispatch(2);
    CHECK(viewed);
    CHECK(*viewed == 2);
    CHECK(called == 1);
}

namespace services {

struct foo
{
    int x = 0;
};

} // namespace services

TEST_CASE("effect with dependencies")
{
    auto debugger = dummy_debugger{};
    auto called   = 0;
    auto f        = services::foo{};
    auto effect   = [&](auto ctx) {
        CHECK(lager::get<services::foo>(ctx).x == 42);
        ++called;
    };
    auto store =
        lager::make_store<int>(0,
                               [=](int model, int action) {
                                   return std::pair{model + action, effect};
                               },
                               [](auto) {},
                               lager::with_manual_event_loop{},
                               lager::with_deps(std::ref(f)),
                               // important: debugger must be last so it can
                               // forward the deps properly
                               lager::with_debugger(debugger));
    f.x = 42;
    store.dispatch(2);
    CHECK(called == 1);
}
