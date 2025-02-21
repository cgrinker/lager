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

#pragma once

#include <lager/context.hpp>
#include <lager/util.hpp>

#include <immer/algorithm.hpp>
#include <immer/box.hpp>
#include <immer/vector.hpp>
#include <immer/vector_transient.hpp>

#include <lager/debug/cereal/immer_box.hpp>
#include <lager/debug/cereal/immer_vector.hpp>
#include <lager/debug/cereal/struct.hpp>
#include <lager/debug/cereal/variant_with_name.hpp>

#include <functional>
#include <variant>

namespace lager {

template <typename Action, typename Model, typename Deps>
struct tree_debugger
{
    using base_action = Action;
    using base_model  = Model;
    using deps_t      = Deps;

    struct pos_t
    {
        std::size_t branch;
        std::size_t step;
    };

    using cursor_t = immer::vector<pos_t>;

    struct goto_action
    {
        cursor_t cursor;
    };
    struct undo_action
    {};
    struct redo_action
    {};
    struct pause_action
    {};
    struct resume_action
    {};

    using action = std::variant<Action,
                                goto_action,
                                undo_action,
                                redo_action,
                                pause_action,
                                resume_action>;

    struct step;
    using history = immer::vector<immer::box<step>>;

    struct step
    {
        Action action;
        Model model;
        immer::vector<history> branches;
    };

    struct summary_step_t;

    using summary_history_t = immer::vector<immer::box<summary_step_t>>;
    using summary_t         = immer::vector<summary_history_t>;

    struct summary_step_t
    {
        std::size_t steps;
        summary_t branches;
    };

    struct model
    {
        cursor_t cursor = {};
        bool paused     = {};
        Model init;
        immer::vector<history> branches = {};
        immer::vector<Action> pending   = {};

        model() = default;
        model(Model i)
            : init{i}
        {}

        using lookup_result = std::pair<std::optional<Action>, const Model&>;

        lookup_result do_lookup(const immer::vector<history>& branches,
                                const cursor_t& cursor,
                                std::size_t cursor_index) const
        {
            auto pos = cursor[cursor_index];
            if (pos.branch >= branches.size())
                throw std::runtime_error{"bad cursor"};
            auto& history = branches[pos.branch];
            if (pos.step >= history.size())
                throw std::runtime_error{"bad cursor"};
            auto& node      = history[pos.step];
            auto next_index = cursor_index + 1;
            return next_index == cursor.size()
                       ? lookup_result{node->action, node->model}
                       : do_lookup(node->branches, cursor, next_index);
        }

        lookup_result lookup(const cursor_t& cursor) const
        {
            return cursor.empty() ? lookup_result{{}, init}
                                  : do_lookup(branches, cursor, 0);
        }

        std::pair<immer::vector<history>, cursor_t>
        do_append(const immer::vector<history>& branches,
                  const cursor_t& cursor,
                  std::size_t cursor_index,
                  const Action& act,
                  const Model& m)
        {
            using namespace std;
            auto pos          = cursor[cursor_index];
            auto next_index   = cursor_index + 1;
            auto new_cursor   = cursor_t{};
            auto new_branches = branches.update(pos.branch, [&](auto history) {
                if (next_index < cursor.size()) {
                    return history.update(pos.step, [&](auto node_) {
                        return node_.update([&](auto node) {
                            tie(node.branches, new_cursor) = do_append(
                                node.branches, cursor, next_index, act, m);
                            return node;
                        });
                    });
                } else {
                    if (pos.step + 1 == history.size()) {
                        new_cursor = cursor.set(cursor_index,
                                                {pos.branch, pos.step + 1});
                        return history.push_back(step{act, m, {}});
                    } else {
                        new_cursor = cursor.push_back({0, 0});
                        return history.update(pos.step, [&](auto node_) {
                            auto node = *node_;
                            node.branches =
                                node.branches.push_back({step{act, m, {}}});
                            return node;
                        });
                    }
                }
            });
            return {new_branches, new_cursor};
        }

        void append(const Action& act, const Model& m)
        {
            using namespace std;
            if (cursor.empty()) {
                branches = branches.push_back({step{act, m, {}}});
                cursor   = {{branches.size() - 1, 0}};
            } else {
                tie(branches, cursor) = do_append(branches, cursor, 0, act, m);
            }
        }

