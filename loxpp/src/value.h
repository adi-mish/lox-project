#pragma once

#include <memory>
#include <string>
#include <variant>

namespace loxpp {

class LoxCallable;
class LoxInstance;

using Value = std::variant<std::monostate,
                           bool,
                           double,
                           std::string,
                           std::shared_ptr<LoxCallable>,
                           std::shared_ptr<LoxInstance>>;

std::string astLiteralToString(const Value& value);

}  // namespace loxpp
