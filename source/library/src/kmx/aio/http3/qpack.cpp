#include <kmx/aio/http3/qpack.hpp>

#include "varint.hpp"

#include <kmx/aio/http3/frame.hpp>

#include <kmx/aio/basic_types.hpp>

#include <array>
#include <string>

namespace kmx::aio::http3::qpack
{
    namespace detail
    {
        [[nodiscard]] std::error_code qpack_decode_error() noexcept
        {
            return make_error_code(::kmx::aio::http3::error_code::message_error);
        }

        using ::kmx::aio::http3::detail::decode_varint;
        using ::kmx::aio::http3::detail::encode_varint;
        using ::kmx::aio::http3::detail::varint_size;

        using static_field_entry = std::pair<std::string_view, std::string_view>;

        inline constexpr std::array<static_field_entry, 12u> static_table = {{
            {":authority", ""},
            {":path", "/"},
            {":method", "CONNECT"},
            {":method", "DELETE"},
            {":method", "GET"},
            {":method", "HEAD"},
            {":method", "OPTIONS"},
            {":method", "POST"},
            {":scheme", "http"},
            {":scheme", "https"},
            {":status", "200"},
            {":status", "404"},
        }};

        [[nodiscard]] std::optional<std::uint64_t> find_exact(std::string_view name, std::string_view value) noexcept
        {
            for (std::size_t index = 0u; index < static_table.size(); ++index)
                if (static_table[index].first == name && static_table[index].second == value)
                    return static_cast<std::uint64_t>(index);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::uint64_t> find_name(std::string_view name) noexcept
        {
            for (std::size_t index = 0u; index < static_table.size(); ++index)
                if (static_table[index].first == name)
                    return static_cast<std::uint64_t>(index);
            return std::nullopt;
        }
    } // namespace detail

    std::optional<std::uint64_t> literal_codec::static_name_index(std::string_view name) noexcept
    {
        return detail::find_name(name);
    }

    std::optional<std::uint64_t> literal_codec::static_field_index(std::string_view name, std::string_view value) noexcept
    {
        return detail::find_exact(name, value);
    }

    std::vector<std::uint8_t> literal_codec::encode(const header_list& headers) noexcept(false)
    {
        std::size_t estimated_capacity = 2u; // 2 bytes prefix
        for (const auto& [name, value]: headers)
        {
            if (const auto exact_index = detail::find_exact(name, value); exact_index.has_value())
            {
                estimated_capacity += 1u + detail::varint_size(*exact_index);
            }
            else if (const auto name_index = detail::find_name(name); name_index.has_value())
            {
                estimated_capacity += 1u + detail::varint_size(*name_index) + detail::varint_size(value.size()) + value.size();
            }
            else
            {
                estimated_capacity +=
                    1u + detail::varint_size(name.size()) + name.size() + detail::varint_size(value.size()) + value.size();
            }
        }

        std::vector<std::uint8_t> block;
        block.reserve(estimated_capacity);

        // Header block prefix: Required Insert Count = 0, Delta Base = 0.
        block.push_back(0u);
        block.push_back(0u);

        for (const auto& [name, value]: headers)
        {
            if (const auto exact_index = detail::find_exact(name, value); exact_index.has_value())
            {
                block.push_back(static_cast<std::uint8_t>(field_representation::indexed_field));
                detail::encode_varint(block, *exact_index);
                continue;
            }

            if (const auto name_index = detail::find_name(name); name_index.has_value())
            {
                block.push_back(static_cast<std::uint8_t>(field_representation::literal_with_name_ref));
                detail::encode_varint(block, *name_index);
                detail::encode_varint(block, value.size());
                block.insert(block.end(), value.begin(), value.end());
                continue;
            }

            block.push_back(static_cast<std::uint8_t>(field_representation::literal_with_name));
            detail::encode_varint(block, name.size());
            block.insert(block.end(), name.begin(), name.end());
            detail::encode_varint(block, value.size());
            block.insert(block.end(), value.begin(), value.end());
        }

        return block;
    }

    std::expected<header_list, std::error_code> literal_codec::decode(std::span<const std::uint8_t> payload) noexcept
    {
        if (payload.size() < 2u)
            return std::unexpected(detail::qpack_decode_error());

        std::size_t offset = 2u; // skip Required Insert Count + Delta Base placeholders
        header_list headers;
        headers.reserve(6u); // Pre-reserve space for common client/server header blocks
        while (offset < payload.size())
        {
            if (offset >= payload.size())
                return std::unexpected(detail::qpack_decode_error());

            const auto representation = static_cast<field_representation>(payload[offset++]);
            if (representation == field_representation::indexed_field)
            {
                auto index = detail::decode_varint(payload, offset);
                if (!index)
                    return std::unexpected(detail::qpack_decode_error());
                offset += index->second;
                if (index->first >= detail::static_table.size())
                    return std::unexpected(detail::qpack_decode_error());
                headers.emplace_back(std::string(detail::static_table[index->first].first),
                                     std::string(detail::static_table[index->first].second));
                continue;
            }

            if (representation == field_representation::literal_with_name_ref)
            {
                auto index = detail::decode_varint(payload, offset);
                if (!index)
                    return std::unexpected(detail::qpack_decode_error());
                offset += index->second;
                if (index->first >= detail::static_table.size())
                    return std::unexpected(detail::qpack_decode_error());

                auto value_len = detail::decode_varint(payload, offset);
                if (!value_len)
                    return std::unexpected(detail::qpack_decode_error());
                offset += value_len->second;
                if (offset + value_len->first > payload.size())
                    return std::unexpected(detail::qpack_decode_error());

                const std::string value(reinterpret_cast<const char*>(payload.data() + offset), static_cast<std::size_t>(value_len->first));
                offset += static_cast<std::size_t>(value_len->first);
                headers.emplace_back(std::string(detail::static_table[index->first].first), value);
                continue;
            }

            if (representation != field_representation::literal_with_name)
                return std::unexpected(detail::qpack_decode_error());

            auto name_len = detail::decode_varint(payload, offset);
            if (!name_len)
                return std::unexpected(detail::qpack_decode_error());
            offset += name_len->second;
            if (offset + name_len->first > payload.size())
                return std::unexpected(detail::qpack_decode_error());

            const std::string name(reinterpret_cast<const char*>(payload.data() + offset), static_cast<std::size_t>(name_len->first));
            offset += static_cast<std::size_t>(name_len->first);

            auto value_len = detail::decode_varint(payload, offset);
            if (!value_len)
                return std::unexpected(detail::qpack_decode_error());
            offset += value_len->second;
            if (offset + value_len->first > payload.size())
                return std::unexpected(detail::qpack_decode_error());

            const std::string value(reinterpret_cast<const char*>(payload.data() + offset), static_cast<std::size_t>(value_len->first));
            offset += static_cast<std::size_t>(value_len->first);

            headers.emplace_back(name, value);
        }

        return headers;
    }
} // namespace kmx::aio::http3::qpack