        bool check(const cursor_t& cursor) const
        {
            try {
                lookup(cursor);
                return true;
            } catch (const std::runtime_error&) {
                return false;
            }
        }

        summary_t do_summary(const immer::vector<history>& branches) const
        {
            auto result = summary_t{}.transient();
            immer::for_each(branches, [&](auto&& history) {
                auto steps   = std::size_t{};
                auto current = summary_history_t{}.transient();
                immer::for_each(history, [&](auto&& step) {
                    if (step->branches.empty())
                        ++steps;
                    else {
                        current.push_back({steps, do_summary(step->branches)});
                        steps = 0;
                    }
                });
                current.push_back(summary_step_t{steps, {}});
                result.push_back(std::move(current).persistent());
            });
            return std::move(result).persistent();
        }

        summary_t summary() const { return do_summary(branches); }

        operator const Model&() const { return lookup(cursor).second; }
    };

    using result_t = std::pair<model, effect<action, deps_t>>;

    template <typename ReducerFn>
    static result_t update(ReducerFn&& reducer, model m, action act)
    {
        return std::visit(
            visitor{
                [&](Action act) -> result_t {
                    if (m.paused) {
                        m.pending = m.pending.push_back(act);
                        return {m, noop};
                    } else {
                        auto eff   = effect<action, deps_t>{noop};
                        auto state = static_cast<base_model>(m);
                        invoke_reducer<deps_t>(
                            reducer, state, act, [&](auto&& e) {
                                eff = LAGER_FWD(e);
                            });
                        m.append(act, state);
                        return {m, eff};
                    }
                },
                [&](goto_action act) -> result_t {
                    if (m.check(act.cursor))
                        m.cursor = act.cursor;
                    return {m, noop};
                },
                [&](undo_action) -> result_t {
                    if (!m.cursor.empty()) {
                        auto index = m.cursor.size() - 1;
                        auto pos   = m.cursor.back();
                        m.cursor   = pos.step > 0
                                       ? m.cursor.set(
                                             index, {pos.branch, pos.step - 1})
                                       : m.cursor.take(index);
                    }
                    return {m, noop};
                },
                [&](redo_action) -> result_t {
                    throw std::runtime_error{"todo"};
                    return {m, noop};
                },
                [&](pause_action) -> result_t {
                    m.paused = true;
                    return {m, [](auto&& ctx) { ctx.pause(); }};
                },
                [&](resume_action) -> result_t {
                    auto resume_eff =
                        effect<action>{[](auto&& ctx) { ctx.resume(); }};
                    auto eff         = effect<action>{noop};
                    auto pending     = m.pending;
                    m.paused         = false;
                    m.pending        = {};
                    std::tie(m, eff) = immer::accumulate(
                        pending,
                        std::pair{m, eff},
                        [&](result_t acc, auto&& act) -> result_t {
                            auto [m, eff] = LAGER_FWD(acc);
                            auto [new_m, new_eff] =
                                update(reducer, std::move(m), LAGER_FWD(act));
                            return {new_m, sequence(eff, new_eff)};
                        });
                    return {m, sequence(resume_eff, eff)};
                },
            },
            act);
    }

    template <typename Server, typename ViewFn>
    static void view(Server& serv, ViewFn&& view, const model& m)
    {
        serv.view(m);
        std::forward<ViewFn>(view)(m);
    }

    LAGER_CEREAL_NESTED_STRUCT(undo_action);
    LAGER_CEREAL_NESTED_STRUCT(redo_action);
    LAGER_CEREAL_NESTED_STRUCT(pause_action);
    LAGER_CEREAL_NESTED_STRUCT(resume_action);
    LAGER_CEREAL_NESTED_STRUCT(goto_action, (cursor));
    LAGER_CEREAL_NESTED_STRUCT(pos_t, (branch)(step));
    LAGER_CEREAL_NESTED_STRUCT(model, (cursor)(paused)(init)(branches));
    LAGER_CEREAL_NESTED_STRUCT(step, (action)(model)(branches));
    LAGER_CEREAL_NESTED_STRUCT(summary_step_t, (steps)(branches));
};

} // namespace lager
