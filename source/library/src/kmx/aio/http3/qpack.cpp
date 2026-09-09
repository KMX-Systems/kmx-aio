#include <kmx/aio/http3/qpack.hpp>

#include "varint.hpp"

#include <kmx/aio/http3/frame.hpp>

#include <array>
#include <optional>
#include <string>
#include <utility>

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

        [[nodiscard]] std::optional<std::uint64_t> find_exact(const std::string_view name, std::string_view value) noexcept
        {
            for (std::size_t index = 0u; index < static_table.size(); ++index)
                if ((static_table[index].first == name) && (static_table[index].second == value))
                    return static_cast<std::uint64_t>(index);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::uint64_t> find_name(const std::string_view name) noexcept
        {
            for (std::size_t index = 0u; index < static_table.size(); ++index)
                if (static_table[index].first == name)
                    return static_cast<std::uint64_t>(index);
            return std::nullopt;
        }
    } // namespace detail

    std::optional<std::uint64_t> literal_codec::static_name_index(const std::string_view name) noexcept
    {
        return detail::find_name(name);
    }

    std::optional<std::uint64_t> literal_codec::static_field_index(const std::string_view name, const std::string_view value) noexcept
    {
        return detail::find_exact(name, value);
    }

    /// @brief What the static table had to offer for one header field.
    /// @details Kept from the single lookup pass so the encoder below need not search the table again.
    struct lookup_result
    {
        /// @brief The entry matching both name and value, when there is one.
        std::optional<std::uint64_t> exact_index;
        /// @brief The entry matching the name alone; never filled in when @ref exact_index is.
        std::optional<std::uint64_t> name_index;
    };

    /// @brief Looks every header field up in the static table, once.
    /// @param headers The fields to encode.
    /// @return One result per field, in the same order.
    /// @details The name-only search is skipped whenever the exact entry was found, because the encoder
    ///          never consults it in that case - and a second walk of the static table per header is the
    ///          bulk of what encoding a small header block costs.
    [[nodiscard]] static std::vector<lookup_result> lookup_headers(const header_list& headers) noexcept(false)
    {
        std::vector<lookup_result> lookups;
        lookups.reserve(headers.size());
        for (const auto& [name, value]: headers)
        {
            auto exact_index = detail::find_exact(name, value);
            auto name_index = exact_index.has_value() ? std::optional<std::uint64_t> {} : detail::find_name(name);
            lookups.push_back({exact_index, name_index});
        }
        return lookups;
    }

    /// @brief Returns how many octets the encoded block will need.
    /// @param headers The fields to encode.
    /// @param lookups What @ref lookup_headers found for them.
    /// @return The size, so the block is allocated once rather than grown.
    [[nodiscard]] static std::size_t estimated_block_size(const header_list& headers,
                                                          const std::vector<lookup_result>& lookups) noexcept
    {
        std::size_t total = 2u; // the two prefix octets
        for (std::size_t i = 0u; i < headers.size(); ++i)
        {
            total += 1u; // the field representation octet
            const auto& [exact_index, name_index] = lookups[i];
            const auto& [name, value] = headers[i];

            if (exact_index.has_value())
            {
                total += detail::varint_size(*exact_index);
                continue;
            }

            const auto value_size = detail::varint_size(value.size()) + value.size();
            total += name_index.has_value() ? (detail::varint_size(*name_index) + value_size)
                                            : (detail::varint_size(name.size()) + name.size() + value_size);
        }
        return total;
    }

    /// @brief Writes one header field into the block.
    /// @param block The block to append to.
    /// @param field What the static table offered for this field.
    /// @param name The field's name.
    /// @param value The field's value.
    static void encode_field(std::vector<std::uint8_t>& block, const lookup_result& field, const std::string& name,
                             const std::string& value) noexcept(false)
    {
        if (field.exact_index.has_value())
        {
            block.push_back(static_cast<std::uint8_t>(field_representation::indexed_field));
            detail::encode_varint(block, *field.exact_index);
            return;
        }

        if (field.name_index.has_value())
            block.push_back(static_cast<std::uint8_t>(field_representation::literal_with_name_ref));
        else
            block.push_back(static_cast<std::uint8_t>(field_representation::literal_with_name));

        if (field.name_index.has_value())
            detail::encode_varint(block, *field.name_index);
        else
        {
            detail::encode_varint(block, name.size());
            block.insert(block.end(), name.begin(), name.end());
        }

        detail::encode_varint(block, value.size());
        block.insert(block.end(), value.begin(), value.end());
    }

    std::vector<std::uint8_t> literal_codec::encode(const header_list& headers) noexcept(false)
    {
        const auto lookups = lookup_headers(headers);

        std::vector<std::uint8_t> block;
        block.reserve(estimated_block_size(headers, lookups));

        // Header block prefix: Required Insert Count = 0, Delta Base = 0.
        block.push_back(0u);
        block.push_back(0u);

        for (std::size_t i = 0u; i < headers.size(); ++i)
            encode_field(block, lookups[i], headers[i].first, headers[i].second);

        return block;
    }

    /// @brief Reads a length-prefixed string at @p offset, advancing it past what was read.
    /// @param payload The block being decoded.
    /// @param offset Where to read from; advanced past the length and the octets alike.
    /// @return The string, or nothing when the length or the octets run past the block.
    [[nodiscard]] static std::optional<std::string> read_string(const cspan_uint8_t payload, std::size_t& offset) noexcept
    {
        const auto length = detail::decode_varint(payload, offset);
        if (!length)
            return {};
        offset += length->second;
        if ((offset + length->first) > payload.size())
            return {};

        std::string value(reinterpret_cast<const char*>(payload.data() + offset), static_cast<std::size_t>(length->first));
        offset += static_cast<std::size_t>(length->first);
        return value;
    }

    /// @brief Reads a static table index at @p offset, advancing it past what was read.
    /// @param payload The block being decoded.
    /// @param offset Where to read from; advanced past the index.
    /// @return The index, or nothing when it is absent or names no entry.
    [[nodiscard]] static std::optional<std::uint64_t> read_static_index(const cspan_uint8_t payload, std::size_t& offset) noexcept
    {
        const auto index = detail::decode_varint(payload, offset);
        if (!index)
            return {};
        offset += index->second;
        if (index->first >= detail::static_table.size())
            return {};
        return index->first;
    }

    /// @brief Reads one header field.
    /// @param payload The block being decoded.
    /// @param offset Where the field's body starts; advanced past it.
    /// @param representation The representation octet already read.
    /// @return The name and value, or nothing when the field cannot be read.
    [[nodiscard]] static std::optional<std::pair<std::string, std::string>> read_field(
        const cspan_uint8_t payload, std::size_t& offset, const field_representation representation) noexcept
    {
        if (representation == field_representation::indexed_field)
        {
            const auto index = read_static_index(payload, offset);
            if (!index.has_value())
                return {};
            return std::pair {std::string {detail::static_table[*index].first}, std::string {detail::static_table[*index].second}};
        }

        if (representation == field_representation::literal_with_name_ref)
        {
            const auto index = read_static_index(payload, offset);
            if (!index.has_value())
                return {};
            auto value = read_string(payload, offset);
            if (!value.has_value())
                return {};
            return std::pair {std::string {detail::static_table[*index].first}, std::move(*value)};
        }

        if (representation != field_representation::literal_with_name)
            return {};

        auto name = read_string(payload, offset);
        if (!name.has_value())
            return {};
        auto value = read_string(payload, offset);
        if (!value.has_value())
            return {};
        return std::pair {std::move(*name), std::move(*value)};
    }

    std::expected<header_list, std::error_code> literal_codec::decode(cspan_uint8_t payload) noexcept
    {
        if (payload.size() < 2u)
            return std::unexpected(detail::qpack_decode_error());

        std::size_t offset = 2u; // past the Required Insert Count and Delta Base placeholders
        header_list headers;
        headers.reserve(6u); // enough for the header block of a typical request or response
        while (offset < payload.size())
        {
            const auto representation = static_cast<field_representation>(payload[offset++]);
            auto field = read_field(payload, offset, representation);
            if (!field.has_value())
                return std::unexpected(detail::qpack_decode_error());

            // Moved, not copied: both strings were built here and have no other owner, so handing them
            // over costs a pointer swap instead of a fresh allocation and copy per header field.
            headers.emplace_back(std::move(field->first), std::move(field->second));
        }

        return headers;
    }

} // namespace kmx::aio::http3::qpack
