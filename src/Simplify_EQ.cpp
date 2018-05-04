#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const EQ *op, ConstBounds *bounds) {
    if (!may_simplify(op->a.type())) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return EQ::make(a, b);
        }
    }

    if (op->a.type().is_bool()) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);
        auto rewrite = IRMatcher::rewriter(IRMatcher::eq(a, b));
        if (rewrite(x == 1, x)) {
            return rewrite.result;
        } else if (rewrite(x == 0, !x)) {
            return mutate(std::move(rewrite.result), bounds);
        } else if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return EQ::make(a, b);
        }
    }

    ConstBounds delta_bounds;
    Expr delta = mutate(op->a - op->b, &delta_bounds);
    const int lanes = op->type.lanes();

    // Attempt to disprove using bounds analysis
    if (delta_bounds.min_defined && delta_bounds.min > 0) {
        return const_false(lanes);
    }

    if (delta_bounds.max_defined && delta_bounds.max < 0) {
        return const_false(lanes);
    }

    // Attempt to disprove using modulus remainder analysis
    if (no_overflow_scalar_int(delta.type())) {
        ModulusRemainder mod_rem = modulus_remainder(delta, alignment_info);
        if (mod_rem.remainder) {
            return const_false();
        }
    }

    auto rewrite = IRMatcher::rewriter(IRMatcher::eq(delta, 0), delta.type());

    if (rewrite(c0 == 0, fold(c0 == 0)) ||
        rewrite(x + c0 == 0, x == fold(-c0)) ||
        rewrite(c0 - x == 0, x == c0)) {
        return rewrite.result;
    }

    if (rewrite(broadcast(x) == 0, broadcast(x == 0, lanes)) ||
        (no_overflow(delta.type()) && rewrite(x * y == 0, (x == 0) || (y == 0))) ||
        rewrite(select(x, 0, y) == 0, x || (y == 0)) ||
        rewrite(select(x, c0, y) == 0, !x && (y == 0), c0 != 0) ||
        rewrite(select(x, y, 0) == 0, !x || (y == 0)) ||
        rewrite(select(x, y, c0) == 0, x && (y == 0), c0 != 0)) {
        return mutate(std::move(rewrite.result), bounds);
    }

    if (const Sub *s = delta.as<Sub>()) {
        if (s->a.same_as(op->a) && s->b.same_as(op->b)) {
            return op;
        } else {
            return EQ::make(s->a, s->b);
        }
    }

    return delta == make_zero(op->a.type());
}

// ne redirects to not eq
Expr Simplify::visit(const NE *op, ConstBounds *bounds) {
    if (!may_simplify(op->a.type())) {
        Expr a = mutate(op->a, nullptr);
        Expr b = mutate(op->b, nullptr);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return NE::make(a, b);
        }
    }

    Expr mutated = mutate(Not::make(EQ::make(op->a, op->b)), bounds);
    if (const NE *ne = mutated.as<NE>()) {
        if (ne->a.same_as(op->a) && ne->b.same_as(op->b)) {
            return op;
        }
    }
    return mutated;
}

}
}
