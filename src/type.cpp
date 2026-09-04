// SPDX-License-Identifier: MIT
#include "fire/type.hpp"

namespace fire {

std::string Type::to_string() const
{
    switch (m_kind) {
    case TypeKind::Int:
        return "int";
    case TypeKind::Float:
        return "float";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::Str:
        return "str";
    case TypeKind::Void:
        return "void";
    case TypeKind::Array:
        return "[" + (m_element != nullptr ? m_element->to_string() : std::string { "?" }) + "]";
    case TypeKind::Error:
        return "<error>";
    }
    return "<unknown>";
}

TypeContext::TypeContext()
{
    m_int = intern(TypeKind::Int, nullptr);
    m_float = intern(TypeKind::Float, nullptr);
    m_bool = intern(TypeKind::Bool, nullptr);
    m_str = intern(TypeKind::Str, nullptr);
    m_void = intern(TypeKind::Void, nullptr);
    m_error = intern(TypeKind::Error, nullptr);
}

const Type* TypeContext::intern(TypeKind kind, const Type* element)
{
    m_owned.push_back(std::make_unique<Type>(kind, element));
    return m_owned.back().get();
}

const Type* TypeContext::array_of(const Type* element)
{
    if (element == nullptr) {
        return m_error;
    }
    if (const auto it = m_arrays.find(element); it != m_arrays.end()) {
        return it->second;
    }
    const Type* created = intern(TypeKind::Array, element);
    m_arrays.emplace(element, created);
    return created;
}

} // namespace fire
